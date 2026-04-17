#pragma once

#include "emulator/core/ICPU.h"
#include <array>
#include <cstdint>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// CPU6502Base — shared implementation for all MOS 6502-family CPUs.
//
// Both CPU8502 (NMOS) and CPU65C02 (CMOS) extend this class and configure
// the 256-entry dispatch table in their constructors via buildNMOSTable()
// followed by any CMOS-specific patches.
//
// All internals are protected so subclasses can reference member-function
// pointers in the dispatch table without casts.
// ---------------------------------------------------------------------------
class CPU6502Base : public ICPU {
public:
    // -----------------------------------------------------------------------
    // Registers  (public — read by UI panels and tests)
    // -----------------------------------------------------------------------
    uint16_t PC = 0x0000;
    uint8_t  A  = 0x00;
    uint8_t  X  = 0x00;
    uint8_t  Y  = 0x00;
    uint8_t  SP = 0xFD;
    uint8_t  P  = 0x24;

    // -----------------------------------------------------------------------
    // Status flag bit positions in P
    // -----------------------------------------------------------------------
    enum Flags : uint8_t {
        C = 1 << 0,
        Z = 1 << 1,
        I = 1 << 2,
        D = 1 << 3,
        B = 1 << 4,
        U = 1 << 5,
        V = 1 << 6,
        N = 1 << 7,
    };

    // -----------------------------------------------------------------------
    // ICPU interface
    // -----------------------------------------------------------------------
    void connectBus(Bus* bus)      override;
    void reset()                   override;
    void clock()                   override;
    void irq()                     override;
    void nmi()                     override;
    bool        complete()    const override;
    uint16_t    getPC()       const override { return PC; }
    std::string stateString() const override;

    // Register getters (used by Application through ICPU*)
    uint8_t regA()  const override { return A;  }
    uint8_t regX()  const override { return X;  }
    uint8_t regY()  const override { return Y;  }
    uint8_t regSP() const override { return SP; }
    uint8_t regP()  const override { return P;  }

protected:
    // -----------------------------------------------------------------------
    // Dispatch table entry
    // -----------------------------------------------------------------------
    struct Instruction {
        const char* name;
        uint8_t (CPU6502Base::*operate )();
        uint8_t (CPU6502Base::*addrmode)();
        uint8_t cycles;
    };

    std::array<Instruction, 256> lookup_{};

    // -----------------------------------------------------------------------
    // Internal state
    // -----------------------------------------------------------------------
    Bus*     bus_      = nullptr;
    uint8_t  fetched_  = 0x00;
    uint16_t addrAbs_  = 0x0000;
    uint16_t addrRel_  = 0x0000;
    uint8_t  opcode_   = 0x00;
    uint8_t  cycles_   = 0;
    bool     nmosBug_  = true;   // true = reproduce NMOS JMP indirect page-wrap bug

    // -----------------------------------------------------------------------
    // Table builder — fills lookup_ with the standard NMOS 6502 table.
    // Subclasses call this in their constructor, then patch CMOS entries.
    // -----------------------------------------------------------------------
    void buildNMOSTable();

    // -----------------------------------------------------------------------
    // Bus / stack helpers
    // -----------------------------------------------------------------------
    virtual uint8_t  busRead (uint16_t addr);
    virtual void     busWrite(uint16_t addr, uint8_t val);
    uint8_t  fetch();
    void     stackPush(uint8_t v);
    uint8_t  stackPop();

    // -----------------------------------------------------------------------
    // Flag helpers
    // -----------------------------------------------------------------------
    void setFlag(Flags f, bool v);
    bool getFlag(Flags f) const;

    // -----------------------------------------------------------------------
    // Addressing modes
    // -----------------------------------------------------------------------
    uint8_t IMP();  uint8_t ACC();  uint8_t IMM();
    uint8_t ZP0();  uint8_t ZPX();  uint8_t ZPY();
    uint8_t ABS();  uint8_t ABX();  uint8_t ABY();
    uint8_t IND();  uint8_t IZX();  uint8_t IZY();
    uint8_t REL();
    // 65C02 additional modes
    uint8_t IZP();   // ($zp)     zero-page indirect
    uint8_t AIIX();  // ($abs,X)  absolute indexed indirect (JMP only)

    // -----------------------------------------------------------------------
    // NMOS instructions (shared by 8502 and 65C02)
    // -----------------------------------------------------------------------
    uint8_t ADC(); uint8_t AND(); uint8_t ASL();
    uint8_t BCC(); uint8_t BCS(); uint8_t BEQ();
    uint8_t BIT(); uint8_t BMI(); uint8_t BNE();
    uint8_t BPL(); uint8_t BRK(); uint8_t BVC();
    uint8_t BVS(); uint8_t CLC(); uint8_t CLD();
    uint8_t CLI(); uint8_t CLV(); uint8_t CMP();
    uint8_t CPX(); uint8_t CPY(); uint8_t DEC();
    uint8_t DEX(); uint8_t DEY(); uint8_t EOR();
    uint8_t INC(); uint8_t INX(); uint8_t INY();
    uint8_t JMP(); uint8_t JSR(); uint8_t LDA();
    uint8_t LDX(); uint8_t LDY(); uint8_t LSR();
    uint8_t NOP(); uint8_t ORA(); uint8_t PHA();
    uint8_t PHP(); uint8_t PLA(); uint8_t PLP();
    uint8_t ROL(); uint8_t ROR(); uint8_t RTI();
    uint8_t RTS(); uint8_t SBC(); uint8_t SEC();
    uint8_t SED(); uint8_t SEI(); uint8_t STA();
    uint8_t STX(); uint8_t STY(); uint8_t TAX();
    uint8_t TAY(); uint8_t TSX(); uint8_t TXA();
    uint8_t TXS(); uint8_t TYA();
    uint8_t XXX();

    // -----------------------------------------------------------------------
    // 65C02 additional instructions
    // -----------------------------------------------------------------------
    uint8_t BRA();      // branch always
    uint8_t STZ();      // store zero
    uint8_t TRB();      // test and reset bits
    uint8_t TSB();      // test and set bits
    uint8_t INA();      // increment A
    uint8_t DEA();      // decrement A
    uint8_t PHX();      uint8_t PHY();   // push X / Y
    uint8_t PLX();      uint8_t PLY();   // pull X / Y
    uint8_t BIT_IMM();  // BIT immediate (sets Z only, not N/V)
};
