#ifndef DECODE_H
#define DECODE_H

#include "instruction_set.h"

// Instruction "decoding" and formatting.
//
// Turns an opcode byte, plus the operand bytes seen on the bus after it, into display text.
//
// The state is shared rather than private because the CPU model reads it too: what an
// instruction did to the CPU depends on which instruction it was and where it was fetched from.
// So declared here, and defined in the .cpp.

extern uint8_t decode_pending;      // Operand bytes still to be read for the in-flight instruction
extern uint8_t decode_have;         // Operand bytes already read for the in-flight instruction
extern uint8_t decode_operand[2];   // Operand bytes, in the order they were fetched (low byte first)
extern uint16_t decode_pc;          // Address bus value when the in-flight opcode was fetched
extern uint8_t decode_operation;    // In-flight opcode's operation
extern uint8_t decode_mode;         // In-flight opcode's addressing mode

// Look up an opcode byte's operation and addressing mode
void lookupOpcode(uint8_t opcode, uint8_t *operation, uint8_t *mode);

// How many operand bytes follow an opcode in this addressing mode
uint8_t operandByteCount(uint8_t mode);

// Build the mnemonic-plus-addressing-mode text shown as soon as an opcode is fetched
// (e.g. "JSR abs", "ORA (zp,X)", "BBR0 zp,rel", "NOP", "???")
void formatOpcode(char *out, size_t outSize, uint8_t operation, uint8_t mode);

// Build the fully-decoded instruction (e.g. "JSR $1234") once all of its operand bytes have
// been captured. `pc` is the address the opcode itself was fetched from
void formatDecoded(char *out, size_t outSize, uint8_t operation, uint8_t mode,
                   const uint8_t *operand, uint16_t pc);

#endif  // DECODE_H
