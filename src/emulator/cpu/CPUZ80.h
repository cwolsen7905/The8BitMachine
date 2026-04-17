#pragma once

#include "emulator/core/ICPU.h"
#include <cstdint>
#include <functional>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// CPUZ80 — Zilog Z80 CPU.
//
// Implements the full documented Z80 instruction set including all four
// prefix groups (CB, ED, DD, FD) and their DDCB/FDCB combinations.
//
// Port I/O is routed through optional callbacks set via setPortHandlers().
// By default all IN instructions return 0xFF (open bus).
//
// The clock() model matches CPU6502Base: each call advances one T-state.
// When cycles_ reaches 0 the next instruction is fetched and executed, and
// cycles_ is set to that instruction's T-state count.  complete() returns
// true when cycles_ == 0 (between instructions).
// ---------------------------------------------------------------------------
class CPUZ80 : public ICPU {
public:
    CPUZ80();

    // -----------------------------------------------------------------------
    // ICPU interface
    // -----------------------------------------------------------------------
    const char* cpuName()    const override { return "Zilog Z80"; }
    void connectBus(Bus* bus)      override;
    void reset()                   override;
    void clock()                   override;
    void irq()                     override;
    void nmi()                     override;
    bool        complete()    const override { return cycles_ == 0; }
    uint16_t    getPC()       const override { return PC; }
    std::string stateString() const override;

    // Expose main pair registers through the ICPU accessors used by the UI.
    // B/C fill the X/Y slots; regSP returns the low byte of SP.
    uint8_t regA()  const override { return A;           }
    uint8_t regX()  const override { return B;           }
    uint8_t regY()  const override { return C;           }
    uint8_t regSP() const override { return uint8_t(SP); }
    uint8_t regP()  const override { return F;           }

    // -----------------------------------------------------------------------
    // Port I/O callbacks — wired by Machine/Application for real machines.
    // The full 16-bit port address is passed (Z80 puts BC on the address bus
    // for IN r,(C)/OUT (C),r; A<<8|n for IN A,(n)/OUT (n),A).
    // -----------------------------------------------------------------------
    void setPortHandlers(std::function<uint8_t(uint16_t)>       rd,
                         std::function<void(uint16_t, uint8_t)> wr);

    // -----------------------------------------------------------------------
    // Registers — public so UI panels and tests can read them directly
    // -----------------------------------------------------------------------
    uint16_t PC = 0x0000;
    uint16_t SP = 0xFFFF;

    uint8_t  A  = 0xFF,  F  = 0xFF;   // main AF
    uint8_t  B  = 0xFF,  C  = 0xFF;   // main BC
    uint8_t  D  = 0xFF,  E  = 0xFF;   // main DE
    uint8_t  H  = 0xFF,  L  = 0xFF;   // main HL

    uint8_t  A_ = 0xFF,  F_ = 0xFF;   // alternate AF'
    uint8_t  B_ = 0xFF,  C_ = 0xFF;   // alternate BC'
    uint8_t  D_ = 0xFF,  E_ = 0xFF;   // alternate DE'
    uint8_t  H_ = 0xFF,  L_ = 0xFF;   // alternate HL'

    uint16_t IX = 0xFFFF;
    uint16_t IY = 0xFFFF;

    uint8_t  I  = 0x00;   // interrupt vector register
    uint8_t  R  = 0x00;   // memory refresh register (cosmetic only)
    uint8_t  IM = 0;      // interrupt mode (0, 1, or 2)
    bool     IFF1 = false;
    bool     IFF2 = false;

    // Convenience 16-bit pair accessors
    uint16_t regBC()   const { return (uint16_t(B) << 8) | C; }
    uint16_t regDE()   const { return (uint16_t(D) << 8) | E; }
    uint16_t regHL()   const { return (uint16_t(H) << 8) | L; }
    uint16_t regSP16() const { return SP; }
    uint16_t regIX()   const { return IX; }
    uint16_t regIY()   const { return IY; }

private:
    Bus*  bus_        = nullptr;
    int   cycles_     = 0;
    bool  halted_     = false;
    bool  irqLine_    = false;   // level-sensitive IRQ signal
    bool  nmiPending_ = false;   // edge-triggered NMI latch
    bool  eiDelay_    = false;   // suppress IRQ for one instruction after EI

    std::function<uint8_t(uint16_t)>       portRead_;
    std::function<void(uint16_t, uint8_t)> portWrite_;

    // -----------------------------------------------------------------------
    // Flag bit masks
    // -----------------------------------------------------------------------
    static constexpr uint8_t FLAG_C  = 0x01;  // carry
    static constexpr uint8_t FLAG_N  = 0x02;  // subtract
    static constexpr uint8_t FLAG_PV = 0x04;  // parity / overflow
    static constexpr uint8_t FLAG_X  = 0x08;  // undocumented (bit 3)
    static constexpr uint8_t FLAG_H  = 0x10;  // half carry
    static constexpr uint8_t FLAG_Y  = 0x20;  // undocumented (bit 5)
    static constexpr uint8_t FLAG_Z  = 0x40;  // zero
    static constexpr uint8_t FLAG_S  = 0x80;  // sign

    void setFlag(uint8_t mask, bool v) { if (v) F |= mask; else F &= ~mask; }
    bool getFlag(uint8_t mask) const   { return (F & mask) != 0; }

    // -----------------------------------------------------------------------
    // Register pair read/write helpers
    // -----------------------------------------------------------------------
    uint16_t rAF() const { return (uint16_t(A) << 8) | F; }
    uint16_t rBC() const { return (uint16_t(B) << 8) | C; }
    uint16_t rDE() const { return (uint16_t(D) << 8) | E; }
    uint16_t rHL() const { return (uint16_t(H) << 8) | L; }
    void wAF(uint16_t v) { A = uint8_t(v >> 8); F = uint8_t(v); }
    void wBC(uint16_t v) { B = uint8_t(v >> 8); C = uint8_t(v); }
    void wDE(uint16_t v) { D = uint8_t(v >> 8); E = uint8_t(v); }
    void wHL(uint16_t v) { H = uint8_t(v >> 8); L = uint8_t(v); }

    // -----------------------------------------------------------------------
    // Memory and port access
    // -----------------------------------------------------------------------
    uint8_t  rd(uint16_t a);
    void     wr(uint16_t a, uint8_t v);
    uint16_t rd16(uint16_t a);
    void     wr16(uint16_t a, uint16_t v);
    uint8_t  fetch();
    uint16_t fetch16();
    void     push16(uint16_t v);
    uint16_t pop16();
    uint8_t  ioIn(uint16_t port);
    void     ioOut(uint16_t port, uint8_t v);

    // -----------------------------------------------------------------------
    // Arithmetic helpers — all update F
    // -----------------------------------------------------------------------
    static bool parity(uint8_t v);

    uint8_t  add8 (uint8_t a, uint8_t b, bool cy = false);
    uint8_t  sub8 (uint8_t a, uint8_t b, bool cy = false);
    uint8_t  inc8 (uint8_t v);
    uint8_t  dec8 (uint8_t v);
    void     and8 (uint8_t v);
    void     or8  (uint8_t v);
    void     xor8 (uint8_t v);
    void     cp8  (uint8_t v);
    void     daa  ();

    uint16_t add16hl(uint16_t hl, uint16_t rp);
    uint16_t adc16hl(uint16_t hl, uint16_t rp);
    uint16_t sbc16hl(uint16_t hl, uint16_t rp);

    // CB prefix rotate/shift — update flags
    uint8_t rlc8(uint8_t v);
    uint8_t rrc8(uint8_t v);
    uint8_t rl8 (uint8_t v);
    uint8_t rr8 (uint8_t v);
    uint8_t sla8(uint8_t v);
    uint8_t sra8(uint8_t v);
    uint8_t sll8(uint8_t v);   // undocumented SLL/SL1
    uint8_t srl8(uint8_t v);

    // -----------------------------------------------------------------------
    // Register index table helpers
    // r: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
    // rp: 0=BC 1=DE 2=HL 3=SP  (rp2: 3=AF instead of SP)
    // -----------------------------------------------------------------------
    uint8_t  getReg (uint8_t r);
    void     setReg (uint8_t r, uint8_t v);
    uint16_t getRP  (uint8_t p);
    void     setRP  (uint8_t p, uint16_t v);
    uint16_t getRP2 (uint8_t p);
    void     setRP2 (uint8_t p, uint16_t v);

    // ALU op dispatch (op 0=ADD 1=ADC 2=SUB 3=SBC 4=AND 5=XOR 6=OR 7=CP)
    void doALU(uint8_t op, uint8_t v);

    // Condition test (cc 0=NZ 1=Z 2=NC 3=C 4=PO 5=PE 6=P 7=M)
    bool testCC(uint8_t cc) const;

    // -----------------------------------------------------------------------
    // Instruction dispatch
    // -----------------------------------------------------------------------
    void execUnprefixed(uint8_t op);
    void execCB        (uint8_t op);
    void execED        (uint8_t op);
    void execXY        (uint8_t op, uint16_t& xy);   // DD or FD context
    void execXYCB      (uint16_t xy);                // DDCB or FDCB context

    // Apply a CB rotate/shift op (0-7) to an 8-bit value; returns result
    uint8_t applyCBOp(uint8_t rot, uint8_t val);
};
