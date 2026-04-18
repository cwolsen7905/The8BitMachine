#pragma once

#include "emulator/core/IBusDevice.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// MOS 6581 / 8580 Sound Interface Device  (SID)
//
// Register map ($00–$1C, 29 registers):
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
//   $17  Res/Filt  — bits 7–4: resonance, bits 3–0: route voices through filter
//   $18  Mode/Vol  — bits 6–4: HP/BP/LP, bit 7: 3OFF, bits 3–0: master volume
//   $19  PotX  (read-only, stub → $FF)
//   $1A  PotY  (read-only, stub → $FF)
//   $1B  OSC3  (read-only — voice 3 waveform high byte)
//   $1C  ENV3  (read-only — voice 3 envelope level)
//
// Control register bits (per-voice):
//   bit 0 = GATE, 1 = SYNC, 2 = RING, 3 = TEST
//   bit 4 = TRI,  5 = SAW,  6 = PULSE, 7 = NOISE
//
// Synthesis:
//   - 24-bit phase accumulator per voice (wraps at 2^24)
//   - Hard sync: voice N resets when voice (N-1)'s phase wraps
//   - Ring mod: when RING+TRI set, triangle fold uses XOR with source MSB
//   - Chamberlin state-variable filter: LP, BP, HP modes selectable
//   - 23-bit noise LFSR with correct 6581 tap positions
// ---------------------------------------------------------------------------

class SID6581 : public IBusDevice {
public:
    SID6581() { reset(); }

    const char* deviceName() const override { return "MOS 6581 SID"; }
    void        reset()            override;
    void        clock()            override {}
    uint8_t     read (uint16_t offset) const override;
    void        write(uint16_t offset, uint8_t value) override;

    bool        hasPanel()  const override { return true; }
    void        drawPanel(const char* title, bool* open) override;
    std::string statusLine() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream s;
        const uint8_t  vol = regs_[REG_MODE_VOL] & 0x0F;
        const uint16_t fc  = (static_cast<uint16_t>(regs_[REG_FC_HI]) << 3)
                           | (regs_[REG_FC_LO] & 0x07);
        const uint16_t f1  = static_cast<uint16_t>(regs_[V1_FREQ_HI]) << 8
                           | regs_[V1_FREQ_LO];
        s << "VOL=" << (unsigned)vol
          << " FC=$" << std::uppercase << std::hex << std::setfill('0')
          << std::setw(3) << fc
          << " V1F=$" << std::setw(4) << f1;
        return s.str();
    }

    void generateSamples(float* out, int frames, float sampleRate);

    // -----------------------------------------------------------------------
    // Register offsets
    // -----------------------------------------------------------------------
    static constexpr uint8_t V1_FREQ_LO = 0x00;
    static constexpr uint8_t V1_FREQ_HI = 0x01;
    static constexpr uint8_t V1_PW_LO   = 0x02;
    static constexpr uint8_t V1_PW_HI   = 0x03;
    static constexpr uint8_t V1_CTRL    = 0x04;
    static constexpr uint8_t V1_AD      = 0x05;
    static constexpr uint8_t V1_SR      = 0x06;

    static constexpr uint8_t V2_FREQ_LO = 0x07;
    static constexpr uint8_t V2_FREQ_HI = 0x08;
    static constexpr uint8_t V2_PW_LO   = 0x09;
    static constexpr uint8_t V2_PW_HI   = 0x0A;
    static constexpr uint8_t V2_CTRL    = 0x0B;
    static constexpr uint8_t V2_AD      = 0x0C;
    static constexpr uint8_t V2_SR      = 0x0D;

    static constexpr uint8_t V3_FREQ_LO = 0x0E;
    static constexpr uint8_t V3_FREQ_HI = 0x0F;
    static constexpr uint8_t V3_PW_LO   = 0x10;
    static constexpr uint8_t V3_PW_HI   = 0x11;
    static constexpr uint8_t V3_CTRL    = 0x12;
    static constexpr uint8_t V3_AD      = 0x13;
    static constexpr uint8_t V3_SR      = 0x14;

    static constexpr uint8_t REG_FC_LO    = 0x15;
    static constexpr uint8_t REG_FC_HI    = 0x16;
    static constexpr uint8_t REG_RES_FILT = 0x17;
    static constexpr uint8_t REG_MODE_VOL = 0x18;
    static constexpr uint8_t REG_POT_X    = 0x19;
    static constexpr uint8_t REG_POT_Y    = 0x1A;
    static constexpr uint8_t REG_OSC3     = 0x1B;
    static constexpr uint8_t REG_ENV3     = 0x1C;

    static constexpr int NUM_REGS = 0x1D;

private:
    enum : uint8_t { ENV_ATK=0, ENV_DEC=1, ENV_SUS=2, ENV_REL=3, ENV_OFF=4 };

    struct Voice {
        uint32_t phase    = 0;
        uint32_t lfsr     = 0x7FFFF8;
        float    envLevel = 0.0f;
        uint8_t  envStage = ENV_OFF;
        bool     prevGate = false;
    };

    uint8_t regs_[NUM_REGS] = {};
    Voice   osc_[3];

    // Chamberlin SVF state (single filter shared across all routed voices)
    float filterLP_ = 0.0f;
    float filterBP_ = 0.0f;

    // OSC3 / ENV3 read-back (written by audio thread, read by emulator thread;
    // 8-bit writes are atomic on all supported platforms so no lock needed)
    std::atomic<uint8_t> osc3Out_{0};
    std::atomic<uint8_t> env3Out_{0};

    mutable std::mutex mutex_;  // protects regs_[] and mutedVoice_[]
    bool mutedVoice_[3] = {};

    static const float kAttackMs[16];
    static const float kDecRelMs[16];

    static constexpr double kSidClock = 985248.0;

    // Waveform + envelope for one voice.
    // Phase advance and sync are handled externally in generateSamples.
    // prevPhase: phase before this sample's advance (for LFSR clocking).
    // srcPhase:  source oscillator phase before advance (for ring mod).
    float synthVoice(int v, uint8_t ctrl, uint16_t pw,
                     uint8_t ad, uint8_t sr, float sampleRate,
                     uint32_t prevPhase, uint32_t srcPhase);
};
