#include "decode.h"

// Instruction "decoding" and formatting.
//
// Turns an opcode byte, plus the operand bytes seen on the bus after it, into display text.

// State for the deferred, full-instruction decode: carried across clock cycles while an
// instruction's operand bytes are being collected, so the fully-resolved instruction (e.g.,
// "JSR $1234") can be appended to the line once the last operand byte has been read.

uint8_t decode_pending = 0;         // Operand bytes still to be read for the in-flight instruction
uint8_t decode_have = 0;            // Operand bytes already read for the in-flight instruction
uint8_t decode_operand[2];          // Operand bytes, in the order they were fetched (low byte first)
uint16_t decode_pc = 0;             // Address bus value when the in-flight opcode was fetched
uint8_t decode_operation = OP_UNK;  // In-flight opcode's operation
uint8_t decode_mode = MODE_NONE;    // In-flight opcode's addressing mode

// Look up an opcode byte's operation and addressing mode
void lookupOpcode(uint8_t opcode, uint8_t *operation, uint8_t *mode) {
  *operation = pgm_read_byte(&OPCODES[opcode].operation);
  *mode = pgm_read_byte(&OPCODES[opcode].mode);
}

// How many operand bytes follow an opcode in this addressing mode
uint8_t operandByteCount(uint8_t mode) {
  return pgm_read_byte(&MODE_OPERAND_BYTES[mode]);
}

// Build the mnemonic-plus-addressing-mode text shown as soon as an opcode is fetched
// (e.g. "JSR abs", "ORA (zp,X)", "BBR0 zp,rel", "NOP", "???")
void formatOpcode(char *out, size_t outSize, uint8_t operation, uint8_t mode) {
  char mnemonic[sizeof(MNEMONICS[0])];
  char suffix[sizeof(MODE_SUFFIX[0])];

  strcpy_P(mnemonic, MNEMONICS[operation]);
  strcpy_P(suffix, MODE_SUFFIX[mode]);

  if (suffix[0]) {
    snprintf(out, outSize, "%s %s", mnemonic, suffix);
  } else {
    snprintf(out, outSize, "%s", mnemonic);
  }
}

// Build the fully-decoded instruction (e.g., "JSR $1234", "ORA ($12,X)", "BBR0 $12,$3456")
// once all of an instruction's operand bytes have been captured. `pc` is the address the
// opcode itself was fetched from, needed to resolve relative-branch targets.
//
// Decoded instruction syntax is effectively a disassembly, and is output using normal
// 6502 assembly-style instructions and operands.
//
// Addresses and branch targets are computed as uint16_t so they wrap at 64K exactly as the
// CPU's program counter does (a backward branch from low memory wraps to the top of the
// address space).
void formatDecoded(char *out, size_t outSize, uint8_t operation, uint8_t mode,
                   const uint8_t *operand, uint16_t pc) {
  char mnemonic[sizeof(MNEMONICS[0])];
  strcpy_P(mnemonic, MNEMONICS[operation]);

  uint16_t addr = (uint16_t)operand[0] | ((uint16_t)operand[1] << 8);

  switch (mode) {
    case MODE_IMM: snprintf(out, outSize, "%s #$%02X",     mnemonic, operand[0]); break;
    case MODE_ZP:  snprintf(out, outSize, "%s $%02X",      mnemonic, operand[0]); break;
    case MODE_ZPX: snprintf(out, outSize, "%s $%02X,X",    mnemonic, operand[0]); break;
    case MODE_ZPY: snprintf(out, outSize, "%s $%02X,Y",    mnemonic, operand[0]); break;
    case MODE_ZPI: snprintf(out, outSize, "%s ($%02X)",    mnemonic, operand[0]); break;
    case MODE_IZX: snprintf(out, outSize, "%s ($%02X,X)",  mnemonic, operand[0]); break;
    case MODE_IZY: snprintf(out, outSize, "%s ($%02X),Y",  mnemonic, operand[0]); break;
    case MODE_ABS: snprintf(out, outSize, "%s $%04X",      mnemonic, addr); break;
    case MODE_ABX: snprintf(out, outSize, "%s $%04X,X",    mnemonic, addr); break;
    case MODE_ABY: snprintf(out, outSize, "%s $%04X,Y",    mnemonic, addr); break;
    case MODE_IND: snprintf(out, outSize, "%s ($%04X)",    mnemonic, addr); break;
    case MODE_IAX: snprintf(out, outSize, "%s ($%04X,X)",  mnemonic, addr); break;

    // Branch targets are relative to the address of the *following* instruction, so the
    // offset is added to the opcode's address plus the instruction's total length
    case MODE_REL:
      snprintf(out, outSize, "%s $%04X", mnemonic,
               (uint16_t)(pc + 2 + (int8_t)operand[0]));
      break;

    // BBRx/BBSx: zero-page address to test, then a relative branch offset
    case MODE_ZPR:
      snprintf(out, outSize, "%s $%02X,$%04X", mnemonic, operand[0],
               (uint16_t)(pc + 3 + (int8_t)operand[1]));
      break;

    // Implied, accumulator and undefined opcodes have no operand to render
    default:
      snprintf(out, outSize, "%s", mnemonic);
      break;
  }
}