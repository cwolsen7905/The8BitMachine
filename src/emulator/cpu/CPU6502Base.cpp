#include "emulator/cpu/CPU6502Base.h"
#include "emulator/core/Bus.h"

#include <iomanip>
#include <sstream>

// ============================================================================
// NMOS dispatch table  (called by both CPU8502 and CPU65C02 constructors)
// ============================================================================

void CPU6502Base::buildNMOSTable() {
    using B = CPU6502Base;
    // clang-format off
    lookup_ = {{
    /* $00 */ {"BRK", &B::BRK, &B::IMP, 7}, /* $01 */ {"ORA", &B::ORA, &B::IZX, 6},
    /* $02 */ {"???", &B::XXX, &B::IMP, 2}, /* $03 */ {"???", &B::XXX, &B::IMP, 8},
    /* $04 */ {"???", &B::NOP, &B::ZP0, 3}, /* $05 */ {"ORA", &B::ORA, &B::ZP0, 3},
    /* $06 */ {"ASL", &B::ASL, &B::ZP0, 5}, /* $07 */ {"???", &B::XXX, &B::IMP, 5},
    /* $08 */ {"PHP", &B::PHP, &B::IMP, 3}, /* $09 */ {"ORA", &B::ORA, &B::IMM, 2},
    /* $0A */ {"ASL", &B::ASL, &B::ACC, 2}, /* $0B */ {"???", &B::XXX, &B::IMP, 2},
    /* $0C */ {"???", &B::NOP, &B::ABS, 4}, /* $0D */ {"ORA", &B::ORA, &B::ABS, 4},
    /* $0E */ {"ASL", &B::ASL, &B::ABS, 6}, /* $0F */ {"???", &B::XXX, &B::IMP, 6},

    /* $10 */ {"BPL", &B::BPL, &B::REL, 2}, /* $11 */ {"ORA", &B::ORA, &B::IZY, 5},
    /* $12 */ {"???", &B::XXX, &B::IMP, 2}, /* $13 */ {"???", &B::XXX, &B::IMP, 8},
    /* $14 */ {"???", &B::NOP, &B::ZPX, 4}, /* $15 */ {"ORA", &B::ORA, &B::ZPX, 4},
    /* $16 */ {"ASL", &B::ASL, &B::ZPX, 6}, /* $17 */ {"???", &B::XXX, &B::IMP, 6},
    /* $18 */ {"CLC", &B::CLC, &B::IMP, 2}, /* $19 */ {"ORA", &B::ORA, &B::ABY, 4},
    /* $1A */ {"???", &B::NOP, &B::IMP, 2}, /* $1B */ {"???", &B::XXX, &B::IMP, 7},
    /* $1C */ {"???", &B::NOP, &B::ABX, 4}, /* $1D */ {"ORA", &B::ORA, &B::ABX, 4},
    /* $1E */ {"ASL", &B::ASL, &B::ABX, 7}, /* $1F */ {"???", &B::XXX, &B::IMP, 7},

    /* $20 */ {"JSR", &B::JSR, &B::ABS, 6}, /* $21 */ {"AND", &B::AND, &B::IZX, 6},
    /* $22 */ {"???", &B::XXX, &B::IMP, 2}, /* $23 */ {"???", &B::XXX, &B::IMP, 8},
    /* $24 */ {"BIT", &B::BIT, &B::ZP0, 3}, /* $25 */ {"AND", &B::AND, &B::ZP0, 3},
    /* $26 */ {"ROL", &B::ROL, &B::ZP0, 5}, /* $27 */ {"???", &B::XXX, &B::IMP, 5},
    /* $28 */ {"PLP", &B::PLP, &B::IMP, 4}, /* $29 */ {"AND", &B::AND, &B::IMM, 2},
    /* $2A */ {"ROL", &B::ROL, &B::ACC, 2}, /* $2B */ {"???", &B::XXX, &B::IMP, 2},
    /* $2C */ {"BIT", &B::BIT, &B::ABS, 4}, /* $2D */ {"AND", &B::AND, &B::ABS, 4},
    /* $2E */ {"ROL", &B::ROL, &B::ABS, 6}, /* $2F */ {"???", &B::XXX, &B::IMP, 6},

    /* $30 */ {"BMI", &B::BMI, &B::REL, 2}, /* $31 */ {"AND", &B::AND, &B::IZY, 5},
    /* $32 */ {"???", &B::XXX, &B::IMP, 2}, /* $33 */ {"???", &B::XXX, &B::IMP, 8},
    /* $34 */ {"???", &B::NOP, &B::ZPX, 4}, /* $35 */ {"AND", &B::AND, &B::ZPX, 4},
    /* $36 */ {"ROL", &B::ROL, &B::ZPX, 6}, /* $37 */ {"???", &B::XXX, &B::IMP, 6},
    /* $38 */ {"SEC", &B::SEC, &B::IMP, 2}, /* $39 */ {"AND", &B::AND, &B::ABY, 4},
    /* $3A */ {"???", &B::NOP, &B::IMP, 2}, /* $3B */ {"???", &B::XXX, &B::IMP, 7},
    /* $3C */ {"???", &B::NOP, &B::ABX, 4}, /* $3D */ {"AND", &B::AND, &B::ABX, 4},
    /* $3E */ {"ROL", &B::ROL, &B::ABX, 7}, /* $3F */ {"???", &B::XXX, &B::IMP, 7},

    /* $40 */ {"RTI", &B::RTI, &B::IMP, 6}, /* $41 */ {"EOR", &B::EOR, &B::IZX, 6},
    /* $42 */ {"???", &B::XXX, &B::IMP, 2}, /* $43 */ {"???", &B::XXX, &B::IMP, 8},
    /* $44 */ {"???", &B::NOP, &B::ZP0, 3}, /* $45 */ {"EOR", &B::EOR, &B::ZP0, 3},
    /* $46 */ {"LSR", &B::LSR, &B::ZP0, 5}, /* $47 */ {"???", &B::XXX, &B::IMP, 5},
    /* $48 */ {"PHA", &B::PHA, &B::IMP, 3}, /* $49 */ {"EOR", &B::EOR, &B::IMM, 2},
    /* $4A */ {"LSR", &B::LSR, &B::ACC, 2}, /* $4B */ {"???", &B::XXX, &B::IMP, 2},
    /* $4C */ {"JMP", &B::JMP, &B::ABS, 3}, /* $4D */ {"EOR", &B::EOR, &B::ABS, 4},
    /* $4E */ {"LSR", &B::LSR, &B::ABS, 6}, /* $4F */ {"???", &B::XXX, &B::IMP, 6},

    /* $50 */ {"BVC", &B::BVC, &B::REL, 2}, /* $51 */ {"EOR", &B::EOR, &B::IZY, 5},
    /* $52 */ {"???", &B::XXX, &B::IMP, 2}, /* $53 */ {"???", &B::XXX, &B::IMP, 8},
    /* $54 */ {"???", &B::NOP, &B::ZPX, 4}, /* $55 */ {"EOR", &B::EOR, &B::ZPX, 4},
    /* $56 */ {"LSR", &B::LSR, &B::ZPX, 6}, /* $57 */ {"???", &B::XXX, &B::IMP, 6},
    /* $58 */ {"CLI", &B::CLI, &B::IMP, 2}, /* $59 */ {"EOR", &B::EOR, &B::ABY, 4},
    /* $5A */ {"???", &B::NOP, &B::IMP, 2}, /* $5B */ {"???", &B::XXX, &B::IMP, 7},
    /* $5C */ {"???", &B::NOP, &B::ABX, 4}, /* $5D */ {"EOR", &B::EOR, &B::ABX, 4},
    /* $5E */ {"LSR", &B::LSR, &B::ABX, 7}, /* $5F */ {"???", &B::XXX, &B::IMP, 7},

    /* $60 */ {"RTS", &B::RTS, &B::IMP, 6}, /* $61 */ {"ADC", &B::ADC, &B::IZX, 6},
    /* $62 */ {"???", &B::XXX, &B::IMP, 2}, /* $63 */ {"???", &B::XXX, &B::IMP, 8},
    /* $64 */ {"???", &B::NOP, &B::ZP0, 3}, /* $65 */ {"ADC", &B::ADC, &B::ZP0, 3},
    /* $66 */ {"ROR", &B::ROR, &B::ZP0, 5}, /* $67 */ {"???", &B::XXX, &B::IMP, 5},
    /* $68 */ {"PLA", &B::PLA, &B::IMP, 4}, /* $69 */ {"ADC", &B::ADC, &B::IMM, 2},
    /* $6A */ {"ROR", &B::ROR, &B::ACC, 2}, /* $6B */ {"???", &B::XXX, &B::IMP, 2},
    /* $6C */ {"JMP", &B::JMP, &B::IND, 5}, /* $6D */ {"ADC", &B::ADC, &B::ABS, 4},
    /* $6E */ {"ROR", &B::ROR, &B::ABS, 6}, /* $6F */ {"???", &B::XXX, &B::IMP, 6},

    /* $70 */ {"BVS", &B::BVS, &B::REL, 2}, /* $71 */ {"ADC", &B::ADC, &B::IZY, 5},
    /* $72 */ {"???", &B::XXX, &B::IMP, 2}, /* $73 */ {"???", &B::XXX, &B::IMP, 8},
    /* $74 */ {"???", &B::NOP, &B::ZPX, 4}, /* $75 */ {"ADC", &B::ADC, &B::ZPX, 4},
    /* $76 */ {"ROR", &B::ROR, &B::ZPX, 6}, /* $77 */ {"???", &B::XXX, &B::IMP, 6},
    /* $78 */ {"SEI", &B::SEI, &B::IMP, 2}, /* $79 */ {"ADC", &B::ADC, &B::ABY, 4},
    /* $7A */ {"???", &B::NOP, &B::IMP, 2}, /* $7B */ {"???", &B::XXX, &B::IMP, 7},
    /* $7C */ {"???", &B::NOP, &B::ABX, 4}, /* $7D */ {"ADC", &B::ADC, &B::ABX, 4},
    /* $7E */ {"ROR", &B::ROR, &B::ABX, 7}, /* $7F */ {"???", &B::XXX, &B::IMP, 7},

    /* $80 */ {"???", &B::NOP, &B::IMM, 2}, /* $81 */ {"STA", &B::STA, &B::IZX, 6},
    /* $82 */ {"???", &B::NOP, &B::IMM, 2}, /* $83 */ {"???", &B::XXX, &B::IMP, 6},
    /* $84 */ {"STY", &B::STY, &B::ZP0, 3}, /* $85 */ {"STA", &B::STA, &B::ZP0, 3},
    /* $86 */ {"STX", &B::STX, &B::ZP0, 3}, /* $87 */ {"???", &B::XXX, &B::IMP, 3},
    /* $88 */ {"DEY", &B::DEY, &B::IMP, 2}, /* $89 */ {"???", &B::NOP, &B::IMM, 2},
    /* $8A */ {"TXA", &B::TXA, &B::IMP, 2}, /* $8B */ {"???", &B::XXX, &B::IMP, 2},
    /* $8C */ {"STY", &B::STY, &B::ABS, 4}, /* $8D */ {"STA", &B::STA, &B::ABS, 4},
    /* $8E */ {"STX", &B::STX, &B::ABS, 4}, /* $8F */ {"???", &B::XXX, &B::IMP, 4},

    /* $90 */ {"BCC", &B::BCC, &B::REL, 2}, /* $91 */ {"STA", &B::STA, &B::IZY, 6},
    /* $92 */ {"???", &B::XXX, &B::IMP, 2}, /* $93 */ {"???", &B::XXX, &B::IMP, 6},
    /* $94 */ {"STY", &B::STY, &B::ZPX, 4}, /* $95 */ {"STA", &B::STA, &B::ZPX, 4},
    /* $96 */ {"STX", &B::STX, &B::ZPY, 4}, /* $97 */ {"???", &B::XXX, &B::IMP, 4},
    /* $98 */ {"TYA", &B::TYA, &B::IMP, 2}, /* $99 */ {"STA", &B::STA, &B::ABY, 5},
    /* $9A */ {"TXS", &B::TXS, &B::IMP, 2}, /* $9B */ {"???", &B::XXX, &B::IMP, 5},
    /* $9C */ {"???", &B::NOP, &B::ABS, 5}, /* $9D */ {"STA", &B::STA, &B::ABX, 5},
    /* $9E */ {"???", &B::XXX, &B::IMP, 5}, /* $9F */ {"???", &B::XXX, &B::IMP, 5},

    /* $A0 */ {"LDY", &B::LDY, &B::IMM, 2}, /* $A1 */ {"LDA", &B::LDA, &B::IZX, 6},
    /* $A2 */ {"LDX", &B::LDX, &B::IMM, 2}, /* $A3 */ {"???", &B::XXX, &B::IMP, 6},
    /* $A4 */ {"LDY", &B::LDY, &B::ZP0, 3}, /* $A5 */ {"LDA", &B::LDA, &B::ZP0, 3},
    /* $A6 */ {"LDX", &B::LDX, &B::ZP0, 3}, /* $A7 */ {"???", &B::XXX, &B::IMP, 3},
    /* $A8 */ {"TAY", &B::TAY, &B::IMP, 2}, /* $A9 */ {"LDA", &B::LDA, &B::IMM, 2},
    /* $AA */ {"TAX", &B::TAX, &B::IMP, 2}, /* $AB */ {"???", &B::XXX, &B::IMP, 2},
    /* $AC */ {"LDY", &B::LDY, &B::ABS, 4}, /* $AD */ {"LDA", &B::LDA, &B::ABS, 4},
    /* $AE */ {"LDX", &B::LDX, &B::ABS, 4}, /* $AF */ {"???", &B::XXX, &B::IMP, 4},

    /* $B0 */ {"BCS", &B::BCS, &B::REL, 2}, /* $B1 */ {"LDA", &B::LDA, &B::IZY, 5},
    /* $B2 */ {"???", &B::XXX, &B::IMP, 2}, /* $B3 */ {"???", &B::XXX, &B::IMP, 5},
    /* $B4 */ {"LDY", &B::LDY, &B::ZPX, 4}, /* $B5 */ {"LDA", &B::LDA, &B::ZPX, 4},
    /* $B6 */ {"LDX", &B::LDX, &B::ZPY, 4}, /* $B7 */ {"???", &B::XXX, &B::IMP, 4},
    /* $B8 */ {"CLV", &B::CLV, &B::IMP, 2}, /* $B9 */ {"LDA", &B::LDA, &B::ABY, 4},
    /* $BA */ {"TSX", &B::TSX, &B::IMP, 2}, /* $BB */ {"???", &B::XXX, &B::IMP, 4},
    /* $BC */ {"LDY", &B::LDY, &B::ABX, 4}, /* $BD */ {"LDA", &B::LDA, &B::ABX, 4},
    /* $BE */ {"LDX", &B::LDX, &B::ABY, 4}, /* $BF */ {"???", &B::XXX, &B::IMP, 4},

    /* $C0 */ {"CPY", &B::CPY, &B::IMM, 2}, /* $C1 */ {"CMP", &B::CMP, &B::IZX, 6},
    /* $C2 */ {"???", &B::NOP, &B::IMM, 2}, /* $C3 */ {"???", &B::XXX, &B::IMP, 8},
    /* $C4 */ {"CPY", &B::CPY, &B::ZP0, 3}, /* $C5 */ {"CMP", &B::CMP, &B::ZP0, 3},
    /* $C6 */ {"DEC", &B::DEC, &B::ZP0, 5}, /* $C7 */ {"???", &B::XXX, &B::IMP, 5},
    /* $C8 */ {"INY", &B::INY, &B::IMP, 2}, /* $C9 */ {"CMP", &B::CMP, &B::IMM, 2},
    /* $CA */ {"DEX", &B::DEX, &B::IMP, 2}, /* $CB */ {"???", &B::XXX, &B::IMP, 2},
    /* $CC */ {"CPY", &B::CPY, &B::ABS, 4}, /* $CD */ {"CMP", &B::CMP, &B::ABS, 4},
    /* $CE */ {"DEC", &B::DEC, &B::ABS, 6}, /* $CF */ {"???", &B::XXX, &B::IMP, 6},

    /* $D0 */ {"BNE", &B::BNE, &B::REL, 2}, /* $D1 */ {"CMP", &B::CMP, &B::IZY, 5},
    /* $D2 */ {"???", &B::XXX, &B::IMP, 2}, /* $D3 */ {"???", &B::XXX, &B::IMP, 8},
    /* $D4 */ {"???", &B::NOP, &B::ZPX, 4}, /* $D5 */ {"CMP", &B::CMP, &B::ZPX, 4},
    /* $D6 */ {"DEC", &B::DEC, &B::ZPX, 6}, /* $D7 */ {"???", &B::XXX, &B::IMP, 6},
    /* $D8 */ {"CLD", &B::CLD, &B::IMP, 2}, /* $D9 */ {"CMP", &B::CMP, &B::ABY, 4},
    /* $DA */ {"???", &B::NOP, &B::IMP, 2}, /* $DB */ {"???", &B::XXX, &B::IMP, 7},
    /* $DC */ {"???", &B::NOP, &B::ABX, 4}, /* $DD */ {"CMP", &B::CMP, &B::ABX, 4},
    /* $DE */ {"DEC", &B::DEC, &B::ABX, 7}, /* $DF */ {"???", &B::XXX, &B::IMP, 7},

    /* $E0 */ {"CPX", &B::CPX, &B::IMM, 2}, /* $E1 */ {"SBC", &B::SBC, &B::IZX, 6},
    /* $E2 */ {"???", &B::NOP, &B::IMM, 2}, /* $E3 */ {"???", &B::XXX, &B::IMP, 8},
    /* $E4 */ {"CPX", &B::CPX, &B::ZP0, 3}, /* $E5 */ {"SBC", &B::SBC, &B::ZP0, 3},
    /* $E6 */ {"INC", &B::INC, &B::ZP0, 5}, /* $E7 */ {"???", &B::XXX, &B::IMP, 5},
    /* $E8 */ {"INX", &B::INX, &B::IMP, 2}, /* $E9 */ {"SBC", &B::SBC, &B::IMM, 2},
    /* $EA */ {"NOP", &B::NOP, &B::IMP, 2}, /* $EB */ {"???", &B::SBC, &B::IMM, 2},
    /* $EC */ {"CPX", &B::CPX, &B::ABS, 4}, /* $ED */ {"SBC", &B::SBC, &B::ABS, 4},
    /* $EE */ {"INC", &B::INC, &B::ABS, 6}, /* $EF */ {"???", &B::XXX, &B::IMP, 6},

    /* $F0 */ {"BEQ", &B::BEQ, &B::REL, 2}, /* $F1 */ {"SBC", &B::SBC, &B::IZY, 5},
    /* $F2 */ {"???", &B::XXX, &B::IMP, 2}, /* $F3 */ {"???", &B::XXX, &B::IMP, 8},
    /* $F4 */ {"???", &B::NOP, &B::ZPX, 4}, /* $F5 */ {"SBC", &B::SBC, &B::ZPX, 4},
    /* $F6 */ {"INC", &B::INC, &B::ZPX, 6}, /* $F7 */ {"???", &B::XXX, &B::IMP, 6},
    /* $F8 */ {"SED", &B::SED, &B::IMP, 2}, /* $F9 */ {"SBC", &B::SBC, &B::ABY, 4},
    /* $FA */ {"???", &B::NOP, &B::IMP, 2}, /* $FB */ {"???", &B::XXX, &B::IMP, 7},
    /* $FC */ {"???", &B::NOP, &B::ABX, 4}, /* $FD */ {"SBC", &B::SBC, &B::ABX, 4},
    /* $FE */ {"INC", &B::INC, &B::ABX, 7}, /* $FF */ {"???", &B::XXX, &B::IMP, 7},
    }};
    // clang-format on
}

// ============================================================================
// Bus / stack helpers
// ============================================================================

uint8_t CPU6502Base::busRead(uint16_t addr)          { return bus_ ? bus_->read(addr) : 0x00; }
void    CPU6502Base::busWrite(uint16_t addr, uint8_t v) { if (bus_) bus_->write(addr, v); }

void    CPU6502Base::stackPush(uint8_t val) { busWrite(0x0100 + SP, val); --SP; }
uint8_t CPU6502Base::stackPop()             { ++SP; return busRead(0x0100 + SP); }

void CPU6502Base::setFlag(Flags f, bool v) {
    if (v) P |=  static_cast<uint8_t>(f);
    else   P &= ~static_cast<uint8_t>(f);
}
bool CPU6502Base::getFlag(Flags f) const { return (P & static_cast<uint8_t>(f)) != 0; }

uint8_t CPU6502Base::fetch() {
    if (lookup_[opcode_].addrmode != &CPU6502Base::IMP &&
        lookup_[opcode_].addrmode != &CPU6502Base::ACC)
        fetched_ = busRead(addrAbs_);
    return fetched_;
}

// ============================================================================
// Lifecycle
// ============================================================================

void CPU6502Base::connectBus(Bus* bus) { bus_ = bus; }

void CPU6502Base::reset() {
    uint16_t lo = busRead(0xFFFC);
    uint16_t hi = busRead(0xFFFD);
    PC = (hi << 8) | lo;
    A = X = Y = 0x00;
    SP       = 0xFD;
    P        = 0x24;
    fetched_ = 0x00;
    addrAbs_ = addrRel_ = 0x0000;
    cycles_  = 8;
}

void CPU6502Base::clock() {
    if (cycles_ == 0) {
        opcode_ = busRead(PC++);
        setFlag(U, true);
        cycles_ = lookup_[opcode_].cycles;
        uint8_t e1 = (this->*lookup_[opcode_].addrmode)();
        uint8_t e2 = (this->*lookup_[opcode_].operate )();
        cycles_ += (e1 & e2);
        setFlag(U, true);
    }
    --cycles_;
}

bool CPU6502Base::complete() const { return cycles_ == 0; }

void CPU6502Base::irq() {
    if (getFlag(I)) return;
    stackPush((PC >> 8) & 0xFF);
    stackPush(PC & 0xFF);
    setFlag(B, false); setFlag(U, true);
    stackPush(P);
    setFlag(I, true);
    uint16_t lo = busRead(0xFFFE);
    uint16_t hi = busRead(0xFFFF);
    PC = (hi << 8) | lo;
    cycles_ = 7;
}

void CPU6502Base::nmi() {
    stackPush((PC >> 8) & 0xFF);
    stackPush(PC & 0xFF);
    setFlag(B, false); setFlag(U, true);
    stackPush(P);
    setFlag(I, true);
    uint16_t lo = busRead(0xFFFA);
    uint16_t hi = busRead(0xFFFB);
    PC = (hi << 8) | lo;
    cycles_ = 8;
}

// ============================================================================
// Addressing modes
// ============================================================================

uint8_t CPU6502Base::IMP() { fetched_ = A; return 0; }
uint8_t CPU6502Base::ACC() { fetched_ = A; return 0; }
uint8_t CPU6502Base::IMM() { addrAbs_ = PC++; return 0; }
uint8_t CPU6502Base::ZP0() { addrAbs_ = busRead(PC++) & 0x00FF; return 0; }
uint8_t CPU6502Base::ZPX() { addrAbs_ = (busRead(PC++) + X) & 0x00FF; return 0; }
uint8_t CPU6502Base::ZPY() { addrAbs_ = (busRead(PC++) + Y) & 0x00FF; return 0; }

uint8_t CPU6502Base::ABS() {
    uint16_t lo = busRead(PC++); uint16_t hi = busRead(PC++);
    addrAbs_ = (hi << 8) | lo;  return 0;
}
uint8_t CPU6502Base::ABX() {
    uint16_t lo = busRead(PC++); uint16_t hi = busRead(PC++);
    uint16_t base = (hi << 8) | lo;
    addrAbs_ = base + X;
    return (addrAbs_ & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}
uint8_t CPU6502Base::ABY() {
    uint16_t lo = busRead(PC++); uint16_t hi = busRead(PC++);
    uint16_t base = (hi << 8) | lo;
    addrAbs_ = base + Y;
    return (addrAbs_ & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

// IND — JMP indirect.  nmosBug_ = true reproduces the NMOS page-wrap bug;
// false (65C02) uses the correct address.
uint8_t CPU6502Base::IND() {
    uint16_t plo = busRead(PC++); uint16_t phi = busRead(PC++);
    uint16_t ptr = (phi << 8) | plo;
    if (nmosBug_ && (plo == 0x00FF))
        addrAbs_ = (static_cast<uint16_t>(busRead(ptr & 0xFF00)) << 8) | busRead(ptr);
    else
        addrAbs_ = (static_cast<uint16_t>(busRead(ptr + 1)) << 8) | busRead(ptr);
    return 0;
}

uint8_t CPU6502Base::IZX() {
    uint16_t t  = busRead(PC++);
    uint16_t lo = busRead((t + X    ) & 0x00FF);
    uint16_t hi = busRead((t + X + 1) & 0x00FF);
    addrAbs_ = (hi << 8) | lo;  return 0;
}
uint8_t CPU6502Base::IZY() {
    uint16_t t    = busRead(PC++);
    uint16_t lo   = busRead(t & 0x00FF);
    uint16_t hi   = busRead((t + 1) & 0x00FF);
    uint16_t base = (hi << 8) | lo;
    addrAbs_ = base + Y;
    return (addrAbs_ & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}
uint8_t CPU6502Base::REL() {
    addrRel_ = busRead(PC++);
    if (addrRel_ & 0x80) addrRel_ |= 0xFF00;
    return 0;
}

// 65C02: ($zp) — zero-page indirect
uint8_t CPU6502Base::IZP() {
    uint8_t  zp  = busRead(PC++);
    uint16_t lo  = busRead(zp & 0x00FF);
    uint16_t hi  = busRead((zp + 1) & 0x00FF);
    addrAbs_ = (hi << 8) | lo;  return 0;
}

// 65C02: ($abs,X) — absolute indexed indirect (used only by JMP $7C)
uint8_t CPU6502Base::AIIX() {
    uint16_t lo  = busRead(PC++); uint16_t hi = busRead(PC++);
    uint16_t ptr = ((hi << 8) | lo) + X;
    addrAbs_ = (static_cast<uint16_t>(busRead(ptr + 1)) << 8) | busRead(ptr);
    return 0;
}

// ============================================================================
// Instructions — NMOS / shared
// ============================================================================

uint8_t CPU6502Base::ADC() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(A)
                 + static_cast<uint16_t>(fetched_)
                 + static_cast<uint16_t>(getFlag(C));
    setFlag(C, tmp > 0x00FF);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    setFlag(V, (~(static_cast<uint16_t>(A) ^ static_cast<uint16_t>(fetched_)) &
                 (static_cast<uint16_t>(A) ^ tmp)) & 0x0080);
    A = tmp & 0x00FF;  return 1;
}
uint8_t CPU6502Base::SBC() {
    fetch();
    uint16_t val = static_cast<uint16_t>(fetched_) ^ 0x00FF;
    uint16_t tmp = static_cast<uint16_t>(A) + val + static_cast<uint16_t>(getFlag(C));
    setFlag(C, tmp & 0xFF00);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    setFlag(V, (tmp ^ static_cast<uint16_t>(A)) & (tmp ^ val) & 0x0080);
    A = tmp & 0x00FF;  return 1;
}
uint8_t CPU6502Base::AND() { fetch(); A = A & fetched_; setFlag(Z, A==0); setFlag(N, A&0x80); return 1; }
uint8_t CPU6502Base::ORA() { fetch(); A = A | fetched_; setFlag(Z, A==0); setFlag(N, A&0x80); return 1; }
uint8_t CPU6502Base::EOR() { fetch(); A = A ^ fetched_; setFlag(Z, A==0); setFlag(N, A&0x80); return 1; }

uint8_t CPU6502Base::BIT() {
    fetch();
    uint8_t tmp = A & fetched_;
    setFlag(Z, tmp == 0x00);
    setFlag(N, fetched_ & (1 << 7));
    setFlag(V, fetched_ & (1 << 6));
    return 0;
}

uint8_t CPU6502Base::ASL() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, tmp & 0xFF00); setFlag(Z, (tmp & 0x00FF)==0); setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU6502Base::ACC) A = tmp & 0x00FF;
    else busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}
uint8_t CPU6502Base::LSR() {
    fetch();
    setFlag(C, fetched_ & 0x0001);
    uint16_t tmp = fetched_ >> 1;
    setFlag(Z, (tmp & 0x00FF)==0); setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU6502Base::ACC) A = tmp & 0x00FF;
    else busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}
uint8_t CPU6502Base::ROL() {
    fetch();
    uint16_t tmp = (static_cast<uint16_t>(fetched_) << 1) | static_cast<uint16_t>(getFlag(C));
    setFlag(C, tmp & 0xFF00); setFlag(Z, (tmp & 0x00FF)==0); setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU6502Base::ACC) A = tmp & 0x00FF;
    else busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}
uint8_t CPU6502Base::ROR() {
    fetch();
    uint16_t tmp = (static_cast<uint16_t>(getFlag(C)) << 7) | (fetched_ >> 1);
    setFlag(C, fetched_ & 0x0001); setFlag(Z, (tmp & 0x00FF)==0); setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU6502Base::ACC) A = tmp & 0x00FF;
    else busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}

uint8_t CPU6502Base::INC() { fetch(); uint8_t t=fetched_+1; busWrite(addrAbs_,t); setFlag(Z,t==0); setFlag(N,t&0x80); return 0; }
uint8_t CPU6502Base::DEC() { fetch(); uint8_t t=fetched_-1; busWrite(addrAbs_,t); setFlag(Z,t==0); setFlag(N,t&0x80); return 0; }
uint8_t CPU6502Base::INX() { ++X; setFlag(Z, X==0); setFlag(N, X&0x80); return 0; }
uint8_t CPU6502Base::INY() { ++Y; setFlag(Z, Y==0); setFlag(N, Y&0x80); return 0; }
uint8_t CPU6502Base::DEX() { --X; setFlag(Z, X==0); setFlag(N, X&0x80); return 0; }
uint8_t CPU6502Base::DEY() { --Y; setFlag(Z, Y==0); setFlag(N, Y&0x80); return 0; }

uint8_t CPU6502Base::LDA() { fetch(); A=fetched_; setFlag(Z,A==0); setFlag(N,A&0x80); return 1; }
uint8_t CPU6502Base::LDX() { fetch(); X=fetched_; setFlag(Z,X==0); setFlag(N,X&0x80); return 1; }
uint8_t CPU6502Base::LDY() { fetch(); Y=fetched_; setFlag(Z,Y==0); setFlag(N,Y&0x80); return 1; }

uint8_t CPU6502Base::STA() { busWrite(addrAbs_, A); return 0; }
uint8_t CPU6502Base::STX() { busWrite(addrAbs_, X); return 0; }
uint8_t CPU6502Base::STY() { busWrite(addrAbs_, Y); return 0; }

uint8_t CPU6502Base::TAX() { X=A; setFlag(Z,X==0); setFlag(N,X&0x80); return 0; }
uint8_t CPU6502Base::TAY() { Y=A; setFlag(Z,Y==0); setFlag(N,Y&0x80); return 0; }
uint8_t CPU6502Base::TXA() { A=X; setFlag(Z,A==0); setFlag(N,A&0x80); return 0; }
uint8_t CPU6502Base::TYA() { A=Y; setFlag(Z,A==0); setFlag(N,A&0x80); return 0; }
uint8_t CPU6502Base::TSX() { X=SP; setFlag(Z,X==0); setFlag(N,X&0x80); return 0; }
uint8_t CPU6502Base::TXS() { SP=X; return 0; }

uint8_t CPU6502Base::PHA() { stackPush(A); return 0; }
uint8_t CPU6502Base::PHP() { stackPush(P | static_cast<uint8_t>(B) | static_cast<uint8_t>(U)); return 0; }
uint8_t CPU6502Base::PLA() { A=stackPop(); setFlag(Z,A==0); setFlag(N,A&0x80); return 0; }
uint8_t CPU6502Base::PLP() { P=stackPop(); setFlag(U, true); return 0; }

uint8_t CPU6502Base::CMP() { fetch(); uint16_t t=static_cast<uint16_t>(A)-static_cast<uint16_t>(fetched_); setFlag(C,A>=fetched_); setFlag(Z,(t&0xFF)==0); setFlag(N,t&0x80); return 1; }
uint8_t CPU6502Base::CPX() { fetch(); uint16_t t=static_cast<uint16_t>(X)-static_cast<uint16_t>(fetched_); setFlag(C,X>=fetched_); setFlag(Z,(t&0xFF)==0); setFlag(N,t&0x80); return 0; }
uint8_t CPU6502Base::CPY() { fetch(); uint16_t t=static_cast<uint16_t>(Y)-static_cast<uint16_t>(fetched_); setFlag(C,Y>=fetched_); setFlag(Z,(t&0xFF)==0); setFlag(N,t&0x80); return 0; }

uint8_t CPU6502Base::BCC() { if (!getFlag(C)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BCS() { if ( getFlag(C)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BEQ() { if ( getFlag(Z)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BNE() { if (!getFlag(Z)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BMI() { if ( getFlag(N)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BPL() { if (!getFlag(N)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BVC() { if (!getFlag(V)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }
uint8_t CPU6502Base::BVS() { if ( getFlag(V)) { ++cycles_; addrAbs_=PC+addrRel_; if((addrAbs_&0xFF00)!=(PC&0xFF00))++cycles_; PC=addrAbs_; } return 0; }

uint8_t CPU6502Base::JMP() { PC = addrAbs_; return 0; }
uint8_t CPU6502Base::JSR() {
    --PC;
    stackPush((PC >> 8) & 0xFF); stackPush(PC & 0xFF);
    PC = addrAbs_;  return 0;
}
uint8_t CPU6502Base::RTS() {
    uint16_t lo = stackPop(); uint16_t hi = stackPop();
    PC = (hi << 8) | lo;  ++PC;  return 0;
}
uint8_t CPU6502Base::BRK() {
    ++PC;
    setFlag(I, true);
    stackPush((PC >> 8) & 0xFF); stackPush(PC & 0xFF);
    setFlag(B, true); stackPush(P); setFlag(B, false);
    uint16_t lo = busRead(0xFFFE); uint16_t hi = busRead(0xFFFF);
    PC = (hi << 8) | lo;  return 0;
}
uint8_t CPU6502Base::RTI() {
    P = stackPop();
    P &= ~static_cast<uint8_t>(B);
    P |=  static_cast<uint8_t>(U);
    uint16_t lo = stackPop(); uint16_t hi = stackPop();
    PC = (hi << 8) | lo;  return 0;
}

uint8_t CPU6502Base::CLC() { setFlag(C,false); return 0; }
uint8_t CPU6502Base::CLD() { setFlag(D,false); return 0; }
uint8_t CPU6502Base::CLI() { setFlag(I,false); return 0; }
uint8_t CPU6502Base::CLV() { setFlag(V,false); return 0; }
uint8_t CPU6502Base::SEC() { setFlag(C,true);  return 0; }
uint8_t CPU6502Base::SED() { setFlag(D,true);  return 0; }
uint8_t CPU6502Base::SEI() { setFlag(I,true);  return 0; }

uint8_t CPU6502Base::NOP() { return 0; }
uint8_t CPU6502Base::XXX() { return 0; }

// ============================================================================
// Instructions — 65C02 additions
// ============================================================================

uint8_t CPU6502Base::BRA() {
    ++cycles_;
    addrAbs_ = PC + addrRel_;
    if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
    PC = addrAbs_;
    return 0;
}

uint8_t CPU6502Base::STZ() { busWrite(addrAbs_, 0x00); return 0; }

uint8_t CPU6502Base::TRB() {
    fetch();
    busWrite(addrAbs_, fetched_ & ~A);
    setFlag(Z, (A & fetched_) == 0);
    return 0;
}

uint8_t CPU6502Base::TSB() {
    fetch();
    busWrite(addrAbs_, fetched_ | A);
    setFlag(Z, (A & fetched_) == 0);
    return 0;
}

uint8_t CPU6502Base::INA() { ++A; setFlag(Z, A==0); setFlag(N, A&0x80); return 0; }
uint8_t CPU6502Base::DEA() { --A; setFlag(Z, A==0); setFlag(N, A&0x80); return 0; }

uint8_t CPU6502Base::PHX() { stackPush(X); return 0; }
uint8_t CPU6502Base::PHY() { stackPush(Y); return 0; }
uint8_t CPU6502Base::PLX() { X=stackPop(); setFlag(Z,X==0); setFlag(N,X&0x80); return 0; }
uint8_t CPU6502Base::PLY() { Y=stackPop(); setFlag(Z,Y==0); setFlag(N,Y&0x80); return 0; }

// BIT immediate on 65C02 only sets Z — N and V are not affected
uint8_t CPU6502Base::BIT_IMM() {
    fetch();
    setFlag(Z, (A & fetched_) == 0);
    return 0;
}

// ============================================================================
// Debug
// ============================================================================

std::string CPU6502Base::stateString() const {
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0');
    ss << "PC:$" << std::setw(4) << static_cast<int>(PC)
       << "  A:$"  << std::setw(2) << static_cast<int>(A)
       << "  X:$"  << std::setw(2) << static_cast<int>(X)
       << "  Y:$"  << std::setw(2) << static_cast<int>(Y)
       << "  SP:$" << std::setw(2) << static_cast<int>(SP)
       << "  P:$"  << std::setw(2) << static_cast<int>(P)
       << "  ["
       << (getFlag(N) ? 'N' : 'n') << (getFlag(V) ? 'V' : 'v') << '-'
       << (getFlag(B) ? 'B' : 'b') << (getFlag(D) ? 'D' : 'd')
       << (getFlag(I) ? 'I' : 'i') << (getFlag(Z) ? 'Z' : 'z')
       << (getFlag(C) ? 'C' : 'c') << ']';
    if (bus_) ss << "  " << lookup_[bus_->read(PC)].name;
    return ss.str();
}
