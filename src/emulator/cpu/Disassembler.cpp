#include "emulator/cpu/Disassembler.h"
#include "emulator/core/Bus.h"

#include <cstdio>

// ============================================================================
// Addressing mode enum (local to this translation unit)
// ============================================================================

enum class AM : uint8_t {
    IMP, ACC, IMM,
    ZP0, ZPX, ZPY,
    ABS, ABX, ABY,
    IND, IZX, IZY,
    REL,
    IZP,  // ($zp)      — 65C02 zero-page indirect
    AIX   // ($abs,X)   — 65C02 absolute-indexed indirect (JMP)
};

// Extra bytes consumed after the opcode byte (0, 1, or 2)
static const uint8_t kModeBytes[15] = {
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
    1, // IZP
    2, // AIX
};

// ============================================================================
// Compact 256-entry opcode table  (name + addressing mode)
// Matches the dispatch table in CPU8502.cpp exactly.
// ============================================================================

struct OpInfo { const char* name; AM mode; };

static const OpInfo kOpcodes[256] = {
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
// WDC 65C02 (CMOS) overlay
//
// Mirrors the entries patched in CPU65C02.cpp so the disassembler shows the
// same mnemonics and addressing modes the CMOS core actually executes.
// Returns the base (NMOS) entry for any opcode the 65C02 doesn't redefine.
// ============================================================================

static OpInfo cmosPatch(uint8_t op, OpInfo base) {
    switch (op) {
        case 0x04: return {"TSB", AM::ZP0};
        case 0x0C: return {"TSB", AM::ABS};
        case 0x12: return {"ORA", AM::IZP};
        case 0x14: return {"TRB", AM::ZP0};
        case 0x1A: return {"INA", AM::IMP};
        case 0x1C: return {"TRB", AM::ABS};
        case 0x32: return {"AND", AM::IZP};
        case 0x34: return {"BIT", AM::ZPX};
        case 0x3A: return {"DEA", AM::IMP};
        case 0x3C: return {"BIT", AM::ABX};
        case 0x52: return {"EOR", AM::IZP};
        case 0x5A: return {"PHY", AM::IMP};
        case 0x64: return {"STZ", AM::ZP0};
        case 0x72: return {"ADC", AM::IZP};
        case 0x74: return {"STZ", AM::ZPX};
        case 0x7A: return {"PLY", AM::IMP};
        case 0x7C: return {"JMP", AM::AIX};
        case 0x80: return {"BRA", AM::REL};
        case 0x89: return {"BIT", AM::IMM};
        case 0x92: return {"STA", AM::IZP};
        case 0x9C: return {"STZ", AM::ABS};
        case 0x9E: return {"STZ", AM::ABX};
        case 0xB2: return {"LDA", AM::IZP};
        case 0xD2: return {"CMP", AM::IZP};
        case 0xDA: return {"PHX", AM::IMP};
        case 0xF2: return {"SBC", AM::IZP};
        case 0xFA: return {"PLX", AM::IMP};
        default:   return base;
    }
}

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
        case AM::IZP:  std::snprintf(buf, sizeof(buf), "($%02X)",     lo);           break;
        case AM::AIX:  std::snprintf(buf, sizeof(buf), "($%04X,X)",   abs);          break;
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
    const Bus& bus, uint16_t startAddr, int count, bool cmos)
{
    std::vector<DisasmLine> result;
    result.reserve(count);

    uint16_t addr = startAddr;

    for (int i = 0; i < count; ++i) {
        DisasmLine line;
        line.addr = addr;

        const uint8_t opcode = bus.read(addr);
        const OpInfo  info   = cmos ? cmosPatch(opcode, kOpcodes[opcode])
                                    : kOpcodes[opcode];
        const int     size   = 1 + static_cast<int>(kModeBytes[static_cast<int>(info.mode)]);

        line.byteCount = size;
        line.bytes[0]  = opcode;

        uint8_t lo = 0, hi = 0;
        if (size >= 2) { lo = bus.read(static_cast<uint16_t>(addr + 1)); line.bytes[1] = lo; }
        if (size >= 3) { hi = bus.read(static_cast<uint16_t>(addr + 2)); line.bytes[2] = hi; }

        line.mnemonic = info.name;
        line.operand  = fmtOperand(info.mode, lo, hi, addr);

        switch (info.mode) {
            case AM::ABS: case AM::ABX: case AM::ABY: case AM::IND: case AM::AIX:
                line.targetAddr = static_cast<uint16_t>((hi << 8) | lo);
                line.hasTarget  = true;
                break;
            case AM::ZP0: case AM::ZPX: case AM::ZPY: case AM::IZP:
                line.targetAddr = lo;
                line.hasTarget  = true;
                break;
            case AM::REL:
                line.targetAddr = static_cast<uint16_t>(
                    addr + 2 + static_cast<int8_t>(lo));
                line.hasTarget  = true;
                break;
            default: break;
        }

        result.push_back(std::move(line));

        // Advance, wrapping within 16-bit space
        addr = static_cast<uint16_t>(addr + size);
    }

    return result;
}

// ============================================================================
// Zilog Z80 disassembler
//
// Uses the systematic x/y/z/p/q opcode decomposition (see "Decoding Z80
// opcodes", Cristian Dinu) so the CB / ED / DD / FD / DDCB / FDCB prefixes are
// all handled with small shared tables.  Goal is correct instruction *sizing*
// (so the listing never desyncs) and accurate mnemonics for the documented
// instruction set; a handful of undocumented IXH/IXL forms are shown too.
// ============================================================================

namespace {

const char* const kZr[8]   = {"B","C","D","E","H","L","(HL)","A"};
const char* const kZrp[4]  = {"BC","DE","HL","SP"};
const char* const kZrp2[4] = {"BC","DE","HL","AF"};
const char* const kZcc[8]  = {"NZ","Z","NC","C","PO","PE","P","M"};
const char* const kZalu[8] = {"ADD A,","ADC A,","SUB ","SBC A,","AND ","XOR ","OR ","CP "};
const char* const kZrot[8] = {"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
const char* const kZim[8]  = {"0","0","1","2","0","0","1","2"};
const char* const kZmisc[8]= {"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"};
const char* const kZbli[4][4] = {
    {"LDI","CPI","INI","OUTI"},
    {"LDD","CPD","IND","OUTD"},
    {"LDIR","CPIR","INIR","OTIR"},
    {"LDDR","CPDR","INDR","OTDR"},
};

// Sequential byte reader over the bus; tracks how many bytes were consumed.
struct ZReader {
    const Bus* bus;
    uint16_t   base;
    int        n = 0;
    uint8_t next() { return bus->read(static_cast<uint16_t>(base + n++)); }
};

std::string zhex8(uint8_t v)  { char b[8];  std::snprintf(b, sizeof b, "$%02X", v); return b; }
std::string zhex16(uint16_t v){ char b[8];  std::snprintf(b, sizeof b, "$%04X", v); return b; }

// Decode one instruction into a combined "MNEM operand" string; fills length,
// and (for jumps/calls) the resolved target address.
std::string z80One(const Bus& bus, uint16_t pc, int& len,
                   uint16_t& target, bool& hasTarget) {
    ZReader r{&bus, pc};
    hasTarget = false;

    int         idx = 0;            // 0 = HL, 1 = IX, 2 = IY
    std::string ix  = "HL";
    uint8_t op = r.next();
    while (op == 0xDD || op == 0xFD) {
        idx = (op == 0xDD) ? 1 : 2;
        ix  = (op == 0xDD) ? "IX" : "IY";
        op  = r.next();
    }

    auto imm16 = [&]() -> uint16_t { uint8_t lo = r.next(), hi = r.next();
                                     return static_cast<uint16_t>(lo | (hi << 8)); };
    auto rpName  = [&](int p) { return (p == 2 && idx) ? ix : std::string(kZrp[p]); };
    auto rp2Name = [&](int p) { return (p == 2 && idx) ? ix : std::string(kZrp2[p]); };
    // Register operand; (HL) becomes (IX+d)/(IY+d) and consumes a displacement.
    auto regName = [&](int i) -> std::string {
        if (idx == 0 || (i != 6 && i != 4 && i != 5)) return kZr[i];
        if (i == 6) { int8_t d = static_cast<int8_t>(r.next());
                      char b[16]; std::snprintf(b, sizeof b, "(%s%+d)", ix.c_str(), d);
                      return b; }
        return ix + (i == 4 ? "H" : "L");   // undocumented IXH/IXL
    };

    std::string s;

    auto decodeCB = [&]() {
        if (idx != 0) {
            int8_t d = static_cast<int8_t>(r.next());   // DDCB: d precedes opcode
            uint8_t o = r.next();
            int x = o >> 6, y = (o >> 3) & 7, z = o & 7;
            char mem[16]; std::snprintf(mem, sizeof mem, "(%s%+d)", ix.c_str(), d);
            const char* extra = (z != 6) ? kZr[z] : nullptr;  // undocumented store
            if (x == 0)      s = std::string(kZrot[y]) + " " + mem;
            else if (x == 1) s = "BIT " + std::to_string(y) + "," + mem;
            else if (x == 2) s = "RES " + std::to_string(y) + "," + mem;
            else             s = "SET " + std::to_string(y) + "," + mem;
            if (x != 1 && extra) s += "," + std::string(extra);   // ld r,(set/res ...)
        } else {
            uint8_t o = r.next();
            int x = o >> 6, y = (o >> 3) & 7, z = o & 7;
            if (x == 0)      s = std::string(kZrot[y]) + " " + kZr[z];
            else if (x == 1) s = "BIT " + std::to_string(y) + "," + kZr[z];
            else if (x == 2) s = "RES " + std::to_string(y) + "," + kZr[z];
            else             s = "SET " + std::to_string(y) + "," + kZr[z];
        }
    };

    auto decodeED = [&]() {
        uint8_t o = r.next();
        int x = o >> 6, y = (o >> 3) & 7, z = o & 7, p = y >> 1, q = y & 1;
        if (x == 1) {
            switch (z) {
                case 0: s = (y == 6) ? "IN (C)" : "IN " + std::string(kZr[y]) + ",(C)"; break;
                case 1: s = (y == 6) ? "OUT (C),0" : "OUT (C)," + std::string(kZr[y]); break;
                case 2: s = (q ? "ADC HL," : "SBC HL,") + std::string(kZrp[p]); break;
                case 3: { uint16_t nn = imm16();
                          s = q ? "LD " + std::string(kZrp[p]) + ",(" + zhex16(nn) + ")"
                                : "LD (" + zhex16(nn) + ")," + std::string(kZrp[p]); break; }
                case 4: s = "NEG"; break;
                case 5: s = (y == 1) ? "RETI" : "RETN"; break;
                case 6: s = "IM " + std::string(kZim[y]); break;
                default: { const char* t[8] = {"LD I,A","LD R,A","LD A,I","LD A,R",
                                               "RRD","RLD","NOP","NOP"};
                           s = t[y]; break; }
            }
        } else if (x == 2 && z <= 3 && y >= 4) {
            s = kZbli[y - 4][z];
        } else {
            s = "NOP";   // invalid ED prefix → NONI/NOP
        }
    };

    if (op == 0xCB)      decodeCB();
    else if (op == 0xED) decodeED();
    else {
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
        switch (x) {
        case 0:
            switch (z) {
            case 0:
                if (y == 0) s = "NOP";
                else if (y == 1) s = "EX AF,AF'";
                else { int8_t e = static_cast<int8_t>(r.next());
                       target = static_cast<uint16_t>(pc + r.n + e); hasTarget = true;
                       if (y == 2)      s = "DJNZ " + zhex16(target);
                       else if (y == 3) s = "JR " + zhex16(target);
                       else             s = "JR " + std::string(kZcc[y - 4]) + "," + zhex16(target); }
                break;
            case 1:
                if (q == 0) { uint16_t nn = imm16(); s = "LD " + rpName(p) + "," + zhex16(nn); }
                else        s = "ADD " + ix + "," + rpName(p);
                break;
            case 2:
                if (q == 0) {
                    if (p == 0) s = "LD (BC),A";
                    else if (p == 1) s = "LD (DE),A";
                    else if (p == 2) { uint16_t nn = imm16(); s = "LD (" + zhex16(nn) + ")," + ix; }
                    else { uint16_t nn = imm16(); s = "LD (" + zhex16(nn) + "),A"; }
                } else {
                    if (p == 0) s = "LD A,(BC)";
                    else if (p == 1) s = "LD A,(DE)";
                    else if (p == 2) { uint16_t nn = imm16(); s = "LD " + ix + ",(" + zhex16(nn) + ")"; }
                    else { uint16_t nn = imm16(); s = "LD A,(" + zhex16(nn) + ")"; }
                }
                break;
            case 3: s = (q ? "DEC " : "INC ") + rpName(p); break;
            case 4: s = "INC " + regName(y); break;
            case 5: s = "DEC " + regName(y); break;
            case 6: { std::string dst = regName(y); uint8_t n = r.next();
                      s = "LD " + dst + "," + zhex8(n); break; }
            default: s = kZmisc[y]; break;
            }
            break;
        case 1:
            if (z == 6 && y == 6) s = "HALT";
            else {
                // When one operand is (IX+d), the other keeps its plain H/L name
                // (the IXH/IXL substitution doesn't apply alongside a displacement).
                const bool mem = (y == 6 || z == 6);
                auto rn = [&](int i) -> std::string {
                    if (i == 6)  return regName(6);              // (IX+d), reads disp
                    if (mem)     return kZr[i];                  // no IXH/IXL remap
                    return regName(i);
                };
                std::string dst = rn(y), src = rn(z);
                s = "LD " + dst + "," + src;
            }
            break;
        case 2: s = std::string(kZalu[y]) + regName(z); break;
        case 3:
            switch (z) {
            case 0: s = "RET " + std::string(kZcc[y]); break;
            case 1:
                if (q == 0) s = "POP " + rp2Name(p);
                else {
                    if (p == 0) s = "RET";
                    else if (p == 1) s = "EXX";
                    else if (p == 2) s = "JP (" + ix + ")";
                    else s = "LD SP," + ix;
                }
                break;
            case 2: { uint16_t nn = imm16(); target = nn; hasTarget = true;
                      s = "JP " + std::string(kZcc[y]) + "," + zhex16(nn); break; }
            case 3:
                if (y == 0) { uint16_t nn = imm16(); target = nn; hasTarget = true; s = "JP " + zhex16(nn); }
                else if (y == 1) decodeCB();
                else if (y == 2) { uint8_t n = r.next(); s = "OUT (" + zhex8(n) + "),A"; }
                else if (y == 3) { uint8_t n = r.next(); s = "IN A,(" + zhex8(n) + ")"; }
                else if (y == 4) s = "EX (SP)," + ix;
                else if (y == 5) s = "EX DE,HL";
                else if (y == 6) s = "DI";
                else s = "EI";
                break;
            case 4: { uint16_t nn = imm16(); target = nn; hasTarget = true;
                      s = "CALL " + std::string(kZcc[y]) + "," + zhex16(nn); break; }
            case 5:
                if (q == 0) s = "PUSH " + rp2Name(p);
                else if (p == 0) { uint16_t nn = imm16(); target = nn; hasTarget = true; s = "CALL " + zhex16(nn); }
                else s = "NOP";   // DD/ED/FD already consumed above
                break;
            case 6: s = std::string(kZalu[y]) + zhex8(r.next()); break;
            default: target = static_cast<uint16_t>(y * 8); hasTarget = true;
                     s = "RST " + zhex16(target); break;
            }
            break;
        }
    }

    len = r.n;
    return s;
}

}  // namespace

std::vector<DisasmLine> Disassembler::disassembleZ80(
    const Bus& bus, uint16_t startAddr, int count)
{
    std::vector<DisasmLine> result;
    result.reserve(count);

    uint16_t addr = startAddr;
    for (int i = 0; i < count; ++i) {
        DisasmLine line;
        line.addr = addr;

        int      len = 1;
        uint16_t tgt = 0;
        bool     hasTgt = false;
        std::string text = z80One(bus, addr, len, tgt, hasTgt);

        if (len < 1) len = 1;
        if (len > 4) len = 4;
        line.byteCount = len;
        for (int b = 0; b < len; ++b)
            line.bytes[b] = bus.read(static_cast<uint16_t>(addr + b));

        // Split "MNEM operand" into the two display columns.
        const size_t sp = text.find(' ');
        if (sp == std::string::npos) {
            line.mnemonic = text;
        } else {
            line.mnemonic = text.substr(0, sp);
            line.operand  = text.substr(sp + 1);
        }
        line.targetAddr = tgt;
        line.hasTarget  = hasTgt;

        result.push_back(std::move(line));
        addr = static_cast<uint16_t>(addr + len);
    }
    return result;
}
