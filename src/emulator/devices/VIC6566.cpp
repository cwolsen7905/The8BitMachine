#include "emulator/devices/VIC6566.h"
#include "emulator/devices/CharROM.h"
#include "emulator/core/Bus.h"
#include <imgui.h>

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
void VIC6566::drawPanel(const char* title, bool* open) {
    ImGui::SetNextWindowSize({ 340.0f, 300.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    // Colour swatch helper using VIC-II NTSC palette
    auto swatch = [](uint8_t idx) {
        idx &= 0x0F;
        // Standard VIC-II 16-colour palette (approximate RGB)
        static const ImVec4 pal[16] = {
            {0,0,0,1},{1,1,1,1},{0.53f,0.13f,0.12f,1},{0.44f,0.75f,0.75f,1},
            {0.55f,0.19f,0.6f,1},{0.31f,0.62f,0.3f,1},{0.11f,0.1f,0.6f,1},
            {0.95f,0.95f,0.45f,1},{0.55f,0.37f,0.1f,1},{0.34f,0.26f,0,1},
            {0.76f,0.38f,0.35f,1},{0.31f,0.31f,0.31f,1},{0.5f,0.5f,0.5f,1},
            {0.65f,0.95f,0.62f,1},{0.45f,0.43f,0.9f,1},{0.7f,0.7f,0.7f,1},
        };
        ImGui::ColorButton("##c", pal[idx],
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
            ImVec2(14, 14));
    };

    if (ImGui::CollapsingHeader("Raster", ImGuiTreeNodeFlags_DefaultOpen)) {
        int rasterCompare = ((reg_[REG_CR1] & 0x80) << 1) | reg_[REG_RSTR];
        ImGui::Text("Current   $%03X  (%d)", rasterY_, rasterY_);
        ImGui::Text("Compare   $%03X  (%d)", rasterCompare, rasterCompare);
        ImGui::Text("ISR $%02X  IEN $%02X",
            (unsigned)reg_[REG_ISR], (unsigned)reg_[REG_IEN]);
    }

    if (ImGui::CollapsingHeader("Colours", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto row = [&](const char* label, uint8_t regIdx) {
            ImGui::Text("%-10s $%X  ", label, (unsigned)(reg_[regIdx] & 0x0F));
            ImGui::SameLine();
            swatch(reg_[regIdx] & 0x0F);
        };
        row("Border",  REG_BORDC);
        row("BG0",     REG_BG0);
        row("BG1",     REG_BG1);
        row("BG2",     REG_BG2);
        row("BG3",     REG_BG3);
    }

    if (ImGui::CollapsingHeader("Control")) {
        const bool den  = (reg_[REG_CR1] & 0x10) != 0;
        const bool bm   = (reg_[REG_CR1] & 0x20) != 0;
        const bool ecm  = (reg_[REG_CR1] & 0x40) != 0;
        const bool mcm  = (reg_[REG_CR2] & 0x10) != 0;
        ImGui::Text("CR1 $%02X  CR2 $%02X  MPTR $%02X",
            (unsigned)reg_[REG_CR1],
            (unsigned)reg_[REG_CR2],
            (unsigned)reg_[REG_MPTR]);
        if (den)  { ImGui::SameLine(); ImGui::TextColored({0.2f,1,0.3f,1}, "DEN"); }
        if (bm)   { ImGui::SameLine(); ImGui::TextColored({1,0.8f,0.2f,1}, "BM"); }
        if (ecm)  { ImGui::SameLine(); ImGui::TextColored({1,0.8f,0.2f,1}, "ECM"); }
        if (mcm)  { ImGui::SameLine(); ImGui::TextColored({1,0.8f,0.2f,1}, "MCM"); }
    }

    ImGui::End();
}

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
    const bool    den    = (reg_[REG_CR1] & 0x10) != 0;
    const uint8_t border = reg_[REG_BORDC] & 0x0F;
    const uint8_t bg     = den ? (reg_[REG_BG0] & 0x0F) : 0;

    fillRect(0,        0,        WIDTH,    HEIGHT,   border);
    fillRect(BORDER_X, BORDER_Y, ACTIVE_W, ACTIVE_H, bg);

    if (den && bus_)
        renderCharMode();
}

// ---------------------------------------------------------------------------
// renderCharMode — standard 40×25 text mode.
//
// Reads screen codes from screen RAM (default $0400), looks each up in the
// embedded CharROM, and renders 8×8 glyphs into the active display area.
// Foreground colour defaults to white (1); colour RAM ($D800) is not yet
// implemented.  Screen codes $80–$FF are rendered in reverse video.
// ---------------------------------------------------------------------------
void VIC6566::renderCharMode() {
    // Screen RAM base: bits 7–4 of REG_MPTR × $0400 (default $D018=$14 → $0400)
    const uint16_t screenBase =
        static_cast<uint16_t>(((reg_[REG_MPTR] >> 4) & 0x0F) * 0x0400);

    const uint8_t bg = reg_[REG_BG0] & 0x0F;
    const uint8_t fg = 1;  // white — TODO: read per-char colour from $D800

    for (int row = 0; row < 25; ++row) {
        for (int col = 0; col < 40; ++col) {
            uint8_t code    = bus_->read(static_cast<uint16_t>(screenBase + row * 40 + col));
            const bool rev  = (code & 0x80) != 0;
            code           &= 0x7F;

            const uint8_t fgc = rev ? bg : fg;
            const uint8_t bgc = rev ? fg : bg;

            const int x0 = BORDER_X + col * 8;
            const int y0 = BORDER_Y + row * 8;

            for (int py = 0; py < 8; ++py) {
                const uint8_t bits = kCharROM[code][py];
                for (int px = 0; px < 8; ++px) {
                    const bool set      = (bits >> (7 - px)) & 1;
                    const Color& c      = kPalette[set ? fgc : bgc];
                    uint8_t* p          = fb_.data() + ((y0 + py) * WIDTH + (x0 + px)) * 4;
                    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 0xFF;
                }
            }
        }
    }
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
