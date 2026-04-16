#pragma once

#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// MOS 6526 Complex Interface Adapter  (CIA)
//
// Used in the Commodore 128 as CIA1 ($F100) and CIA2 ($F200).
//
// Implemented registers (this pass):
//   $00  PRA   — Port A data
//   $01  PRB   — Port B data
//   $02  DDRA  — Port A direction (1=output)
//   $03  DDRB  — Port B direction
//   $04  TALO  — Timer A latch/counter low
//   $05  TAHI  — Timer A latch/counter high
//   $06  TBLO  — Timer B latch/counter low  (stub — writable, no countdown)
//   $07  TBHI  — Timer B latch/counter high (stub)
//   $08  TOD_10THS — Time-of-Day tenths (stub)
//   $09  TOD_SEC   — seconds (stub)
//   $0A  TOD_MIN   — minutes (stub)
//   $0B  TOD_HR    — hours   (stub)
//   $0C  SDR   — Serial Data Register (stub)
//   $0D  ICR   — Interrupt Control Register
//   $0E  CRA   — Control Register A  (Timer A)
//   $0F  CRB   — Control Register B  (Timer B, stub)
//
// ICR bit masks:
//   Bit 0  TA   — Timer A underflow
//   Bit 1  TB   — Timer B underflow  (stub)
//   Bit 7  IR   — Any enabled interrupt fired  (read-only in flag byte)
//
// CRA bit masks:
//   Bit 0  START    — 1 = timer running
//   Bit 3  ONSHOT   — 1 = stop after one underflow, 0 = continuous
//   Bit 4  LOAD     — 1 = force load latch → counter (self-clearing)
//   Bit 6  INMODE   — 0 = count ϕ2 cycles, 1 = count CNT pulses (stub=0)
// ---------------------------------------------------------------------------

class CIA6526 {
public:
    // Callback fired when the IRQ line is asserted.
    // The connected CPU should call cpu_.irq() in response.
    std::function<void()> onIRQ;

    CIA6526() { reset(); }

    void reset();

    // Called once per emulated clock cycle.
    void clock();

    uint8_t read (uint8_t reg) const;
    void    write(uint8_t reg, uint8_t value);

    // -----------------------------------------------------------------------
    // Register offsets
    // -----------------------------------------------------------------------
    static constexpr uint8_t REG_PRA      = 0x00;
    static constexpr uint8_t REG_PRB      = 0x01;
    static constexpr uint8_t REG_DDRA     = 0x02;
    static constexpr uint8_t REG_DDRB     = 0x03;
    static constexpr uint8_t REG_TALO     = 0x04;
    static constexpr uint8_t REG_TAHI     = 0x05;
    static constexpr uint8_t REG_TBLO     = 0x06;
    static constexpr uint8_t REG_TBHI     = 0x07;
    static constexpr uint8_t REG_TOD_10   = 0x08;
    static constexpr uint8_t REG_TOD_SEC  = 0x09;
    static constexpr uint8_t REG_TOD_MIN  = 0x0A;
    static constexpr uint8_t REG_TOD_HR   = 0x0B;
    static constexpr uint8_t REG_SDR      = 0x0C;
    static constexpr uint8_t REG_ICR      = 0x0D;
    static constexpr uint8_t REG_CRA      = 0x0E;
    static constexpr uint8_t REG_CRB      = 0x0F;

    // ICR bits
    static constexpr uint8_t ICR_TA  = 1 << 0;
    static constexpr uint8_t ICR_TB  = 1 << 1;
    static constexpr uint8_t ICR_IR  = 1 << 7;

    // CRA bits
    static constexpr uint8_t CRA_START   = 1 << 0;
    static constexpr uint8_t CRA_ONESHOT = 1 << 3;
    static constexpr uint8_t CRA_LOAD    = 1 << 4;

private:
    // Port registers
    uint8_t pra_  = 0xFF;  // Port A data
    uint8_t prb_  = 0xFF;  // Port B data
    uint8_t ddra_ = 0x00;  // Port A direction (all input)
    uint8_t ddrb_ = 0x00;  // Port B direction (all input)

    // Timer A
    uint16_t timerALatch_   = 0xFFFF;  // reload value
    uint16_t timerACounter_ = 0xFFFF;  // running counter
    uint8_t  cra_           = 0x00;    // Control Register A

    // Timer B (stub — stores values, no countdown)
    uint16_t timerBLatch_   = 0xFFFF;
    uint8_t  crb_           = 0x00;

    // TOD stubs
    uint8_t tod10_  = 0x00;
    uint8_t todSec_ = 0x00;
    uint8_t todMin_ = 0x00;
    uint8_t todHr_  = 0x00;

    // Serial data register stub
    uint8_t sdr_ = 0x00;

    // Interrupt state
    uint8_t icrMask_  = 0x00;  // which sources are enabled (write side)
    uint8_t icrFlags_ = 0x00;  // which sources have fired  (read side)

    void timerAUnderflow();
};
