// Instruction Set Tables
//
// The single definition of every table declared in instruction_set.h. Each one is in
// PROGMEM, so it lives in flash and is thus read with pgm_read_byte()/strcpy_P() rather
// than being addressed directly.
//
// The static_asserts below are the reason each table is defined with an unspecified bound:
// the initialiser states the length, and the assert checks it against the enum. A table
// that fell out of step with its enum would otherwise index silently off the end.

#include "instruction_set.h"

// Mnemonic text, indexed by Operation. Order MUST match the enum.
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

// Addressing-mode text appended after the mnemonic. Order MUST match the enum.
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

// Operand bytes that follow the opcode, per addressing mode. Order MUST match the enum.
//
// MODE_NONE is 0: the undefined opcodes are treated as single-byte, which is what the
// monitor has always assumed. On real silicon their lengths vary (most are 1 byte, but
// e.g. $02 is 2 and $5C is 3), so a run of them will desynchronise the operand tracking.
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

// Shorthand used only by the opcode table below
#define OP(operation, mode) { OP_##operation, MODE_##mode }
#define ___                 { OP_UNK, MODE_NONE }

// W65C02S opcode table, indexed directly by the data (OPCODE) byte
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
