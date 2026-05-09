#include "EpyxFastLoad.h"
#include <imgui.h>
#include <fstream>

static constexpr size_t kRomSize = 8192;

// ---------------------------------------------------------------------------
// Capacitor helpers
// ---------------------------------------------------------------------------

void EpyxFastLoad::armCapacitor() const {
    romEnabled_         = true;
    cyclesUntilDisable_ = kCapacitorCycles;
}

// ---------------------------------------------------------------------------
// IBusDevice
// ---------------------------------------------------------------------------

void EpyxFastLoad::reset() {
    armCapacitor();  // ROM re-enables on CPU reset
}

void EpyxFastLoad::clock() {
    if (romEnabled_ && --cyclesUntilDisable_ <= 0)
        romEnabled_ = false;
}

uint8_t EpyxFastLoad::read(uint16_t offset) const {
    if (romEnabled_ && !rom_.empty()) {
        armCapacitor();
        return rom_[offset & 0x1FFF];
    }
    // Capacitor discharged — ROM deselected; fall through to underlying RAM.
    // On real hardware the bus sees RAM at $8000-$9FFF when the cartridge ROM
    // chip's /CS is deasserted.
    return ram_ ? ram_->read(static_cast<uint16_t>(0x8000 + offset)) : 0xFF;
}

// ---------------------------------------------------------------------------
// IO1 / IO2 (called by C64IOSpace)
// ---------------------------------------------------------------------------

void EpyxFastLoad::triggerIO1Read() {
    if (!rom_.empty()) armCapacitor();
}

uint8_t EpyxFastLoad::readIO2(uint8_t addrLow) const {
    if (rom_.size() < kRomSize) return 0xFF;
    return rom_[0x1F00 | addrLow];  // last 256 bytes, no capacitor gating
}

// ---------------------------------------------------------------------------
// IPeripheral
// ---------------------------------------------------------------------------

bool EpyxFastLoad::mount(const std::string& path) {
    mountError_.clear();

    std::ifstream f(path, std::ios::binary);
    if (!f) { mountError_ = "Cannot open: " + path; return false; }

    f.seekg(0, std::ios::end);
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);

    if (sz != kRomSize) {
        mountError_ = "Expected 8192 bytes, got " + std::to_string(sz);
        return false;
    }

    rom_.resize(kRomSize);
    f.read(reinterpret_cast<char*>(rom_.data()), kRomSize);
    imagePath_ = path;
    armCapacitor();
    return true;
}

void EpyxFastLoad::eject() {
    rom_.clear();
    imagePath_.clear();
    mountError_.clear();
    romEnabled_         = false;
    cyclesUntilDisable_ = 0;
}

// ---------------------------------------------------------------------------
// IHasPanel
// ---------------------------------------------------------------------------

void EpyxFastLoad::drawPanel(const char* title, bool* open) {
    ImGui::SetNextWindowSize({ 320.f, 160.f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    ImGui::TextUnformatted("Epyx FastLoad Cartridge");
    ImGui::Separator();

    if (rom_.empty()) {
        ImGui::TextDisabled("No cartridge mounted");
    } else {
        ImGui::Text("Image   : %s", imagePath_.c_str());
        ImGui::Text("ROM     : %zu bytes", rom_.size());
        ImGui::Text("Enabled : %s", romEnabled_ ? "yes" : "no");
        if (romEnabled_)
            ImGui::Text("Timeout : %d cycles", cyclesUntilDisable_);
    }

    ImGui::End();
}
