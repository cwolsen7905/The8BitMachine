#include "emulator/devices/SID6581.h"

void SID6581::reset() {
    for (auto& r : regs_) r = 0x00;
}

uint8_t SID6581::read(uint16_t offset) const {
    if (offset >= NUM_REGS) return 0xFF;
    switch (offset) {
        case REG_POT_X: return 0xFF;
        case REG_POT_Y: return 0xFF;
        case REG_OSC3:  return 0x00;
        case REG_ENV3:  return 0x00;
        // Write-only registers return 0xFF on real hardware
        default:        return 0xFF;
    }
}

void SID6581::write(uint16_t offset, uint8_t value) {
    if (offset < NUM_REGS)
        regs_[offset] = value;
}
