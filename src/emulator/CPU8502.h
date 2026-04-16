#pragma once

#include <array>
#include <cstdint>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// MOS 8502 — CMOS variant of the 6502, used in the Commodore 128.
// Same programmer model as the NMOS 6502; runs at up to 2 MHz.
// ---------------------------------------------------------------------------
class CPU8502 {
public:
    // -----------------------------------------------------------------------
    // Registers
    // -----------------------------------------------------------------------
    uint16_t PC = 0x0000;   // Program Counter
    uint8_t  A  = 0x00;     // Accumulator
    uint8_t  X  = 0x00;     // X Index
    uint8_t  Y  = 0x00;     // Y Index
    uint8_t  SP = 0xFD;     // Stack Pointer  ($0100 + SP)
    uint8_t  P  = 0x24;     // Processor Status (Unused | IRQ Disable set)

    // -----------------------------------------------------------------------
    // Status flag bits  (bit positions in P)
    // -----------------------------------------------------------------------
    enum Flags : uint8_t {
        C = 1 << 0,  // Carry
        Z = 1 << 1,  // Zero
        I = 1 << 2,  // IRQ Disable
        D = 1 << 3,  // Decimal  (present but not used on NMOS 6502 in NES;
                     //           functional on 65C02/8502)
        B = 1 << 4,  // Break    (set on BRK/PHP, clear on IRQ/NMI stack push)
        U = 1 << 5,  // Unused   (always reads 1 from CPU)
        V = 1 << 6,  // Overflow
        N = 1 << 7,  // Negative
    };

    // -----------------------------------------------------------------------
    // Public interface
    // -----------------------------------------------------------------------
    CPU8502();

    void connectBus(Bus* bus);

    void reset();           // Assert RESET
    void clock();           // Single clock tick
    void irq();             // Assert IRQ line
    void nmi();             // Assert NMI line

    bool        complete()    const;  // True when current instruction finished
    std::string stateString() const;  // One-line register dump for the terminal

private:
    // -----------------------------------------------------------------------
    // Instruction table entry
    // -----------------------------------------------------------------------
    struct Instruction {
        const char* name;
        uint8_t (CPU8502::*operate )();
        uint8_t (CPU8502::*addrmode)();
        uint8_t cycles;
    };

    std::array<Instruction, 256> lookup_;

    // -----------------------------------------------------------------------
    // Internal state
    // -----------------------------------------------------------------------
    Bus*     bus_     = nullptr;

    uint8_t  fetched_ = 0x00;     // Operand value read by current addressing mode
    uint16_t addrAbs_ = 0x0000;   // Effective address
    uint16_t addrRel_ = 0x0000;   // Branch offset (sign-extended to 16 bits)
    uint8_t  opcode_  = 0x00;     // Currently executing opcode
    uint8_t  cycles_  = 0;        // Remaining cycles for current instruction

    // -----------------------------------------------------------------------
    // Bus / stack helpers
    // -----------------------------------------------------------------------
    uint8_t  busRead (uint16_t addr);
    void     busWrite(uint16_t addr, uint8_t val);

    uint8_t  fetch();              // Read operand using current addressing mode
    void     stackPush(uint8_t v);
    uint8_t  stackPop();

    // -----------------------------------------------------------------------
    // Flag helpers
    // -----------------------------------------------------------------------
    void setFlag(Flags f, bool v);
    bool getFlag(Flags f) const;

    // -----------------------------------------------------------------------
    // Addressing modes  (return 1 if page cross possible, else 0)
    // -----------------------------------------------------------------------
    uint8_t IMP();  // Implied
    uint8_t ACC();  // Accumulator
    uint8_t IMM();  // Immediate          #$xx
    uint8_t ZP0();  // Zero Page          $xx
    uint8_t ZPX();  // Zero Page, X       $xx, X
    uint8_t ZPY();  // Zero Page, Y       $xx, Y
    uint8_t ABS();  // Absolute           $xxxx
    uint8_t ABX();  // Absolute, X        $xxxx, X
    uint8_t ABY();  // Absolute, Y        $xxxx, Y
    uint8_t IND();  // Indirect           ($xxxx)   — JMP only; reproduces 6502 page bug
    uint8_t IZX();  // Indexed Indirect   ($xx, X)
    uint8_t IZY();  // Indirect Indexed   ($xx), Y
    uint8_t REL();  // Relative           branch offset

    // -----------------------------------------------------------------------
    // Instructions  (return 1 if extra cycle possible on page cross, else 0)
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
    uint8_t XXX(); // Catch-all for illegal / undefined opcodes
};
