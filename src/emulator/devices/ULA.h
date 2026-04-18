#pragma once

#include "emulator/core/IBusDevice.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// ULA — ZX Spectrum ULA (Uncommitted Logic Array).
//
// Responsibilities:
//   * Port I/O — responds to even-addressed ports (bit 0 = 0):
//       OUT: bits 2-0 set the border colour
//       IN:  returns keyboard matrix state in bits 4-0 (active-low)
//   * Keyboard — 8 half-rows × 5 bits; setKey(row, bit, pressed)
//   * Display rendering — 256×192 bitmap with 8×8 attribute cells,
//       drawn into a 352×272 RGBA framebuffer (48px border each side)
//   * Frame timing — fires onIRQ once every kCyclesPerFrame T-states,
//       then renders the next frame; cycle count wraps continuously so it
//       spans display frames naturally
//
// The ULA is clocked by Machine::clock() at the same rate as the Z80 CPU
// (one call per T-state).  Machine::buildSpectrumPreset() wires the port
// callbacks to CPUZ80::setPortHandlers().
// ---------------------------------------------------------------------------
class ULA : public IBusDevice {
public:
    static constexpr int WIDTH  = 352;   // 48 + 256 + 48
    static constexpr int HEIGHT = 272;   // 40 + 192 + 40

    // Border pixels on each side
    static constexpr int BORDER_X = (WIDTH  - 256) / 2;   // 48
    static constexpr int BORDER_Y = (HEIGHT - 192) / 2;   // 40

    // T-states per 50 Hz frame at 3.5 MHz
    static constexpr int kCyclesPerFrame = 69888;

    ULA();

    // -----------------------------------------------------------------------
    // IBusDevice — the ULA presents no memory-mapped registers on the bus
    // (keyboard/border are accessed via Z80 IN/OUT instructions).  A dummy
    // bus entry is registered so the Machine Designer can show the ULA panel.
    // -----------------------------------------------------------------------
    const char* deviceName() const override { return "ULA"; }
    void    reset()                          override;
    void    clock()                          override;
    uint8_t read (uint16_t)            const override { return 0xFF; }
    void    write(uint16_t, uint8_t)         override {}
    bool    hasPanel()                 const override { return true; }
    void    drawPanel(const char* title, bool* open) override;
    std::string statusLine()           const override;

    // -----------------------------------------------------------------------
    // Port I/O — called by CPUZ80 port handlers set in buildSpectrumPreset
    // -----------------------------------------------------------------------
    uint8_t portRead (uint16_t port);
    void    portWrite(uint16_t port, uint8_t val);

    // -----------------------------------------------------------------------
    // Keyboard — row 0-7, bit 0-4 (active-low; 0 = pressed)
    // See Matrix layout in ULA.cpp
    // -----------------------------------------------------------------------
    void setKey(int row, int bit, bool pressed);
    bool keyState(int row, int bit) const;
    void clearAllKeys();

    // -----------------------------------------------------------------------
    // Bus — needed to read screen / attribute RAM during renderFrame()
    // -----------------------------------------------------------------------
    void connectBus(Bus* bus) { bus_ = bus; }

    // -----------------------------------------------------------------------
    // Framebuffer — RGBA, WIDTH × HEIGHT × 4 bytes
    // -----------------------------------------------------------------------
    const uint8_t* framebuffer() const { return fb_.data(); }

    // -----------------------------------------------------------------------
    // IRQ callback — fired once per frame (50 Hz in authentic timing)
    // -----------------------------------------------------------------------
    std::function<void()> onIRQ;

    // -----------------------------------------------------------------------
    // Accessors for the UI panel
    // -----------------------------------------------------------------------
    uint8_t borderColor() const { return border_; }
    int     frameCount()  const { return frameCount_; }

private:
    Bus* bus_ = nullptr;

    std::array<uint8_t, WIDTH * HEIGHT * 4> fb_{};

    uint8_t border_     = 7;       // white
    uint8_t keyMatrix_[8]{};       // 8 half-rows; bits 4-0 active-low (0=pressed)

    int  clockCnt_  = 0;
    int  frameCount_= 0;
    bool flashState_= false;

    void renderFrame();

    // ZX Spectrum 16-colour RGBA palette (normals + bright)
    static const uint8_t kR[16];
    static const uint8_t kG[16];
    static const uint8_t kB[16];
};
