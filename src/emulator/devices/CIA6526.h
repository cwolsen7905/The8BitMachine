#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// MOS 6526 Complex Interface Adapter  (CIA)
//
// Register map ($00–$0F, 4 low address bits select the register):
//   $00  PRA   — Port A data          $08  TOD_10THS — tenths  (BCD 0–9)
//   $01  PRB   — Port B data          $09  TOD_SEC   — seconds (BCD 0–59)
//   $02  DDRA  — Port A direction     $0A  TOD_MIN   — minutes (BCD 0–59)
//   $03  DDRB  — Port B direction     $0B  TOD_HR    — hours   (BCD 1–12, bit7=PM)
//   $04  TALO  — Timer A low          $0C  SDR       — serial  (stub)
//   $05  TAHI  — Timer A high         $0D  ICR       — Interrupt Control
//   $06  TBLO  — Timer B low          $0E  CRA       — Control A
//   $07  TBHI  — Timer B high         $0F  CRB       — Control B
//
// ICR bits: 0=TA underflow, 1=TB underflow, 2=TOD alarm, 7=IR (any enabled)
// CRA: bit 0=START, bit 3=ONESHOT, bit 4=LOAD
// CRB: bit 0=START, bit 3=ONESHOT, bit 4=LOAD, bit 6=INMODE (0=ϕ2, 1=TA),
//       bit 7=ALARM (0=write TOD time, 1=write TOD alarm)
// TOD: reading $0B latches the time until $08 is read; alarm fires ICR bit 2
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
    std::string statusLine() const override;
    bool        hasPanel()  const override { return true; }
    void        drawPanel(const char* title, bool* open) override;

    // Callback fired when an unmasked interrupt fires.
    std::function<void()> onIRQ;

    // Non-destructive register peeks (for UI display — do NOT use read() for ICR).
    uint16_t timerACounter() const { return timerACounter_; }
    uint16_t timerBCounter() const { return timerBCounter_; }
    uint8_t  crA()           const { return cra_; }
    uint8_t  crB()           const { return crb_; }
    uint8_t  icrFlags()      const { return icrFlags_; }
    uint8_t  icrMask()       const { return icrMask_; }
    uint8_t  todTenths()     const { return tod10_ & 0x0F; }
    uint8_t  todSec()        const { return todSec_; }
    uint8_t  todMin()        const { return todMin_; }
    uint8_t  todHr()         const { return todHr_; }

    // -----------------------------------------------------------------------
    // Keyboard matrix — 8 columns × 8 rows, active-low.
    // col = PA bit index (0–7), row = PB bit index (0–7).
    // PRB reads the AND of all selected columns' row bytes.
    // -----------------------------------------------------------------------
    void setKey(int col, int row, bool pressed);

    // Port/DDR snapshots for debug panels (non-destructive reads)
    uint8_t portA() const { return pra_; }
    uint8_t portB() const { return prb_; }
    uint8_t ddrA()  const { return ddra_; }
    uint8_t ddrB()  const { return ddrb_; }

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
    static constexpr uint8_t ICR_TOD = 1 << 2;
    static constexpr uint8_t ICR_IR  = 1 << 7;

    // CRA bits
    static constexpr uint8_t CRA_START   = 1 << 0;
    static constexpr uint8_t CRA_ONESHOT = 1 << 3;
    static constexpr uint8_t CRA_LOAD    = 1 << 4;

    // CRB bits
    static constexpr uint8_t CRB_START   = 1 << 0;
    static constexpr uint8_t CRB_ONESHOT = 1 << 3;
    static constexpr uint8_t CRB_LOAD    = 1 << 4;
    static constexpr uint8_t CRB_INMODE  = 1 << 6;  // 0=ϕ2, 1=count TA underflows
    static constexpr uint8_t CRB_ALARM   = 1 << 7;  // 0=write TOD time, 1=write alarm

private:
    // Port registers
    uint8_t pra_  = 0xFF;
    uint8_t prb_  = 0xFF;
    uint8_t ddra_ = 0x00;
    uint8_t ddrb_ = 0x00;

    // Timer A
    uint16_t timerALatch_   = 0xFFFF;
    uint16_t timerACounter_ = 0xFFFF;
    uint8_t  cra_           = 0x00;

    // Timer B
    uint16_t timerBLatch_   = 0xFFFF;
    uint16_t timerBCounter_ = 0xFFFF;
    uint8_t  crb_           = 0x00;

    // TOD — BCD clock
    uint8_t tod10_  = 0x00;   // tenths of seconds (0–9)
    uint8_t todSec_ = 0x00;   // seconds (BCD 0x00–0x59)
    uint8_t todMin_ = 0x00;   // minutes (BCD 0x00–0x59)
    uint8_t todHr_  = 0x01;   // hours   (BCD 0x01–0x12, bit 7 = PM)

    // TOD alarm (written when CRB bit 7 = 1)
    uint8_t todAlarm10_  = 0x00;
    uint8_t todAlarmSec_ = 0x00;
    uint8_t todAlarmMin_ = 0x00;
    uint8_t todAlarmHr_  = 0x00;

    // TOD latch (frozen on HR read until 10THS read)
    uint8_t  todLatch10_  = 0x00;
    uint8_t  todLatchSec_ = 0x00;
    uint8_t  todLatchMin_ = 0x00;
    uint8_t  todLatchHr_  = 0x01;
    bool     todLatched_  = false;

    // TOD advancement: accumulates clock() calls; advances tenths every ~100 000 cycles
    uint32_t todCycleAcc_    = 0;
    uint32_t todCyclePeriod_ = 100000;  // ~1 MHz / 10 Hz

    // Serial data register stub
    uint8_t sdr_ = 0x00;

    // Interrupt state
    uint8_t icrMask_  = 0x00;
    uint8_t icrFlags_ = 0x00;

    // Keyboard matrix — column bytes, active-low (0xFF = no keys pressed)
    uint8_t keyMatrix_[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    void timerAUnderflow();
    void timerBUnderflow();
    void tickTOD();
    void checkTODAlarm();

    // BCD helpers
    static uint8_t bcdInc(uint8_t bcd, uint8_t max);  // max in BCD
    static int     bcdToInt(uint8_t bcd) {
        return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
    }
};
