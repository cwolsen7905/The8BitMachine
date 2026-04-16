#pragma once

#include "IBusDevice.h"
#include <cstdint>
#include <functional>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// MOS 6526 Complex Interface Adapter  (CIA)
//
// Register map ($00–$0F, 4 low address bits select the register):
//   $00  PRA   — Port A data          $08  TOD_10THS — tenths  (stub)
//   $01  PRB   — Port B data          $09  TOD_SEC   — seconds (stub)
//   $02  DDRA  — Port A direction     $0A  TOD_MIN   — minutes (stub)
//   $03  DDRB  — Port B direction     $0B  TOD_HR    — hours   (stub)
//   $04  TALO  — Timer A low          $0C  SDR       — serial  (stub)
//   $05  TAHI  — Timer A high         $0D  ICR       — Interrupt Control
//   $06  TBLO  — Timer B low  (stub)  $0E  CRA       — Control A (Timer A)
//   $07  TBHI  — Timer B high (stub)  $0F  CRB       — Control B (stub)
//
// ICR: bit 0 = TA underflow, bit 1 = TB (stub), bit 7 = IR (any enabled)
// CRA: bit 0 = START, bit 3 = ONESHOT, bit 4 = LOAD (self-clearing)
// ---------------------------------------------------------------------------

class CIA6526 : public IBusDevice {
public:
    CIA6526() { reset(); }

    // IBusDevice interface
    const char* deviceName() const override { return "MOS 6526 CIA"; }
    void        reset()            override;
    void        clock()            override;
    uint8_t     read (uint16_t offset) const override;
    void        write(uint16_t offset, uint8_t value) override;
    std::string statusLine() const override {
        std::ostringstream s;
        s << "TA=$" << std::uppercase << std::hex << std::setfill('0')
          << std::setw(4) << (unsigned)timerACounter_
          << " CRA=$" << std::setw(2) << (unsigned)cra_
          << ((cra_ & CRA_START) ? " RUN" : " STP");
        return s.str();
    }

    // Callback fired when an unmasked interrupt fires.
    std::function<void()> onIRQ;

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
