#ifndef CPU_MODEL_H
#define CPU_MODEL_H

#include "instruction_set.h"


// CPU Model - Public Interface
//
// The status register never fully appears on the bus during normal execution, since not all flags
// are "physical", and only appears at all with certain instructions (e.g., PHP/PLP/BRK/RTI) so the
// only way to get the flags on a per-instruction basis is to "model" them.  This is the public
// interface to that modelling logic (cpu_model.cpp, unsurprisingly).
//
// The modelled registers, the status register and its "known" bits, and the captured bus
// traffic are all private to cpu_model.cpp: nothing outside the model has any business
// reading them.
//
// Usage, per bus cycle:
//
//   opcode fetch  ->  completeInstruction(address)         // resolve the instruction just ended
//                     busCaptureReset()                    // then start capturing the next one
//   anything else ->  busCapture(address, data, isWrite)
//
// and formatStatus() renders the result for display.


// True once an instruction has been seen from its opcode fetch onwards, so its bus cycles
// are being captured. False at power-on, when the monitor may have started watching midway
// through an instruction and completing it would produce nonsense.
extern bool cpu_tracking;

// Discard the bus traffic captured for the previous instruction, ready for the next one.
void busCaptureReset();

// Record one bus cycle belonging to the in-flight instruction. Operand fetches must be
// filtered out by the caller; everything else is a data, stack, dummy or vector cycle.
void busCapture(uint16_t address, uint8_t data, bool isWrite);

// Work out what the instruction that has just finished did to the CPU, using the bus traffic
// captured while it executed. Called when the next opcode fetch shows the previous
// instruction has run to completion; `nextFetch` is the address that opcode came from.
void completeInstruction(uint16_t nextFetch);

// Forget everything. Used at startup and on reset, where nothing about the CPU is known.
void cpuForgetAll();

// Render the status register as eight flag characters plus a divergence marker: an upper
// case letter means the flag is set, lower case means clear, '?' means the model does not
// know, and '-' marks the two bit positions that are not real flags. A trailing '!' means
// the model disagreed with a status register value seen on the bus.
void formatStatus(char *out, size_t outSize);

#endif  // CPU_MODEL_H
