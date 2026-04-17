#pragma once

#include "emulator/core/IBusDevice.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// VIC6566 — MOS 6566/6567/8566 Video Interface Controller stub.
//
// Implements the 64-register VIC-IIe interface and generates a 320×200 RGBA
// framebuffer.  Rendering is intentionally minimal for this first revision:
//   • Each frame is rendered as a solid background colour (register $21).
//   • Raster counter advances cycle-accurately; raster IRQ is fully wired.
//   • All 64 registers are readable/writable; collision registers clear on read.
//
// Character and sprite rendering, border colour, and bitmap mode are left as
// TODO items for future revisions.
//
// Address range: typically $D000–$D3FF; the Bus passes an offset (0x000–0x3FF).
// ---------------------------------------------------------------------------
class VIC6566 : public IBusDevice {
public:
    // Full rasterised frame including border area (NTSC proportions)
    static constexpr int BORDER_X = 40;   // pixels left and right of active area
    static constexpr int BORDER_Y = 40;   // pixels above and below active area
    static constexpr int ACTIVE_W = 320;
    static constexpr int ACTIVE_H = 200;
    static constexpr int WIDTH    = ACTIVE_W + 2 * BORDER_X;  // 400
    static constexpr int HEIGHT   = ACTIVE_H + 2 * BORDER_Y;  // 280

    // -----------------------------------------------------------------------
    // IBusDevice interface
    // -----------------------------------------------------------------------
    const char* deviceName() const override { return "VIC-IIe"; }
    void        reset()            override;
    void        clock()            override;
    uint8_t     read (uint16_t off) const override;
    void        write(uint16_t off, uint8_t val) override;
    std::string statusLine() const override;
    bool        hasPanel()  const override { return true; }
    void        drawPanel(const char* title, bool* open) override;

    // -----------------------------------------------------------------------
    // Bus access — VIC needs to read character/bitmap data from RAM.
    // Call once before emulation begins; pointer may be null (safe).
    // -----------------------------------------------------------------------
    void connectBus(Bus* b) { bus_ = b; }

    // -----------------------------------------------------------------------
    // Colour RAM — 1 KB at $D800–$DBFF (CPU side, 4-bit per cell).
    // C64IOSpace routes reads/writes here; VIC reads it during rendering.
    // -----------------------------------------------------------------------
    uint8_t readColorRam (uint16_t offset) const { return colorRAM_[offset & 0x3FF] | 0xF0; }
    void    writeColorRam(uint16_t offset, uint8_t val) { colorRAM_[offset & 0x3FF] = val & 0x0F; }

    // -----------------------------------------------------------------------
    // Framebuffer — Application polls this each frame to upload the texture.
    // -----------------------------------------------------------------------
    const uint8_t* framebuffer() const { return fb_.data(); }
    bool frameDirty() const { return dirty_; }
    void clearDirty()       { dirty_ = false; }

    // -----------------------------------------------------------------------
    // IRQ output — wired to the CPU IRQ line by Machine.
    // -----------------------------------------------------------------------
    std::function<void()> onIRQ;

    // -----------------------------------------------------------------------
    // Register offsets (public for Application status panel)
    // -----------------------------------------------------------------------
    static constexpr uint8_t REG_CR1   = 0x11;  // control register 1
    static constexpr uint8_t REG_RSTR  = 0x12;  // raster compare / current raster
    static constexpr uint8_t REG_CR2   = 0x16;  // control register 2
    static constexpr uint8_t REG_MPTR  = 0x18;  // memory pointers
    static constexpr uint8_t REG_ISR   = 0x19;  // interrupt status
    static constexpr uint8_t REG_IEN   = 0x1A;  // interrupt enable
    static constexpr uint8_t REG_BORDC = 0x20;  // border colour
    static constexpr uint8_t REG_BG0   = 0x21;  // background colour 0
    static constexpr uint8_t REG_BG1   = 0x22;
    static constexpr uint8_t REG_BG2   = 0x23;
    static constexpr uint8_t REG_BG3   = 0x24;

private:
    // NTSC timing constants
    static constexpr int CYCLES_PER_LINE = 65;
    static constexpr int LINES_PER_FRAME = 263;

    // Colour palette — standard VIC-II 16-colour NTSC palette (R, G, B)
    struct Color { uint8_t r, g, b; };
    static const Color kPalette[16];

    mutable uint8_t reg_[64] = {};  // mutable: ISR and collision regs clear on read
    std::array<uint8_t, WIDTH * HEIGHT * 4> fb_ = {};

    uint8_t colorRAM_[1024] = {};  // 4-bit per cell; initialized to 0 (black)

    Bus* bus_     = nullptr;
    int  rasterX_ = 0;
    int  rasterY_ = 0;
    bool dirty_   = false;

    void renderFrame();
    void renderCharMode();
    void fillRect(int x, int y, int w, int h, uint8_t colorIdx);
    void checkRasterIRQ();
};
