#pragma once

#include "emulator/core/IBusDevice.h"
#include "emulator/core/IHasPanel.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
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
//
// Chip model:
//   The 6581 (NMOS) and 8580 (HMOS-II) differ substantially in their filter.
//   The 8580 cutoff is linear over 0–12.5 kHz; the 6581 cutoff spans only
//   ~220 Hz–7.5 kHz with a pronounced knee, which is why 6581 tunes sound
//   much darker.  setModel() selects between them; the choice is persisted in
//   the machine config as "sid_model".
// ---------------------------------------------------------------------------

class SID6581 : public IBusDevice, public IHasPanel {
public:
    // Chip revision — selects the filter cutoff curve (see header comment).
    enum class Model : uint8_t { MOS6581 = 0, MOS8580 = 1 };

    SID6581() { reset(); }

    void  setModel(Model m) { std::lock_guard<std::mutex> lock(mutex_); model_ = m; }
    Model model() const     { std::lock_guard<std::mutex> lock(mutex_); return model_; }

    // Config-file spelling of the model ("6581" / "8580").
    const char* modelName() const {
        return model() == Model::MOS8580 ? "8580" : "6581";
    }
    void setModelByName(const std::string& n) {
        setModel(n == "8580" ? Model::MOS8580 : Model::MOS6581);
    }

    // Cutoff register (0–2047) → Hz for the given model.  Exposed so the panel
    // can show the real frequency instead of only the raw register value.
    static float cutoffHz(uint16_t fcReg, Model m);

    // Resonance nibble (0–15) → filter damping (1/Q).
    static float dampingForRes(uint8_t res);

    // IBusDevice
    const char* deviceName() const override {
        return model() == Model::MOS8580 ? "MOS 8580 SID" : "MOS 6581 SID";
    }
    void        reset()            override;
    void        clock()            override {}
    uint8_t     read (uint16_t offset) const override;
    void        write(uint16_t offset, uint8_t value) override;

    // IHasPanel
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

    // Noise shift-register seeds.  These deliberately differ: reSID resets the
    // register to 0x7FFFFE at power-on, whereas holding the TEST bit steadily
    // fills it with ones, so TEST converges on 0x7FFFFF.
    static constexpr uint32_t kLfsrReset    = 0x7FFFFE;
    static constexpr uint32_t kLfsrTestFill = 0x7FFFFF;

    struct Voice {
        uint32_t phase    = 0;
        uint32_t lfsr     = kLfsrReset;
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

    mutable std::mutex mutex_;  // protects regs_[], mutedVoice_[] and model_
    bool mutedVoice_[3] = {};

    // Chip revision.  Not cleared by reset() — it is a hardware property of the
    // machine, not runtime state, and is restored from the machine config.
    Model model_ = Model::MOS6581;

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
