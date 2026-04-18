#include "emulator/devices/ULA.h"
#include "emulator/core/Bus.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <sstream>

// ---------------------------------------------------------------------------
// ZX Spectrum 16-colour palette
// Colours 0-7: normal; 8-15: bright (same hue, full brightness)
// Order: black, blue, red, magenta, green, cyan, yellow, white
// ---------------------------------------------------------------------------
const uint8_t ULA::kR[16] = {
    0x00, 0x00, 0xCD, 0xCD, 0x00, 0x00, 0xCD, 0xCD,   // normal
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF    // bright
};
const uint8_t ULA::kG[16] = {
    0x00, 0x00, 0x00, 0x00, 0xCD, 0xCD, 0xCD, 0xCD,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
const uint8_t ULA::kB[16] = {
    0x00, 0xCD, 0x00, 0xCD, 0x00, 0xCD, 0x00, 0xCD,
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF
};

// ---------------------------------------------------------------------------
// Spectrum keyboard matrix — 8 half-rows × 5 keys
// Row selected when corresponding address line A8-A15 is LOW.
//
//   Row 0 (A8 =0, port 0xFEFE): CS  Z  X  C  V
//   Row 1 (A9 =0, port 0xFDFE): A   S  D  F  G
//   Row 2 (A10=0, port 0xFBFE): Q   W  E  R  T
//   Row 3 (A11=0, port 0xF7FE): 1   2  3  4  5
//   Row 4 (A12=0, port 0xEFFE): 0   9  8  7  6
//   Row 5 (A13=0, port 0xDFFE): P   O  I  U  Y
//   Row 6 (A14=0, port 0xBFFE): EN  L  K  J  H
//   Row 7 (A15=0, port 0x7FFE): SP  SS M  N  B
//
// Bit 0 = key nearest centre of keyboard, bit 4 = key farthest from centre.
// Active-low: 0 = key pressed, 1 = key released.
// ---------------------------------------------------------------------------

ULA::ULA() {
    std::memset(keyMatrix_, 0xFF, sizeof(keyMatrix_));
    fb_.fill(0xFF);
}

void ULA::reset() {
    border_     = 7;
    clockCnt_   = 0;
    frameCount_ = 0;
    flashState_ = false;
    std::memset(keyMatrix_, 0xFF, sizeof(keyMatrix_));
}

// ============================================================================
// clock — one T-state per call
// ============================================================================

void ULA::clock() {
    if (++clockCnt_ >= kCyclesPerFrame) {
        clockCnt_ = 0;
        ++frameCount_;
        // Flash toggles every 16 frames (≈ 3.125 Hz at 50 Hz)
        if ((frameCount_ & 0x0F) == 0) flashState_ = !flashState_;
        renderFrame();
        if (onIRQ) onIRQ();
    }
}

// ============================================================================
// Port I/O
// ============================================================================

uint8_t ULA::portRead(uint16_t port) {
    if (port & 0x01) return 0xFF;   // only even ports

    uint8_t upper  = uint8_t(port >> 8);
    uint8_t result = 0x1F;           // all keys up

    for (int row = 0; row < 8; row++) {
        if (!(upper & (1 << row)))
            result &= keyMatrix_[row];
    }
    // Bit 6 = EAR (1 = no tape signal); bits 7,5 float high
    return 0xE0 | (result & 0x1F);
}

void ULA::portWrite(uint16_t port, uint8_t val) {
    if (port & 0x01) return;
    border_ = val & 0x07;
}

// ============================================================================
// Keyboard
// ============================================================================

void ULA::setKey(int row, int bit, bool pressed) {
    if (row < 0 || row > 7 || bit < 0 || bit > 4) return;
    if (pressed)
        keyMatrix_[row] &= ~(1 << bit);
    else
        keyMatrix_[row] |=  (1 << bit);
}

bool ULA::keyState(int row, int bit) const {
    if (row < 0 || row > 7 || bit < 0 || bit > 4) return false;
    return !(keyMatrix_[row] & (1 << bit));
}

void ULA::clearAllKeys() {
    std::memset(keyMatrix_, 0xFF, sizeof(keyMatrix_));
}

// ============================================================================
// renderFrame — build the RGBA framebuffer
// ============================================================================

void ULA::renderFrame() {
    // Fill entire framebuffer with border colour
    const uint8_t br = kR[border_];
    const uint8_t bg = kG[border_];
    const uint8_t bb = kB[border_];

    uint8_t* p = fb_.data();
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        p[0] = br; p[1] = bg; p[2] = bb; p[3] = 0xFF;
        p += 4;
    }

    if (!bus_) return;

    // Render 256×192 active area
    for (int y = 0; y < 192; ++y) {
        // ZX Spectrum screen address decoding:
        //   bit pattern: 010 YY YYY XXX XXXXX
        //   where the Y bits are: [7:6]=third [5:3]=first [2:0]=second
        //   i.e. addr = 0x4000 | (y[7:6] << 11) | (y[2:0] << 8) | (y[5:3] << 5)
        // Simplified for 0-191:
        //   y[7:6] = y/64, y[5:3] = (y%64)/8, y[2:0] = y%8
        uint16_t screenRow = uint16_t(
            0x4000 |
            ((y & 0xC0) << 5) |    // bits 7:6 → address bits 12:11
            ((y & 0x07) << 8) |    // bits 2:0 → address bits 10:8
            ((y & 0x38) << 2)      // bits 5:3 → address bits  7:5
        );
        uint16_t attrRow  = uint16_t(0x5800 + (y / 8) * 32);

        for (int col = 0; col < 32; ++col) {
            uint8_t pixels = bus_->read(screenRow + col);
            uint8_t attr   = bus_->read(attrRow  + col);

            uint8_t inkIdx   =  attr & 0x07;
            uint8_t paperIdx = (attr >> 3) & 0x07;
            bool    bright   =  (attr & 0x40) != 0;
            bool    flash    =  (attr & 0x80) != 0;

            if (bright) { inkIdx += 8; paperIdx += 8; }
            if (flash && flashState_) std::swap(inkIdx, paperIdx);

            for (int bit = 7; bit >= 0; --bit) {
                bool    on  = (pixels >> bit) & 1;
                uint8_t ci  = on ? inkIdx : paperIdx;
                int     px  = BORDER_X + col * 8 + (7 - bit);
                int     py  = BORDER_Y + y;
                uint8_t* dst = fb_.data() + (py * WIDTH + px) * 4;
                dst[0] = kR[ci];
                dst[1] = kG[ci];
                dst[2] = kB[ci];
                dst[3] = 0xFF;
            }
        }
    }
}

// ============================================================================
// IBusDevice panel
// ============================================================================

std::string ULA::statusLine() const {
    std::ostringstream ss;
    ss << "border=" << int(border_) << " frame=" << frameCount_;
    return ss.str();
}

void ULA::drawPanel(const char* title, bool* open) {
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    static const char* kColorNames[8] = {
        "Black","Blue","Red","Magenta","Green","Cyan","Yellow","White"
    };

    ImGui::Text("Border");
    ImGui::SameLine(80);
    ImGui::ColorButton("##border",
        ImVec4(kR[border_] / 255.0f, kG[border_] / 255.0f,
               kB[border_] / 255.0f, 1.0f),
        ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
    ImGui::SameLine();
    ImGui::Text("%d (%s)", int(border_), kColorNames[border_ & 7]);

    ImGui::Separator();
    ImGui::Text("Frame  %d", frameCount_);
    ImGui::Text("Flash  %s", flashState_ ? "ON" : "off");

    ImGui::End();
}
