#include "emulator/cpu/CPU8502.h"
#include "emulator/core/Bus.h"

#include <iomanip>
#include <sstream>

// ============================================================================
// Constructor — build the 256-entry opcode lookup table.
//
// Format per entry: { "NAME", &operate, &addrmode, base_cycles }
//
// Illegal/undefined opcodes are mapped to XXX (extended NOP behaviour).
// Extra cycles for page crossing are resolved at runtime by ANDing the
// return values of addrmode() and operate() — both must return 1 for the
// cycle to be added (stores never add a cycle even if a page is crossed).
// ============================================================================

CPU8502::CPU8502() {
    // clang-format off
    lookup_ = {{
    //        name   operate              addrmode       cyc
    /* $00 */ {"BRK", &CPU8502::BRK, &CPU8502::IMP, 7},
    /* $01 */ {"ORA", &CPU8502::ORA, &CPU8502::IZX, 6},
    /* $02 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $03 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $04 */ {"???", &CPU8502::NOP, &CPU8502::ZP0, 3},
    /* $05 */ {"ORA", &CPU8502::ORA, &CPU8502::ZP0, 3},
    /* $06 */ {"ASL", &CPU8502::ASL, &CPU8502::ZP0, 5},
    /* $07 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $08 */ {"PHP", &CPU8502::PHP, &CPU8502::IMP, 3},
    /* $09 */ {"ORA", &CPU8502::ORA, &CPU8502::IMM, 2},
    /* $0A */ {"ASL", &CPU8502::ASL, &CPU8502::ACC, 2},
    /* $0B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $0C */ {"???", &CPU8502::NOP, &CPU8502::ABS, 4},
    /* $0D */ {"ORA", &CPU8502::ORA, &CPU8502::ABS, 4},
    /* $0E */ {"ASL", &CPU8502::ASL, &CPU8502::ABS, 6},
    /* $0F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},

    /* $10 */ {"BPL", &CPU8502::BPL, &CPU8502::REL, 2},
    /* $11 */ {"ORA", &CPU8502::ORA, &CPU8502::IZY, 5},
    /* $12 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $13 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $14 */ {"???", &CPU8502::NOP, &CPU8502::ZPX, 4},
    /* $15 */ {"ORA", &CPU8502::ORA, &CPU8502::ZPX, 4},
    /* $16 */ {"ASL", &CPU8502::ASL, &CPU8502::ZPX, 6},
    /* $17 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $18 */ {"CLC", &CPU8502::CLC, &CPU8502::IMP, 2},
    /* $19 */ {"ORA", &CPU8502::ORA, &CPU8502::ABY, 4},
    /* $1A */ {"???", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $1B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    /* $1C */ {"???", &CPU8502::NOP, &CPU8502::ABX, 4},
    /* $1D */ {"ORA", &CPU8502::ORA, &CPU8502::ABX, 4},
    /* $1E */ {"ASL", &CPU8502::ASL, &CPU8502::ABX, 7},
    /* $1F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},

    /* $20 */ {"JSR", &CPU8502::JSR, &CPU8502::ABS, 6},
    /* $21 */ {"AND", &CPU8502::AND, &CPU8502::IZX, 6},
    /* $22 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $23 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $24 */ {"BIT", &CPU8502::BIT, &CPU8502::ZP0, 3},
    /* $25 */ {"AND", &CPU8502::AND, &CPU8502::ZP0, 3},
    /* $26 */ {"ROL", &CPU8502::ROL, &CPU8502::ZP0, 5},
    /* $27 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $28 */ {"PLP", &CPU8502::PLP, &CPU8502::IMP, 4},
    /* $29 */ {"AND", &CPU8502::AND, &CPU8502::IMM, 2},
    /* $2A */ {"ROL", &CPU8502::ROL, &CPU8502::ACC, 2},
    /* $2B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $2C */ {"BIT", &CPU8502::BIT, &CPU8502::ABS, 4},
    /* $2D */ {"AND", &CPU8502::AND, &CPU8502::ABS, 4},
    /* $2E */ {"ROL", &CPU8502::ROL, &CPU8502::ABS, 6},
    /* $2F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},

    /* $30 */ {"BMI", &CPU8502::BMI, &CPU8502::REL, 2},
    /* $31 */ {"AND", &CPU8502::AND, &CPU8502::IZY, 5},
    /* $32 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $33 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $34 */ {"???", &CPU8502::NOP, &CPU8502::ZPX, 4},
    /* $35 */ {"AND", &CPU8502::AND, &CPU8502::ZPX, 4},
    /* $36 */ {"ROL", &CPU8502::ROL, &CPU8502::ZPX, 6},
    /* $37 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $38 */ {"SEC", &CPU8502::SEC, &CPU8502::IMP, 2},
    /* $39 */ {"AND", &CPU8502::AND, &CPU8502::ABY, 4},
    /* $3A */ {"???", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $3B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    /* $3C */ {"???", &CPU8502::NOP, &CPU8502::ABX, 4},
    /* $3D */ {"AND", &CPU8502::AND, &CPU8502::ABX, 4},
    /* $3E */ {"ROL", &CPU8502::ROL, &CPU8502::ABX, 7},
    /* $3F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},

    /* $40 */ {"RTI", &CPU8502::RTI, &CPU8502::IMP, 6},
    /* $41 */ {"EOR", &CPU8502::EOR, &CPU8502::IZX, 6},
    /* $42 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $43 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $44 */ {"???", &CPU8502::NOP, &CPU8502::ZP0, 3},
    /* $45 */ {"EOR", &CPU8502::EOR, &CPU8502::ZP0, 3},
    /* $46 */ {"LSR", &CPU8502::LSR, &CPU8502::ZP0, 5},
    /* $47 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $48 */ {"PHA", &CPU8502::PHA, &CPU8502::IMP, 3},
    /* $49 */ {"EOR", &CPU8502::EOR, &CPU8502::IMM, 2},
    /* $4A */ {"LSR", &CPU8502::LSR, &CPU8502::ACC, 2},
    /* $4B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $4C */ {"JMP", &CPU8502::JMP, &CPU8502::ABS, 3},
    /* $4D */ {"EOR", &CPU8502::EOR, &CPU8502::ABS, 4},
    /* $4E */ {"LSR", &CPU8502::LSR, &CPU8502::ABS, 6},
    /* $4F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},

    /* $50 */ {"BVC", &CPU8502::BVC, &CPU8502::REL, 2},
    /* $51 */ {"EOR", &CPU8502::EOR, &CPU8502::IZY, 5},
    /* $52 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $53 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $54 */ {"???", &CPU8502::NOP, &CPU8502::ZPX, 4},
    /* $55 */ {"EOR", &CPU8502::EOR, &CPU8502::ZPX, 4},
    /* $56 */ {"LSR", &CPU8502::LSR, &CPU8502::ZPX, 6},
    /* $57 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $58 */ {"CLI", &CPU8502::CLI, &CPU8502::IMP, 2},
    /* $59 */ {"EOR", &CPU8502::EOR, &CPU8502::ABY, 4},
    /* $5A */ {"???", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $5B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    /* $5C */ {"???", &CPU8502::NOP, &CPU8502::ABX, 4},
    /* $5D */ {"EOR", &CPU8502::EOR, &CPU8502::ABX, 4},
    /* $5E */ {"LSR", &CPU8502::LSR, &CPU8502::ABX, 7},
    /* $5F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},

    /* $60 */ {"RTS", &CPU8502::RTS, &CPU8502::IMP, 6},
    /* $61 */ {"ADC", &CPU8502::ADC, &CPU8502::IZX, 6},
    /* $62 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $63 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $64 */ {"???", &CPU8502::NOP, &CPU8502::ZP0, 3},
    /* $65 */ {"ADC", &CPU8502::ADC, &CPU8502::ZP0, 3},
    /* $66 */ {"ROR", &CPU8502::ROR, &CPU8502::ZP0, 5},
    /* $67 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $68 */ {"PLA", &CPU8502::PLA, &CPU8502::IMP, 4},
    /* $69 */ {"ADC", &CPU8502::ADC, &CPU8502::IMM, 2},
    /* $6A */ {"ROR", &CPU8502::ROR, &CPU8502::ACC, 2},
    /* $6B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $6C */ {"JMP", &CPU8502::JMP, &CPU8502::IND, 5},
    /* $6D */ {"ADC", &CPU8502::ADC, &CPU8502::ABS, 4},
    /* $6E */ {"ROR", &CPU8502::ROR, &CPU8502::ABS, 6},
    /* $6F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},

    /* $70 */ {"BVS", &CPU8502::BVS, &CPU8502::REL, 2},
    /* $71 */ {"ADC", &CPU8502::ADC, &CPU8502::IZY, 5},
    /* $72 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $73 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $74 */ {"???", &CPU8502::NOP, &CPU8502::ZPX, 4},
    /* $75 */ {"ADC", &CPU8502::ADC, &CPU8502::ZPX, 4},
    /* $76 */ {"ROR", &CPU8502::ROR, &CPU8502::ZPX, 6},
    /* $77 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $78 */ {"SEI", &CPU8502::SEI, &CPU8502::IMP, 2},
    /* $79 */ {"ADC", &CPU8502::ADC, &CPU8502::ABY, 4},
    /* $7A */ {"???", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $7B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    /* $7C */ {"???", &CPU8502::NOP, &CPU8502::ABX, 4},
    /* $7D */ {"ADC", &CPU8502::ADC, &CPU8502::ABX, 4},
    /* $7E */ {"ROR", &CPU8502::ROR, &CPU8502::ABX, 7},
    /* $7F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},

    /* $80 */ {"???", &CPU8502::NOP, &CPU8502::IMM, 2},
    /* $81 */ {"STA", &CPU8502::STA, &CPU8502::IZX, 6},
    /* $82 */ {"???", &CPU8502::NOP, &CPU8502::IMM, 2},
    /* $83 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $84 */ {"STY", &CPU8502::STY, &CPU8502::ZP0, 3},
    /* $85 */ {"STA", &CPU8502::STA, &CPU8502::ZP0, 3},
    /* $86 */ {"STX", &CPU8502::STX, &CPU8502::ZP0, 3},
    /* $87 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 3},
    /* $88 */ {"DEY", &CPU8502::DEY, &CPU8502::IMP, 2},
    /* $89 */ {"???", &CPU8502::NOP, &CPU8502::IMM, 2},
    /* $8A */ {"TXA", &CPU8502::TXA, &CPU8502::IMP, 2},
    /* $8B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $8C */ {"STY", &CPU8502::STY, &CPU8502::ABS, 4},
    /* $8D */ {"STA", &CPU8502::STA, &CPU8502::ABS, 4},
    /* $8E */ {"STX", &CPU8502::STX, &CPU8502::ABS, 4},
    /* $8F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 4},

    /* $90 */ {"BCC", &CPU8502::BCC, &CPU8502::REL, 2},
    /* $91 */ {"STA", &CPU8502::STA, &CPU8502::IZY, 6},
    /* $92 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $93 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $94 */ {"STY", &CPU8502::STY, &CPU8502::ZPX, 4},
    /* $95 */ {"STA", &CPU8502::STA, &CPU8502::ZPX, 4},
    /* $96 */ {"STX", &CPU8502::STX, &CPU8502::ZPY, 4},
    /* $97 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 4},
    /* $98 */ {"TYA", &CPU8502::TYA, &CPU8502::IMP, 2},
    /* $99 */ {"STA", &CPU8502::STA, &CPU8502::ABY, 5},
    /* $9A */ {"TXS", &CPU8502::TXS, &CPU8502::IMP, 2},
    /* $9B */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $9C */ {"???", &CPU8502::NOP, &CPU8502::ABS, 5},
    /* $9D */ {"STA", &CPU8502::STA, &CPU8502::ABX, 5},
    /* $9E */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $9F */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},

    /* $A0 */ {"LDY", &CPU8502::LDY, &CPU8502::IMM, 2},
    /* $A1 */ {"LDA", &CPU8502::LDA, &CPU8502::IZX, 6},
    /* $A2 */ {"LDX", &CPU8502::LDX, &CPU8502::IMM, 2},
    /* $A3 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $A4 */ {"LDY", &CPU8502::LDY, &CPU8502::ZP0, 3},
    /* $A5 */ {"LDA", &CPU8502::LDA, &CPU8502::ZP0, 3},
    /* $A6 */ {"LDX", &CPU8502::LDX, &CPU8502::ZP0, 3},
    /* $A7 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 3},
    /* $A8 */ {"TAY", &CPU8502::TAY, &CPU8502::IMP, 2},
    /* $A9 */ {"LDA", &CPU8502::LDA, &CPU8502::IMM, 2},
    /* $AA */ {"TAX", &CPU8502::TAX, &CPU8502::IMP, 2},
    /* $AB */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $AC */ {"LDY", &CPU8502::LDY, &CPU8502::ABS, 4},
    /* $AD */ {"LDA", &CPU8502::LDA, &CPU8502::ABS, 4},
    /* $AE */ {"LDX", &CPU8502::LDX, &CPU8502::ABS, 4},
    /* $AF */ {"???", &CPU8502::XXX, &CPU8502::IMP, 4},

    /* $B0 */ {"BCS", &CPU8502::BCS, &CPU8502::REL, 2},
    /* $B1 */ {"LDA", &CPU8502::LDA, &CPU8502::IZY, 5},
    /* $B2 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $B3 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $B4 */ {"LDY", &CPU8502::LDY, &CPU8502::ZPX, 4},
    /* $B5 */ {"LDA", &CPU8502::LDA, &CPU8502::ZPX, 4},
    /* $B6 */ {"LDX", &CPU8502::LDX, &CPU8502::ZPY, 4},
    /* $B7 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 4},
    /* $B8 */ {"CLV", &CPU8502::CLV, &CPU8502::IMP, 2},
    /* $B9 */ {"LDA", &CPU8502::LDA, &CPU8502::ABY, 4},
    /* $BA */ {"TSX", &CPU8502::TSX, &CPU8502::IMP, 2},
    /* $BB */ {"???", &CPU8502::XXX, &CPU8502::IMP, 4},
    /* $BC */ {"LDY", &CPU8502::LDY, &CPU8502::ABX, 4},
    /* $BD */ {"LDA", &CPU8502::LDA, &CPU8502::ABX, 4},
    /* $BE */ {"LDX", &CPU8502::LDX, &CPU8502::ABY, 4},
    /* $BF */ {"???", &CPU8502::XXX, &CPU8502::IMP, 4},

    /* $C0 */ {"CPY", &CPU8502::CPY, &CPU8502::IMM, 2},
    /* $C1 */ {"CMP", &CPU8502::CMP, &CPU8502::IZX, 6},
    /* $C2 */ {"???", &CPU8502::NOP, &CPU8502::IMM, 2},
    /* $C3 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $C4 */ {"CPY", &CPU8502::CPY, &CPU8502::ZP0, 3},
    /* $C5 */ {"CMP", &CPU8502::CMP, &CPU8502::ZP0, 3},
    /* $C6 */ {"DEC", &CPU8502::DEC, &CPU8502::ZP0, 5},
    /* $C7 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $C8 */ {"INY", &CPU8502::INY, &CPU8502::IMP, 2},
    /* $C9 */ {"CMP", &CPU8502::CMP, &CPU8502::IMM, 2},
    /* $CA */ {"DEX", &CPU8502::DEX, &CPU8502::IMP, 2},
    /* $CB */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $CC */ {"CPY", &CPU8502::CPY, &CPU8502::ABS, 4},
    /* $CD */ {"CMP", &CPU8502::CMP, &CPU8502::ABS, 4},
    /* $CE */ {"DEC", &CPU8502::DEC, &CPU8502::ABS, 6},
    /* $CF */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},

    /* $D0 */ {"BNE", &CPU8502::BNE, &CPU8502::REL, 2},
    /* $D1 */ {"CMP", &CPU8502::CMP, &CPU8502::IZY, 5},
    /* $D2 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $D3 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $D4 */ {"???", &CPU8502::NOP, &CPU8502::ZPX, 4},
    /* $D5 */ {"CMP", &CPU8502::CMP, &CPU8502::ZPX, 4},
    /* $D6 */ {"DEC", &CPU8502::DEC, &CPU8502::ZPX, 6},
    /* $D7 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $D8 */ {"CLD", &CPU8502::CLD, &CPU8502::IMP, 2},
    /* $D9 */ {"CMP", &CPU8502::CMP, &CPU8502::ABY, 4},
    /* $DA */ {"???", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $DB */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    /* $DC */ {"???", &CPU8502::NOP, &CPU8502::ABX, 4},
    /* $DD */ {"CMP", &CPU8502::CMP, &CPU8502::ABX, 4},
    /* $DE */ {"DEC", &CPU8502::DEC, &CPU8502::ABX, 7},
    /* $DF */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},

    /* $E0 */ {"CPX", &CPU8502::CPX, &CPU8502::IMM, 2},
    /* $E1 */ {"SBC", &CPU8502::SBC, &CPU8502::IZX, 6},
    /* $E2 */ {"???", &CPU8502::NOP, &CPU8502::IMM, 2},
    /* $E3 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $E4 */ {"CPX", &CPU8502::CPX, &CPU8502::ZP0, 3},
    /* $E5 */ {"SBC", &CPU8502::SBC, &CPU8502::ZP0, 3},
    /* $E6 */ {"INC", &CPU8502::INC, &CPU8502::ZP0, 5},
    /* $E7 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 5},
    /* $E8 */ {"INX", &CPU8502::INX, &CPU8502::IMP, 2},
    /* $E9 */ {"SBC", &CPU8502::SBC, &CPU8502::IMM, 2},
    /* $EA */ {"NOP", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $EB */ {"???", &CPU8502::SBC, &CPU8502::IMM, 2},  // unofficial SBC
    /* $EC */ {"CPX", &CPU8502::CPX, &CPU8502::ABS, 4},
    /* $ED */ {"SBC", &CPU8502::SBC, &CPU8502::ABS, 4},
    /* $EE */ {"INC", &CPU8502::INC, &CPU8502::ABS, 6},
    /* $EF */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},

    /* $F0 */ {"BEQ", &CPU8502::BEQ, &CPU8502::REL, 2},
    /* $F1 */ {"SBC", &CPU8502::SBC, &CPU8502::IZY, 5},
    /* $F2 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 2},
    /* $F3 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 8},
    /* $F4 */ {"???", &CPU8502::NOP, &CPU8502::ZPX, 4},
    /* $F5 */ {"SBC", &CPU8502::SBC, &CPU8502::ZPX, 4},
    /* $F6 */ {"INC", &CPU8502::INC, &CPU8502::ZPX, 6},
    /* $F7 */ {"???", &CPU8502::XXX, &CPU8502::IMP, 6},
    /* $F8 */ {"SED", &CPU8502::SED, &CPU8502::IMP, 2},
    /* $F9 */ {"SBC", &CPU8502::SBC, &CPU8502::ABY, 4},
    /* $FA */ {"???", &CPU8502::NOP, &CPU8502::IMP, 2},
    /* $FB */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    /* $FC */ {"???", &CPU8502::NOP, &CPU8502::ABX, 4},
    /* $FD */ {"SBC", &CPU8502::SBC, &CPU8502::ABX, 4},
    /* $FE */ {"INC", &CPU8502::INC, &CPU8502::ABX, 7},
    /* $FF */ {"???", &CPU8502::XXX, &CPU8502::IMP, 7},
    }};
    // clang-format on
}

// ============================================================================
// Bus helpers
// ============================================================================

uint8_t CPU8502::busRead(uint16_t addr) {
    return bus_ ? bus_->read(addr) : 0x00;
}

void CPU8502::busWrite(uint16_t addr, uint8_t val) {
    if (bus_) bus_->write(addr, val);
}

// ============================================================================
// Stack helpers   (stack lives at $0100–$01FF)
// ============================================================================

void CPU8502::stackPush(uint8_t val) {
    busWrite(0x0100 + SP, val);
    --SP;
}

uint8_t CPU8502::stackPop() {
    ++SP;
    return busRead(0x0100 + SP);
}

// ============================================================================
// Flag helpers
// ============================================================================

void CPU8502::setFlag(Flags f, bool v) {
    if (v) P |=  static_cast<uint8_t>(f);
    else   P &= ~static_cast<uint8_t>(f);
}

bool CPU8502::getFlag(Flags f) const {
    return (P & static_cast<uint8_t>(f)) != 0;
}

// ============================================================================
// Fetch  — read operand for the current instruction.
// IMP/ACC modes pre-load fetched_ from A; all others read from addrAbs_.
// ============================================================================

uint8_t CPU8502::fetch() {
    if (lookup_[opcode_].addrmode != &CPU8502::IMP &&
        lookup_[opcode_].addrmode != &CPU8502::ACC)
        fetched_ = busRead(addrAbs_);
    return fetched_;
}

// ============================================================================
// Lifecycle
// ============================================================================

void CPU8502::connectBus(Bus* bus) { bus_ = bus; }

void CPU8502::reset() {
    uint16_t lo = busRead(0xFFFC);
    uint16_t hi = busRead(0xFFFD);
    PC = (hi << 8) | lo;

    A       = 0x00;
    X       = 0x00;
    Y       = 0x00;
    SP      = 0xFD;
    P       = 0x24;  // Unused | IRQ Disable
    fetched_ = 0x00;
    addrAbs_ = 0x0000;
    addrRel_ = 0x0000;
    cycles_  = 8;   // Reset sequence takes 8 cycles
}

// Single clock tick.
// When cycles_ hits 0 a new instruction is fetched and executed in one shot;
// the resulting cycle count is then ticked down on subsequent calls.
void CPU8502::clock() {
    if (cycles_ == 0) {
        opcode_ = busRead(PC++);
        setFlag(U, true);   // Unused bit always set before executing

        cycles_ = lookup_[opcode_].cycles;

        uint8_t extra1 = (this->*lookup_[opcode_].addrmode)();
        uint8_t extra2 = (this->*lookup_[opcode_].operate )();

        // Extra cycle only added when *both* the addressing mode and the
        // operation allow it (stores never add extra cycles even on page cross).
        cycles_ += (extra1 & extra2);

        setFlag(U, true);   // Ensure always set after instruction
    }
    --cycles_;
}

bool CPU8502::complete() const { return cycles_ == 0; }

void CPU8502::irq() {
    if (getFlag(I)) return;

    stackPush((PC >> 8) & 0xFF);
    stackPush(PC & 0xFF);

    setFlag(B, false);
    setFlag(U, true);
    stackPush(P);        // push P with original I=0 so RTI restores it correctly
    setFlag(I, true);    // set I only after push

    uint16_t lo = busRead(0xFFFE);
    uint16_t hi = busRead(0xFFFF);
    PC = (hi << 8) | lo;
    cycles_ = 7;
}

void CPU8502::nmi() {
    stackPush((PC >> 8) & 0xFF);
    stackPush(PC & 0xFF);

    setFlag(B, false);
    setFlag(U, true);
    stackPush(P);        // same fix — push before setting I
    setFlag(I, true);

    uint16_t lo = busRead(0xFFFA);
    uint16_t hi = busRead(0xFFFB);
    PC = (hi << 8) | lo;
    cycles_ = 8;
}

// ============================================================================
// Addressing modes
// Return 1 if the instruction may need an extra cycle on page crossing.
// ============================================================================

// IMP — Implied: no explicit operand; pre-load fetched_ from A for shift ops.
uint8_t CPU8502::IMP() { fetched_ = A; return 0; }

// ACC — Accumulator: operand is A.
uint8_t CPU8502::ACC() { fetched_ = A; return 0; }

// IMM — Immediate: operand is the byte immediately after the opcode.
uint8_t CPU8502::IMM() { addrAbs_ = PC++; return 0; }

// ZP0 — Zero Page: 8-bit address; high byte always $00.
uint8_t CPU8502::ZP0() {
    addrAbs_ = busRead(PC++) & 0x00FF;
    return 0;
}

// ZPX — Zero Page, X: zero-page address + X (wraps within page 0).
uint8_t CPU8502::ZPX() {
    addrAbs_ = (busRead(PC++) + X) & 0x00FF;
    return 0;
}

// ZPY — Zero Page, Y: zero-page address + Y (wraps within page 0).
uint8_t CPU8502::ZPY() {
    addrAbs_ = (busRead(PC++) + Y) & 0x00FF;
    return 0;
}

// ABS — Absolute: full 16-bit address in next two bytes (lo, hi).
uint8_t CPU8502::ABS() {
    uint16_t lo = busRead(PC++);
    uint16_t hi = busRead(PC++);
    addrAbs_    = (hi << 8) | lo;
    return 0;
}

// ABX — Absolute, X: base + X; extra cycle if page boundary crossed.
uint8_t CPU8502::ABX() {
    uint16_t lo   = busRead(PC++);
    uint16_t hi   = busRead(PC++);
    uint16_t base = (hi << 8) | lo;
    addrAbs_      = base + X;
    return (addrAbs_ & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

// ABY — Absolute, Y: base + Y; extra cycle if page boundary crossed.
uint8_t CPU8502::ABY() {
    uint16_t lo   = busRead(PC++);
    uint16_t hi   = busRead(PC++);
    uint16_t base = (hi << 8) | lo;
    addrAbs_      = base + Y;
    return (addrAbs_ & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

// IND — Indirect: used only by JMP. Reproduces the NMOS 6502 page-wrap bug:
// if the low byte of the pointer is $FF, the high byte wraps within the page.
uint8_t CPU8502::IND() {
    uint16_t plo = busRead(PC++);
    uint16_t phi = busRead(PC++);
    uint16_t ptr = (phi << 8) | plo;

    if (plo == 0x00FF)
        addrAbs_ = (static_cast<uint16_t>(busRead(ptr & 0xFF00)) << 8) | busRead(ptr);
    else
        addrAbs_ = (static_cast<uint16_t>(busRead(ptr + 1)) << 8) | busRead(ptr);

    return 0;
}

// IZX — Indexed Indirect: zero-page pointer + X gives a 16-bit address.
uint8_t CPU8502::IZX() {
    uint16_t t  = busRead(PC++);
    uint16_t lo = busRead((t + X    ) & 0x00FF);
    uint16_t hi = busRead((t + X + 1) & 0x00FF);
    addrAbs_    = (hi << 8) | lo;
    return 0;
}

// IZY — Indirect Indexed: zero-page pointer gives base; Y added after.
// Extra cycle if page boundary crossed.
uint8_t CPU8502::IZY() {
    uint16_t t    = busRead(PC++);
    uint16_t lo   = busRead(t & 0x00FF);
    uint16_t hi   = busRead((t + 1) & 0x00FF);
    uint16_t base = (hi << 8) | lo;
    addrAbs_      = base + Y;
    return (addrAbs_ & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

// REL — Relative: signed 8-bit offset used by branch instructions.
uint8_t CPU8502::REL() {
    addrRel_ = busRead(PC++);
    if (addrRel_ & 0x80) addrRel_ |= 0xFF00;  // sign-extend to 16 bits
    return 0;
}

// ============================================================================
// Instructions
// ============================================================================

// --- Arithmetic ---

// ADC — Add with Carry
uint8_t CPU8502::ADC() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(A)
                 + static_cast<uint16_t>(fetched_)
                 + static_cast<uint16_t>(getFlag(C));
    setFlag(C, tmp > 0x00FF);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    // Overflow: sign of result differs from sign of both inputs
    setFlag(V, (~(static_cast<uint16_t>(A) ^ static_cast<uint16_t>(fetched_)) &
                 (static_cast<uint16_t>(A) ^ tmp)) & 0x0080);
    A = tmp & 0x00FF;
    return 1;
}

// SBC — Subtract with Carry  (implemented as ADC with inverted operand)
uint8_t CPU8502::SBC() {
    fetch();
    uint16_t value = static_cast<uint16_t>(fetched_) ^ 0x00FF;
    uint16_t tmp   = static_cast<uint16_t>(A)
                   + value
                   + static_cast<uint16_t>(getFlag(C));
    setFlag(C, tmp & 0xFF00);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    setFlag(V, (tmp ^ static_cast<uint16_t>(A)) & (tmp ^ value) & 0x0080);
    A = tmp & 0x00FF;
    return 1;
}

// --- Logical ---

uint8_t CPU8502::AND() {
    fetch();
    A = A & fetched_;
    setFlag(Z, A == 0x00);
    setFlag(N, A & 0x80);
    return 1;
}

uint8_t CPU8502::ORA() {
    fetch();
    A = A | fetched_;
    setFlag(Z, A == 0x00);
    setFlag(N, A & 0x80);
    return 1;
}

uint8_t CPU8502::EOR() {
    fetch();
    A = A ^ fetched_;
    setFlag(Z, A == 0x00);
    setFlag(N, A & 0x80);
    return 1;
}

// BIT — Bit Test: sets Z, N, V without modifying A
uint8_t CPU8502::BIT() {
    fetch();
    uint8_t tmp = A & fetched_;
    setFlag(Z, tmp == 0x00);
    setFlag(N, fetched_ & (1 << 7));
    setFlag(V, fetched_ & (1 << 6));
    return 0;
}

// --- Shifts & Rotates ---

// ASL — Arithmetic Shift Left
uint8_t CPU8502::ASL() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, tmp & 0xFF00);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU8502::ACC)
        A = tmp & 0x00FF;
    else
        busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}

// LSR — Logical Shift Right
uint8_t CPU8502::LSR() {
    fetch();
    setFlag(C, fetched_ & 0x0001);
    uint16_t tmp = fetched_ >> 1;
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU8502::ACC)
        A = tmp & 0x00FF;
    else
        busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}

// ROL — Rotate Left through Carry
uint8_t CPU8502::ROL() {
    fetch();
    uint16_t tmp = (static_cast<uint16_t>(fetched_) << 1)
                 | static_cast<uint16_t>(getFlag(C));
    setFlag(C, tmp & 0xFF00);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU8502::ACC)
        A = tmp & 0x00FF;
    else
        busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}

// ROR — Rotate Right through Carry
uint8_t CPU8502::ROR() {
    fetch();
    uint16_t tmp = (static_cast<uint16_t>(getFlag(C)) << 7)
                 | (fetched_ >> 1);
    setFlag(C, fetched_ & 0x0001);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    if (lookup_[opcode_].addrmode == &CPU8502::ACC)
        A = tmp & 0x00FF;
    else
        busWrite(addrAbs_, tmp & 0x00FF);
    return 0;
}

// --- Increments / Decrements ---

uint8_t CPU8502::INC() {
    fetch();
    uint8_t tmp = fetched_ + 1;
    busWrite(addrAbs_, tmp);
    setFlag(Z, tmp == 0x00);
    setFlag(N, tmp & 0x80);
    return 0;
}

uint8_t CPU8502::DEC() {
    fetch();
    uint8_t tmp = fetched_ - 1;
    busWrite(addrAbs_, tmp);
    setFlag(Z, tmp == 0x00);
    setFlag(N, tmp & 0x80);
    return 0;
}

uint8_t CPU8502::INX() { ++X; setFlag(Z, X == 0x00); setFlag(N, X & 0x80); return 0; }
uint8_t CPU8502::INY() { ++Y; setFlag(Z, Y == 0x00); setFlag(N, Y & 0x80); return 0; }
uint8_t CPU8502::DEX() { --X; setFlag(Z, X == 0x00); setFlag(N, X & 0x80); return 0; }
uint8_t CPU8502::DEY() { --Y; setFlag(Z, Y == 0x00); setFlag(N, Y & 0x80); return 0; }

// --- Loads ---

uint8_t CPU8502::LDA() { fetch(); A = fetched_; setFlag(Z, A == 0); setFlag(N, A & 0x80); return 1; }
uint8_t CPU8502::LDX() { fetch(); X = fetched_; setFlag(Z, X == 0); setFlag(N, X & 0x80); return 1; }
uint8_t CPU8502::LDY() { fetch(); Y = fetched_; setFlag(Z, Y == 0); setFlag(N, Y & 0x80); return 1; }

// --- Stores ---

uint8_t CPU8502::STA() { busWrite(addrAbs_, A); return 0; }
uint8_t CPU8502::STX() { busWrite(addrAbs_, X); return 0; }
uint8_t CPU8502::STY() { busWrite(addrAbs_, Y); return 0; }

// --- Transfers ---

uint8_t CPU8502::TAX() { X = A; setFlag(Z, X == 0); setFlag(N, X & 0x80); return 0; }
uint8_t CPU8502::TAY() { Y = A; setFlag(Z, Y == 0); setFlag(N, Y & 0x80); return 0; }
uint8_t CPU8502::TXA() { A = X; setFlag(Z, A == 0); setFlag(N, A & 0x80); return 0; }
uint8_t CPU8502::TYA() { A = Y; setFlag(Z, A == 0); setFlag(N, A & 0x80); return 0; }
uint8_t CPU8502::TSX() { X = SP; setFlag(Z, X == 0); setFlag(N, X & 0x80); return 0; }
uint8_t CPU8502::TXS() { SP = X; return 0; }

// --- Stack ---

uint8_t CPU8502::PHA() { stackPush(A); return 0; }
uint8_t CPU8502::PHP() { stackPush(P | static_cast<uint8_t>(B) | static_cast<uint8_t>(U)); return 0; }

uint8_t CPU8502::PLA() {
    A = stackPop();
    setFlag(Z, A == 0x00);
    setFlag(N, A & 0x80);
    return 0;
}

uint8_t CPU8502::PLP() {
    P = stackPop();
    setFlag(U, true);   // Unused always 1
    return 0;
}

// --- Comparisons ---

uint8_t CPU8502::CMP() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(A) - static_cast<uint16_t>(fetched_);
    setFlag(C, A >= fetched_);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    return 1;
}

uint8_t CPU8502::CPX() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(X) - static_cast<uint16_t>(fetched_);
    setFlag(C, X >= fetched_);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    return 0;
}

uint8_t CPU8502::CPY() {
    fetch();
    uint16_t tmp = static_cast<uint16_t>(Y) - static_cast<uint16_t>(fetched_);
    setFlag(C, Y >= fetched_);
    setFlag(Z, (tmp & 0x00FF) == 0);
    setFlag(N, tmp & 0x0080);
    return 0;
}

// --- Branches ---
// Branches directly add cycles rather than returning extra-cycle signals
// because: taken branch +1 cycle, page cross on taken branch +1 more cycle.

uint8_t CPU8502::BCC() {
    if (!getFlag(C)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BCS() {
    if (getFlag(C)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BEQ() {
    if (getFlag(Z)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BNE() {
    if (!getFlag(Z)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BMI() {
    if (getFlag(N)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BPL() {
    if (!getFlag(N)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BVC() {
    if (!getFlag(V)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

uint8_t CPU8502::BVS() {
    if (getFlag(V)) {
        ++cycles_;
        addrAbs_ = PC + addrRel_;
        if ((addrAbs_ & 0xFF00) != (PC & 0xFF00)) ++cycles_;
        PC = addrAbs_;
    }
    return 0;
}

// --- Jumps & Subroutines ---

uint8_t CPU8502::JMP() { PC = addrAbs_; return 0; }

uint8_t CPU8502::JSR() {
    --PC;  // Push the address of the last byte of the JSR instruction
    stackPush((PC >> 8) & 0x00FF);
    stackPush(PC & 0x00FF);
    PC = addrAbs_;
    return 0;
}

uint8_t CPU8502::RTS() {
    uint16_t lo = stackPop();
    uint16_t hi = stackPop();
    PC = (hi << 8) | lo;
    ++PC;  // JSR pushed PC-1, so advance past it
    return 0;
}

// BRK — Force Interrupt
uint8_t CPU8502::BRK() {
    ++PC;   // BRK has a padding byte; skip it
    setFlag(I, true);
    stackPush((PC >> 8) & 0x00FF);
    stackPush(PC & 0x00FF);
    setFlag(B, true);
    stackPush(P);
    setFlag(B, false);
    uint16_t lo = busRead(0xFFFE);
    uint16_t hi = busRead(0xFFFF);
    PC = (hi << 8) | lo;
    return 0;
}

// RTI — Return from Interrupt
uint8_t CPU8502::RTI() {
    P = stackPop();
    P &= ~static_cast<uint8_t>(B);  // B not set by hardware after RTI
    P |=  static_cast<uint8_t>(U);  // Unused always 1

    uint16_t lo = stackPop();
    uint16_t hi = stackPop();
    PC = (hi << 8) | lo;
    return 0;
}

// --- Flag operations ---

uint8_t CPU8502::CLC() { setFlag(C, false); return 0; }
uint8_t CPU8502::CLD() { setFlag(D, false); return 0; }
uint8_t CPU8502::CLI() { setFlag(I, false); return 0; }
uint8_t CPU8502::CLV() { setFlag(V, false); return 0; }
uint8_t CPU8502::SEC() { setFlag(C, true);  return 0; }
uint8_t CPU8502::SED() { setFlag(D, true);  return 0; }
uint8_t CPU8502::SEI() { setFlag(I, true);  return 0; }

// --- NOP & illegal catch-all ---

uint8_t CPU8502::NOP() { return 0; }
uint8_t CPU8502::XXX() { return 0; }

// ============================================================================
// Debug
// ============================================================================

std::string CPU8502::stateString() const {
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0');
    ss << "PC:$" << std::setw(4) << static_cast<int>(PC)
       << "  A:$"  << std::setw(2) << static_cast<int>(A)
       << "  X:$"  << std::setw(2) << static_cast<int>(X)
       << "  Y:$"  << std::setw(2) << static_cast<int>(Y)
       << "  SP:$" << std::setw(2) << static_cast<int>(SP)
       << "  P:$"  << std::setw(2) << static_cast<int>(P)
       << "  ["
       << (getFlag(N) ? 'N' : 'n')
       << (getFlag(V) ? 'V' : 'v')
       << '-'
       << (getFlag(B) ? 'B' : 'b')
       << (getFlag(D) ? 'D' : 'd')
       << (getFlag(I) ? 'I' : 'i')
       << (getFlag(Z) ? 'Z' : 'z')
       << (getFlag(C) ? 'C' : 'c')
       << ']';

    // Show the next instruction name by peeking at memory (read-only)
    if (bus_) {
        uint8_t nextOp = bus_->read(PC);
        ss << "  " << lookup_[nextOp].name;
    }

    return ss.str();
}
