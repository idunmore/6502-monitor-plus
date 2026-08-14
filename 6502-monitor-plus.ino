// Extended version of Ben Eater's (https://eater.net) "6502-monitor.ino" sketch.
//
// Adds opcode/instruction mnemonic, and addressing mode, when CPU is reading an OPCODE
// (as indicated by the W65C02S's SYNC pin being HIGH). Fully decodes multi-byte instructions
// appending the resolved instruction (e.g. "JSR $1234") to the end of the line once decoded.

// This work, "6502-monitor_plus" is an extension to "6502-monitor.ino", by Ben Eater,
// used under CC BY 4.0.  "6502-monitor_plus" is, similarly, licensed under CC BY 4.0
// by Ian Dunmore.
//
// Copyright ©️ 2026, Ian Dunmore
// License: CC BY 4.0

// Needed for PROGMEM
 #include <avr/pgmspace.h>

// Address line PINS (52 is LSB)
const char ADDR[] = {22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52};

// Data PINS (53 is LSB)
const char DATA[] = {39, 41, 43, 45, 47, 49, 51, 53};

// Arduino Pin #   -> // W65C02S pin
#define CLOCK 2       // W65C02S pin 37
#define READ_WRITE 3  // W65C02S pin 34
#define SYNC 4        // W65C02S pin 7

// ---------------------------------------------------------------------------------------
// Instruction set description
//
// Each opcode is described as an (operation, addressing mode) pair rather than as a single
// display string. The display text is built from the pair on demand, and the same pair
// drives operand-length calculation and instruction formatting — so there is exactly one
// place that knows what any given opcode is.
// ---------------------------------------------------------------------------------------

// Operations. Values are indices into MNEMONICS[], so the two MUST stay in the same order
enum Operation : uint8_t {
  OP_ADC,  OP_AND,  OP_ASL,
  OP_BBR0, OP_BBR1, OP_BBR2, OP_BBR3, OP_BBR4, OP_BBR5, OP_BBR6, OP_BBR7,
  OP_BBS0, OP_BBS1, OP_BBS2, OP_BBS3, OP_BBS4, OP_BBS5, OP_BBS6, OP_BBS7,
  OP_BCC,  OP_BCS,  OP_BEQ,  OP_BIT,  OP_BMI,  OP_BNE,  OP_BPL,  OP_BRA,
  OP_BRK,  OP_BVC,  OP_BVS,
  OP_CLC,  OP_CLD,  OP_CLI,  OP_CLV,  OP_CMP,  OP_CPX,  OP_CPY,
  OP_DEC,  OP_DEX,  OP_DEY,
  OP_EOR,
  OP_INC,  OP_INX,  OP_INY,
  OP_JMP,  OP_JSR,
  OP_LDA,  OP_LDX,  OP_LDY,  OP_LSR,
  OP_NOP,
  OP_ORA,
  OP_PHA,  OP_PHP,  OP_PHX,  OP_PHY,  OP_PLA,  OP_PLP,  OP_PLX,  OP_PLY,
  OP_RMB0, OP_RMB1, OP_RMB2, OP_RMB3, OP_RMB4, OP_RMB5, OP_RMB6, OP_RMB7,
  OP_ROL,  OP_ROR,  OP_RTI,  OP_RTS,
  OP_SBC,  OP_SEC,  OP_SED,  OP_SEI,
  OP_SMB0, OP_SMB1, OP_SMB2, OP_SMB3, OP_SMB4, OP_SMB5, OP_SMB6, OP_SMB7,
  OP_STA,  OP_STP,  OP_STX,  OP_STY,  OP_STZ,
  OP_TAX,  OP_TAY,  OP_TRB,  OP_TSB,  OP_TSX,  OP_TXA,  OP_TXS,  OP_TYA,
  OP_WAI,
  OP_UNK,           // Undefined/unimplemented opcode
  OPERATION_COUNT   
};

// Mnemonic text, indexed by Operation. Order MUST match Operation enum above
const char MNEMONICS[][5] PROGMEM = {
  "ADC",  "AND",  "ASL",
  "BBR0", "BBR1", "BBR2", "BBR3", "BBR4", "BBR5", "BBR6", "BBR7",
  "BBS0", "BBS1", "BBS2", "BBS3", "BBS4", "BBS5", "BBS6", "BBS7",
  "BCC",  "BCS",  "BEQ",  "BIT",  "BMI",  "BNE",  "BPL",  "BRA",
  "BRK",  "BVC",  "BVS",
  "CLC",  "CLD",  "CLI",  "CLV",  "CMP",  "CPX",  "CPY",
  "DEC",  "DEX",  "DEY",
  "EOR",
  "INC",  "INX",  "INY",
  "JMP",  "JSR",
  "LDA",  "LDX",  "LDY",  "LSR",
  "NOP",
  "ORA",
  "PHA",  "PHP",  "PHX",  "PHY",  "PLA",  "PLP",  "PLX",  "PLY",
  "RMB0", "RMB1", "RMB2", "RMB3", "RMB4", "RMB5", "RMB6", "RMB7",
  "ROL",  "ROR",  "RTI",  "RTS",
  "SBC",  "SEC",  "SED",  "SEI",
  "SMB0", "SMB1", "SMB2", "SMB3", "SMB4", "SMB5", "SMB6", "SMB7",
  "STA",  "STP",  "STX",  "STY",  "STZ",
  "TAX",  "TAY",  "TRB",  "TSB",  "TSX",  "TXA",  "TXS",  "TYA",
  "WAI",
  "???"
};

// Make sure the number of mnemonics matches the number of operations
static_assert(sizeof(MNEMONICS) / sizeof(MNEMONICS[0]) == OPERATION_COUNT,
              "MNEMONICS[] and the Operation enum are out of step");

// Addressing modes. Values are indices into MODE_SUFFIX[] and MODE_OPERAND_BYTES[], so all
// *three* MUST stay in the same order.
//
// IMP and ACC render identically (bare mnemonic) but are kept distinct: the emulator needs
// to tell "ASL A" from "ASL $1234".
enum AddressMode : uint8_t {
  MODE_IMP,       // Implied            e.g., NOP, CLC, RTS
  MODE_ACC,       // Accumulator        e.g., ASL A, INC A
  MODE_IMM,       // Immediate          #
  MODE_ZP,        // Zero page          zp
  MODE_ZPX,       // Zero page,X        zp,X
  MODE_ZPY,       // Zero page,Y        zp,Y
  MODE_ZPI,       // Zero page indirect (zp)
  MODE_IZX,       // Indexed indirect   (zp,X)
  MODE_IZY,       // Indirect indexed   (zp),Y
  MODE_ABS,       // Absolute           abs
  MODE_ABX,       // Absolute,X         abs,X
  MODE_ABY,       // Absolute,Y         abs,Y
  MODE_IND,       // Indirect           (abs)
  MODE_IAX,       // Indexed indirect   (abs,X)
  MODE_REL,       // Relative branch    rel
  MODE_ZPR,       // Zero page + branch zp,rel
  MODE_NONE,      // Undefined opcode
  ADDRESS_MODE_COUNT
};

// Addressing-mode text appended after the mnemonic. Order MUST match the AddressMode enum above
const char MODE_SUFFIX[][8] PROGMEM = {
  "",         // MODE_IMP
  "",         // MODE_ACC
  "#",        // MODE_IMM
  "zp",       // MODE_ZP
  "zp,X",     // MODE_ZPX
  "zp,Y",     // MODE_ZPY
  "(zp)",     // MODE_ZPI
  "(zp,X)",   // MODE_IZX
  "(zp),Y",   // MODE_IZY
  "abs",      // MODE_ABS
  "abs,X",    // MODE_ABX
  "abs,Y",    // MODE_ABY
  "(abs)",    // MODE_IND
  "(abs,X)",  // MODE_IAX
  "rel",      // MODE_REL
  "zp,rel",   // MODE_ZPR
  ""          // MODE_NONE
};

// Make sure the number of addressing-mode suffixes matches the number of AddressMode entries
static_assert(sizeof(MODE_SUFFIX) / sizeof(MODE_SUFFIX[0]) == ADDRESS_MODE_COUNT,
              "MODE_SUFFIX[] and the AddressMode enum are out of step");

// Operand bytes that follow the opcode, per addressing mode. Order MUST match the AddressMode enum
//
// MODE_NONE is 0: so undefined opcodes are treated as single-byte (which is what the
// monitor has always assumed).  On real silicon their lengths vary (most are 1 byte, but
// e.g. $02 is 2 and $5C is 3), so a run of them will de-synchronize the operand tracking.
const uint8_t MODE_OPERAND_BYTES[] PROGMEM = {
  0,  // MODE_IMP
  0,  // MODE_ACC
  1,  // MODE_IMM
  1,  // MODE_ZP
  1,  // MODE_ZPX
  1,  // MODE_ZPY
  1,  // MODE_ZPI
  1,  // MODE_IZX
  1,  // MODE_IZY
  2,  // MODE_ABS
  2,  // MODE_ABX
  2,  // MODE_ABY
  2,  // MODE_IND
  2,  // MODE_IAX
  1,  // MODE_REL
  2,  // MODE_ZPR
  0   // MODE_NONE
};

// Make sure the number of operand-byte-lengths matches the number of AddressMode entries
static_assert(sizeof(MODE_OPERAND_BYTES) / sizeof(MODE_OPERAND_BYTES[0]) == ADDRESS_MODE_COUNT,
              "MODE_OPERAND_BYTES[] and the AddressMode enum are out of step");

// One entry per opcode, holding the operation and its addressing mode
struct OpcodeEntry {
  uint8_t operation;
  uint8_t mode;
};

#define OP(operation, mode) { OP_##operation, MODE_##mode }
#define ___                 { OP_UNK, MODE_NONE }

// W65C02S opcode table in flash ("PROGMEM"), indexed directly by the data (OPCODE) byte
const OpcodeEntry OPCODES[256] PROGMEM = {
  OP(BRK,IMP),  OP(ORA,IZX),  ___,          ___,          OP(TSB,ZP),   OP(ORA,ZP),   OP(ASL,ZP),   OP(RMB0,ZP),   // 00
  OP(PHP,IMP),  OP(ORA,IMM),  OP(ASL,ACC),  ___,          OP(TSB,ABS),  OP(ORA,ABS),  OP(ASL,ABS),  OP(BBR0,ZPR),  // 08
  OP(BPL,REL),  OP(ORA,IZY),  OP(ORA,ZPI),  ___,          OP(TRB,ZP),   OP(ORA,ZPX),  OP(ASL,ZPX),  OP(RMB1,ZP),   // 10
  OP(CLC,IMP),  OP(ORA,ABY),  OP(INC,ACC),  ___,          OP(TRB,ABS),  OP(ORA,ABX),  OP(ASL,ABX),  OP(BBR1,ZPR),  // 18
  OP(JSR,ABS),  OP(AND,IZX),  ___,          ___,          OP(BIT,ZP),   OP(AND,ZP),   OP(ROL,ZP),   OP(RMB2,ZP),   // 20
  OP(PLP,IMP),  OP(AND,IMM),  OP(ROL,ACC),  ___,          OP(BIT,ABS),  OP(AND,ABS),  OP(ROL,ABS),  OP(BBR2,ZPR),  // 28
  OP(BMI,REL),  OP(AND,IZY),  OP(AND,ZPI),  ___,          OP(BIT,ZPX),  OP(AND,ZPX),  OP(ROL,ZPX),  OP(RMB3,ZP),   // 30
  OP(SEC,IMP),  OP(AND,ABY),  OP(DEC,ACC),  ___,          OP(BIT,ABX),  OP(AND,ABX),  OP(ROL,ABX),  OP(BBR3,ZPR),  // 38
  OP(RTI,IMP),  OP(EOR,IZX),  ___,          ___,          ___,          OP(EOR,ZP),   OP(LSR,ZP),   OP(RMB4,ZP),   // 40
  OP(PHA,IMP),  OP(EOR,IMM),  OP(LSR,ACC),  ___,          OP(JMP,ABS),  OP(EOR,ABS),  OP(LSR,ABS),  OP(BBR4,ZPR),  // 48
  OP(BVC,REL),  OP(EOR,IZY),  OP(EOR,ZPI),  ___,          ___,          OP(EOR,ZPX),  OP(LSR,ZPX),  OP(RMB5,ZP),   // 50
  OP(CLI,IMP),  OP(EOR,ABY),  OP(PHY,IMP),  ___,          ___,          OP(EOR,ABX),  OP(LSR,ABX),  OP(BBR5,ZPR),  // 58
  OP(RTS,IMP),  OP(ADC,IZX),  ___,          ___,          OP(STZ,ZP),   OP(ADC,ZP),   OP(ROR,ZP),   OP(RMB6,ZP),   // 60
  OP(PLA,IMP),  OP(ADC,IMM),  OP(ROR,ACC),  ___,          OP(JMP,IND),  OP(ADC,ABS),  OP(ROR,ABS),  OP(BBR6,ZPR),  // 68
  OP(BVS,REL),  OP(ADC,IZY),  OP(ADC,ZPI),  ___,          OP(STZ,ZPX),  OP(ADC,ZPX),  OP(ROR,ZPX),  OP(RMB7,ZP),   // 70
  OP(SEI,IMP),  OP(ADC,ABY),  OP(PLY,IMP),  ___,          OP(JMP,IAX),  OP(ADC,ABX),  OP(ROR,ABX),  OP(BBR7,ZPR),  // 78
  OP(BRA,REL),  OP(STA,IZX),  ___,          ___,          OP(STY,ZP),   OP(STA,ZP),   OP(STX,ZP),   OP(SMB0,ZP),   // 80
  OP(DEY,IMP),  OP(BIT,IMM),  OP(TXA,IMP),  ___,          OP(STY,ABS),  OP(STA,ABS),  OP(STX,ABS),  OP(BBS0,ZPR),  // 88
  OP(BCC,REL),  OP(STA,IZY),  OP(STA,ZPI),  ___,          OP(STY,ZPX),  OP(STA,ZPX),  OP(STX,ZPY),  OP(SMB1,ZP),   // 90
  OP(TYA,IMP),  OP(STA,ABY),  OP(TXS,IMP),  ___,          OP(STZ,ABS),  OP(STA,ABX),  OP(STZ,ABX),  OP(BBS1,ZPR),  // 98
  OP(LDY,IMM),  OP(LDA,IZX),  OP(LDX,IMM),  ___,          OP(LDY,ZP),   OP(LDA,ZP),   OP(LDX,ZP),   OP(SMB2,ZP),   // A0
  OP(TAY,IMP),  OP(LDA,IMM),  OP(TAX,IMP),  ___,          OP(LDY,ABS),  OP(LDA,ABS),  OP(LDX,ABS),  OP(BBS2,ZPR),  // A8
  OP(BCS,REL),  OP(LDA,IZY),  OP(LDA,ZPI),  ___,          OP(LDY,ZPX),  OP(LDA,ZPX),  OP(LDX,ZPY),  OP(SMB3,ZP),   // B0
  OP(CLV,IMP),  OP(LDA,ABY),  OP(TSX,IMP),  ___,          OP(LDY,ABX),  OP(LDA,ABX),  OP(LDX,ABY),  OP(BBS3,ZPR),  // B8
  OP(CPY,IMM),  OP(CMP,IZX),  ___,          ___,          OP(CPY,ZP),   OP(CMP,ZP),   OP(DEC,ZP),   OP(SMB4,ZP),   // C0
  OP(INY,IMP),  OP(CMP,IMM),  OP(DEX,IMP),  OP(WAI,IMP),  OP(CPY,ABS),  OP(CMP,ABS),  OP(DEC,ABS),  OP(BBS4,ZPR),  // C8
  OP(BNE,REL),  OP(CMP,IZY),  OP(CMP,ZPI),  ___,          ___,          OP(CMP,ZPX),  OP(DEC,ZPX),  OP(SMB5,ZP),   // D0
  OP(CLD,IMP),  OP(CMP,ABY),  OP(PHX,IMP),  OP(STP,IMP),  ___,          OP(CMP,ABX),  OP(DEC,ABX),  OP(BBS5,ZPR),  // D8
  OP(CPX,IMM),  OP(SBC,IZX),  ___,          ___,          OP(CPX,ZP),   OP(SBC,ZP),   OP(INC,ZP),   OP(SMB6,ZP),   // E0
  OP(INX,IMP),  OP(SBC,IMM),  OP(NOP,IMP),  ___,          OP(CPX,ABS),  OP(SBC,ABS),  OP(INC,ABS),  OP(BBS6,ZPR),  // E8
  OP(BEQ,REL),  OP(SBC,IZY),  OP(SBC,ZPI),  ___,          ___,          OP(SBC,ZPX),  OP(INC,ZPX),  OP(SMB7,ZP),   // F0
  OP(SED,IMP),  OP(SBC,ABY),  OP(PLX,IMP),  ___,          ___,          OP(SBC,ABX),  OP(INC,ABX),  OP(BBS7,ZPR)   // F8
};

#undef OP
#undef ___

// State for the deferred, full-instruction decode: carried across clock cycles while an
// instruction's operand bytes are being collected, so the fully-resolved instruction (e.g.
// "JSR $1234") can be appended to the line once the last operand byte has been read.

uint8_t decode_pending = 0;         // operand bytes still to be read for the in-flight instruction
uint8_t decode_have = 0;            // operand bytes already read for the in-flight instruction
uint8_t decode_operand[2];          // operand bytes, in the order they were fetched (low byte first)
uint16_t decode_pc = 0;             // address bus value when the in-flight opcode was fetched
uint8_t decode_operation = OP_UNK;  // the in-flight opcode's operation
uint8_t decode_mode = MODE_NONE;    // the in-flight opcode's addressing mode

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
// (e.g. "JSR abs", "ORA (zp,X)", "BBR0 zp,rel", "NOP", "???").
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

// Build the fully-decoded instruction (e.g. "JSR $1234", "ORA ($12,X)", "BBR0 $12,$3456")
// once all of an instruction's operand bytes have been captured. `pc` is the address the
// opcode itself was fetched from (needed to resolve relative-branch targets).
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
    // offset is added to the opcode's address plus the instruction's total length.
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

// Setup the Arduino for monitoring ...
void setup() {
  // Address pins
  for (int n = 0; n < 16; n += 1) {
    pinMode(ADDR[n], INPUT);
  }

  // Data pins
  for (int n = 0; n < 8; n += 1) {
    pinMode(DATA[n], INPUT);
  }

  // Control pins
  pinMode(CLOCK, INPUT);
  pinMode(READ_WRITE, INPUT);
  pinMode(SYNC, INPUT);

  // Interrupt service routine called on rising CLOCK signal
  attachInterrupt(digitalPinToInterrupt(CLOCK), onClock, RISING);

  Serial.begin(57600);
}

// Interrupt handler, so we only run on a clock pulse
void onClock() {
  char output[64];
  char mnemonic[16];
  char decoded[20];
  decoded[0] = '\0';

  // Read the address, and send a binary version to the serial output
  unsigned int address = 0;
  for (int n = 0; n < 16; n += 1) {
    int bit = digitalRead(ADDR[n]) ? 1 : 0;
    Serial.print(bit);
    address = (address << 1) + bit;
  }

  Serial.print("   ");

  // Read the data byte, and send a binary version to the serial output
  unsigned int data = 0;
  for (int n = 0; n < 8; n += 1) {
    int bit = digitalRead(DATA[n]) ? 1 : 0;
    Serial.print(bit);
    data = (data << 1) + bit;
  }

  // The 6502/W65C02S's SYNC pin is HIGH when reading an OPCODE (rather than
  // data or an address, etc.); get the OPCODE when appropriate or "---" if not.
  if (digitalRead(SYNC)) {
    lookupOpcode((uint8_t)data, &decode_operation, &decode_mode);
    formatOpcode(mnemonic, sizeof(mnemonic), decode_operation, decode_mode);

    // Start (or restart) tracking this instruction's operand bytes, if it has any.
    decode_pending = operandByteCount(decode_mode);
    decode_have = 0;
    decode_pc = address;

    // Implied/accumulator instructions (e.g. NOP, INY) take no operand bytes, so
    // there's nothing to wait for — the "decoded" instruction is the mnemonic
    // itself, and it's appended right away on this same line.
    if (decode_pending == 0) {
      formatDecoded(decoded, sizeof(decoded), decode_operation, decode_mode,
                    decode_operand, decode_pc);
    }
  } else {
    strcpy(mnemonic, "---");

    // Collect this instruction's operand bytes as they go by; once the last one
    // arrives, resolve the full instruction so it can be appended to this line.
    if (decode_pending > 0) {
      decode_operand[decode_have] = (uint8_t)data;
      decode_have += 1;
      decode_pending -= 1;

      if (decode_pending == 0) {
        formatDecoded(decoded, sizeof(decoded), decode_operation, decode_mode,
                      decode_operand, decode_pc);
      }
    }
  }

  // Format and output the current address, data, R/W flag and opcode, plus the
  // fully-decoded instruction, if one just completed on this line.
  sprintf(output, "   %04x  %c %02x  %-12s", address, digitalRead(READ_WRITE) ? 'r' : 'W', data, mnemonic);
  if (decoded[0]) {
    strcat(output, "  ");
    strcat(output, decoded);
  }
  Serial.println(output);
}

void loop() {
}
