#include "Disassembler.h"
#include "Bus.h"

#include <cstdio>

// ============================================================================
// Addressing mode enum (local to this translation unit)
// ============================================================================

enum class AM : uint8_t {
    IMP, ACC, IMM,
    ZP0, ZPX, ZPY,
    ABS, ABX, ABY,
    IND, IZX, IZY,
    REL
};

// Extra bytes consumed after the opcode byte (0, 1, or 2)
static const uint8_t kModeBytes[13] = {
    0, // IMP
    0, // ACC
    1, // IMM
    1, // ZP0
    1, // ZPX
    1, // ZPY
    2, // ABS
    2, // ABX
    2, // ABY
    2, // IND
    1, // IZX
    1, // IZY
    1, // REL
};

// ============================================================================
// Compact 256-entry opcode table  (name + addressing mode)
// Matches the dispatch table in CPU8502.cpp exactly.
// ============================================================================

static const struct { const char* name; AM mode; } kOpcodes[256] = {
/*$00*/{"BRK",AM::IMP},{"ORA",AM::IZX},{"???",AM::IMP},{"???",AM::IMP},
/*$04*/{"???",AM::ZP0},{"ORA",AM::ZP0},{"ASL",AM::ZP0},{"???",AM::IMP},
/*$08*/{"PHP",AM::IMP},{"ORA",AM::IMM},{"ASL",AM::ACC},{"???",AM::IMP},
/*$0C*/{"???",AM::ABS},{"ORA",AM::ABS},{"ASL",AM::ABS},{"???",AM::IMP},

/*$10*/{"BPL",AM::REL},{"ORA",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$14*/{"???",AM::ZPX},{"ORA",AM::ZPX},{"ASL",AM::ZPX},{"???",AM::IMP},
/*$18*/{"CLC",AM::IMP},{"ORA",AM::ABY},{"???",AM::IMP},{"???",AM::IMP},
/*$1C*/{"???",AM::ABX},{"ORA",AM::ABX},{"ASL",AM::ABX},{"???",AM::IMP},

/*$20*/{"JSR",AM::ABS},{"AND",AM::IZX},{"???",AM::IMP},{"???",AM::IMP},
/*$24*/{"BIT",AM::ZP0},{"AND",AM::ZP0},{"ROL",AM::ZP0},{"???",AM::IMP},
/*$28*/{"PLP",AM::IMP},{"AND",AM::IMM},{"ROL",AM::ACC},{"???",AM::IMP},
/*$2C*/{"BIT",AM::ABS},{"AND",AM::ABS},{"ROL",AM::ABS},{"???",AM::IMP},

/*$30*/{"BMI",AM::REL},{"AND",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$34*/{"???",AM::ZPX},{"AND",AM::ZPX},{"ROL",AM::ZPX},{"???",AM::IMP},
/*$38*/{"SEC",AM::IMP},{"AND",AM::ABY},{"???",AM::IMP},{"???",AM::IMP},
/*$3C*/{"???",AM::ABX},{"AND",AM::ABX},{"ROL",AM::ABX},{"???",AM::IMP},

/*$40*/{"RTI",AM::IMP},{"EOR",AM::IZX},{"???",AM::IMP},{"???",AM::IMP},
/*$44*/{"???",AM::ZP0},{"EOR",AM::ZP0},{"LSR",AM::ZP0},{"???",AM::IMP},
/*$48*/{"PHA",AM::IMP},{"EOR",AM::IMM},{"LSR",AM::ACC},{"???",AM::IMP},
/*$4C*/{"JMP",AM::ABS},{"EOR",AM::ABS},{"LSR",AM::ABS},{"???",AM::IMP},

/*$50*/{"BVC",AM::REL},{"EOR",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$54*/{"???",AM::ZPX},{"EOR",AM::ZPX},{"LSR",AM::ZPX},{"???",AM::IMP},
/*$58*/{"CLI",AM::IMP},{"EOR",AM::ABY},{"???",AM::IMP},{"???",AM::IMP},
/*$5C*/{"???",AM::ABX},{"EOR",AM::ABX},{"LSR",AM::ABX},{"???",AM::IMP},

/*$60*/{"RTS",AM::IMP},{"ADC",AM::IZX},{"???",AM::IMP},{"???",AM::IMP},
/*$64*/{"???",AM::ZP0},{"ADC",AM::ZP0},{"ROR",AM::ZP0},{"???",AM::IMP},
/*$68*/{"PLA",AM::IMP},{"ADC",AM::IMM},{"ROR",AM::ACC},{"???",AM::IMP},
/*$6C*/{"JMP",AM::IND},{"ADC",AM::ABS},{"ROR",AM::ABS},{"???",AM::IMP},

/*$70*/{"BVS",AM::REL},{"ADC",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$74*/{"???",AM::ZPX},{"ADC",AM::ZPX},{"ROR",AM::ZPX},{"???",AM::IMP},
/*$78*/{"SEI",AM::IMP},{"ADC",AM::ABY},{"???",AM::IMP},{"???",AM::IMP},
/*$7C*/{"???",AM::ABX},{"ADC",AM::ABX},{"ROR",AM::ABX},{"???",AM::IMP},

/*$80*/{"???",AM::IMM},{"STA",AM::IZX},{"???",AM::IMM},{"???",AM::IMP},
/*$84*/{"STY",AM::ZP0},{"STA",AM::ZP0},{"STX",AM::ZP0},{"???",AM::IMP},
/*$88*/{"DEY",AM::IMP},{"???",AM::IMM},{"TXA",AM::IMP},{"???",AM::IMP},
/*$8C*/{"STY",AM::ABS},{"STA",AM::ABS},{"STX",AM::ABS},{"???",AM::IMP},

/*$90*/{"BCC",AM::REL},{"STA",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$94*/{"STY",AM::ZPX},{"STA",AM::ZPX},{"STX",AM::ZPY},{"???",AM::IMP},
/*$98*/{"TYA",AM::IMP},{"STA",AM::ABY},{"TXS",AM::IMP},{"???",AM::IMP},
/*$9C*/{"???",AM::ABS},{"STA",AM::ABX},{"???",AM::IMP},{"???",AM::IMP},

/*$A0*/{"LDY",AM::IMM},{"LDA",AM::IZX},{"LDX",AM::IMM},{"???",AM::IMP},
/*$A4*/{"LDY",AM::ZP0},{"LDA",AM::ZP0},{"LDX",AM::ZP0},{"???",AM::IMP},
/*$A8*/{"TAY",AM::IMP},{"LDA",AM::IMM},{"TAX",AM::IMP},{"???",AM::IMP},
/*$AC*/{"LDY",AM::ABS},{"LDA",AM::ABS},{"LDX",AM::ABS},{"???",AM::IMP},

/*$B0*/{"BCS",AM::REL},{"LDA",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$B4*/{"LDY",AM::ZPX},{"LDA",AM::ZPX},{"LDX",AM::ZPY},{"???",AM::IMP},
/*$B8*/{"CLV",AM::IMP},{"LDA",AM::ABY},{"TSX",AM::IMP},{"???",AM::IMP},
/*$BC*/{"LDY",AM::ABX},{"LDA",AM::ABX},{"LDX",AM::ABY},{"???",AM::IMP},

/*$C0*/{"CPY",AM::IMM},{"CMP",AM::IZX},{"???",AM::IMM},{"???",AM::IMP},
/*$C4*/{"CPY",AM::ZP0},{"CMP",AM::ZP0},{"DEC",AM::ZP0},{"???",AM::IMP},
/*$C8*/{"INY",AM::IMP},{"CMP",AM::IMM},{"DEX",AM::IMP},{"???",AM::IMP},
/*$CC*/{"CPY",AM::ABS},{"CMP",AM::ABS},{"DEC",AM::ABS},{"???",AM::IMP},

/*$D0*/{"BNE",AM::REL},{"CMP",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$D4*/{"???",AM::ZPX},{"CMP",AM::ZPX},{"DEC",AM::ZPX},{"???",AM::IMP},
/*$D8*/{"CLD",AM::IMP},{"CMP",AM::ABY},{"???",AM::IMP},{"???",AM::IMP},
/*$DC*/{"???",AM::ABX},{"CMP",AM::ABX},{"DEC",AM::ABX},{"???",AM::IMP},

/*$E0*/{"CPX",AM::IMM},{"SBC",AM::IZX},{"???",AM::IMM},{"???",AM::IMP},
/*$E4*/{"CPX",AM::ZP0},{"SBC",AM::ZP0},{"INC",AM::ZP0},{"???",AM::IMP},
/*$E8*/{"INX",AM::IMP},{"SBC",AM::IMM},{"NOP",AM::IMP},{"???",AM::IMM},
/*$EC*/{"CPX",AM::ABS},{"SBC",AM::ABS},{"INC",AM::ABS},{"???",AM::IMP},

/*$F0*/{"BEQ",AM::REL},{"SBC",AM::IZY},{"???",AM::IMP},{"???",AM::IMP},
/*$F4*/{"???",AM::ZPX},{"SBC",AM::ZPX},{"INC",AM::ZPX},{"???",AM::IMP},
/*$F8*/{"SED",AM::IMP},{"SBC",AM::ABY},{"???",AM::IMP},{"???",AM::IMP},
/*$FC*/{"???",AM::ABX},{"SBC",AM::ABX},{"INC",AM::ABX},{"???",AM::IMP},
};

// ============================================================================
// Operand formatter
// ============================================================================

static std::string fmtOperand(AM mode, uint8_t lo, uint8_t hi, uint16_t instrAddr) {
    char buf[32];
    uint16_t abs = static_cast<uint16_t>((hi << 8) | lo);

    switch (mode) {
        case AM::IMP:  return "";
        case AM::ACC:  return "A";
        case AM::IMM:  std::snprintf(buf, sizeof(buf), "#$%02X",      lo);           break;
        case AM::ZP0:  std::snprintf(buf, sizeof(buf), "$%02X",       lo);           break;
        case AM::ZPX:  std::snprintf(buf, sizeof(buf), "$%02X,X",     lo);           break;
        case AM::ZPY:  std::snprintf(buf, sizeof(buf), "$%02X,Y",     lo);           break;
        case AM::ABS:  std::snprintf(buf, sizeof(buf), "$%04X",       abs);          break;
        case AM::ABX:  std::snprintf(buf, sizeof(buf), "$%04X,X",     abs);          break;
        case AM::ABY:  std::snprintf(buf, sizeof(buf), "$%04X,Y",     abs);          break;
        case AM::IND:  std::snprintf(buf, sizeof(buf), "($%04X)",     abs);          break;
        case AM::IZX:  std::snprintf(buf, sizeof(buf), "($%02X,X)",   lo);           break;
        case AM::IZY:  std::snprintf(buf, sizeof(buf), "($%02X),Y",   lo);           break;
        case AM::REL: {
            // Resolve branch target: PC after instruction (instrAddr+2) + signed offset
            uint16_t target = static_cast<uint16_t>(instrAddr + 2 + static_cast<int8_t>(lo));
            std::snprintf(buf, sizeof(buf), "$%04X", target);
            break;
        }
        default: return "???";
    }
    return buf;
}

// ============================================================================
// Public API
// ============================================================================

std::vector<DisasmLine> Disassembler::disassemble(
    const Bus& bus, uint16_t startAddr, int count)
{
    std::vector<DisasmLine> result;
    result.reserve(count);

    uint16_t addr = startAddr;

    for (int i = 0; i < count; ++i) {
        DisasmLine line;
        line.addr = addr;

        const uint8_t opcode = bus.read(addr);
        const auto&   info   = kOpcodes[opcode];
        const int     size   = 1 + static_cast<int>(kModeBytes[static_cast<int>(info.mode)]);

        line.byteCount = size;
        line.bytes[0]  = opcode;

        uint8_t lo = 0, hi = 0;
        if (size >= 2) { lo = bus.read(static_cast<uint16_t>(addr + 1)); line.bytes[1] = lo; }
        if (size >= 3) { hi = bus.read(static_cast<uint16_t>(addr + 2)); line.bytes[2] = hi; }

        line.mnemonic = info.name;
        line.operand  = fmtOperand(info.mode, lo, hi, addr);

        result.push_back(std::move(line));

        // Advance, wrapping within 16-bit space
        addr = static_cast<uint16_t>(addr + size);
    }

    return result;
}
