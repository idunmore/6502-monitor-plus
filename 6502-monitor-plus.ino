// Extended version of Ben Eater's (https://eater.net) "6502-monitor.ino" sketch.
//
// Adds opcode/instruction mnemonic, and addressing mode, when CPU is reading an OPCODE
// (as indicated by the W65C02S's SYNC pin being HIGH).

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

// W65C02S opcode table in flash ("PROGMEM") — 256 × 12 bytes = 3072 bytes of flash,
// rather than in SRAM; indexed directly by the data (OPCODE) byte.
//
// Addressing mode notation:
//
//   zp = zero page | abs = absolute | # = immediate | () = indirect | , X or ,Y indexed
//   (zp,X) = zero page indexed indirect | (zp),Y = zero page indirect, indexed 
const char OPCODES[256][12] PROGMEM = {
  "BRK",      "ORA (zp,X)",  "???",       "???",       "TSB zp",       "ORA zp",     "ASL zp",     "RMB0 zp",      // 00
  "PHP",      "ORA #",       "ASL",       "???",       "TSB abs",      "ORA abs",    "ASL abs",    "BBR0 rel",     // 08
  "BPL rel",  "ORA (zp),Y",  "ORA (zp)",  "???",       "TRB zp",       "ORA zp,X",   "ASL zp,X",   "RMB1 zp",      // 10
  "CLC",      "ORA abs,Y",   "INC",       "???",       "TRB abs",      "ORA abs,X",  "ASL abs,X",  "BBR1 rel",     // 18
  "JSR abs",  "AND (zp,X)",  "???",       "???",       "BIT zp",       "AND zp",     "ROL zp",     "RMB2 zp",      // 20
  "PLP",      "AND #",       "ROL",       "???",       "BIT abs",      "AND abs",    "ROL abs",    "BBR2 rel",     // 28
  "BMI rel"   "AND (zp),Y",  "AND (zp)",  "???",       "BIT zp,X",     "AND zp,X",   "ROL zp,X",   "RMB3 zp",      // 30
  "SEC",      "AND abs,Y",   "DEC",       "???",       "BIT abs,X",    "AND abs,X",  "ROL abs,X",  "BBR3 el",      // 38
  "RTI",      "EOR (zp,X)",  "???",       "???",       "???",          "EOR zp",     "LSR zp",     "RMB4 zp",      // 40
  "PHA",      "EOR #",       "LSR",       "???",       "JMP abs",      "EOR abs",    "LSR abs",    "BBR4 rel",     // 48
  "BVC rel",  "EOR (zp),Y",  "EOR (zp)",  "???",       "???",          "EOR zp,X",   "LSR zp,X",   "RMB5 zp",      // 50
  "CLI",      "EOR abs,Y",   "PHY",       "???",       "???",          "EOR abs,X",  "LSR abs,X",  "BBR5 rel",     // 58
  "RTS",      "ADC (zp,X)",  "???",       "???",       "STZ zp",       "ADC zp",     "ROR zp",     "RMB6 zp",      // 60
  "PLA",      "ADC #",       "ROR",       "???",       "JMP (abs)",    "ADC abs",    "ROR abs",    "BBR6 rel",     // 68
  "BVS rel",  "ADC (zp),Y",  "ADC (zp)",  "???",       "STZ zp,x",     "ADC zp,X",   "ROR zp,X",   "RMB7 zp",      // 70
  "SEI",      "ADC abs,Y",   "PLY",       "???",       "JMP (abs,X)",  "ADC abs,X",  "ROR abs,X",  "BBR7 rel",     // 78
  "BRA rel",  "STA (zp,X)",  "???",       "???",       "STY zp",       "STA zp",     "STX zp",     "SMB0 zp",      // 80
  "DEY",      "BIT #",       "TXA",       "???",       "STY abs",      "STA abs",    "STX abs",    "BBS0 rel",     // 88
  "BCC rel",  "STA (zp),Y",  "STA (zp)",  "???",       "STY zp,X",     "STA zp,X",   "STX zp,Y",   "SMB1 zp",      // 90
  "TYA",      "STA abs,Y",   "TXS",       "???",       "STZ abs",      "STA abs,X",  "STX abs,X",  "BBS1 rel",     // 98
  "LDY #",    "LDA (zp,X)",  "LDX #",     "???",       "LDY zp",       "LDA zp",     "LDX zp",     "SMB2 zp",      // A0
  "TAY",      "LDA #",       "TAX",       "???",       "LDY abs",      "LDA abs",    "LDX abs",    "BBS2 rel",     // A8
  "BCS",      "LDA (zp),Y",  "LDA (zp)",  "???",       "LDY zp,X",     "LDA zp,X",   "LDX zp,Y",   "SMB3 zp",      // B0
  "CLV",      "LDA abs,Y",   "TSX",       "???",       "LDY abs,X",    "LDA abs,X",  "LDX abs,Y",  "BBS3 rel",     // B8
  "CPY #",    "CMP (zp,X)",  "???",       "???",       "CPY zp",       "CMP zp",     "DEC zp",     "SMB4 zp",      // C0
  "INY",      "CMP #",       "DEX",       "WAI",       "CPY abs",      "CMP abs",    "DEC abs",    "BBS4 rel",     // C8
  "BNE rel",  "CMP (zp),Y",  "CMP (zp)",  "???",       "???",          "CMP zp,X",   "DEC zp,X",   "SMB5 zp",      // D0
  "CLD",      "CMP abs,Y",   "PHX",       "STP",       "???",          "CMP abs,X",  "DEC abs,X",  "BBS5 rel",     // D8
  "CPX #",    "SBC (zp,X)",  "???",       "???",       "CPX zp",       "SBC zp",     "INC zp",     "SMB6 zp",      // E0
  "INX",      "SBC #",       "NOP",       "???",       "CPX abs",      "SBC abs",    "INC abs",    "BBS6 rel",     // E8
  "BEQ",      "SBC (zp),Y",  "SBC (zp)",  "???",       "???",          "SBC zp,X",   "INC zp,X",   "SMB7 zp",      // F0
  "SED",      "SBC abs,Y",   "PLX",       "???",       "???",          "SBC abs,X",  "INC abs,X",  "BBS7 rel"      // F8
};

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
  char output[32];
  char mnemonic[12];

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
    memcpy_P(mnemonic, OPCODES[data], sizeof(mnemonic));  
  } else {
    strcpy(mnemonic, "---");
  }

  // Format and output the current address, data, R/W flag and opcode
  sprintf(output, "   %04x  %c %02x  %s", address, digitalRead(READ_WRITE) ? 'r' : 'W', data, mnemonic);
  Serial.println(output);
}

void loop() {
}
