#include "emulator/devices/AppleIIIO.h"
#include <imgui.h>

void AppleIIIO::reset() {
    keyLatch_ = 0;
    textMode_ = true;
    page2_    = false;
    hiRes_    = false;
    mixed_    = false;
}

void AppleIIIO::pressKey(uint8_t ascii) {
    keyLatch_ = ascii | 0x80;  // bit 7 = strobe
}

// ---------------------------------------------------------------------------
// read — all $C000-$C00F mirror keyboard; $C010-$C01F clear strobe.
// Soft-switch reads ($C050-$C057) activate the switch and return open bus.
// ---------------------------------------------------------------------------
uint8_t AppleIIIO::read(uint16_t addr) const {
    if (addr <= 0x0F)                    // $C000-$C00F: keyboard latch
        return keyLatch_;
    if (addr >= 0x10 && addr <= 0x1F) { // $C010-$C01F: clear strobe
        keyLatch_ &= 0x7F;
        return keyLatch_;
    }
    if (addr >= 0x50 && addr <= 0x57)   // $C050-$C057: soft switches (read activates)
        const_cast<AppleIIIO*>(this)->applySoftSwitch(addr);
    return 0xFF;
}

// ---------------------------------------------------------------------------
// write — soft switches are activated on write too; keyboard latch is read-only.
// ---------------------------------------------------------------------------
void AppleIIIO::write(uint16_t addr, uint8_t /*val*/) {
    if (addr >= 0x50 && addr <= 0x57)
        applySoftSwitch(addr);
}

// ---------------------------------------------------------------------------
// applySoftSwitch — update internal state and fire callbacks
// ---------------------------------------------------------------------------
void AppleIIIO::applySoftSwitch(uint16_t addr) {
    switch (addr) {
        case 0x50: textMode_ = false; if (onTextMode) onTextMode(false); break; // GRAPHICS
        case 0x51: textMode_ = true;  if (onTextMode) onTextMode(true);  break; // TEXT
        case 0x52: mixed_    = false; if (onMixed)    onMixed(false);    break; // FULLSCR
        case 0x53: mixed_    = true;  if (onMixed)    onMixed(true);     break; // MIXED
        case 0x54: page2_    = false; if (onPage2)    onPage2(false);    break; // PAGE1
        case 0x55: page2_    = true;  if (onPage2)    onPage2(true);     break; // PAGE2
        case 0x56: hiRes_    = false; if (onHiRes)    onHiRes(false);    break; // LORES
        case 0x57: hiRes_    = true;  if (onHiRes)    onHiRes(true);     break; // HIRES
        default:   break;
    }
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

void AppleIIIO::drawPanel(const char* title, bool* open) {
    ImGui::SetNextWindowSize({ 360.0f, 200.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader("Keyboard", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool strobe = (keyLatch_ & 0x80) != 0;
        ImGui::Text("Latch:  $%02X  ('%c')",
            keyLatch_ & 0x7F,
            (keyLatch_ & 0x7F) >= 0x20 ? (keyLatch_ & 0x7F) : '.');
        ImGui::Text("Strobe: %s", strobe ? "SET" : "clear");
    }

    if (ImGui::CollapsingHeader("Soft Switches", ImGuiTreeNodeFlags_DefaultOpen)) {
        // TEXT / GRAPHICS
        if (ImGui::RadioButton("TEXT",     textMode_))  applySoftSwitch(0x51);
        ImGui::SameLine();
        if (ImGui::RadioButton("GRAPHICS", !textMode_)) applySoftSwitch(0x50);

        // LO-RES / HI-RES (only meaningful in graphics mode, but always toggleable)
        ImGui::BeginDisabled(textMode_);
        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::RadioButton("LO-RES", !hiRes_)) applySoftSwitch(0x56);
        ImGui::SameLine();
        if (ImGui::RadioButton("HI-RES",  hiRes_)) applySoftSwitch(0x57);
        ImGui::EndDisabled();

        // PAGE 1 / PAGE 2
        if (ImGui::RadioButton("PAGE 1", !page2_)) applySoftSwitch(0x54);
        ImGui::SameLine();
        if (ImGui::RadioButton("PAGE 2",  page2_)) applySoftSwitch(0x55);

        // MIXED
        ImGui::SameLine(0.0f, 16.0f);
        bool mixed = mixed_;
        if (ImGui::Checkbox("MIXED", &mixed)) applySoftSwitch(mixed ? 0x53 : 0x52);
    }

    ImGui::End();
}

std::string AppleIIIO::statusLine() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "KBD $%02X  %s  PAGE%d",
        keyLatch_ & 0x7F,
        textMode_ ? "TEXT" : (hiRes_ ? "HIRES" : "LORES"),
        page2_ ? 2 : 1);
    return buf;
}
