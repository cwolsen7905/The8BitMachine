#include "emulator/devices/CIA6526.h"
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// Keyboard matrix
// ---------------------------------------------------------------------------

void CIA6526::setKey(int col, int row, bool pressed) {
    if (col < 0 || col > 7 || row < 0 || row > 7) return;
    if (pressed) keyMatrix_[col] &= ~static_cast<uint8_t>(1 << row);
    else         keyMatrix_[col] |=  static_cast<uint8_t>(1 << row);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void CIA6526::reset() {
    pra_  = 0xFF;  prb_  = 0xFF;
    ddra_ = 0x00;  ddrb_ = 0x00;
    for (auto& col : keyMatrix_) col = 0xFF;

    timerALatch_   = 0xFFFF;
    timerACounter_ = 0xFFFF;
    cra_           = 0x00;

    timerBLatch_   = 0xFFFF;
    timerBCounter_ = 0xFFFF;
    crb_           = 0x00;

    tod10_  = 0x00;  todSec_ = 0x00;
    todMin_ = 0x00;  todHr_  = 0x01;

    todAlarm10_  = 0x00;  todAlarmSec_ = 0x00;
    todAlarmMin_ = 0x00;  todAlarmHr_  = 0x00;

    todLatch10_  = 0x00;  todLatchSec_ = 0x00;
    todLatchMin_ = 0x00;  todLatchHr_  = 0x01;
    todLatched_  = false;

    todCycleAcc_ = 0;

    sdr_      = 0x00;
    icrMask_  = 0x00;
    icrFlags_ = 0x00;
}

// ---------------------------------------------------------------------------
// Clock — called every ϕ2 cycle
// ---------------------------------------------------------------------------

void CIA6526::clock() {
    // --- Timer A ---
    if (cra_ & CRA_START) {
        if (timerACounter_ == 0)
            timerAUnderflow();
        else
            --timerACounter_;
    }

    // --- Timer B ---
    if (crb_ & CRB_START) {
        // INMODE bit: 0 = count ϕ2 cycles, 1 = count Timer A underflows
        // Timer A underflow increments happen inside timerAUnderflow().
        if (!(crb_ & CRB_INMODE)) {
            if (timerBCounter_ == 0)
                timerBUnderflow();
            else
                --timerBCounter_;
        }
    }

    // --- TOD advancement ---
    if (++todCycleAcc_ >= todCyclePeriod_) {
        todCycleAcc_ = 0;
        tickTOD();
    }
}

void CIA6526::timerAUnderflow() {
    timerACounter_ = timerALatch_;
    if (cra_ & CRA_ONESHOT) cra_ &= ~CRA_START;

    icrFlags_ |= ICR_TA;
    if (icrMask_ & ICR_TA) {
        icrFlags_ |= ICR_IR;
        if (onIRQ) onIRQ();
    }

    // Drive Timer B if in INMODE=1 (count TA underflows)
    if ((crb_ & CRB_START) && (crb_ & CRB_INMODE)) {
        if (timerBCounter_ == 0)
            timerBUnderflow();
        else
            --timerBCounter_;
    }
}

void CIA6526::timerBUnderflow() {
    timerBCounter_ = timerBLatch_;
    if (crb_ & CRB_ONESHOT) crb_ &= ~CRB_START;

    icrFlags_ |= ICR_TB;
    if (icrMask_ & ICR_TB) {
        icrFlags_ |= ICR_IR;
        if (onIRQ) onIRQ();
    }
}

// ---------------------------------------------------------------------------
// TOD helpers
// ---------------------------------------------------------------------------

// BCD increment with wraparound.  max is the BCD ceiling (e.g. 0x09, 0x59).
uint8_t CIA6526::bcdInc(uint8_t bcd, uint8_t max) {
    uint8_t lo = (bcd & 0x0F) + 1;
    uint8_t hi = (bcd >> 4) & 0x0F;
    if (lo > 9) { lo = 0; ++hi; }
    uint8_t result = static_cast<uint8_t>((hi << 4) | lo);
    return (result > max) ? 0x00 : result;
}

void CIA6526::tickTOD() {
    // Advance tenths
    uint8_t newTenths = (tod10_ & 0x0F) + 1;
    if (newTenths <= 9) {
        tod10_ = newTenths;
    } else {
        tod10_ = 0;
        // Advance seconds
        uint8_t newSec = bcdInc(todSec_, 0x59);
        if (newSec == 0x00 && todSec_ == 0x59) {
            todSec_ = 0x00;
            // Advance minutes
            uint8_t newMin = bcdInc(todMin_, 0x59);
            if (newMin == 0x00 && todMin_ == 0x59) {
                todMin_ = 0x00;
                // Advance hours (1–12 with AM/PM toggle)
                uint8_t pm   = todHr_ & 0x80;
                uint8_t hrs  = todHr_ & 0x7F;
                uint8_t next = bcdInc(hrs, 0x12);
                if (next == 0x00) next = 0x01;
                if (hrs == 0x11) pm ^= 0x80;     // toggle AM/PM at 11→12
                todHr_ = static_cast<uint8_t>(pm | next);
            } else {
                todMin_ = newMin;
            }
        } else {
            todSec_ = newSec;
        }
    }

    checkTODAlarm();
}

void CIA6526::checkTODAlarm() {
    if ((tod10_  & 0x0F) == (todAlarm10_  & 0x0F) &&
        todSec_  == todAlarmSec_  &&
        todMin_  == todAlarmMin_  &&
        (todHr_ & 0x9F) == (todAlarmHr_ & 0x9F)) {

        icrFlags_ |= ICR_TOD;
        if (icrMask_ & ICR_TOD) {
            icrFlags_ |= ICR_IR;
            if (onIRQ) onIRQ();
        }
    }
}

// ---------------------------------------------------------------------------
// Register read
// ---------------------------------------------------------------------------

uint8_t CIA6526::read(uint16_t offset) const {
    switch (offset & 0x0F) {
        case REG_PRA: return pra_;
        case REG_PRB: {
            uint8_t result = 0xFF;
            for (int col = 0; col < 8; ++col)
                if (!(pra_ & (1 << col)))
                    result &= keyMatrix_[col];
            return result;
        }
        case REG_DDRA: return ddra_;
        case REG_DDRB: return ddrb_;

        case REG_TALO: return static_cast<uint8_t>(timerACounter_ & 0xFF);
        case REG_TAHI: return static_cast<uint8_t>(timerACounter_ >> 8);
        case REG_TBLO: return static_cast<uint8_t>(timerBCounter_ & 0xFF);
        case REG_TBHI: return static_cast<uint8_t>(timerBCounter_ >> 8);

        // TOD: reading HR latches the time; reading 10THS releases the latch.
        case REG_TOD_HR: {
            auto* self = const_cast<CIA6526*>(this);
            self->todLatch10_  = tod10_;
            self->todLatchSec_ = todSec_;
            self->todLatchMin_ = todMin_;
            self->todLatchHr_  = todHr_;
            self->todLatched_  = true;
            return todHr_;
        }
        case REG_TOD_MIN: return todLatched_ ? todLatchMin_ : todMin_;
        case REG_TOD_SEC: return todLatched_ ? todLatchSec_ : todSec_;
        case REG_TOD_10: {
            auto* self = const_cast<CIA6526*>(this);
            self->todLatched_ = false;
            return tod10_;
        }

        case REG_SDR: return sdr_;

        case REG_ICR: {
            uint8_t val = icrFlags_;
            const_cast<CIA6526*>(this)->icrFlags_ = 0x00;
            return val;
        }
        case REG_CRA: return cra_;
        case REG_CRB: return crb_ & ~CRB_ALARM;  // bit 7 is write-only
        default:      return 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Register write
// ---------------------------------------------------------------------------

void CIA6526::write(uint16_t offset, uint8_t value) {
    switch (offset & 0x0F) {
        case REG_PRA:  pra_  = value; break;
        case REG_PRB:  prb_  = value; break;
        case REG_DDRA: ddra_ = value; break;
        case REG_DDRB: ddrb_ = value; break;

        case REG_TALO:
            timerALatch_ = (timerALatch_ & 0xFF00) | value;
            break;
        case REG_TAHI:
            timerALatch_ = (timerALatch_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            if (!(cra_ & CRA_START))
                timerACounter_ = timerALatch_;
            break;

        case REG_TBLO:
            timerBLatch_ = (timerBLatch_ & 0xFF00) | value;
            break;
        case REG_TBHI:
            timerBLatch_ = (timerBLatch_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            if (!(crb_ & CRB_START))
                timerBCounter_ = timerBLatch_;
            break;

        // TOD writes: CRB bit 7 = 1 → write alarm; bit 7 = 0 → write time
        case REG_TOD_10:
            if (crb_ & CRB_ALARM) todAlarm10_  = value & 0x0F;
            else                  tod10_        = value & 0x0F;
            break;
        case REG_TOD_SEC:
            if (crb_ & CRB_ALARM) todAlarmSec_ = value & 0x7F;
            else                  todSec_       = value & 0x7F;
            break;
        case REG_TOD_MIN:
            if (crb_ & CRB_ALARM) todAlarmMin_ = value & 0x7F;
            else                  todMin_       = value & 0x7F;
            break;
        case REG_TOD_HR:
            if (crb_ & CRB_ALARM) todAlarmHr_  = value & 0x9F;
            else                  todHr_        = value & 0x9F;
            break;

        case REG_SDR: sdr_ = value; break;

        case REG_ICR:
            if (value & 0x80) icrMask_ |=  (value & 0x1F);
            else              icrMask_ &= ~(value & 0x1F);
            break;

        case REG_CRA:
            cra_ = value;
            if (value & CRA_LOAD) {
                timerACounter_ = timerALatch_;
                cra_ &= ~CRA_LOAD;
            }
            break;

        case REG_CRB:
            crb_ = value;
            if (value & CRB_LOAD) {
                timerBCounter_ = timerBLatch_;
                crb_ &= ~CRB_LOAD;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// statusLine
// ---------------------------------------------------------------------------

std::string CIA6526::statusLine() const {
    std::ostringstream s;
    s << std::uppercase << std::hex << std::setfill('0');
    s << "TA=$" << std::setw(4) << (unsigned)timerACounter_
      << ((cra_ & CRA_START) ? " RUN" : " STP")
      << "  TB=$" << std::setw(4) << (unsigned)timerBCounter_
      << ((crb_ & CRB_START) ? " RUN" : " STP");
    s << std::dec
      << "  TOD=" << bcdToInt(todHr_ & 0x7F) << ":"
      << std::setfill('0') << std::setw(2) << bcdToInt(todMin_) << ":"
      << std::setw(2) << bcdToInt(todSec_) << "."
      << (int)(tod10_ & 0x0F)
      << ((todHr_ & 0x80) ? "pm" : "am");
    return s.str();
}
