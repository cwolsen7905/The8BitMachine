#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// MOS 6581 / 8580 Sound Interface Device  (SID)
//
// Register map ($00–$1C, 29 registers, 4 low address bits NOT used — full
// offset is used here so the bus offset matches the chip's $D400 base):
//
//   Voice 1: $00–$06   Voice 2: $07–$0D   Voice 3: $0E–$14
//     +0  Freq Lo         +0  Freq Lo         +0  Freq Lo
//     +1  Freq Hi         +1  Freq Hi         +1  Freq Hi
//     +2  PW Lo           +2  PW Lo           +2  PW Lo
//     +3  PW Hi (4-bit)   +3  PW Hi (4-bit)   +3  PW Hi (4-bit)
//     +4  Control         +4  Control         +4  Control
//     +5  Attack/Decay    +5  Attack/Decay    +5  Attack/Decay
//     +6  Sustain/Release +6  Sustain/Release +6  Sustain/Release
//
//   $15  Filter Cutoff Lo (3-bit)
//   $16  Filter Cutoff Hi
//   $17  Filter Res/Route
//   $18  Mode/Volume
//   $19  PotX  (read-only, stub → $FF)
//   $1A  PotY  (read-only, stub → $FF)
//   $1B  OSC3  (read-only, stub → $00)
//   $1C  ENV3  (read-only, stub → $00)
//
// Control register bits (per-voice):
//   bit 0 = GATE, 1 = SYNC, 2 = RING, 3 = TEST
//   bit 4 = TRI,  5 = SAW,  6 = PULSE, 7 = NOISE
// ---------------------------------------------------------------------------

class SID6581 : public IBusDevice {
public:
    SID6581() { reset(); }

    const char* deviceName() const override { return "MOS 6581 SID"; }
    void        reset()            override;
    void        clock()            override {}   // synthesis in a future step
    uint8_t     read (uint16_t offset) const override;
    void        write(uint16_t offset, uint8_t value) override;

    std::string statusLine() const override {
        std::ostringstream s;
        const uint8_t vol  = regs_[REG_MODE_VOL] & 0x0F;
        const uint16_t fc  = (static_cast<uint16_t>(regs_[REG_FC_HI]) << 3)
                           | (regs_[REG_FC_LO] & 0x07);
        const uint16_t f1  = static_cast<uint16_t>(regs_[1]) << 8 | regs_[0];
        s << "VOL=" << (unsigned)vol
          << " FC=$" << std::uppercase << std::hex << std::setfill('0')
          << std::setw(3) << fc
          << " V1F=$" << std::setw(4) << f1;
        return s.str();
    }

    // -----------------------------------------------------------------------
    // Register offsets
    // -----------------------------------------------------------------------
    // Voice 1
    static constexpr uint8_t V1_FREQ_LO = 0x00;
    static constexpr uint8_t V1_FREQ_HI = 0x01;
    static constexpr uint8_t V1_PW_LO   = 0x02;
    static constexpr uint8_t V1_PW_HI   = 0x03;
    static constexpr uint8_t V1_CTRL    = 0x04;
    static constexpr uint8_t V1_AD      = 0x05;
    static constexpr uint8_t V1_SR      = 0x06;
    // Voice 2
    static constexpr uint8_t V2_FREQ_LO = 0x07;
    static constexpr uint8_t V2_FREQ_HI = 0x08;
    static constexpr uint8_t V2_PW_LO   = 0x09;
    static constexpr uint8_t V2_PW_HI   = 0x0A;
    static constexpr uint8_t V2_CTRL    = 0x0B;
    static constexpr uint8_t V2_AD      = 0x0C;
    static constexpr uint8_t V2_SR      = 0x0D;
    // Voice 3
    static constexpr uint8_t V3_FREQ_LO = 0x0E;
    static constexpr uint8_t V3_FREQ_HI = 0x0F;
    static constexpr uint8_t V3_PW_LO   = 0x10;
    static constexpr uint8_t V3_PW_HI   = 0x11;
    static constexpr uint8_t V3_CTRL    = 0x12;
    static constexpr uint8_t V3_AD      = 0x13;
    static constexpr uint8_t V3_SR      = 0x14;
    // Filter / global
    static constexpr uint8_t REG_FC_LO      = 0x15;
    static constexpr uint8_t REG_FC_HI      = 0x16;
    static constexpr uint8_t REG_RES_FILT   = 0x17;
    static constexpr uint8_t REG_MODE_VOL   = 0x18;
    // Read-only
    static constexpr uint8_t REG_POT_X      = 0x19;
    static constexpr uint8_t REG_POT_Y      = 0x1A;
    static constexpr uint8_t REG_OSC3       = 0x1B;
    static constexpr uint8_t REG_ENV3       = 0x1C;

    static constexpr int NUM_REGS = 0x1D;

private:
    uint8_t regs_[NUM_REGS] = {};
};
