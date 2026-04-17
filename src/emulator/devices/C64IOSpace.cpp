#include "emulator/devices/C64IOSpace.h"

uint8_t C64IOSpace::read(uint16_t offset) const {
    if      (offset < 0x400) return vic_  ? vic_ ->read(offset & 0x3F)  : 0xFF;
    else if (offset < 0x800) return sid_  ? sid_ ->read(offset & 0xFF)  : 0xFF;
    else if (offset < 0xC00) return 0x0F;  // colour RAM stub (4-bit, always reads $0F)
    else if (offset < 0xD00) return cia1_ ? cia1_->read(offset & 0x0F)  : 0xFF;
    else if (offset < 0xE00) return cia2_ ? cia2_->read(offset & 0x0F)  : 0xFF;
    return 0xFF;
}

void C64IOSpace::write(uint16_t offset, uint8_t value) {
    if      (offset < 0x400) { if (vic_)  vic_ ->write(offset & 0x3F,  value); }
    else if (offset < 0x800) { if (sid_)  sid_ ->write(offset & 0xFF,  value); }
    else if (offset < 0xC00) { /* colour RAM stub — writes ignored */ }
    else if (offset < 0xD00) { if (cia1_) cia1_->write(offset & 0x0F,  value); }
    else if (offset < 0xE00) { if (cia2_) cia2_->write(offset & 0x0F,  value); }
}
