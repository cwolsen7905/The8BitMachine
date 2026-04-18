#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// AppleIIIO — Apple IIe I/O soft-switch space ($C000-$C0FF)
//
// Handles:
//   $C000     (R)   Keyboard data (bit 7 = strobe, bits 6:0 = ASCII)
//   $C010     (R/W) Clear keyboard strobe
//   $C050     (R/W) GRAPHICS  — switch to graphics mode
//   $C051     (R/W) TEXT      — switch to text mode
//   $C052     (R/W) FULLSCR   — disable mixed mode
//   $C053     (R/W) MIXED     — enable mixed (text + graphics)
//   $C054     (R/W) PAGE1     — select display page 1
//   $C055     (R/W) PAGE2     — select display page 2
//   $C056     (R/W) LORES     — select lo-res graphics
//   $C057     (R/W) HIRES     — select hi-res graphics
//
// All other addresses in the range return $FF / ignore writes.
// ---------------------------------------------------------------------------
class AppleIIIO : public IBusDevice {
public:
    const char* deviceName() const override { return "Apple IIe I/O"; }
    void    reset()                          override;
    void    clock()                          override {}
    uint8_t read (uint16_t addr) const       override;
    void    write(uint16_t addr, uint8_t val) override;
    bool    hasPanel()      const            override { return true; }
    void    drawPanel(const char* title, bool* open) override;
    std::string statusLine() const           override;

    // -----------------------------------------------------------------------
    // Keyboard — called by Machine key handler on key-down
    // -----------------------------------------------------------------------
    void pressKey(uint8_t ascii);  // stores ascii | 0x80 in latch

    // -----------------------------------------------------------------------
    // Video soft-switch callbacks — wired to AppleIIVideo in the preset builder
    // -----------------------------------------------------------------------
    std::function<void(bool)> onTextMode;  // true = text, false = graphics
    std::function<void(bool)> onPage2;     // true = page 2
    std::function<void(bool)> onHiRes;     // true = hi-res, false = lo-res
    std::function<void(bool)> onMixed;     // true = mixed mode

private:
    mutable uint8_t keyLatch_ = 0;  // bit 7 = strobe set, bits 6:0 = ASCII

    // Mirror soft-switch state for the panel display
    bool textMode_ = true;
    bool page2_    = false;
    bool hiRes_    = false;
    bool mixed_    = false;

    void applySoftSwitch(uint16_t addr);
};
