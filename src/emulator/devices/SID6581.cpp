#include "emulator/devices/SID6581.h"
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// ADSR timing tables (from MOS 6581 datasheet)
// ---------------------------------------------------------------------------

const float SID6581::kAttackMs[16] = {
      2,    8,   16,   24,   38,   56,   68,   80,
    100,  250,  500,  800, 1000, 3000, 5000, 8000
};

const float SID6581::kDecRelMs[16] = {
      6,   24,   48,   72,  114,  168,  204,  240,
    300,  750, 1500, 2400, 3000, 9000,15000,24000
};

// ---------------------------------------------------------------------------
// IBusDevice
// ---------------------------------------------------------------------------

void SID6581::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memset(regs_, 0, sizeof(regs_));
    for (auto& v : osc_) {
        v.phase    = 0;
        v.lfsr     = 0x7FFFF8;
        v.envLevel = 0.0f;
        v.envStage = ENV_OFF;
        v.prevGate = false;
    }
}

uint8_t SID6581::read(uint16_t offset) const {
    if (offset >= NUM_REGS) return 0xFF;
    switch (offset) {
        case REG_POT_X: return 0xFF;
        case REG_POT_Y: return 0xFF;
        case REG_OSC3:  return 0x00;
        case REG_ENV3:  return 0x00;
        default:        return 0xFF;  // write-only registers
    }
}

void SID6581::write(uint16_t offset, uint8_t value) {
    if (offset < NUM_REGS) {
        std::lock_guard<std::mutex> lock(mutex_);
        regs_[offset] = value;
    }
}

// ---------------------------------------------------------------------------
// Per-voice synthesis helper — advances one audio sample.
// Returns waveform × envelope sample in range [-1, +1].
// ---------------------------------------------------------------------------

float SID6581::synthVoice(int v, uint8_t ctrl, uint32_t freq, uint16_t pw,
                           uint8_t ad, uint8_t sr, float sampleRate)
{
    Voice& osc = osc_[v];

    // --- Gate edge detection ---
    bool gate = (ctrl & 0x01) != 0;
    if (gate && !osc.prevGate) { osc.envStage = ENV_ATK; }
    if (!gate && osc.prevGate) { osc.envStage = ENV_REL; }
    osc.prevGate = gate;

    // --- Phase advance ---
    // phase is 32-bit; top 24 bits are the SID 24-bit oscillator phase.
    uint32_t prevPhase = osc.phase;
    uint32_t phaseInc  = static_cast<uint32_t>(freq * kSidClock / sampleRate);

    if (ctrl & 0x08) {
        // TEST bit: hold phase at 0, freeze LFSR
        osc.phase = 0;
        osc.lfsr  = 0x7FFFFF;
    } else {
        osc.phase += phaseInc;
    }

    uint32_t p = osc.phase >> 8;  // 24-bit SID phase

    // --- Waveform generation ---
    float waveOut = 0.0f;
    int   waveCnt = 0;

    if (ctrl & 0x10) {  // Triangle
        uint32_t tri = (p & 0x800000) ? (~p & 0xFFFFFF) : p;
        waveOut += (tri >> 15) / 127.5f - 1.0f;
        ++waveCnt;
    }
    if (ctrl & 0x20) {  // Sawtooth
        waveOut += (p >> 15) / 127.5f - 1.0f;
        ++waveCnt;
    }
    if (ctrl & 0x40) {  // Pulse
        uint32_t pwThresh = static_cast<uint32_t>(pw) << 12;
        waveOut += (osc.phase >= pwThresh) ? 1.0f : -1.0f;
        ++waveCnt;
    }
    if (ctrl & 0x80) {  // Noise — 23-bit LFSR, clocked when phase bit 19 rises
        bool curBit  = (osc.phase   >> (8 + 19)) & 1;
        bool prevBit = (prevPhase   >> (8 + 19)) & 1;
        if (curBit && !prevBit) {
            uint32_t feedback = ((osc.lfsr >> 22) ^ (osc.lfsr >> 17)) & 1;
            osc.lfsr = ((osc.lfsr << 1) & 0x7FFFFF) | feedback;
        }
        // Output: 8 taps from the LFSR (positions match real 6581)
        uint8_t n = static_cast<uint8_t>(
            ((osc.lfsr >> 22) & 1) << 7 |
            ((osc.lfsr >> 20) & 1) << 6 |
            ((osc.lfsr >> 16) & 1) << 5 |
            ((osc.lfsr >> 13) & 1) << 4 |
            ((osc.lfsr >> 11) & 1) << 3 |
            ((osc.lfsr >>  7) & 1) << 2 |
            ((osc.lfsr >>  5) & 1) << 1 |
            ((osc.lfsr >>  2) & 1));
        waveOut += n / 127.5f - 1.0f;
        ++waveCnt;
    }

    if (waveCnt == 0) return 0.0f;
    if (waveCnt > 1)  waveOut /= static_cast<float>(waveCnt);

    // --- ADSR envelope ---
    uint8_t atkNib = (ad >> 4) & 0x0F;
    uint8_t decNib =  ad       & 0x0F;
    uint8_t susNib = (sr >> 4) & 0x0F;
    uint8_t relNib =  sr       & 0x0F;
    float   susLvl = susNib / 15.0f;

    switch (osc.envStage) {
        case ENV_ATK: {
            float inc = 1.0f / (kAttackMs[atkNib] * 0.001f * sampleRate);
            osc.envLevel += inc;
            if (osc.envLevel >= 1.0f) { osc.envLevel = 1.0f; osc.envStage = ENV_DEC; }
            break;
        }
        case ENV_DEC: {
            float dec = (1.0f - susLvl) / (kDecRelMs[decNib] * 0.001f * sampleRate);
            osc.envLevel -= dec;
            if (osc.envLevel <= susLvl) { osc.envLevel = susLvl; osc.envStage = ENV_SUS; }
            break;
        }
        case ENV_SUS:
            osc.envLevel = susLvl;
            break;
        case ENV_REL: {
            float dec = osc.envLevel / (kDecRelMs[relNib] * 0.001f * sampleRate);
            osc.envLevel -= dec;
            if (osc.envLevel <= 0.001f) { osc.envLevel = 0.0f; osc.envStage = ENV_OFF; }
            break;
        }
        case ENV_OFF:
            osc.envLevel = 0.0f;
            break;
    }

    return waveOut * osc.envLevel;
}

// ---------------------------------------------------------------------------
// generateSamples — called from SDL audio callback (audio thread)
// ---------------------------------------------------------------------------

void SID6581::generateSamples(float* out, int frames, float sampleRate) {
    // Snapshot registers so we don't hold the lock during synthesis
    uint8_t regs[NUM_REGS];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::memcpy(regs, regs_, NUM_REGS);
    }

    float masterVol = (regs[REG_MODE_VOL] & 0x0F) / 15.0f;

    // Per-voice register bases
    static constexpr int base[3] = { 0, 7, 14 };

    for (int i = 0; i < frames; ++i) {
        float mix = 0.0f;
        for (int v = 0; v < 3; ++v) {
            int b = base[v];
            uint32_t freq = static_cast<uint32_t>(regs[b+1]) << 8 | regs[b];
            uint16_t pw   = (static_cast<uint16_t>(regs[b+3] & 0x0F) << 8) | regs[b+2];
            mix += synthVoice(v, regs[b+4], freq, pw, regs[b+5], regs[b+6], sampleRate);
        }
        out[i] = (mix / 3.0f) * masterVol;
    }
}
