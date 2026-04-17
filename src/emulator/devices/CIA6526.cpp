#include "emulator/devices/CIA6526.h"

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void CIA6526::setKey(int col, int row, bool pressed) {
    if (col < 0 || col > 7 || row < 0 || row > 7) return;
    if (pressed) keyMatrix_[col] &= ~static_cast<uint8_t>(1 << row);
    else         keyMatrix_[col] |=  static_cast<uint8_t>(1 << row);
}

void CIA6526::reset() {
    pra_  = 0xFF;  prb_  = 0xFF;
    ddra_ = 0x00;  ddrb_ = 0x00;
    for (auto& col : keyMatrix_) col = 0xFF;

    timerALatch_   = 0xFFFF;
    timerACounter_ = 0xFFFF;
    cra_           = 0x00;

    timerBLatch_   = 0xFFFF;
    crb_           = 0x00;

    tod10_ = todSec_ = todMin_ = todHr_ = 0x00;
    sdr_   = 0x00;

    icrMask_  = 0x00;
    icrFlags_ = 0x00;
}

// ---------------------------------------------------------------------------
// Clock — called every ϕ2 cycle
// ---------------------------------------------------------------------------

void CIA6526::clock() {
    if (!(cra_ & CRA_START)) return;   // Timer A not running

    if (timerACounter_ == 0) {
        timerAUnderflow();
    } else {
        --timerACounter_;
    }
}

void CIA6526::timerAUnderflow() {
    // Reload counter from latch
    timerACounter_ = timerALatch_;

    // One-shot: stop after underflow
    if (cra_ & CRA_ONESHOT)
        cra_ &= ~CRA_START;

    // Set TA flag
    icrFlags_ |= ICR_TA;

    // Fire IRQ if TA is unmasked
    if ((icrMask_ & ICR_TA) && onIRQ) {
        icrFlags_ |= ICR_IR;
        onIRQ();
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
        case REG_DDRA:    return ddra_;
        case REG_DDRB:    return ddrb_;
        case REG_TALO:    return static_cast<uint8_t>(timerACounter_ & 0xFF);
        case REG_TAHI:    return static_cast<uint8_t>(timerACounter_ >> 8);
        case REG_TBLO:    return static_cast<uint8_t>(timerBLatch_ & 0xFF);
        case REG_TBHI:    return static_cast<uint8_t>(timerBLatch_ >> 8);
        case REG_TOD_10:  return tod10_;
        case REG_TOD_SEC: return todSec_;
        case REG_TOD_MIN: return todMin_;
        case REG_TOD_HR:  return todHr_;
        case REG_SDR:     return sdr_;
        case REG_ICR: {
            // Reading ICR returns flags and clears them
            uint8_t val       = icrFlags_;
            // Cast away const for this side-effect-on-read register.
            // This is the standard 6526 behaviour.
            const_cast<CIA6526*>(this)->icrFlags_ = 0x00;
            return val;
        }
        case REG_CRA:  return cra_;
        case REG_CRB:  return crb_;
        default:       return 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Register write
// ---------------------------------------------------------------------------

void CIA6526::write(uint16_t offset, uint8_t value) {
    switch (offset & 0x0F) {
        case REG_PRA:     pra_  = value; break;
        case REG_PRB:     prb_  = value; break;
        case REG_DDRA:    ddra_ = value; break;
        case REG_DDRB:    ddrb_ = value; break;

        case REG_TALO:
            // Write to latch low; counter not affected until LOAD or underflow
            timerALatch_ = (timerALatch_ & 0xFF00) | value;
            break;
        case REG_TAHI:
            timerALatch_ = (timerALatch_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            // Writing TAHI also loads the counter if timer is stopped
            if (!(cra_ & CRA_START))
                timerACounter_ = timerALatch_;
            break;

        case REG_TBLO:
            timerBLatch_ = (timerBLatch_ & 0xFF00) | value;
            break;
        case REG_TBHI:
            timerBLatch_ = (timerBLatch_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            break;

        case REG_TOD_10:  tod10_  = value & 0x0F; break;
        case REG_TOD_SEC: todSec_ = value & 0x7F; break;
        case REG_TOD_MIN: todMin_ = value & 0x7F; break;
        case REG_TOD_HR:  todHr_  = value & 0x9F; break;

        case REG_SDR: sdr_ = value; break;

        case REG_ICR:
            // Bit 7 = 1: set mask bits  /  Bit 7 = 0: clear mask bits
            if (value & 0x80)
                icrMask_ |=  (value & 0x1F);
            else
                icrMask_ &= ~(value & 0x1F);
            break;

        case REG_CRA:
            cra_ = value;
            // Bit 4 (LOAD) forces an immediate latch → counter copy
            if (value & CRA_LOAD) {
                timerACounter_ = timerALatch_;
                cra_ &= ~CRA_LOAD;   // self-clearing
            }
            break;

        case REG_CRB:
            crb_ = value;
            break;
    }
}
