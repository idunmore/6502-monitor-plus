#ifndef INSTRUCTION_SET_H
#define INSTRUCTION_SET_H

// This header is included from both the sketch (.ino) and from plain .cpp files, which do
// not get the Arduino IDE's automatic <Arduino.h>, so we pull in what we need ourselves.
#include <Arduino.h>       // uint8_t, size_t
#include <avr/pgmspace.h>  // PROGMEM, pgm_read_byte, strcpy_P

// Instruction Set Description
//
// Each opcode is described as an (operation, addressing mode) pair rather than as a single
// display string. The display text is rebuilt from the pair on demand, and the same pair
// drives operand-length calculation and instruction formatting ... so there is exactly one
// place that knows what any given opcode is.
//
// This header *declares* the tables; they are defined once, in instruction_set.cpp.
// Defining them here instead would give every translation unit that includes the header its
// own private copy in flash — around 1.3K each, most of it the opcode table.

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
  OP_UNK,          // undefined/unimplemented opcode
  OPERATION_COUNT
};

// Addressing modes. Values are indices into MODE_SUFFIX[] and MODE_OPERAND_BYTES[], so all
// three MUST stay in the same order.
//
// IMP and ACC render identically (bare mnemonic) but are kept distinct as the emulator needs
// to tell "ASL A" from "ASL $1234".
enum AddressMode : uint8_t {
  MODE_IMP,           // Implied            e.g. NOP, CLC, RTS
  MODE_ACC,           // Accumulator        e.g. ASL A, INC A
  MODE_IMM,           // Immediate          #
  MODE_ZP,            // Zero page          zp
  MODE_ZPX,           // Zero page,X        zp,X
  MODE_ZPY,           // Zero page,Y        zp,Y
  MODE_ZPI,           // Zero page indirect (zp)
  MODE_IZX,           // Indexed indirect   (zp,X)
  MODE_IZY,           // Indirect indexed   (zp),Y
  MODE_ABS,           // Absolute           abs
  MODE_ABX,           // Absolute,X         abs,X
  MODE_ABY,           // Absolute,Y         abs,Y
  MODE_IND,           // Indirect           (abs)
  MODE_IAX,           // Indexed indirect   (abs,X)
  MODE_REL,           // Relative branch    rel
  MODE_ZPR,           // Zero page + branch zp,rel
  MODE_NONE,          // Undefined opcode
  ADDRESS_MODE_COUNT
};

// One entry per opcode, holding the operation and its addressing mode
struct OpcodeEntry {
  uint8_t operation;
  uint8_t mode;
};

// Mnemonic text, indexed by Operation.
//
// Declared with an unspecified first bound so the definition's initialiser remains the one
// place the length is stated; the element type is complete, so indexing and sizeof of an
// element both still work here.
extern const char MNEMONICS[][5] PROGMEM;

// Addressing-mode text appended after the mnemonic, indexed by AddressMode
extern const char MODE_SUFFIX[][8] PROGMEM;

// Operand bytes that follow the opcode, indexed by AddressMode
extern const uint8_t MODE_OPERAND_BYTES[] PROGMEM;

// W65C02S opcode table in flash ("PROGMEM"), indexed directly by the data (OPCODE) byte
extern const OpcodeEntry OPCODES[256] PROGMEM;

#endif  // INSTRUCTION_SET_H
