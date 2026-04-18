#pragma once

#include "emulator/core/IBusDevice.h"
#include "emulator/core/IHasPanel.h"
#include <array>
#include <cstdint>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// AppleIIVideo — Apple IIe display controller
//
// Responsibilities:
//   * Frame timing — fires once per ~60 Hz frame (~17030 cycles at 1 MHz)
//   * Text mode (40×24) — reads character codes from bus RAM ($0400 or $0800)
//     and renders each character using the embedded 7×8 font
//   * Hi-Res mode (280×192) — reads pixel bytes from bus RAM ($2000 or $4000)
//     and renders as monochrome 1-bit pixels
//   * Soft switch state (textMode, page2, hiRes, mixed) — set by AppleIIIO
//
// Display: 280×192 RGBA framebuffer (green phosphor theme).
// ---------------------------------------------------------------------------
class AppleIIVideo : public IBusDevice, public IHasPanel {
public:
    static constexpr int WIDTH           = 280;
    static constexpr int HEIGHT          = 192;
    static constexpr int kCyclesPerFrame = 17030;  // ~60 Hz at ~1 MHz

    AppleIIVideo();

    // -----------------------------------------------------------------------
    // IBusDevice — no memory-mapped registers; clocked via activeFixedDevices_
    // -----------------------------------------------------------------------
    const char* deviceName() const override { return "Apple IIe Video"; }
    void    reset()                          override;
    void    clock()                          override;
    uint8_t read (uint16_t) const            override { return 0xFF; }
    void    write(uint16_t, uint8_t)         override {}
    std::string statusLine() const           override;

    // IHasPanel
    void    drawPanel(const char* title, bool* open) override;

    // -----------------------------------------------------------------------
    // Bus — needed to read screen RAM during renderFrame()
    // -----------------------------------------------------------------------
    void connectBus(Bus* bus) { bus_ = bus; }

    // -----------------------------------------------------------------------
    // Soft switch state — updated by AppleIIIO on $C050-$C057 accesses
    // -----------------------------------------------------------------------
    void setTextMode(bool t) { textMode_ = t; }
    void setPage2   (bool p) { page2_    = p; }
    void setHiRes   (bool h) { hiRes_    = h; }
    void setMixed   (bool m) { mixed_    = m; }

    bool textMode() const { return textMode_; }
    bool page2()    const { return page2_;    }
    bool hiRes()    const { return hiRes_;    }
    bool mixed()    const { return mixed_;    }

    // -----------------------------------------------------------------------
    // Framebuffer — RGBA, WIDTH × HEIGHT × 4 bytes
    // -----------------------------------------------------------------------
    const uint8_t* framebuffer() const { return fb_.data(); }
    bool frameDirty() const { return dirty_; }
    void clearDirty()       { dirty_ = false; }

private:
    Bus* bus_ = nullptr;
    std::array<uint8_t, WIDTH * HEIGHT * 4> fb_{};

    bool textMode_ = true;
    bool page2_    = false;
    bool hiRes_    = false;
    bool mixed_    = false;

    int  clockCnt_   = 0;
    int  frameCount_ = 0;
    bool flashState_ = false;
    bool dirty_      = false;

    void renderFrame();
    void renderText40(uint16_t page, int startRow, int endRow);
    void renderHiRes (uint16_t page, int startRow, int endRow);
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void putChar(int col, int row, uint8_t ch);

    // Embedded 7×8 character ROM, ASCII-indexed (0-127).
    // Each entry: 8 bytes, one per pixel row (top→bottom).
    // Bits [6:0] are the 7 pixels, bit 6 = leftmost pixel.
    static const uint8_t kFont[128][8];

    // Green phosphor palette
    static constexpr uint8_t kBgR = 0,  kBgG = 17,  kBgB = 0;
    static constexpr uint8_t kFgR = 51, kFgG = 255, kFgB = 51;
};
