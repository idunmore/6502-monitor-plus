
// CPU model
//
// The status register never fully appears on the bus during normal execution, since not all flags
// are "physical", and only appears at all with certain instructions (see below) so the
// only way to get the flags on a per-instruction basis is to "model" them.
//
// This is a *trace-driven* model rather than a standalone "emulator": every value the CPU reads
// or writes is visible on the bus, so operands and results are OBSERVED instead of being computed
// from a memory image.  This is a significant simplification, as it removes the need for a memory
// model, a program counter model, or cycle timing.
//
// Every piece of modelled state carries a "known" bit alongside it. At power-on nothing is
// known and the monitor may well have started watching midway through an instruction.
// Anything that cannot be derived is marked unknown rather than guessed (or left in its prior state),
// so the display can show '?' instead of (potentially) misleading state.
//
// Some bus traffic is ground truth and is "adopted" directly:
//
//   PHP, BRK and interrupt sequences  push P        -> the real status register
//   PLP and RTI                       pull P        -> the real status register
//   STA/STX/STY and PHA/PHX/PHY       write A/X/Y   -> the real register contents
//
// Where an adopted P disagrees with the model, the difference is reported with a '!' marker
// rather than being silently absorbed; that is how the remaining gaps get found.

#include "cpu_model.h"

// The model reads the decoder's state directly: what an instruction did to the CPU depends
// on which instruction it was, its addressing mode, and where it was fetched from
#include "decode.h"

// Status register bits
#define FLAG_C 0x01
#define FLAG_Z 0x02
#define FLAG_I 0x04
#define FLAG_D 0x08
#define FLAG_B 0x10   // Not a real register bit; exists only in a pushed copy of P
#define FLAG_U 0x20   // Unused; reads as 1 when pushed
#define FLAG_V 0x40
#define FLAG_N 0x80

// The six bits that are genuinely part of the CPU's status register. B and U are displayed
// as '-' because the 65C02 has no such flags to track — B is synthesised on a push to say
// whether the push came from BRK/PHP or from a hardware interrupt.
#define FLAGS_REAL (FLAG_N | FLAG_V | FLAG_D | FLAG_I | FLAG_Z | FLAG_C)

// "Known" bits for the registers
#define KNOWN_A 0x01
#define KNOWN_X 0x02
#define KNOWN_Y 0x04
#define KNOWN_S 0x08

// Modelled CPU state
static uint8_t cpu_a = 0, cpu_x = 0, cpu_y = 0, cpu_s = 0;
static uint8_t cpu_p = 0;
static uint8_t cpu_regs_known = 0;  // KNOWN_* bits for registers whose values are trustworthy
static uint8_t cpu_p_known = 0;     // FLAG_* bits whose values in cpu_p are trustworthy
static bool cpu_diverged = false;   // Model disagreed with an observed P; displayed as '!'

// The main monitor loop reads this ...
bool cpu_tracking = false;    // An instruction is in flight, and its bus cycles are captured

// Bus traffic captured for the in-flight instruction, used to resolve its effect once the
// instruction has finished (which we know when the next opcode fetch arrives).
//
// "Last read" deliberately means the last non-operand read.  For a load it is the value
// loaded, no matter how many pointer/index cycles preceded it, and for a read-modify-write
// it is the original value ... the result of an RMW arrives separately as the write.
static uint8_t bus_last_read = 0;
static uint8_t bus_last_write = 0;
static bool bus_have_read = false;
static bool bus_have_write = false;
static uint8_t bus_stack_read[2];         // First two stack reads; [1] is the value for pulls and RTI
static uint8_t bus_stack_write[3];        // First three stack writes; [2] is P for BRK/interrupts
static uint8_t bus_stack_reads = 0;
static uint8_t bus_stack_writes = 0;
static uint8_t bus_stack_read_low = 0;    // Low byte of the last stack address read
static uint8_t bus_stack_write_low = 0;   // Low byte of the last stack address written
static uint8_t bus_vector = 0;            // Low byte of the first $FFFA-$FFFF read seen, else 0

// Give a flag a known value
static void flagWrite(uint8_t mask, bool value) {
  if (value) {
    cpu_p |= mask;
  } else {
    cpu_p &= (uint8_t)~mask;
  }
  cpu_p_known |= mask;
}

// Mark flags as no longer trustworthy
static void flagForget(uint8_t mask) {
  cpu_p_known &= (uint8_t)~mask;
}

// Set N and Z from a known result value, as almost every data-handling instruction does
static void flagsFromValue(uint8_t value) {
  flagWrite(FLAG_N, value & 0x80);
  flagWrite(FLAG_Z, value == 0);
}

// Adopt a status register value seen on the bus, reporting any disagreement with the model
static void adoptStatus(uint8_t observed) {
  uint8_t checkable = cpu_p_known & FLAGS_REAL;
  if ((cpu_p ^ observed) & checkable) {
    cpu_diverged = true;
  }
  cpu_p = (uint8_t)((cpu_p & ~FLAGS_REAL) | (observed & FLAGS_REAL));
  cpu_p_known = FLAGS_REAL;
}

// Forget everything. Used at startup and on reset, where nothing about the CPU is known
void cpuForgetAll() {
  cpu_regs_known = 0;
  cpu_p_known = 0;
}

// Discard the bus traffic captured for the previous instruction, ready for the next one
void busCaptureReset() {
  bus_have_read = false;
  bus_have_write = false;
  bus_stack_reads = 0;
  bus_stack_writes = 0;
  bus_vector = 0;
}

// Record one bus cycle belonging to the in-flight instruction. Operand fetches are filtered
// out by the caller, so everything arriving here is a data, stack, dummy or vector cycle.
void busCapture(uint16_t address, uint8_t data, bool isWrite) {
  bool isStack = (address >= 0x0100 && address <= 0x01FF);

  if (isWrite) {
    bus_last_write = data;
    bus_have_write = true;
    if (isStack) {
      if (bus_stack_writes < sizeof(bus_stack_write)) {
        bus_stack_write[bus_stack_writes] = data;
      }
      bus_stack_writes += 1;
      bus_stack_write_low = (uint8_t)(address & 0xFF);
    }
  } else {
    // An instruction's trailing internal cycles can show up as a read from the address the
    // next instruction starts at; notably the extra cycle that ADC and SBC take in decimal
    // mode on the 65C02. An instruction's operand never comes from there, so such a read
    // must not be allowed to displace the value that does. (A program reading its own next
    // opcode as data would be missed; that costs a '?' rather than a wrong answer.)
    uint16_t nextInstruction = (uint16_t)(decode_pc + 1 + operandByteCount(decode_mode));
    if (address != nextInstruction) {
      bus_last_read = data;
      bus_have_read = true;
    }
    if (isStack) {
      if (bus_stack_reads < sizeof(bus_stack_read)) {
        bus_stack_read[bus_stack_reads] = data;
      }
      bus_stack_reads += 1;
      bus_stack_read_low = (uint8_t)(address & 0xFF);
    }
    // A read from the vector block means a reset or an interrupt sequence is under way
    if (address >= 0xFFFA && bus_vector == 0) {
      bus_vector = (uint8_t)(address & 0xFF);
    }
  }
}

// The value an instruction operated on: the immediate byte for immediate mode, otherwise
// whatever the CPU last read that was not part of the instruction itself
static bool operandValue(uint8_t mode, uint8_t *value) {
  if (mode == MODE_IMM) {
    // Only if the operand byte was *actually seen*. If the instruction was abandoned before
    // its operand was fetched, decode_operand still holds the previous instruction's byte,
    // and using it would produce a presumed-correct-but-actually-wrong answer rather than
    // an honest '?'.
    if (decode_have < 1) {
      return false;
    }
    *value = decode_operand[0];
    return true;
  }
  if (bus_have_read) {
    *value = bus_last_read;
    return true;
  }
  return false;
}

// Apply a read-modify-write instruction's flag effects. The carry comes from the bit shifted
// out of the *original* value (the last read), and N/Z from the result (the write) ... so for
// the memory forms no arithmetic is needed at all, both values having been seen on the bus.
static void applyMemoryShift(bool carryFromBit7) {
  if (bus_have_read) {
    flagWrite(FLAG_C, bus_last_read & (carryFromBit7 ? 0x80 : 0x01));
  } else {
    flagForget(FLAG_C);
  }
  if (bus_have_write) {
    flagsFromValue(bus_last_write);
  } else {
    flagForget(FLAG_N | FLAG_Z);
  }
}

// Apply an accumulator-mode shift or rotate. `carryIn` is only used by the rotates
static void applyAccumulatorShift(bool left, bool rotate) {
  bool needCarry = rotate;
  if (!(cpu_regs_known & KNOWN_A) || (needCarry && !(cpu_p_known & FLAG_C))) {
    cpu_regs_known &= (uint8_t)~KNOWN_A;
    flagForget(FLAG_N | FLAG_Z | FLAG_C);
    return;
  }

  uint8_t carryIn = (rotate && (cpu_p & FLAG_C)) ? 1 : 0;
  uint8_t result;

  if (left) {
    result = (uint8_t)(cpu_a << 1) | carryIn;
    flagWrite(FLAG_C, cpu_a & 0x80);
  } else {
    result = (uint8_t)(cpu_a >> 1) | (uint8_t)(carryIn << 7);
    flagWrite(FLAG_C, cpu_a & 0x01);
  }

  cpu_a = result;
  flagsFromValue(result);
}

// Apply a compare. Compares are unaffected by decimal mode, so this is always binary
static void applyCompare(uint8_t reg, bool regKnown, uint8_t mode) {
  uint8_t value;
  if (!regKnown || !operandValue(mode, &value)) {
    flagForget(FLAG_N | FLAG_Z | FLAG_C);
    return;
  }
  flagWrite(FLAG_C, reg >= value);
  flagWrite(FLAG_Z, reg == value);
  flagWrite(FLAG_N, (uint8_t)(reg - value) & 0x80);
}

// Load a register from the bus, or mark it unknown if the value was never seen
static void applyLoad(uint8_t *reg, uint8_t knownBit, uint8_t mode) {
  uint8_t value;
  if (operandValue(mode, &value)) {
    *reg = value;
    cpu_regs_known |= knownBit;
    flagsFromValue(value);
  } else {
    cpu_regs_known &= (uint8_t)~knownBit;
    flagForget(FLAG_N | FLAG_Z);
  }
}

// Move one register to another, as the transfer instructions do
static void applyTransfer(uint8_t *dst, uint8_t dstKnownBit, uint8_t src, bool srcKnown) {
  if (srcKnown) {
    *dst = src;
    cpu_regs_known |= dstKnownBit;
    flagsFromValue(src);
  } else {
    cpu_regs_known &= (uint8_t)~dstKnownBit;
    flagForget(FLAG_N | FLAG_Z);
  }
}

// Apply a bitwise instruction to the accumulator
static void applyLogic(uint8_t operation, uint8_t mode) {
  uint8_t value;
  if (!(cpu_regs_known & KNOWN_A) || !operandValue(mode, &value)) {
    cpu_regs_known &= (uint8_t)~KNOWN_A;
    flagForget(FLAG_N | FLAG_Z);
    return;
  }

  switch (operation) {
    case OP_AND: cpu_a &= value; break;
    case OP_ORA: cpu_a |= value; break;
    default:     cpu_a ^= value; break;   // OP_EOR
  }
  flagsFromValue(cpu_a);
}

// Apply ADC or SBC which are the only instructions whose result never reaches the bus,
// and so the only ones that have to be calculated rather than observed.
//
// My mental model for this was all off here, so it is with thanks to Bruce Clark and his
// "Decimal Mode" tutorial (https://6502.org/tutorials/decimal_mode.html) that I still have
// some remaining shred of sanity.
//
// Assuming I understood it, and coded it correctly (any errors here are definitely mine) ...
//
// Decimal mode (should) follow the sequences set out in the afore-mentioned tutorial, and
// that's also the source for the relevant 65C02's flag behavior.
//
// The 65C02 differs from the NMOS 6502 (was news to me; 99% of my 6502 code was on the
// original part) in that N, V and Z are all valid in decimal mode (and that's why decimal
// ADC and SBC take an extra cycle on this part), but the flags do not all come from the
// same place:
//
//                  ADC decimal              SBC decimal
//     A            decimal result           decimal result
//     C            decimal result           binary result
//     V            signed, before the       binary result
//                  high-nibble adjustment
//     N, Z         decimal result           decimal result
static void applyArithmetic(bool subtract, uint8_t mode) {
  uint8_t m = 0;

  // Every input has to be known: the accumulator, the operand, the carry going in, and the
  // decimal flag that decides which arithmetic applies at all
  if (!(cpu_regs_known & KNOWN_A) ||
      !(cpu_p_known & FLAG_C) ||
      !(cpu_p_known & FLAG_D) ||
      !operandValue(mode, &m)) {
    cpu_regs_known &= (uint8_t)~KNOWN_A;
    flagForget(FLAG_N | FLAG_V | FLAG_Z | FLAG_C);
    return;
  }

  uint8_t a = cpu_a;
  uint8_t carryIn = (cpu_p & FLAG_C) ? 1 : 0;
  bool decimal = (cpu_p & FLAG_D) != 0;

  // The binary result is always needed: it is the whole answer in binary mode, and in
  // decimal mode it still supplies V for ADC and both V and C for SBC. Subtraction is
  // addition of the complemented operand, the same as the actual hardware.
  uint8_t addend = subtract ? (uint8_t)~m : m;
  uint16_t binarySum = (uint16_t)a + addend + carryIn;
  uint8_t binaryResult = (uint8_t)binarySum;
  bool binaryCarry = (binarySum > 0xFF);
  bool binaryOverflow = ((~(a ^ addend) & (a ^ binaryResult) & 0x80) != 0);

  if (!decimal) {
    cpu_a = binaryResult;
    flagWrite(FLAG_C, binaryCarry);
    flagWrite(FLAG_V, binaryOverflow);
    flagsFromValue(binaryResult);
    return;
  }

  if (!subtract) {
    // Seq. 1: low nibble first, carrying a decimal adjustment into the high nibble.
    uint8_t low = (uint8_t)((a & 0x0F) + (m & 0x0F) + carryIn);
    if (low >= 0x0A) {
      low = (uint8_t)(((low + 0x06) & 0x0F) + 0x10);
    }

    // Seq. 2: the same sum in signed arithmetic, taken before the high nibble is adjusted.
    // V reports that this intermediate value fell outside a signed byte.
    int16_t signedTotal = (int16_t)((int8_t)(a & 0xF0)) + (int8_t)(m & 0xF0) + low;
    flagWrite(FLAG_V, signedTotal < -128 || signedTotal > 127);

    uint16_t total = (uint16_t)(a & 0xF0) + (m & 0xF0) + low;
    if (total >= 0x00A0) {
      total += 0x0060;
    }

    cpu_a = (uint8_t)total;
    flagWrite(FLAG_C, total >= 0x0100);
  } else {
    // Seq. 4, the 65C02's own subtraction sequence: do the binary subtraction, then correct
    // each nibble that borrowed.
    int16_t low = (int16_t)(a & 0x0F) - (int16_t)(m & 0x0F) + carryIn - 1;
    int16_t total = (int16_t)a - (int16_t)m + carryIn - 1;

    if (total < 0) {
      total -= 0x60;
    }
    if (low < 0) {
      total -= 0x06;
    }

    cpu_a = (uint8_t)total;
    flagWrite(FLAG_C, binaryCarry);
    flagWrite(FLAG_V, binaryOverflow);
  }

  // On the 65C02 both N and Z follow the decimal result (unlike the NMOS 6502)
  flagsFromValue(cpu_a);
}

// Adopt a register value straight off the bus, as seen in a store or a push
static void adoptRegister(uint8_t *reg, uint8_t knownBit, bool haveValue, uint8_t value) {
  if (haveValue) {
    *reg = value;
    cpu_regs_known |= knownBit;
  }
}

// Pull a register from the stack. A pull reads the stack twice ... once at the current stack
// pointer, which is discarded, and once at the incremented pointer, which is the value, so
// the second stack read is what actually lands in the register.
static void applyLoadFromPull(uint8_t *reg, uint8_t knownBit) {
  if (bus_stack_reads >= 2) {
    *reg = bus_stack_read[1];
    cpu_regs_known |= knownBit;
    flagsFromValue(bus_stack_read[1]);
  } else {
    cpu_regs_known &= (uint8_t)~knownBit;
    flagForget(FLAG_N | FLAG_Z);
  }
}

// Increment or decrement a register by one
static void applyRegisterStep(uint8_t *reg, uint8_t knownBit, int8_t delta) {
  if (!(cpu_regs_known & knownBit)) {
    flagForget(FLAG_N | FLAG_Z);
    return;
  }
  *reg = (uint8_t)(*reg + delta);
  flagsFromValue(*reg);
}

// Increment or decrement a memory location. The CPU writes the result to the bus, so the
// new value is observed rather than calculated and the original value is never needed
static void applyMemoryStep() {
  if (bus_have_write) {
    flagsFromValue(bus_last_write);
  } else {
    flagForget(FLAG_N | FLAG_Z);
  }
}

// A conditional branch reports the flag it tested, free of charge: which way it went is
// visible as the address of the next opcode fetch. `flag` is the flag under test and
// `takenWhenSet` says whether the branch is taken when that flag is set; BEQ tests Z and is
// taken when Z is set, BNE tests the same flag and is taken when it is clear.
//
// This is the one place a flag can become known without ever having been on the bus, so it is
// also worth having as a check: the CPU's own decision is ground truth, and disagreeing with
// it means the model is wrong!
//
// `nextFetch` is the address the following opcode was fetched from
static void applyBranch(uint8_t flag, bool takenWhenSet, uint16_t nextFetch) {
  // Without the offset byte there is no way to know where a taken branch would have gone.
  if (decode_have < 1) {
    return;
  }

  uint16_t notTaken = (uint16_t)(decode_pc + 2);
  uint16_t target = (uint16_t)(notTaken + (int8_t)decode_operand[0]);

  // An offset of zero sends both paths to the same place, so nothing can be inferred
  if (target == notTaken) {
    return;
  }

  bool taken;
  if (nextFetch == target) {
    taken = true;
  } else if (nextFetch == notTaken) {
    taken = false;
  } else {
    // Neither: an interrupt stepped in, or a bus cycle was missed. Infer nothing!
    return;
  }

  bool value = (taken == takenWhenSet);
  if ((cpu_p_known & flag) && (((cpu_p & flag) != 0) != value)) {
    cpu_diverged = true;
  }
  flagWrite(flag, value);
}

// Work out what the instruction that has just finished did to the CPU, using the bus traffic
// captured while it executed. Called when the next opcode fetch shows the previous
// instruction has run to completion; `nextFetch` is the address that opcode came from.
void completeInstruction(uint16_t nextFetch) {
  // The divergence marker reports on the instruction that has just finished, so it is
  // cleared here and set again only if this instruction's bus traffic contradicts the model.
  cpu_diverged = false;

  // The stack pointer can be read straight off the addresses of any stack cycles: a push
  // leaves it one below the last address written, a pull leaves it at the last address read.
  if (bus_stack_writes > 0) {
    cpu_s = (uint8_t)(bus_stack_write_low - 1);
    cpu_regs_known |= KNOWN_S;
  } else if (bus_stack_reads > 0) {
    cpu_s = bus_stack_read_low;
    cpu_regs_known |= KNOWN_S;
  }

  // A vector fetch means a reset or an interrupt took over, and the opcode that was fetched
  // was discarded rather than executed. Handle that instead of the instruction
  if (bus_vector != 0) {
    if (bus_vector == 0xFC) {
      // Reset. Only the interrupt-disable and decimal flags have defined values afterwards
      cpuForgetAll();
    } else {
      // BRK, IRQ or NMI. The third stack write is the pre-interrupt status register
      if (bus_stack_writes >= 3) {
        adoptStatus(bus_stack_write[2]);
      }
    }
    // Both reset and interrupt entry disable interrupts and clear decimal mode on the 65C02.
    flagWrite(FLAG_I, true);
    flagWrite(FLAG_D, false);
    return;
  }

  switch (decode_operation) {
    // Flags set or cleared outright
    case OP_CLC: flagWrite(FLAG_C, false); break;
    case OP_SEC: flagWrite(FLAG_C, true);  break;
    case OP_CLI: flagWrite(FLAG_I, false); break;
    case OP_SEI: flagWrite(FLAG_I, true);  break;
    case OP_CLD: flagWrite(FLAG_D, false); break;
    case OP_SED: flagWrite(FLAG_D, true);  break;
    case OP_CLV: flagWrite(FLAG_V, false); break;

    // Loads 
    case OP_LDA: applyLoad(&cpu_a, KNOWN_A, decode_mode); break;
    case OP_LDX: applyLoad(&cpu_x, KNOWN_X, decode_mode); break;
    case OP_LDY: applyLoad(&cpu_y, KNOWN_Y, decode_mode); break;

    // Stores. No flag effects, but the written byte is the register, "free of charge"
    case OP_STA: adoptRegister(&cpu_a, KNOWN_A, bus_have_write, bus_last_write); break;
    case OP_STX: adoptRegister(&cpu_x, KNOWN_X, bus_have_write, bus_last_write); break;
    case OP_STY: adoptRegister(&cpu_y, KNOWN_Y, bus_have_write, bus_last_write); break;
    case OP_STZ: break;

    // Transfers
    case OP_TAX: applyTransfer(&cpu_x, KNOWN_X, cpu_a, cpu_regs_known & KNOWN_A); break;
    case OP_TAY: applyTransfer(&cpu_y, KNOWN_Y, cpu_a, cpu_regs_known & KNOWN_A); break;
    case OP_TXA: applyTransfer(&cpu_a, KNOWN_A, cpu_x, cpu_regs_known & KNOWN_X); break;
    case OP_TYA: applyTransfer(&cpu_a, KNOWN_A, cpu_y, cpu_regs_known & KNOWN_Y); break;
    case OP_TSX: applyTransfer(&cpu_x, KNOWN_X, cpu_s, cpu_regs_known & KNOWN_S); break;

    // TXS is the one transfer that touches no flags
    case OP_TXS:
      if (cpu_regs_known & KNOWN_X) {
        cpu_s = cpu_x;
        cpu_regs_known |= KNOWN_S;
      } else {
        cpu_regs_known &= (uint8_t)~KNOWN_S;
      }
      break;

    // Pushes. Again, the pushed byte is the register
    case OP_PHA: adoptRegister(&cpu_a, KNOWN_A, bus_stack_writes > 0, bus_stack_write[0]); break;
    case OP_PHX: adoptRegister(&cpu_x, KNOWN_X, bus_stack_writes > 0, bus_stack_write[0]); break;
    case OP_PHY: adoptRegister(&cpu_y, KNOWN_Y, bus_stack_writes > 0, bus_stack_write[0]); break;

    // PHP puts the real status register on the bus ... thus ground truth, and a check on the model
    case OP_PHP:
      if (bus_stack_writes > 0) {
        adoptStatus(bus_stack_write[0]);
      }
      break;

    // Pulls. A pull reads the stack twice; the second read is the value
    case OP_PLA:
      applyLoadFromPull(&cpu_a, KNOWN_A);
      break;
    case OP_PLX:
      applyLoadFromPull(&cpu_x, KNOWN_X);
      break;
    case OP_PLY:
      applyLoadFromPull(&cpu_y, KNOWN_Y);
      break;

    // PLP and RTI both restore P from the stack, and both make it *entirely* known
    case OP_PLP:
    case OP_RTI:
      if (bus_stack_reads >= 2) {
        adoptStatus(bus_stack_read[1]);
      } else {
        flagForget(FLAGS_REAL);
      }
      break;

    // Logic
    case OP_AND:
    case OP_ORA:
    case OP_EOR:
      applyLogic(decode_operation, decode_mode);
      break;

    // Compares
    case OP_CMP: applyCompare(cpu_a, cpu_regs_known & KNOWN_A, decode_mode); break;
    case OP_CPX: applyCompare(cpu_x, cpu_regs_known & KNOWN_X, decode_mode); break;
    case OP_CPY: applyCompare(cpu_y, cpu_regs_known & KNOWN_Y, decode_mode); break;

    // BIT

    // Immediate BIT is a 65C02 addition and, unlike every other form, affects only Z
    case OP_BIT: {
      uint8_t value;
      if (!operandValue(decode_mode, &value)) {
        flagForget(decode_mode == MODE_IMM ? FLAG_Z : (FLAG_N | FLAG_V | FLAG_Z));
        break;
      }
      if (decode_mode != MODE_IMM) {
        flagWrite(FLAG_N, value & 0x80);
        flagWrite(FLAG_V, value & 0x40);
      }
      if (cpu_regs_known & KNOWN_A) {
        flagWrite(FLAG_Z, (cpu_a & value) == 0);
      } else {
        flagForget(FLAG_Z);
      }
      break;
    }

    // TSB/TRB test against the accumulator and affect Z only
    case OP_TSB:
    case OP_TRB:
      if ((cpu_regs_known & KNOWN_A) && bus_have_read) {
        flagWrite(FLAG_Z, (cpu_a & bus_last_read) == 0);
      } else {
        flagForget(FLAG_Z);
      }
      break;

    // Shifts and rotates
    case OP_ASL:
      if (decode_mode == MODE_ACC) applyAccumulatorShift(true, false); else applyMemoryShift(true);
      break;
    case OP_LSR:
      if (decode_mode == MODE_ACC) applyAccumulatorShift(false, false); else applyMemoryShift(false);
      break;
    case OP_ROL:
      if (decode_mode == MODE_ACC) applyAccumulatorShift(true, true); else applyMemoryShift(true);
      break;
    case OP_ROR:
      if (decode_mode == MODE_ACC) applyAccumulatorShift(false, true); else applyMemoryShift(false);
      break;

    // Increment and decrement

    // The memory forms write their result to the bus, so no arithmetic is needed
    case OP_INC:
      if (decode_mode == MODE_ACC) applyRegisterStep(&cpu_a, KNOWN_A, +1); else applyMemoryStep();
      break;
    case OP_DEC:
      if (decode_mode == MODE_ACC) applyRegisterStep(&cpu_a, KNOWN_A, -1); else applyMemoryStep();
      break;
    case OP_INX: applyRegisterStep(&cpu_x, KNOWN_X, +1); break;
    case OP_DEX: applyRegisterStep(&cpu_x, KNOWN_X, -1); break;
    case OP_INY: applyRegisterStep(&cpu_y, KNOWN_Y, +1); break;
    case OP_DEY: applyRegisterStep(&cpu_y, KNOWN_Y, -1); break;

    // Arithmetic
    case OP_ADC: applyArithmetic(false, decode_mode); break;
    case OP_SBC: applyArithmetic(true,  decode_mode); break;

    // BRK, when it did not produce a vector fetch we recognised
    case OP_BRK:
      if (bus_stack_writes >= 3) {
        adoptStatus(bus_stack_write[2]);
      }
      flagWrite(FLAG_I, true);
      flagWrite(FLAG_D, false);
      break;

    // Conditional branches, which reveal the flag they tested
    case OP_BCC: applyBranch(FLAG_C, false, nextFetch); break;
    case OP_BCS: applyBranch(FLAG_C, true,  nextFetch); break;
    case OP_BNE: applyBranch(FLAG_Z, false, nextFetch); break;
    case OP_BEQ: applyBranch(FLAG_Z, true,  nextFetch); break;
    case OP_BPL: applyBranch(FLAG_N, false, nextFetch); break;
    case OP_BMI: applyBranch(FLAG_N, true,  nextFetch); break;
    case OP_BVC: applyBranch(FLAG_V, false, nextFetch); break;
    case OP_BVS: applyBranch(FLAG_V, true,  nextFetch); break;

    // An undefined opcode means the trace may have de-synchronized
    case OP_UNK:
      cpuForgetAll();
      break;

    // Everything else leaves the registers and flags alone: BRA, which is unconditional and
    // so tests nothing, JMP, JSR, RTS, NOP, WAI, STP, and the bit-manipulation instructions
    // RMB/SMB/BBR/BBS — BBR and BBS branch on a bit in memory, not on a flag.
    default:
      break;
  }
}

// Render the status register as eight flag characters plus a divergence marker: an upper
// case letter means the flag is set, lower case means clear, '?' means the model does not
// know, and '-' marks the two bit positions that are not real flags. A trailing '!' means
// the model disagreed with a status register value seen on the bus.
void formatStatus(char *out, size_t outSize) {
  static const char FLAG_CHARS[8] = {'N', 'V', '-', '-', 'D', 'I', 'Z', 'C'};
  char text[10];

  for (uint8_t i = 0; i < 8; i += 1) {
    uint8_t bit = (uint8_t)(0x80 >> i);
    char name = FLAG_CHARS[i];

    if (name == '-') {
      text[i] = '-';
    } else if (!(cpu_p_known & bit)) {
      text[i] = '?';
    } else if (cpu_p & bit) {
      text[i] = name;
    } else {
      text[i] = (char)(name | 0x20);
    }
  }

  text[8] = cpu_diverged ? '!' : ' ';
  text[9] = '\0';
  snprintf(out, outSize, "%s", text);
}
