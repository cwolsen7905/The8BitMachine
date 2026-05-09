#include "emulator/devices/DiskII.h"
#include <algorithm>
#include <fstream>
#include <imgui.h>

// ---------------------------------------------------------------------------
void DiskII::reset() {
    phases_    = 0;
    halfTrack_ = 0;
    lastPhase_ = -1;
    motorOn_   = false;
    q6_        = false;
    q7_        = false;
    nibPos_    = 0;
}

// ---------------------------------------------------------------------------
// Stepper motor — called when a phase turns ON.
// Adjacent phase in the sequence → move one half-track.
// ---------------------------------------------------------------------------
void DiskII::stepMotor(int phase) const {
    if (lastPhase_ < 0) {
        lastPhase_ = phase;
        return;
    }
    int delta = (phase - lastPhase_ + 4) % 4;
    if (delta == 1)
        halfTrack_ = std::min(halfTrack_ + 1, 2 * (kTracks - 1));
    else if (delta == 3)
        halfTrack_ = std::max(halfTrack_ - 1, 0);
    lastPhase_ = phase;
}

// ---------------------------------------------------------------------------
// Apply the side effect of accessing a soft switch (shared by read/write).
// ---------------------------------------------------------------------------
void DiskII::activateSwitch(uint16_t offset) const {
    switch (offset) {
        // Phase stepper (even = off, odd = on)
        case 0x01: stepMotor(0); break;
        case 0x03: stepMotor(1); break;
        case 0x05: stepMotor(2); break;
        case 0x07: stepMotor(3); break;
        case 0x00: case 0x02: case 0x04: case 0x06: break; // phase off — no step needed

        case 0x08: motorOn_ = false; break;
        case 0x09: motorOn_ = true;  break;

        // Drive select — only drive 1 supported; drive 2 effectively disables reading
        case 0x0A: break;  // drive 1 selected (default)
        case 0x0B: break;  // drive 2 selected (unimplemented)

        case 0x0C: q6_ = false; break;
        case 0x0D: q6_ = true;  break;
        case 0x0E: q7_ = false; break;
        case 0x0F: q7_ = true;  break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
uint8_t DiskII::read(uint16_t offset) const {
    activateSwitch(offset);

    // $C0EC (Q6L): data latch — return next nibble when motor is spinning
    if (offset == 0x0C && motorOn_ && !q6_ && !q7_ && loaded_) {
        int track = std::min(halfTrack_ / 2, kTracks - 1);
        const auto& trk = nibs_[track];
        if (!trk.empty())
            return trk[nibPos_++ % trk.size()];
    }

    // $C0ED (Q6H): write-protect sense — bit 7 = 1 means write-protected
    if (offset == 0x0D && !q7_)
        return 0x80;  // always report write-protected

    return 0xFF;
}

// ---------------------------------------------------------------------------
void DiskII::write(uint16_t offset, uint8_t /*val*/) {
    activateSwitch(offset);
}

// ---------------------------------------------------------------------------
bool DiskII::mount(const std::string& path) {
    mountError_.clear();

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { mountError_ = "Cannot open: " + path; return false; }

    auto size = static_cast<size_t>(f.tellg());
    if (size != 35 * 16 * 256) {
        mountError_ = "Not a valid .dsk/.do image (expected 143360 bytes)";
        return false;
    }

    f.seekg(0);
    std::vector<uint8_t> dsk(size);
    f.read(reinterpret_cast<char*>(dsk.data()), static_cast<std::streamsize>(size));
    if (!f) { mountError_ = "Read error: " + path; return false; }

    buildNibTracks(dsk);
    imagePath_ = path;
    loaded_    = true;
    nibPos_    = 0;
    return true;
}

// ---------------------------------------------------------------------------
void DiskII::eject() {
    loaded_    = false;
    nibPos_    = 0;
    imagePath_.clear();
    mountError_.clear();
    for (auto& t : nibs_) t.clear();
}

// ---------------------------------------------------------------------------
void DiskII::buildNibTracks(const std::vector<uint8_t>& dsk) {
    for (int t = 0; t < kTracks; t++)
        encodeTrack(t, dsk.data() + t * 16 * 256);
}

// ---------------------------------------------------------------------------
// Encode one track of 16 sectors into a nibble stream using 6-and-2 GCR.
//
// Format per sector:
//   5 self-sync bytes | D5 AA 96 [4&4 addr] DE AA EB |
//   5 self-sync bytes | D5 AA AD [6&2 data 343 bytes] DE AA EB | gap
// ---------------------------------------------------------------------------
void DiskII::encodeTrack(int trackNum, const uint8_t* sectors16) {
    auto& track = nibs_[trackNum];
    track.clear();
    track.reserve(6656);  // typical Apple II track size

    auto emit = [&](uint8_t b) { track.push_back(b); };

    auto selfSync = [&](int n) {
        for (int i = 0; i < n; i++) emit(0xFF);
    };

    // 4-and-4 encoding of one byte → two disk bytes
    auto enc44 = [&](uint8_t b) {
        emit((b >> 1) | 0xAA);
        emit(b | 0xAA);
    };

    constexpr uint8_t kVol = 0xFE;  // DOS 3.3 volume byte

    for (int phys = 0; phys < 16; phys++) {
        int logical = kInterleave[phys];
        const uint8_t* sec = sectors16 + logical * 256;

        // ---- Address field ----
        selfSync(5);
        emit(0xD5); emit(0xAA); emit(0x96);

        uint8_t achk = kVol ^ static_cast<uint8_t>(trackNum) ^ static_cast<uint8_t>(phys);
        enc44(kVol);
        enc44(static_cast<uint8_t>(trackNum));
        enc44(static_cast<uint8_t>(phys));
        enc44(achk);

        emit(0xDE); emit(0xAA); emit(0xEB);

        // ---- Data field ----
        selfSync(5);
        emit(0xD5); emit(0xAA); emit(0xAD);

        // Build 86 aux bytes from low 2 bits of 3 groups of sector bytes.
        // Bits within each byte are reversed (bit0 ↔ bit1) per Apple II spec.
        uint8_t aux[86] = {};
        for (int i = 0; i < 86; i++) {
            auto rev2 = [](uint8_t v) -> uint8_t {
                return static_cast<uint8_t>(((v & 1) << 1) | (v >> 1));
            };
            uint8_t lo0 = rev2(sec[i]       & 0x03);
            uint8_t lo1 = rev2(sec[i + 86]  & 0x03);
            uint8_t lo2 = (i + 172 < 256) ? rev2(sec[i + 172] & 0x03) : 0;
            aux[i] = lo0 | static_cast<uint8_t>(lo1 << 2) | static_cast<uint8_t>(lo2 << 4);
        }

        // XOR-differential encode: emit GCR62[raw ^ prev]; prev = raw.
        // Aux bytes written in reverse order, then primary bytes, then checksum.
        uint8_t prev = 0;
        for (int i = 85; i >= 0; i--) {
            emit(kGCR62[(aux[i] ^ prev) & 0x3F]);
            prev = aux[i];
        }
        for (int i = 0; i < 256; i++) {
            uint8_t raw = sec[i] >> 2;
            emit(kGCR62[(raw ^ prev) & 0x3F]);
            prev = raw;
        }
        emit(kGCR62[prev & 0x3F]);  // checksum byte

        emit(0xDE); emit(0xAA); emit(0xEB);
    }
}

// ---------------------------------------------------------------------------
void DiskII::drawPanel(const char* title, bool* open) {
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    ImGui::Text("Image  : %s", imagePath_.empty() ? "(none)" : imagePath_.c_str());
    ImGui::Text("Motor  : %s", motorOn_ ? "ON" : "off");
    ImGui::Text("Track  : %d (half %d)", halfTrack_ / 2, halfTrack_);
    ImGui::Text("NibPos : %d", nibPos_);
    ImGui::Text("Q6/Q7  : %d / %d", q6_ ? 1 : 0, q7_ ? 1 : 0);

    if (!mountError_.empty()) {
        ImGui::Separator();
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", mountError_.c_str());
    }

    ImGui::End();
}
