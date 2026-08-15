// Extended version of Ben Eater's (https://eater.net) "6502-monitor.ino" sketch.
//
// Adds opcode/instruction mnemonic, and addressing mode, when CPU is reading an OPCODE
// (as indicated by the W65C02S's SYNC pin being HIGH). Fully decodes multi-byte instructions
// appending the resolved instruction (e.g. "JSR $1234") to the end of the line once decoded.
//
// Includes a "modelled" (rather than fully-emulated) display of the 6502's status register,
// showing the state of the CPU's status flags as they are PRIOR to the execution of the
// instruction shown to their right; this lets you see what the instruction SHOULD do if it
// is using flags to direct behavior (ADC/SBC, branches, etc.).

// This work, "6502-monitor_plus" is an extension to "6502-monitor.ino", by Ben Eater,
// used under CC BY 4.0.  "6502-monitor_plus" is, similarly, licensed under CC BY 4.0
// by Ian Dunmore.
//
// Copyright (C) 2026, Ian Dunmore
// License: CC BY 4.0

// Basic mode of operation is that all hardware interaction occurs within the code in this file,
// which samples the W65C02S bus on clock tick, hands the result to the decoder (and formatter),
// and the CPU model, to determine the various flag states, and then outputs one line per bus
// cycle to the serial output.

// The rest of the "project" is:
//
//   decode.cpp           Opcode -> mnemonic, addressing mode and decoded instruction/formatting
//   cpu_model.cpp        Works out the CPU's flags and registers from the bus traffic
//   instruction_set.cpp  W65C02S opcode/ mnemonic tables, in flash
//
// Header files only expose what's needed/forward-declared, since C++ makes prototypes optional.

#include "decode.h"
#include "cpu_model.h"

// Address line PINS (52 is LSB)
const char ADDR[16] = {22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52};

// Data PINS (53 is LSB)
const char DATA[8] = {39, 41, 43, 45, 47, 49, 51, 53};

// Arduino Pin #   -> // W65C02S pin
#define CLOCK 2       // W65C02S pin 37
#define READ_WRITE 3  // W65C02S pin 34
#define SYNC 4        // W65C02S pin 7

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

// Everything happens in the clock interrupt handler, so there is nothing to do here
void loop() {
}

// Interrupt handler, so we only run on a clock pulse
void onClock() {
  char output[80];
  char mnemonic[16];
  char decoded[20];
  char status[10];
  decoded[0] = '\0';

  // The original code read the address bus, output that information to the serial port,
  // then read the data bus, and output that, etc.  This, along with the additional processing
  // for the status flag, was causing "split reads" (the address changes between being read and
  // reading the data bus, so the data bus contained the "next" value, not the current one) at
  // lower clock speeds than I was happy with.
  //
  // Here, I've updated the code to read both address and data buses, as well as the R/W and
  // SYNC pins *before* outputting *anything* over serial.
  //
  // Serial.print() at 57600 baud takes about 174us per character and blocks once the transmit
  // buffer fills, so printing each bit as it is read, per the above, spreads the sample over
  // milliseconds.
  //
  // On a clock fast enough for that to cover a whole bus cycle, the ADDRESS ends up describing
  // an earlier cycle than the DATA and SYNC do: the opcode column still looks right, because
  // the opcode byte and SYNC are read together, but every address is stale by a cycle. Operand
  // tracking then fails to match its own instruction, and relative branch targets come out one
  // byte low.
  //
  // If you see things getting out of sync (it'll happen to the status flag first, as missed
  // instructions will upset the modeling), lower your clock speed.  The standard clock-module
  // is **more** than capable of outrunning even this optimized version of the monitor's
  // sampling code.  
  unsigned int address = 0;
  for (int n = 0; n < 16; n += 1) {
    address = (address << 1) + (digitalRead(ADDR[n]) ? 1 : 0);
  }

  unsigned int data = 0;
  for (int n = 0; n < 8; n += 1) {
    data = (data << 1) + (digitalRead(DATA[n]) ? 1 : 0);
  }

  bool isWrite = !digitalRead(READ_WRITE);

  // The 6502/W65C02S's SYNC pin is HIGH when reading an OPCODE (rather than
  // data or an address, etc.)
  bool isOpcode = digitalRead(SYNC);

  // Now the buses/pins are safely captured, send the binary address and data columns, most
  // significant bit first
  for (int n = 15; n >= 0; n -= 1) {
    Serial.print((int)((address >> n) & 1));
  }

  Serial.print("   ");

  for (int n = 7; n >= 0; n -= 1) {
    Serial.print((int)((data >> n) & 1));
  }

  // Get the OPCODE when appropriate, or "---" if this is not an opcode fetch
  if (isOpcode) {
    // A new opcode fetch means the previous instruction has run to completion and every
    // bus cycle it produced has been seen, so its effect on the CPU can now be worked out.
    // The flags shown from here until the next opcode fetch are therefore the state the
    // CPU is in as THIS instruction BEGINS — equivalently, the *result* of the one *before* it.
    if (cpu_tracking) {
      completeInstruction((uint16_t)address);
    }
    cpu_tracking = true;
    busCaptureReset();

    lookupOpcode((uint8_t)data, &decode_operation, &decode_mode);
    formatOpcode(mnemonic, sizeof(mnemonic), decode_operation, decode_mode);

    // Start (or restart) tracking this instruction's operand bytes, if it has any
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
    //
    // Operand bytes are the ones fetched from the addresses immediately following the
    // opcode. Checking the address rather than simply counting cycles keeps operands
    // distinct from the data, stack and dummy cycles mixed in among them and means an
    // instruction abandoned part-way, as happens when an interrupt takes over, is not
    // decoded as though it had run.
    if (decode_pending > 0 && !isWrite &&
        (uint16_t)address == (uint16_t)(decode_pc + 1 + decode_have)) {
      decode_operand[decode_have] = (uint8_t)data;
      decode_have += 1;
      decode_pending -= 1;

      if (decode_pending == 0) {
        formatDecoded(decoded, sizeof(decoded), decode_operation, decode_mode,
                      decode_operand, decode_pc);
      }
    } else if (cpu_tracking) {
      // Anything else is the instruction actually doing its work: reading an operand from
      // memory, using the stack, or one of the CPU's internal cycles
      busCapture((uint16_t)address, (uint8_t)data, isWrite);
    }
  }

  // Format and output the current address, data, R/W flag and opcode, plus the CPU
  // status flags and the fully-decoded instruction, if one just completed, on this line
  sprintf(output, "   %04x  %c %02x  %-12s", address, isWrite ? 'W' : 'r', data, mnemonic);
  if (decoded[0]) {
    formatStatus(status, sizeof(status));
    strcat(output, "  ");
    strcat(output, status);
    strcat(output, " ");
    strcat(output, decoded);
  }
  Serial.println(output);
}
