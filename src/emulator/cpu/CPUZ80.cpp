#include "emulator/cpu/CPUZ80.h"
#include "emulator/core/Bus.h"

#include <iomanip>
#include <sstream>

// ============================================================================
// Construction / ICPU lifecycle
// ============================================================================

CPUZ80::CPUZ80() {
    portRead_  = [](uint16_t)          -> uint8_t { return 0xFF; };
    portWrite_ = [](uint16_t, uint8_t)             {};
}

void CPUZ80::setPortHandlers(std::function<uint8_t(uint16_t)>       rd,
                              std::function<void(uint16_t, uint8_t)> wr) {
    portRead_  = rd;
    portWrite_ = wr;
}

void CPUZ80::connectBus(Bus* bus) { bus_ = bus; }

void CPUZ80::reset() {
    PC = 0x0000;
    SP = 0xFFFF;
    A  = F  = 0xFF;
    B  = C  = 0xFF;
    D  = E  = 0xFF;
    H  = L  = 0xFF;
    A_ = F_ = 0xFF;
    B_ = C_ = 0xFF;
    D_ = E_ = 0xFF;
    H_ = L_ = 0xFF;
    IX = IY = 0xFFFF;
    I  = R  = 0;
    IM = 0;
    IFF1 = IFF2 = false;
    halted_     = false;
    irqLine_    = false;
    nmiPending_ = false;
    eiDelay_    = false;
    cycles_     = 0;
}

void CPUZ80::irq() { irqLine_ = true;  }
void CPUZ80::nmi() { nmiPending_ = true; }

// ============================================================================
// clock() — one T-state per call
// ============================================================================

void CPUZ80::clock() {
    if (cycles_ > 0) {
        --cycles_;
        return;
    }

    // --- NMI (edge-triggered, higher priority than IRQ) --------------------
    if (nmiPending_) {
        nmiPending_ = false;
        halted_     = false;
        IFF2        = IFF1;
        IFF1        = false;
        eiDelay_    = false;
        R           = (R + 1) & 0x7F;
        push16(PC);
        PC      = 0x0066;
        cycles_ = 11 - 1;
        return;
    }

    // --- Maskable IRQ (level-sensitive) ------------------------------------
    if (irqLine_ && IFF1 && !eiDelay_) {
        halted_  = false;
        IFF1     = IFF2 = false;
        R        = (R + 1) & 0x7F;
        switch (IM) {
            case 0:   // execute opcode on bus (treat as RST 38H)
            case 1:
                push16(PC);
                PC      = 0x0038;
                cycles_ = 13 - 1;
                break;
            case 2: {
                uint16_t vec = rd((uint16_t(I) << 8) | 0xFF);
                vec |= uint16_t(rd((uint16_t(I) << 8) | 0xFF)) << 8;
                // Use I register + 0xFF as the data-bus value (common safe default)
                uint16_t addr = (uint16_t(I) << 8) | 0xFF;
                push16(PC);
                PC      = rd16(addr);
                cycles_ = 19 - 1;
                break;
            }
        }
        return;
    }

    // --- HALT: spin until interrupt ----------------------------------------
    if (halted_) {
        R       = (R + 1) & 0x7F;
        cycles_ = 4 - 1;
        return;
    }

    // --- EI delay: clear suppression flag before this instruction ----------
    bool wasEIDelay = eiDelay_;
    if (wasEIDelay) eiDelay_ = false;

    // --- Fetch and execute --------------------------------------------------
    R = (R + 1) & 0x7F;
    uint8_t op = fetch();
    cycles_ = 4 - 1;   // default; execUnprefixed/etc. will add extra cycles

    if (op == 0xCB) {
        R  = (R + 1) & 0x7F;
        op = fetch();
        execCB(op);
    } else if (op == 0xED) {
        R  = (R + 1) & 0x7F;
        op = fetch();
        execED(op);
    } else if (op == 0xDD) {
        R  = (R + 1) & 0x7F;
        op = fetch();
        execXY(op, IX);
    } else if (op == 0xFD) {
        R  = (R + 1) & 0x7F;
        op = fetch();
        execXY(op, IY);
    } else {
        execUnprefixed(op);
    }
}

// ============================================================================
// Memory / port access
// ============================================================================

uint8_t CPUZ80::rd(uint16_t a) {
    return bus_ ? bus_->read(a) : 0xFF;
}
void CPUZ80::wr(uint16_t a, uint8_t v) {
    if (bus_) bus_->write(a, v);
}
uint16_t CPUZ80::rd16(uint16_t a) {
    return uint16_t(rd(a)) | (uint16_t(rd(a + 1)) << 8);
}
void CPUZ80::wr16(uint16_t a, uint16_t v) {
    wr(a,     uint8_t(v));
    wr(a + 1, uint8_t(v >> 8));
}
uint8_t CPUZ80::fetch() {
    return rd(PC++);
}
uint16_t CPUZ80::fetch16() {
    uint16_t lo = fetch();
    uint16_t hi = fetch();
    return lo | (hi << 8);
}
void CPUZ80::push16(uint16_t v) {
    wr(--SP, uint8_t(v >> 8));
    wr(--SP, uint8_t(v));
}
uint16_t CPUZ80::pop16() {
    uint16_t lo = rd(SP++);
    uint16_t hi = rd(SP++);
    return lo | (hi << 8);
}
uint8_t CPUZ80::ioIn(uint16_t port) {
    return portRead_(port);
}
void CPUZ80::ioOut(uint16_t port, uint8_t v) {
    portWrite_(port, v);
}

// ============================================================================
// Flag/arithmetic helpers
// ============================================================================

bool CPUZ80::parity(uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (~v) & 1;
}

uint8_t CPUZ80::add8(uint8_t a, uint8_t b, bool cy) {
    uint16_t r = uint16_t(a) + b + (cy ? 1 : 0);
    uint8_t  res = uint8_t(r);
    setFlag(FLAG_S,  res & 0x80);
    setFlag(FLAG_Z,  res == 0);
    setFlag(FLAG_H,  ((a & 0x0F) + (b & 0x0F) + (cy ? 1 : 0)) > 0x0F);
    setFlag(FLAG_PV, (~(a ^ b) & (a ^ res)) & 0x80);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_C,  r > 0xFF);
    setFlag(FLAG_X,  res & FLAG_X);
    setFlag(FLAG_Y,  res & FLAG_Y);
    return res;
}

uint8_t CPUZ80::sub8(uint8_t a, uint8_t b, bool cy) {
    uint16_t r   = uint16_t(a) - b - (cy ? 1 : 0);
    uint8_t  res = uint8_t(r);
    setFlag(FLAG_S,  res & 0x80);
    setFlag(FLAG_Z,  res == 0);
    setFlag(FLAG_H,  (int(a & 0x0F) - int(b & 0x0F) - (cy ? 1 : 0)) < 0);
    setFlag(FLAG_PV, ((a ^ b) & (a ^ res)) & 0x80);
    setFlag(FLAG_N,  true);
    setFlag(FLAG_C,  r > 0xFF);
    setFlag(FLAG_X,  res & FLAG_X);
    setFlag(FLAG_Y,  res & FLAG_Y);
    return res;
}

uint8_t CPUZ80::inc8(uint8_t v) {
    uint8_t res = v + 1;
    setFlag(FLAG_S,  res & 0x80);
    setFlag(FLAG_Z,  res == 0);
    setFlag(FLAG_H,  (v & 0x0F) == 0x0F);
    setFlag(FLAG_PV, v == 0x7F);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_X,  res & FLAG_X);
    setFlag(FLAG_Y,  res & FLAG_Y);
    return res;
}

uint8_t CPUZ80::dec8(uint8_t v) {
    uint8_t res = v - 1;
    setFlag(FLAG_S,  res & 0x80);
    setFlag(FLAG_Z,  res == 0);
    setFlag(FLAG_H,  (v & 0x0F) == 0);
    setFlag(FLAG_PV, v == 0x80);
    setFlag(FLAG_N,  true);
    setFlag(FLAG_X,  res & FLAG_X);
    setFlag(FLAG_Y,  res & FLAG_Y);
    return res;
}

void CPUZ80::and8(uint8_t v) {
    A &= v;
    setFlag(FLAG_S,  A & 0x80);
    setFlag(FLAG_Z,  A == 0);
    setFlag(FLAG_H,  true);
    setFlag(FLAG_PV, parity(A));
    setFlag(FLAG_N,  false);
    setFlag(FLAG_C,  false);
    setFlag(FLAG_X,  A & FLAG_X);
    setFlag(FLAG_Y,  A & FLAG_Y);
}

void CPUZ80::or8(uint8_t v) {
    A |= v;
    setFlag(FLAG_S,  A & 0x80);
    setFlag(FLAG_Z,  A == 0);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_PV, parity(A));
    setFlag(FLAG_N,  false);
    setFlag(FLAG_C,  false);
    setFlag(FLAG_X,  A & FLAG_X);
    setFlag(FLAG_Y,  A & FLAG_Y);
}

void CPUZ80::xor8(uint8_t v) {
    A ^= v;
    setFlag(FLAG_S,  A & 0x80);
    setFlag(FLAG_Z,  A == 0);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_PV, parity(A));
    setFlag(FLAG_N,  false);
    setFlag(FLAG_C,  false);
    setFlag(FLAG_X,  A & FLAG_X);
    setFlag(FLAG_Y,  A & FLAG_Y);
}

void CPUZ80::cp8(uint8_t v) {
    sub8(A, v);   // sets flags; discards result
    // undocumented: X/Y come from the operand, not the result
    setFlag(FLAG_X, v & FLAG_X);
    setFlag(FLAG_Y, v & FLAG_Y);
}

void CPUZ80::daa() {
    uint8_t  cf = getFlag(FLAG_C) ? 1 : 0;
    uint8_t  hf = getFlag(FLAG_H) ? 1 : 0;
    uint8_t  nf = getFlag(FLAG_N) ? 1 : 0;
    uint8_t  a  = A;
    uint8_t  correction = 0;
    bool     newC = false;

    if (!nf) {
        if (hf || (a & 0x0F) > 9)          correction |= 0x06;
        if (cf || a > 0x99)               { correction |= 0x60; newC = true; }
        A += correction;
    } else {
        if (hf || (a & 0x0F) > 9)          correction |= 0x06;
        if (cf || a > 0x99)               { correction |= 0x60; newC = true; }
        A -= correction;
    }
    setFlag(FLAG_S,  A & 0x80);
    setFlag(FLAG_Z,  A == 0);
    setFlag(FLAG_H,  !nf ? ((a & 0x0F) > 9) : (hf && (a & 0x0F) < 6));
    setFlag(FLAG_PV, parity(A));
    setFlag(FLAG_C,  newC || cf);
    setFlag(FLAG_X,  A & FLAG_X);
    setFlag(FLAG_Y,  A & FLAG_Y);
}

// 16-bit HL arithmetic

uint16_t CPUZ80::add16hl(uint16_t hl, uint16_t rp) {
    uint32_t r = uint32_t(hl) + rp;
    uint16_t res = uint16_t(r);
    setFlag(FLAG_H,  ((hl & 0x0FFF) + (rp & 0x0FFF)) > 0x0FFF);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_C,  r > 0xFFFF);
    setFlag(FLAG_X,  (res >> 8) & FLAG_X);
    setFlag(FLAG_Y,  (res >> 8) & FLAG_Y);
    return res;
}

uint16_t CPUZ80::adc16hl(uint16_t hl, uint16_t rp) {
    uint32_t cy  = getFlag(FLAG_C) ? 1 : 0;
    uint32_t r   = uint32_t(hl) + rp + cy;
    uint16_t res = uint16_t(r);
    setFlag(FLAG_S,  res & 0x8000);
    setFlag(FLAG_Z,  res == 0);
    setFlag(FLAG_H,  ((hl & 0x0FFF) + (rp & 0x0FFF) + cy) > 0x0FFF);
    setFlag(FLAG_PV, (~(hl ^ rp) & (hl ^ res)) & 0x8000);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_C,  r > 0xFFFF);
    setFlag(FLAG_X,  (res >> 8) & FLAG_X);
    setFlag(FLAG_Y,  (res >> 8) & FLAG_Y);
    return res;
}

uint16_t CPUZ80::sbc16hl(uint16_t hl, uint16_t rp) {
    uint32_t cy  = getFlag(FLAG_C) ? 1 : 0;
    uint32_t r   = uint32_t(hl) - rp - cy;
    uint16_t res = uint16_t(r);
    setFlag(FLAG_S,  res & 0x8000);
    setFlag(FLAG_Z,  res == 0);
    setFlag(FLAG_H,  (int(hl & 0x0FFF) - int(rp & 0x0FFF) - int(cy)) < 0);
    setFlag(FLAG_PV, ((hl ^ rp) & (hl ^ res)) & 0x8000);
    setFlag(FLAG_N,  true);
    setFlag(FLAG_C,  r > 0xFFFF);
    setFlag(FLAG_X,  (res >> 8) & FLAG_X);
    setFlag(FLAG_Y,  (res >> 8) & FLAG_Y);
    return res;
}

// CB rotate/shift operations

uint8_t CPUZ80::rlc8(uint8_t v) {
    bool b7 = v & 0x80;
    v = (v << 1) | (b7 ? 1 : 0);
    setFlag(FLAG_C,  b7);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::rrc8(uint8_t v) {
    bool b0 = v & 0x01;
    v = (v >> 1) | (b0 ? 0x80 : 0);
    setFlag(FLAG_C,  b0);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::rl8(uint8_t v) {
    bool oldC = getFlag(FLAG_C);
    bool b7   = v & 0x80;
    v = (v << 1) | (oldC ? 1 : 0);
    setFlag(FLAG_C,  b7);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::rr8(uint8_t v) {
    bool oldC = getFlag(FLAG_C);
    bool b0   = v & 0x01;
    v = (v >> 1) | (oldC ? 0x80 : 0);
    setFlag(FLAG_C,  b0);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::sla8(uint8_t v) {
    bool b7 = v & 0x80;
    v <<= 1;
    setFlag(FLAG_C,  b7);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::sra8(uint8_t v) {
    bool b0 = v & 0x01;
    v = (v >> 1) | (v & 0x80);  // keep sign
    setFlag(FLAG_C,  b0);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::sll8(uint8_t v) {  // undocumented: shifts left, sets bit 0
    bool b7 = v & 0x80;
    v = (v << 1) | 0x01;
    setFlag(FLAG_C,  b7);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  v & 0x80);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::srl8(uint8_t v) {
    bool b0 = v & 0x01;
    v >>= 1;
    setFlag(FLAG_C,  b0);
    setFlag(FLAG_N,  false);
    setFlag(FLAG_H,  false);
    setFlag(FLAG_S,  false);
    setFlag(FLAG_Z,  v == 0);
    setFlag(FLAG_PV, parity(v));
    setFlag(FLAG_X,  v & FLAG_X);
    setFlag(FLAG_Y,  v & FLAG_Y);
    return v;
}

uint8_t CPUZ80::applyCBOp(uint8_t rot, uint8_t val) {
    switch (rot) {
        case 0: return rlc8(val);
        case 1: return rrc8(val);
        case 2: return rl8 (val);
        case 3: return rr8 (val);
        case 4: return sla8(val);
        case 5: return sra8(val);
        case 6: return sll8(val);
        default: return srl8(val);
    }
}

// ============================================================================
// Register table helpers
// ============================================================================

uint8_t CPUZ80::getReg(uint8_t r) {
    switch (r & 7) {
        case 0: return B;
        case 1: return C;
        case 2: return D;
        case 3: return E;
        case 4: return H;
        case 5: return L;
        case 6: return rd(rHL());
        default: return A;
    }
}

void CPUZ80::setReg(uint8_t r, uint8_t v) {
    switch (r & 7) {
        case 0: B = v; break;
        case 1: C = v; break;
        case 2: D = v; break;
        case 3: E = v; break;
        case 4: H = v; break;
        case 5: L = v; break;
        case 6: wr(rHL(), v); break;
        default: A = v; break;
    }
}

uint16_t CPUZ80::getRP(uint8_t p) {
    switch (p & 3) {
        case 0: return rBC();
        case 1: return rDE();
        case 2: return rHL();
        default: return SP;
    }
}

void CPUZ80::setRP(uint8_t p, uint16_t v) {
    switch (p & 3) {
        case 0: wBC(v); break;
        case 1: wDE(v); break;
        case 2: wHL(v); break;
        default: SP = v; break;
    }
}

uint16_t CPUZ80::getRP2(uint8_t p) {
    switch (p & 3) {
        case 0: return rBC();
        case 1: return rDE();
        case 2: return rHL();
        default: return rAF();
    }
}

void CPUZ80::setRP2(uint8_t p, uint16_t v) {
    switch (p & 3) {
        case 0: wBC(v); break;
        case 1: wDE(v); break;
        case 2: wHL(v); break;
        default: wAF(v); break;
    }
}

void CPUZ80::doALU(uint8_t op, uint8_t v) {
    switch (op & 7) {
        case 0: A = add8(A, v);         break;   // ADD A,v
        case 1: A = add8(A, v, getFlag(FLAG_C)); break;   // ADC A,v
        case 2: A = sub8(A, v);         break;   // SUB v
        case 3: A = sub8(A, v, getFlag(FLAG_C)); break;   // SBC A,v
        case 4: and8(v);                break;   // AND v
        case 5: xor8(v);                break;   // XOR v
        case 6: or8(v);                 break;   // OR v
        default: cp8(v);                break;   // CP v
    }
}

bool CPUZ80::testCC(uint8_t cc) const {
    switch (cc & 7) {
        case 0: return !getFlag(FLAG_Z);   // NZ
        case 1: return  getFlag(FLAG_Z);   // Z
        case 2: return !getFlag(FLAG_C);   // NC
        case 3: return  getFlag(FLAG_C);   // C
        case 4: return !getFlag(FLAG_PV);  // PO
        case 5: return  getFlag(FLAG_PV);  // PE
        case 6: return !getFlag(FLAG_S);   // P (positive)
        default: return getFlag(FLAG_S);   // M (minus)
    }
}

// ============================================================================
// execUnprefixed — structured decode using x/y/z/p/q fields
// ============================================================================

void CPUZ80::execUnprefixed(uint8_t op) {
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    if (x == 1) {
        // LD r[y], r[z]  —  or HALT if y==6 && z==6
        if (y == 6 && z == 6) {
            halted_ = true;
            cycles_ += 0;   // 4 total
        } else {
            uint8_t v = getReg(z);
            setReg(y, v);
            if (y == 6 || z == 6) cycles_ += 3;   // 7 total when (HL) involved
        }
        return;
    }

    if (x == 2) {
        // ALU A, r[z]
        uint8_t v = getReg(z);
        doALU(y, v);
        if (z == 6) cycles_ += 3;   // (HL) — 7 total
        return;
    }

    if (x == 3) {
        switch (z) {
            case 0:   // RET cc
                if (testCC(y)) {
                    PC      = pop16();
                    cycles_ += 6;   // 11 total
                } else {
                    cycles_ += 1;   // 5 total
                }
                break;

            case 1:   // POP / RET / EXX / JP(HL) / LD SP,HL
                if (q == 0) {
                    setRP2(p, pop16());
                    cycles_ += 6;   // 10 total
                } else {
                    switch (p) {
                        case 0: PC = pop16(); cycles_ += 6;  break;  // RET  (10)
                        case 1: // EXX
                            std::swap(B, B_); std::swap(C, C_);
                            std::swap(D, D_); std::swap(E, E_);
                            std::swap(H, H_); std::swap(L, L_);
                            break;  // 4
                        case 2: PC = rHL(); break;    // JP (HL) — 4
                        case 3: SP = rHL(); break;    // LD SP,HL — 6
                    }
                    if (p == 3) cycles_ += 2;
                }
                break;

            case 2: {  // JP cc,nn
                uint16_t addr = fetch16();
                if (testCC(y)) PC = addr;
                cycles_ += 6;   // 10 total
                break;
            }

            case 3:   // miscellaneous
                switch (y) {
                    case 0: PC = fetch16(); cycles_ += 6; break;   // JP nn (10)
                    case 1: /* CB prefix — handled in clock() */ break;
                    case 2: {                                       // OUT (n),A
                        uint8_t port = fetch();
                        ioOut((uint16_t(A) << 8) | port, A);
                        cycles_ += 7;   // 11 total
                        break;
                    }
                    case 3: {                                       // IN A,(n)
                        uint8_t port = fetch();
                        A = ioIn((uint16_t(A) << 8) | port);
                        cycles_ += 7;   // 11 total
                        break;
                    }
                    case 4: {                                       // EX (SP),HL
                        uint16_t tmp = rd16(SP);
                        wr16(SP, rHL());
                        wHL(tmp);
                        cycles_ += 15;  // 19 total
                        break;
                    }
                    case 5:                                         // EX DE,HL
                        std::swap(D, H);
                        std::swap(E, L);
                        break;   // 4
                    case 6: IFF1 = IFF2 = false; break;             // DI (4)
                    case 7:                                         // EI (4)
                        IFF1 = IFF2 = true;
                        eiDelay_ = true;
                        break;
                }
                break;

            case 4: {  // CALL cc,nn
                uint16_t addr = fetch16();
                if (testCC(y)) {
                    push16(PC);
                    PC      = addr;
                    cycles_ += 13;  // 17 total
                } else {
                    cycles_ += 6;   // 10 total
                }
                break;
            }

            case 5:   // PUSH / CALL nn / DD/ED/FD prefixes (handled in clock)
                if (q == 0) {
                    push16(getRP2(p));
                    cycles_ += 7;   // 11 total
                } else {
                    switch (p) {
                        case 0:  // CALL nn
                        {
                            uint16_t addr = fetch16();
                            push16(PC);
                            PC = addr;
                            cycles_ += 13;  // 17 total
                        }
                        break;
                        // p=1 DD, p=2 ED, p=3 FD — handled as prefixes in clock()
                        default: break;
                    }
                }
                break;

            case 6: {  // ALU A, n
                uint8_t v = fetch();
                doALU(y, v);
                cycles_ += 3;   // 7 total
                break;
            }

            case 7:   // RST y*8
                push16(PC);
                PC      = uint16_t(y) * 8;
                cycles_ += 7;   // 11 total
                break;
        }
        return;
    }

    // x == 0
    switch (z) {
        case 0:   // NOP / EX AF,AF' / DJNZ / JR / JR cc
            switch (y) {
                case 0:  break;   // NOP (4)
                case 1:  // EX AF,AF'
                    std::swap(A, A_);
                    std::swap(F, F_);
                    break;
                case 2: {  // DJNZ d
                    int8_t d = int8_t(fetch());
                    if (--B != 0) {
                        PC      += d;
                        cycles_ += 9;   // 13 total
                    } else {
                        cycles_ += 4;   // 8 total
                    }
                    break;
                }
                case 3: {  // JR d
                    int8_t d = int8_t(fetch());
                    PC += d;
                    cycles_ += 8;   // 12 total
                    break;
                }
                default: {  // JR cc[y-4], d  (y=4..7)
                    int8_t d = int8_t(fetch());
                    if (testCC(y - 4)) {
                        PC      += d;
                        cycles_ += 5;   // 12 total (7 base + 5)
                    } else {
                        cycles_ += 3;   // 7 total
                    }
                    break;
                }
            }
            break;

        case 1: {  // LD rp,nn  /  ADD HL,rp
            if (q == 0) {
                setRP(p, fetch16());
                cycles_ += 6;   // 10 total
            } else {
                wHL(add16hl(rHL(), getRP(p)));
                cycles_ += 7;   // 11 total
            }
            break;
        }

        case 2:  // indirect loads
            switch (y) {
                case 0: wr(rBC(), A); cycles_ += 3; break;          // LD (BC),A (7)
                case 1: A = rd(rBC()); cycles_ += 3; break;         // LD A,(BC) (7)
                case 2: wr(rDE(), A); cycles_ += 3; break;          // LD (DE),A (7)
                case 3: A = rd(rDE()); cycles_ += 3; break;         // LD A,(DE) (7)
                case 4: {                                            // LD (nn),HL (16)
                    uint16_t nn = fetch16();
                    wr16(nn, rHL());
                    cycles_ += 12;
                    break;
                }
                case 5: {                                            // LD HL,(nn) (16)
                    uint16_t nn = fetch16();
                    wHL(rd16(nn));
                    cycles_ += 12;
                    break;
                }
                case 6: {                                            // LD (nn),A (13)
                    uint16_t nn = fetch16();
                    wr(nn, A);
                    cycles_ += 9;
                    break;
                }
                case 7: {                                            // LD A,(nn) (13)
                    uint16_t nn = fetch16();
                    A = rd(nn);
                    cycles_ += 9;
                    break;
                }
            }
            break;

        case 3:   // INC rp / DEC rp
            if (q == 0) {
                setRP(p, getRP(p) + 1);
            } else {
                setRP(p, getRP(p) - 1);
            }
            cycles_ += 2;   // 6 total
            break;

        case 4:   // INC r[y]
            if (y == 6) {
                wr(rHL(), inc8(rd(rHL())));
                cycles_ += 7;   // 11 total
            } else {
                setReg(y, inc8(getReg(y)));
                // 4 total
            }
            break;

        case 5:   // DEC r[y]
            if (y == 6) {
                wr(rHL(), dec8(rd(rHL())));
                cycles_ += 7;   // 11 total
            } else {
                setReg(y, dec8(getReg(y)));
            }
            break;

        case 6:   // LD r[y], n
        {
            uint8_t n = fetch();
            if (y == 6) {
                wr(rHL(), n);
                cycles_ += 6;   // 10 total
            } else {
                setReg(y, n);
                cycles_ += 3;   // 7 total
            }
            break;
        }

        case 7:   // RLCA/RRCA/RLA/RRA/DAA/CPL/SCF/CCF
            switch (y) {
                case 0:  // RLCA
                {
                    bool b7 = A & 0x80;
                    A = (A << 1) | (b7 ? 1 : 0);
                    setFlag(FLAG_C, b7);
                    setFlag(FLAG_H, false);
                    setFlag(FLAG_N, false);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
                }
                case 1:  // RRCA
                {
                    bool b0 = A & 0x01;
                    A = (A >> 1) | (b0 ? 0x80 : 0);
                    setFlag(FLAG_C, b0);
                    setFlag(FLAG_H, false);
                    setFlag(FLAG_N, false);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
                }
                case 2:  // RLA
                {
                    bool oldC = getFlag(FLAG_C);
                    bool b7   = A & 0x80;
                    A = (A << 1) | (oldC ? 1 : 0);
                    setFlag(FLAG_C, b7);
                    setFlag(FLAG_H, false);
                    setFlag(FLAG_N, false);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
                }
                case 3:  // RRA
                {
                    bool oldC = getFlag(FLAG_C);
                    bool b0   = A & 0x01;
                    A = (A >> 1) | (oldC ? 0x80 : 0);
                    setFlag(FLAG_C, b0);
                    setFlag(FLAG_H, false);
                    setFlag(FLAG_N, false);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
                }
                case 4: daa();  break;   // DAA
                case 5:         // CPL
                    A = ~A;
                    setFlag(FLAG_H, true);
                    setFlag(FLAG_N, true);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
                case 6:         // SCF
                    setFlag(FLAG_C, true);
                    setFlag(FLAG_H, false);
                    setFlag(FLAG_N, false);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
                case 7:         // CCF
                    setFlag(FLAG_H, getFlag(FLAG_C));
                    setFlag(FLAG_C, !getFlag(FLAG_C));
                    setFlag(FLAG_N, false);
                    setFlag(FLAG_X, A & FLAG_X);
                    setFlag(FLAG_Y, A & FLAG_Y);
                    break;
            }
            break;
    }
}

// ============================================================================
// execCB — CB-prefix instructions (rotates, shifts, BIT/RES/SET)
// ============================================================================

void CPUZ80::execCB(uint8_t op) {
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;

    bool useMem = (z == 6);
    uint8_t v = getReg(z);

    if (x == 0) {
        // rotate/shift
        v = applyCBOp(y, v);
        setReg(z, v);
        cycles_ += useMem ? 11 : 4;   // 15 or 8 total
    } else if (x == 1) {
        // BIT y, r[z]
        uint8_t bit = 1 << y;
        setFlag(FLAG_Z,  !(v & bit));
        setFlag(FLAG_PV, !(v & bit));   // same as Z for BIT
        setFlag(FLAG_S,  (y == 7) && (v & bit));
        setFlag(FLAG_H,  true);
        setFlag(FLAG_N,  false);
        cycles_ += useMem ? 8 : 4;   // 12 or 8 total
    } else if (x == 2) {
        // RES y, r[z]
        v &= ~(1 << y);
        setReg(z, v);
        cycles_ += useMem ? 11 : 4;
    } else {
        // SET y, r[z]
        v |= (1 << y);
        setReg(z, v);
        cycles_ += useMem ? 11 : 4;
    }
}

// ============================================================================
// execED — extended instructions
// ============================================================================

void CPUZ80::execED(uint8_t op) {
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    if (x == 1) {
        switch (z) {
            case 0:   // IN r[y],(C)  — or IN F,(C) if y==6
            {
                uint8_t v = ioIn(rBC());
                if (y != 6) setReg(y, v);
                setFlag(FLAG_S,  v & 0x80);
                setFlag(FLAG_Z,  v == 0);
                setFlag(FLAG_H,  false);
                setFlag(FLAG_PV, parity(v));
                setFlag(FLAG_N,  false);
                setFlag(FLAG_X,  v & FLAG_X);
                setFlag(FLAG_Y,  v & FLAG_Y);
                cycles_ += 8;   // 12 total
                break;
            }
            case 1:   // OUT (C),r[y]  — or OUT (C),0 if y==6
            {
                uint8_t v = (y == 6) ? 0 : getReg(y);
                ioOut(rBC(), v);
                cycles_ += 8;   // 12 total
                break;
            }
            case 2:
                if (q == 0) {
                    wHL(sbc16hl(rHL(), getRP(p)));
                    cycles_ += 11;  // 15 total
                } else {
                    wHL(adc16hl(rHL(), getRP(p)));
                    cycles_ += 11;  // 15 total
                }
                break;
            case 3:
                if (q == 0) {      // LD (nn), rp
                    uint16_t nn = fetch16();
                    wr16(nn, getRP(p));
                    cycles_ += 16;  // 20 total
                } else {           // LD rp, (nn)
                    uint16_t nn = fetch16();
                    setRP(p, rd16(nn));
                    cycles_ += 16;  // 20 total
                }
                break;
            case 4:   // NEG
            {
                uint8_t v = A;
                A = 0;
                A = sub8(A, v);
                cycles_ += 4;   // 8 total
                break;
            }
            case 5:   // RETN (y=0) / RETI (y=1) / other NEG aliases
                IFF1 = IFF2;
                PC   = pop16();
                cycles_ += 10;  // 14 total
                break;
            case 6:   // IM n
                switch (y) {
                    case 0: case 1: IM = 0; break;
                    case 2: case 6: IM = 1; break;
                    case 3: case 7: IM = 2; break;
                    default: IM = 0; break;
                }
                cycles_ += 4;   // 8 total
                break;
            case 7:   // special load
                switch (y) {
                    case 0: I = A; cycles_ += 5; break;             // LD I,A (9)
                    case 1: R = A; cycles_ += 5; break;             // LD R,A (9)
                    case 2: {                                        // LD A,I (9)
                        A = I;
                        setFlag(FLAG_S,  A & 0x80);
                        setFlag(FLAG_Z,  A == 0);
                        setFlag(FLAG_H,  false);
                        setFlag(FLAG_PV, IFF2);
                        setFlag(FLAG_N,  false);
                        cycles_ += 5;
                        break;
                    }
                    case 3: {                                        // LD A,R (9)
                        A = R;
                        setFlag(FLAG_S,  A & 0x80);
                        setFlag(FLAG_Z,  A == 0);
                        setFlag(FLAG_H,  false);
                        setFlag(FLAG_PV, IFF2);
                        setFlag(FLAG_N,  false);
                        cycles_ += 5;
                        break;
                    }
                    case 4: {                                        // RLD (18)
                        uint8_t mem = rd(rHL());
                        uint8_t tmp = A & 0x0F;
                        A   = (A & 0xF0) | (mem >> 4);
                        wr(rHL(), (mem << 4) | tmp);
                        setFlag(FLAG_S,  A & 0x80);
                        setFlag(FLAG_Z,  A == 0);
                        setFlag(FLAG_H,  false);
                        setFlag(FLAG_PV, parity(A));
                        setFlag(FLAG_N,  false);
                        cycles_ += 14;
                        break;
                    }
                    case 5: {                                        // RRD (18)
                        uint8_t mem = rd(rHL());
                        uint8_t tmp = A & 0x0F;
                        A   = (A & 0xF0) | (mem & 0x0F);
                        wr(rHL(), (tmp << 4) | (mem >> 4));
                        setFlag(FLAG_S,  A & 0x80);
                        setFlag(FLAG_Z,  A == 0);
                        setFlag(FLAG_H,  false);
                        setFlag(FLAG_PV, parity(A));
                        setFlag(FLAG_N,  false);
                        cycles_ += 14;
                        break;
                    }
                    default: break;
                }
                break;
        }
        return;
    }

    if (x == 2) {
        // Block instructions
        switch (op) {
            case 0xA0: {  // LDI
                uint8_t v = rd(rHL());
                wr(rDE(), v);
                wHL(rHL() + 1);
                wDE(rDE() + 1);
                wBC(rBC() - 1);
                setFlag(FLAG_H,  false);
                setFlag(FLAG_N,  false);
                setFlag(FLAG_PV, rBC() != 0);
                setFlag(FLAG_X,  (A + v) & FLAG_X);
                setFlag(FLAG_Y,  (A + v) & FLAG_Y);
                cycles_ += 12;  // 16 total
                break;
            }
            case 0xA1: {  // CPI
                uint8_t v   = rd(rHL());
                uint8_t res = A - v;
                wHL(rHL() + 1);
                wBC(rBC() - 1);
                setFlag(FLAG_S,  res & 0x80);
                setFlag(FLAG_Z,  res == 0);
                setFlag(FLAG_H,  (A & 0x0F) < (v & 0x0F));
                setFlag(FLAG_PV, rBC() != 0);
                setFlag(FLAG_N,  true);
                uint8_t n = res - (getFlag(FLAG_H) ? 1 : 0);
                setFlag(FLAG_X,  n & FLAG_X);
                setFlag(FLAG_Y,  n & FLAG_Y);
                cycles_ += 12;  // 16 total
                break;
            }
            case 0xA2: {  // INI
                uint8_t v = ioIn(rBC());
                wr(rHL(), v);
                B--;
                wHL(rHL() + 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                cycles_ += 12;  // 16 total
                break;
            }
            case 0xA3: {  // OUTI
                uint8_t v = rd(rHL());
                B--;
                ioOut(rBC(), v);
                wHL(rHL() + 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                cycles_ += 12;  // 16 total
                break;
            }
            case 0xA8: {  // LDD
                uint8_t v = rd(rHL());
                wr(rDE(), v);
                wHL(rHL() - 1);
                wDE(rDE() - 1);
                wBC(rBC() - 1);
                setFlag(FLAG_H,  false);
                setFlag(FLAG_N,  false);
                setFlag(FLAG_PV, rBC() != 0);
                setFlag(FLAG_X,  (A + v) & FLAG_X);
                setFlag(FLAG_Y,  (A + v) & FLAG_Y);
                cycles_ += 12;
                break;
            }
            case 0xA9: {  // CPD
                uint8_t v   = rd(rHL());
                uint8_t res = A - v;
                wHL(rHL() - 1);
                wBC(rBC() - 1);
                setFlag(FLAG_S,  res & 0x80);
                setFlag(FLAG_Z,  res == 0);
                setFlag(FLAG_H,  (A & 0x0F) < (v & 0x0F));
                setFlag(FLAG_PV, rBC() != 0);
                setFlag(FLAG_N,  true);
                uint8_t n = res - (getFlag(FLAG_H) ? 1 : 0);
                setFlag(FLAG_X,  n & FLAG_X);
                setFlag(FLAG_Y,  n & FLAG_Y);
                cycles_ += 12;
                break;
            }
            case 0xAA: {  // IND
                uint8_t v = ioIn(rBC());
                wr(rHL(), v);
                B--;
                wHL(rHL() - 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                cycles_ += 12;
                break;
            }
            case 0xAB: {  // OUTD
                uint8_t v = rd(rHL());
                B--;
                ioOut(rBC(), v);
                wHL(rHL() - 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                cycles_ += 12;
                break;
            }
            case 0xB0: {  // LDIR
                uint8_t v = rd(rHL());
                wr(rDE(), v);
                wHL(rHL() + 1);
                wDE(rDE() + 1);
                wBC(rBC() - 1);
                setFlag(FLAG_H,  false);
                setFlag(FLAG_N,  false);
                setFlag(FLAG_PV, false);
                if (rBC() != 0) {
                    PC -= 2;   // repeat
                    cycles_ += 17;  // 21 total
                } else {
                    cycles_ += 12;  // 16 total
                }
                break;
            }
            case 0xB1: {  // CPIR
                uint8_t v   = rd(rHL());
                uint8_t res = A - v;
                wHL(rHL() + 1);
                wBC(rBC() - 1);
                setFlag(FLAG_S,  res & 0x80);
                setFlag(FLAG_Z,  res == 0);
                setFlag(FLAG_H,  (A & 0x0F) < (v & 0x0F));
                setFlag(FLAG_PV, rBC() != 0);
                setFlag(FLAG_N,  true);
                if (rBC() != 0 && res != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            case 0xB2: {  // INIR
                uint8_t v = ioIn(rBC());
                wr(rHL(), v);
                B--;
                wHL(rHL() + 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                if (B != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            case 0xB3: {  // OTIR
                uint8_t v = rd(rHL());
                B--;
                ioOut(rBC(), v);
                wHL(rHL() + 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                if (B != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            case 0xB8: {  // LDDR
                uint8_t v = rd(rHL());
                wr(rDE(), v);
                wHL(rHL() - 1);
                wDE(rDE() - 1);
                wBC(rBC() - 1);
                setFlag(FLAG_H,  false);
                setFlag(FLAG_N,  false);
                setFlag(FLAG_PV, false);
                if (rBC() != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            case 0xB9: {  // CPDR
                uint8_t v   = rd(rHL());
                uint8_t res = A - v;
                wHL(rHL() - 1);
                wBC(rBC() - 1);
                setFlag(FLAG_S,  res & 0x80);
                setFlag(FLAG_Z,  res == 0);
                setFlag(FLAG_H,  (A & 0x0F) < (v & 0x0F));
                setFlag(FLAG_PV, rBC() != 0);
                setFlag(FLAG_N,  true);
                if (rBC() != 0 && res != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            case 0xBA: {  // INDR
                uint8_t v = ioIn(rBC());
                wr(rHL(), v);
                B--;
                wHL(rHL() - 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                if (B != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            case 0xBB: {  // OTDR
                uint8_t v = rd(rHL());
                B--;
                ioOut(rBC(), v);
                wHL(rHL() - 1);
                setFlag(FLAG_Z,  B == 0);
                setFlag(FLAG_N,  true);
                if (B != 0) {
                    PC -= 2;
                    cycles_ += 17;
                } else {
                    cycles_ += 12;
                }
                break;
            }
            default: break;   // NOP for invalid ED opcodes
        }
        return;
    }

    // x==0 or x==3: NOP (invalid ED opcodes)
}

// ============================================================================
// execXY — DD or FD prefix (HL replaced by IX or IY)
// ============================================================================

void CPUZ80::execXY(uint8_t op, uint16_t& xy) {
    // For DD/FD: most instructions that reference HL or (HL) use xy and (xy+d).
    // Instructions referencing H/L directly still use H and L (documented behavior).
    // The displacement byte is fetched when (xy+d) is accessed.

    if (op == 0xCB) {
        execXYCB(xy);
        return;
    }

    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    // Helper lambdas for xy-indexed memory access
    auto readXY = [&]() -> uint8_t {
        int8_t d = int8_t(fetch());
        cycles_ += 4;   // extra for displacement + memory
        return rd(xy + d);
    };
    auto writeXY = [&](uint8_t v) {
        int8_t d = int8_t(fetch());
        cycles_ += 4;
        wr(xy + d, v);
    };
    auto readXYpre = [&](int8_t d) -> uint8_t {
        cycles_ += 4;
        return rd(xy + d);
    };
    auto writeXYpre = [&](int8_t d, uint8_t v) {
        cycles_ += 4;
        wr(xy + d, v);
    };
    (void)readXYpre;
    (void)writeXYpre;

    // x==1: LD r,r' but (HL) → (xy+d)
    if (x == 1) {
        if (y == 6 && z == 6) { halted_ = true; return; }

        if (z == 6) {
            // source is (xy+d)
            int8_t d = int8_t(fetch());
            uint8_t v = rd(xy + d);
            setReg(y, v);
            cycles_ += 15;  // 19 total
        } else if (y == 6) {
            // destination is (xy+d)
            int8_t d = int8_t(fetch());
            wr(xy + d, getReg(z));
            cycles_ += 15;  // 19 total
        } else {
            // neither references (HL) — normal LD r,r
            setReg(y, getReg(z));
            // 8 total for DD/FD prefix + 4
        }
        return;
    }

    // x==2: ALU A,r but r==6 uses (xy+d)
    if (x == 2) {
        uint8_t v;
        if (z == 6) {
            int8_t d = int8_t(fetch());
            v = rd(xy + d);
            cycles_ += 15;  // 19 total
        } else {
            v = getReg(z);
        }
        doALU(y, v);
        return;
    }

    if (x == 0) {
        switch (z) {
            case 1:   // LD xy,nn  /  ADD xy,rp
                if (q == 0) {
                    xy = fetch16();
                    cycles_ += 10;  // 14 total
                } else {
                    // ADD xy,rp — rp table uses xy instead of HL at p==2
                    uint16_t rp;
                    switch (p) {
                        case 0: rp = rBC(); break;
                        case 1: rp = rDE(); break;
                        case 2: rp = xy;    break;
                        default: rp = SP;   break;
                    }
                    xy = add16hl(xy, rp);
                    cycles_ += 11;  // 15 total
                }
                break;
            case 2:   // LD (nn),xy  /  LD xy,(nn)
                if (y == 4) {
                    uint16_t nn = fetch16();
                    wr16(nn, xy);
                    cycles_ += 16;  // 20 total
                } else if (y == 5) {
                    uint16_t nn = fetch16();
                    xy = rd16(nn);
                    cycles_ += 16;  // 20 total
                } else {
                    // other indirect loads don't involve xy — pass to normal
                    execUnprefixed(op);
                }
                break;
            case 3:   // INC xy / DEC xy
                if (q == 0 && p == 2) { ++xy; cycles_ += 6; }
                else if (q == 1 && p == 2) { --xy; cycles_ += 6; }
                else execUnprefixed(op);
                break;
            case 4:   // INC (xy+d) or INC r
                if (y == 6) {
                    int8_t d = int8_t(fetch());
                    uint16_t addr = xy + d;
                    wr(addr, inc8(rd(addr)));
                    cycles_ += 19;  // 23 total
                } else {
                    setReg(y, inc8(getReg(y)));
                    // 8 total
                }
                break;
            case 5:   // DEC (xy+d) or DEC r
                if (y == 6) {
                    int8_t d = int8_t(fetch());
                    uint16_t addr = xy + d;
                    wr(addr, dec8(rd(addr)));
                    cycles_ += 19;
                } else {
                    setReg(y, dec8(getReg(y)));
                }
                break;
            case 6:   // LD (xy+d),n or LD r,n
                if (y == 6) {
                    int8_t  d = int8_t(fetch());
                    uint8_t n = fetch();
                    wr(uint16_t(xy + d), n);
                    cycles_ += 15;  // 19 total
                } else {
                    setReg(y, fetch());
                    cycles_ += 3;   // 7 total (no xy involved)
                }
                break;
            default:
                execUnprefixed(op);
                break;
        }
        return;
    }

    if (x == 3) {
        switch (z) {
            case 1:   // POP xy / JP (xy) / LD SP,xy
                if (q == 0 && p == 2) {
                    xy = pop16();
                    cycles_ += 10;  // 14 total
                } else if (q == 1 && p == 2) {
                    PC = xy;        // JP (xy) — 8 total
                    cycles_ += 4;
                } else if (q == 1 && p == 3) {
                    SP = xy;        // LD SP,xy — 10 total
                    cycles_ += 6;
                } else {
                    execUnprefixed(op);
                }
                break;
            case 3:   // EX (SP),xy
                if (y == 4) {
                    uint16_t tmp = rd16(SP);
                    wr16(SP, xy);
                    xy = tmp;
                    cycles_ += 15;  // 23 total (prefix already added 4+4)
                } else {
                    execUnprefixed(op);
                }
                break;
            case 5:   // PUSH xy
                if (q == 0 && p == 2) {
                    push16(xy);
                    cycles_ += 11;  // 15 total
                } else {
                    execUnprefixed(op);
                }
                break;
            default:
                execUnprefixed(op);
                break;
        }
        return;
    }
}

// ============================================================================
// execXYCB — DDCB/FDCB: bit ops on (xy+d)
// ============================================================================

void CPUZ80::execXYCB(uint16_t xy) {
    int8_t  d    = int8_t(fetch());
    uint8_t op   = fetch();
    uint16_t addr = uint16_t(xy + d);
    uint8_t val  = rd(addr);

    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;

    if (x == 0) {
        val = applyCBOp(y, val);
        wr(addr, val);
        // Also stores result in r[z] for undocumented behaviour (z != 6)
        if (z != 6) setReg(z, val);
        cycles_ += 19;  // 23 total
    } else if (x == 1) {
        uint8_t bit = 1 << y;
        setFlag(FLAG_Z,  !(val & bit));
        setFlag(FLAG_PV, !(val & bit));
        setFlag(FLAG_S,  (y == 7) && (val & bit));
        setFlag(FLAG_H,  true);
        setFlag(FLAG_N,  false);
        cycles_ += 16;  // 20 total
    } else if (x == 2) {
        val &= ~(1 << y);
        wr(addr, val);
        if (z != 6) setReg(z, val);
        cycles_ += 19;
    } else {
        val |= (1 << y);
        wr(addr, val);
        if (z != 6) setReg(z, val);
        cycles_ += 19;
    }
}

// ============================================================================
// stateString
// ============================================================================

std::string CPUZ80::stateString() const {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    auto flag = [&](char name, uint8_t mask) -> char {
        return (F & mask) ? name : '.';
    };

    ss << "AF:"  << std::setw(2) << int(A) << std::setw(2) << int(F)
       << " BC:" << std::setw(2) << int(B) << std::setw(2) << int(C)
       << " DE:" << std::setw(2) << int(D) << std::setw(2) << int(E)
       << " HL:" << std::setw(2) << int(H) << std::setw(2) << int(L)
       << "  IX:" << std::setw(4) << int(IX)
       << " IY:"  << std::setw(4) << int(IY)
       << " SP:"  << std::setw(4) << int(SP)
       << " PC:"  << std::setw(4) << int(PC)
       << "  "
       << flag('S', FLAG_S) << flag('Z', FLAG_Z) << flag('H', FLAG_H)
       << flag('P', FLAG_PV)<< flag('N', FLAG_N) << flag('C', FLAG_C)
       << "  IM" << int(IM);
    return ss.str();
}
