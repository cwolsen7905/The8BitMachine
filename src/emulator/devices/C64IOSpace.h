#pragma once

#include "emulator/core/IBusDevice.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/devices/SID6581.h"
#include "emulator/devices/VIC6566.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// C64IOSpace — dispatches the $D000–$DFFF I/O window on the Commodore 64.
//
// Used as one option inside a SwitchableRegion.  The SwitchableRegion passes
// an offset relative to $D000; this device decodes it and forwards to the
// appropriate chip (VIC, SID, CIA1, CIA2, or colour RAM stub).
//
// Chip-select decoding (matches real 6567/6526 CS lines):
//   $000–$3FF  VIC-IIe    (offset & $3F passed to chip)
//   $400–$7FF  SID 6581   (offset & $FF passed to chip)
//   $800–$BFF  Colour RAM (4-bit, stub — reads $0F, writes ignored)
//   $C00–$CFF  CIA1       (offset & $0F passed to chip)
//   $D00–$DFF  CIA2       (offset & $0F passed to chip)
//   $E00–$FFF  Open bus   ($FF)
// ---------------------------------------------------------------------------
class C64IOSpace : public IBusDevice {
public:
    C64IOSpace(VIC6566* vic, SID6581* sid, CIA6526* cia1, CIA6526* cia2)
        : vic_(vic), sid_(sid), cia1_(cia1), cia2_(cia2) {}

    const char* deviceName() const override { return "C64 I/O Space"; }
    void        reset()      override {}   // each chip is reset by Machine::reset()
    void        clock()      override {}   // each chip is clocked by Machine::clock()

    uint8_t read (uint16_t offset) const override;
    void    write(uint16_t offset, uint8_t value) override;
    std::string statusLine() const override { return "VIC + SID + CIA1 + CIA2"; }

    SubRange findSubDevice(const IBusDevice* dev) const override {
        if (dev == vic_)  return {0x000, 0x3FF};
        if (dev == sid_)  return {0x400, 0x7FF};
        if (dev == cia1_) return {0xC00, 0xCFF};
        if (dev == cia2_) return {0xD00, 0xDFF};
        return {};
    }

private:
    VIC6566* vic_;
    SID6581* sid_;
    CIA6526* cia1_;
    CIA6526* cia2_;
};
