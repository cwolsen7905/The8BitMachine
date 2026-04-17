#include "emulator/devices/SID6581.h"
#include <algorithm>
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
    filterLP_ = 0.0f;
    filterBP_ = 0.0f;
    osc3Out_.store(0);
    env3Out_.store(0);
}

uint8_t SID6581::read(uint16_t offset) const {
    if (offset >= NUM_REGS) return 0xFF;
    switch (offset) {
        case REG_POT_X: return 0xFF;
        case REG_POT_Y: return 0xFF;
        case REG_OSC3:  return osc3Out_.load();
        case REG_ENV3:  return env3Out_.load();
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
// synthVoice — waveform generation + ADSR for one voice.
// Phase advance is done externally; prevPhase is the phase before advance,
// srcPhase is the source oscillator's phase before advance (for ring mod).
// ---------------------------------------------------------------------------

float SID6581::synthVoice(int v, uint8_t ctrl, uint16_t pw,
                           uint8_t ad, uint8_t sr, float sampleRate,
                           uint32_t prevPhase, uint32_t srcPhase)
{
    Voice& osc = osc_[v];
    uint32_t p = osc.phase;

    // --- Gate edge detection ---
    bool gate = ctrl & 0x01;
    if (gate && !osc.prevGate) osc.envStage = ENV_ATK;
    if (!gate && osc.prevGate) osc.envStage = ENV_REL;
    osc.prevGate = gate;

    // --- Waveform generation ---
    float waveOut = 0.0f;
    int   waveCnt = 0;

    if (ctrl & 0x10) {
        // Triangle with optional ring modulation.
        // RING: XOR the fold bit with the source oscillator MSB, creating AM.
        uint32_t foldBit = (ctrl & 0x04) ? ((p ^ srcPhase) & 0x800000)
                                          : (p & 0x800000);
        uint32_t tri = foldBit ? (~p & 0xFFFFFF) : p;
        waveOut += tri / static_cast<float>(1 << 23) - 1.0f;
        ++waveCnt;
    }
    if (ctrl & 0x20) {
        waveOut += p / static_cast<float>(1 << 23) - 1.0f;
        ++waveCnt;
    }
    if (ctrl & 0x40) {
        uint32_t pwThresh = static_cast<uint32_t>(pw) << 12;
        waveOut += (p >= pwThresh) ? 1.0f : -1.0f;
        ++waveCnt;
    }
    if (ctrl & 0x80) {
        // Noise: clock LFSR when phase bit 19 transitions 0→1
        bool curBit  = (p         >> 19) & 1;
        bool prevBit = (prevPhase >> 19) & 1;
        if (curBit && !prevBit) {
            uint32_t fb = ((osc.lfsr >> 22) ^ (osc.lfsr >> 17)) & 1;
            osc.lfsr = ((osc.lfsr << 1) & 0x7FFFFF) | fb;
        }
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

    if (waveCnt == 0) waveOut = 0.0f;
    else if (waveCnt > 1) waveOut /= static_cast<float>(waveCnt);

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
    uint8_t regs[NUM_REGS];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::memcpy(regs, regs_, NUM_REGS);
    }

    // --- Global registers ---
    float   masterVol = (regs[REG_MODE_VOL] & 0x0F) / 15.0f;
    uint8_t filtMode  =  regs[REG_MODE_VOL] >> 4;   // bits: 7=3OFF 6=HP 5=BP 4=LP
    uint8_t filtRoute =  regs[REG_RES_FILT] & 0x0F; // bits 0-2: FILT1/2/3
    uint8_t res       = (regs[REG_RES_FILT] >> 4) & 0x0F;

    // Chamberlin SVF coefficients (computed once per buffer)
    uint16_t fcReg = (static_cast<uint16_t>(regs[REG_FC_HI]) << 3)
                   | (regs[REG_FC_LO] & 0x07);
    float fcHz = 30.0f + fcReg * (12000.0f - 30.0f) / 2047.0f;
    float F    = 2.0f * std::sin(static_cast<float>(M_PI) * fcHz / sampleRate);
    F = std::min(F, 1.5f);  // clamp for filter stability

    // damping: res=0 → 2.0 (no resonance), res=15 → 0.1 (sharp peak)
    float damping = 2.0f - res * (1.9f / 15.0f);

    // Voice register bases
    static constexpr int    kBase[3]    = { 0, 7, 14 };
    // Hard sync and ring mod: voice N references voice (N+2)%3 as source
    static constexpr int    kSyncSrc[3] = { 2, 0, 1 };

    for (int i = 0; i < frames; ++i) {

        // ---- Step 1: compute phase increments for all voices ----
        uint32_t prev[3], inc[3];
        for (int v = 0; v < 3; ++v) {
            prev[v] = osc_[v].phase;
            int b = kBase[v];
            uint32_t freq = static_cast<uint32_t>(regs[b+1]) << 8 | regs[b];
            inc[v] = static_cast<uint32_t>(freq * kSidClock / sampleRate);
        }

        // ---- Step 2: hard sync — reset target if source phase wraps ----
        for (int v = 0; v < 3; ++v) {
            if (!(regs[kBase[v]+4] & 0x02)) continue;  // SYNC bit not set
            int src = kSyncSrc[v];
            if ((prev[src] + inc[src]) > 0xFFFFFF)     // source wrapped
                inc[v] = 0;  // target resets to prev[v]=0 effectively on next step
        }

        // ---- Step 3: advance all phases ----
        for (int v = 0; v < 3; ++v) {
            uint8_t ctrl = regs[kBase[v]+4];
            if (ctrl & 0x08) {          // TEST bit: freeze at 0
                osc_[v].phase = 0;
                osc_[v].lfsr  = 0x7FFFFF;
            } else {
                osc_[v].phase = (prev[v] + inc[v]) & 0xFFFFFF;
            }
        }

        // ---- Step 4: waveform + envelope for each voice ----
        float voiceOut[3];
        for (int v = 0; v < 3; ++v) {
            int b = kBase[v];
            uint16_t pw = (static_cast<uint16_t>(regs[b+3] & 0x0F) << 8) | regs[b+2];
            voiceOut[v] = synthVoice(v, regs[b+4], pw,
                                     regs[b+5], regs[b+6], sampleRate,
                                     prev[v], prev[kSyncSrc[v]]);
        }

        // Update OSC3 / ENV3 read-back registers
        osc3Out_.store(static_cast<uint8_t>(osc_[2].phase >> 16));
        env3Out_.store(static_cast<uint8_t>(osc_[2].envLevel * 255.0f));

        // ---- Step 5: filter routing ----
        float filtIn    = 0.0f;
        float directOut = 0.0f;
        for (int v = 0; v < 3; ++v) {
            bool v3off = (v == 2) && (filtMode & 0x08);
            if (filtRoute & (1 << v))
                filtIn += voiceOut[v];
            else if (!v3off)
                directOut += voiceOut[v];
            // voice 3 with 3OFF and no FILT3: silenced
        }

        // ---- Step 6: Chamberlin SVF filter ----
        float hp     = filtIn - filterLP_ - damping * filterBP_;
        filterBP_   += F * hp;
        filterLP_   += F * filterBP_;

        // Clamp filter state to prevent denormals / blowup
        filterLP_ = std::max(-4.0f, std::min(4.0f, filterLP_));
        filterBP_ = std::max(-4.0f, std::min(4.0f, filterBP_));

        float filtOut = 0.0f;
        if (filtMode & 0x01) filtOut += filterLP_;  // LP
        if (filtMode & 0x02) filtOut += filterBP_;  // BP
        if (filtMode & 0x04) filtOut += hp;          // HP

        // ---- Step 7: mix and master volume ----
        out[i] = ((directOut + filtOut) / 3.0f) * masterVol;
    }
}
