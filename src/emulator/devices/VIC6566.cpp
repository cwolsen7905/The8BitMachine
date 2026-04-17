#include "emulator/devices/VIC6566.h"

#include <cstring>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// Standard VIC-II NTSC colour palette
// ---------------------------------------------------------------------------
const VIC6566::Color VIC6566::kPalette[16] = {
    {  0,   0,   0},  //  0  Black
    {255, 255, 255},  //  1  White
    {136,   0,   0},  //  2  Red
    {170, 255, 238},  //  3  Cyan
    {204,  68, 204},  //  4  Purple
    {  0, 204,  85},  //  5  Green
    {  0,   0, 170},  //  6  Blue
    {238, 238, 119},  //  7  Yellow
    {221, 136,  85},  //  8  Orange
    {102,  68,   0},  //  9  Brown
    {255, 119, 119},  // 10  Light Red
    { 51,  51,  51},  // 11  Dark Grey
    {119, 119, 119},  // 12  Medium Grey
    {170, 255, 102},  // 13  Light Green
    {  0, 136, 255},  // 14  Light Blue
    {187, 187, 187},  // 15  Light Grey
};

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------
void VIC6566::reset() {
    std::memset(reg_, 0, sizeof(reg_));

    // Power-on defaults that produce a visible blue screen (C64-compatible)
    reg_[REG_CR1]   = 0x1B;  // DEN=1, RSEL=1, YSCROLL=3
    reg_[REG_CR2]   = 0x08;  // CSEL=1
    reg_[REG_MPTR]  = 0x14;  // VM=$0400, CB=$1000
    reg_[REG_BORDC] = 0x0E;  // Light blue border
    reg_[REG_BG0]   = 0x06;  // Blue background

    rasterX_ = 0;
    rasterY_ = 0;
    dirty_   = false;

    // Render an initial blue frame so the screen is not blank on startup
    renderFrame();
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// clock — one VIC cycle.  63 or 65 cycles per raster line, 263 lines/frame.
// ---------------------------------------------------------------------------
void VIC6566::clock() {
    if (++rasterX_ < CYCLES_PER_LINE) return;
    rasterX_ = 0;

    checkRasterIRQ();

    if (++rasterY_ < LINES_PER_FRAME) return;
    rasterY_ = 0;

    renderFrame();
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// checkRasterIRQ — fires when the current raster line matches the compare
// value stored in REG_CR1 (bit 7 = RC8) and REG_RSTR, and IRQ is enabled.
// ---------------------------------------------------------------------------
void VIC6566::checkRasterIRQ() {
    const uint16_t compare =
        static_cast<uint16_t>(reg_[REG_RSTR]) |
        ((reg_[REG_CR1] & 0x80) ? 0x100u : 0u);

    if (static_cast<uint16_t>(rasterY_) != compare) return;
    if (!(reg_[REG_IEN] & 0x01)) return;

    reg_[REG_ISR] |= 0x81;  // bit 0 (raster) + bit 7 (any IRQ)
    if (onIRQ) onIRQ();
}

// ---------------------------------------------------------------------------
// read
// ---------------------------------------------------------------------------
uint8_t VIC6566::read(uint16_t off) const {
    const uint8_t reg = off & 0x3F;  // registers mirror every 64 bytes

    if (reg >= 0x2F) return 0xFF;    // unimplemented registers read $FF

    switch (reg) {
        case REG_CR1:
            // Return current raster line bit 8 in bit 7
            return (reg_[REG_CR1] & 0x7F) |
                   ((rasterY_ & 0x100) ? 0x80 : 0x00);

        case REG_RSTR:
            return static_cast<uint8_t>(rasterY_ & 0xFF);

        case 0x1E:  // sprite-sprite collision — cleared on read
        case 0x1F: {// sprite-background collision — cleared on read
            const uint8_t v = reg_[reg];
            reg_[reg] = 0;
            return v;
        }

        case REG_ISR: {
            const uint8_t v = reg_[REG_ISR];
            reg_[REG_ISR] = 0;  // cleared on read
            return v;
        }

        default:
            return reg_[reg];
    }
}

// ---------------------------------------------------------------------------
// write
// ---------------------------------------------------------------------------
void VIC6566::write(uint16_t off, uint8_t val) {
    const uint8_t reg = off & 0x3F;
    if (reg >= 0x2F) return;  // unimplemented — ignore writes

    switch (reg) {
        case REG_CR1:
            // Preserve RC8 (bit 7) as part of the raster compare — don't
            // stomp the live raster counter.  Store the full written value.
            reg_[REG_CR1] = val;
            break;

        case REG_ISR:
            // Writing clears the bits specified by val (acknowledge)
            reg_[REG_ISR] &= ~val;
            break;

        default:
            reg_[reg] = val;
            break;
    }
}

// ---------------------------------------------------------------------------
// statusLine
// ---------------------------------------------------------------------------
std::string VIC6566::statusLine() const {
    const bool den = (reg_[REG_CR1] & 0x10) != 0;
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0');
    ss << "Raster:$" << std::setw(3) << rasterY_
       << "  BG:$"   << std::setw(2) << (int)(reg_[REG_BG0] & 0x0F)
       << "  "       << (den ? "DEN" : "   ");
    return ss.str();
}

// ---------------------------------------------------------------------------
// renderFrame — produce one full 320×200 RGBA frame.
//
// Current stub: fills the active display area with the background colour.
// TODO: character-mode rendering, sprite rendering, border colour, bitmap mode.
// ---------------------------------------------------------------------------
void VIC6566::renderFrame() {
    const bool den = (reg_[REG_CR1] & 0x10) != 0;
    const uint8_t bg = den ? (reg_[REG_BG0] & 0x0F) : 0;
    fillRect(0, 0, WIDTH, HEIGHT, bg);
}

// ---------------------------------------------------------------------------
// fillRect — paint a solid colour rectangle into the framebuffer.
// ---------------------------------------------------------------------------
void VIC6566::fillRect(int x, int y, int w, int h, uint8_t colorIdx) {
    const Color& c = kPalette[colorIdx & 0x0F];
    for (int row = y; row < y + h && row < HEIGHT; ++row) {
        uint8_t* p = fb_.data() + (row * WIDTH + x) * 4;
        for (int col = 0; col < w && (x + col) < WIDTH; ++col, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 0xFF;
        }
    }
}
