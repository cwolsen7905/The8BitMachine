#include "Application.h"
#include "emulator/cpu/Disassembler.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Breakpoints panel
// ---------------------------------------------------------------------------

void Application::drawBreakpoints() {
    ImGui::SetNextWindowSize({ 260.0f, 300.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Breakpoints", &showBreakpoints_)) {
        ImGui::End();
        return;
    }

    // Collect and sort so the list is stable
    std::vector<uint16_t> sorted(breakpoints_.begin(), breakpoints_.end());
    std::sort(sorted.begin(), sorted.end());

    if (ImGui::Button("Clear All") && !breakpoints_.empty())
        breakpoints_.clear();

    ImGui::Separator();

    // List
    ImGui::BeginChild("##bplist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8.0f));
    if (sorted.empty()) {
        ImGui::TextDisabled("No breakpoints set.");
        ImGui::TextDisabled("Click an address in the");
        ImGui::TextDisabled("Disassembler to add one.");
    }
    for (uint16_t addr : sorted) {
        ImGui::PushID(addr);
        char label[8];
        std::snprintf(label, sizeof(label), "$%04X", addr);
        if (ImGui::Selectable(label, false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 28.0f, 0))) {
            disasmViewAddr_ = addr;
            disasmFollowPC_ = false;
            showDisasm_     = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            breakpoints_.erase(addr);
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Add row
    ImGui::SetNextItemWidth(60.0f);
    const bool enter = ImGui::InputText("##bpadd", bpAddInput_, sizeof(bpAddInput_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add") || enter) && bpAddInput_[0] != '\0') {
        breakpoints_.insert(static_cast<uint16_t>(std::stoul(bpAddInput_, nullptr, 16)));
        bpAddInput_[0] = '\0';
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Watchpoints panel
// ---------------------------------------------------------------------------

void Application::drawWatchpoints() {
    ImGui::SetNextWindowSize({ 300.0f, 280.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Watchpoints", &showWatchpoints_)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear All") && !watchpoints_.empty())
        watchpoints_.clear();

    ImGui::Separator();

    ImGui::BeginChild("##wplist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8.0f));
    if (watchpoints_.empty()) {
        ImGui::TextDisabled("No watchpoints set.");
        ImGui::TextDisabled("Add an address below to");
        ImGui::TextDisabled("break on read or write.");
    }
    for (int i = 0; i < (int)watchpoints_.size(); ++i) {
        auto& wp = watchpoints_[i];
        ImGui::PushID(i);
        char label[8];
        std::snprintf(label, sizeof(label), "$%04X", wp.addr);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(60.0f);
        ImGui::Checkbox("R", &wp.onRead);
        ImGui::SameLine();
        ImGui::Checkbox("W", &wp.onWrite);
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            watchpoints_.erase(watchpoints_.begin() + i);
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::SetNextItemWidth(60.0f);
    const bool enter = ImGui::InputText("##wpadd", wpAddInput_, sizeof(wpAddInput_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add") || enter) && wpAddInput_[0] != '\0') {
        uint16_t addr = static_cast<uint16_t>(std::stoul(wpAddInput_, nullptr, 16));
        bool exists = false;
        for (const auto& wp : watchpoints_)
            if (wp.addr == addr) { exists = true; break; }
        if (!exists)
            watchpoints_.push_back({ addr, true, true });
        wpAddInput_[0] = '\0';
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Disassembler panel
// ---------------------------------------------------------------------------

void Application::drawDisassembler() {
    ImGui::SetNextWindowSize({ 480.0f, 500.0f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("Disassembler", &showDisasm_);

    ICPU& cpu = machine_.cpu();

    ImGui::Checkbox("Follow PC", &disasmFollowPC_);
    ImGui::SameLine(0.0f, 16.0f);

    ImGui::TextUnformatted("Go to:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(52.0f);
    const bool gotoEnter = ImGui::InputText(
        "##goto", disasmGotoInput_, sizeof(disasmGotoInput_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool gotoClick = ImGui::Button("Go");

    if ((gotoEnter || gotoClick) && disasmGotoInput_[0] != '\0') {
        disasmViewAddr_ = static_cast<uint16_t>(std::stoul(disasmGotoInput_, nullptr, 16));
        disasmFollowPC_ = false;
    }

    ImGui::Separator();

    const uint16_t viewStart = disasmFollowPC_
        ? static_cast<uint16_t>(cpu.getPC() > 40 ? cpu.getPC() - 40 : 0)
        : disasmViewAddr_;

    const auto lines = Disassembler::disassemble(machine_.bus(), viewStart, 60);

    constexpr ImGuiTableFlags tflags =
        ImGuiTableFlags_RowBg           |
        ImGuiTableFlags_BordersInnerV   |
        ImGuiTableFlags_ScrollY         |
        ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##disasm", 3, tflags)) {
        ImGui::End();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Addr",        ImGuiTableColumnFlags_WidthFixed,   58.0f);
    ImGui::TableSetupColumn("Bytes",       ImGuiTableColumnFlags_WidthFixed,   76.0f);
    ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const auto& line : lines) {
        const bool isPC = (line.addr == cpu.getPC());
        const bool hasBP = breakpoints_.count(line.addr) > 0;

        ImGui::TableNextRow();

        if (hasBP)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   IM_COL32(90, 20, 20, 255));

        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(line.addr);
        if (ImGui::Selectable("##row", hasBP,
                ImGuiSelectableFlags_SpanAllColumns |
                ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0.0f, ImGui::GetTextLineHeight()))) {
            if (hasBP) breakpoints_.erase(line.addr);
            else        breakpoints_.insert(line.addr);
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PopID();

        if (isPC && hasBP)
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "►$%04X", line.addr);
        else if (isPC)
            ImGui::TextColored({ 0.2f, 1.0f, 0.4f, 1.0f }, "► $%04X", line.addr);
        else if (hasBP)
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "● $%04X", line.addr);
        else
            ImGui::TextDisabled("  $%04X", line.addr);

        ImGui::TableSetColumnIndex(1);
        {
            char byteBuf[12] = {};
            int  pos = 0;
            for (int b = 0; b < line.byteCount; ++b) {
                if (b > 0) byteBuf[pos++] = ' ';
                std::snprintf(byteBuf + pos, sizeof(byteBuf) - pos, "%02X", line.bytes[b]);
                pos += 2;
            }
            if (isPC)
                ImGui::TextColored({ 0.2f, 1.0f, 0.4f, 1.0f }, "%s", byteBuf);
            else
                ImGui::TextDisabled("%s", byteBuf);
        }

        ImGui::TableSetColumnIndex(2);
        if (isPC) {
            ImGui::TextColored({ 1.0f, 1.0f, 0.3f, 1.0f }, "%s", line.mnemonic.c_str());
            if (!line.operand.empty()) {
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextColored({ 0.2f, 1.0f, 0.4f, 1.0f }, "%s", line.operand.c_str());
            }
        } else {
            ImGui::Text("%s", line.mnemonic.c_str());
            if (!line.operand.empty()) {
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("%s", line.operand.c_str());
            }
        }
        {
            auto it = disasmLabels_.find(line.addr);
            if (it != disasmLabels_.end()) {
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::TextDisabled("; %s", it->second.c_str());
            } else if (line.hasTarget) {
                auto tit = disasmLabels_.find(line.targetAddr);
                if (tit != disasmLabels_.end()) {
                    ImGui::SameLine(0.0f, 10.0f);
                    ImGui::TextDisabled("; %s", tit->second.c_str());
                }
            }
        }

        if (isPC && disasmFollowPC_)
            ImGui::SetScrollHereY(0.25f);
    }

    ImGui::EndTable();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Memory viewer panel
// ---------------------------------------------------------------------------

void Application::drawMemoryViewer() {
    if (memColorsDirty_) rebuildMemRegionColors();

    const uint16_t pc = machine_.cpu().getPC();

    ImGui::SetNextWindowSize({ 680.0f, 500.0f }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Viewer", &showMemView_)) {
        ImGui::Checkbox("Follow PC", &memViewFollowPC_);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("PC=$%04X", (unsigned)pc);
        ImGui::SameLine(0.0f, 20.0f);

        // ROM edit toggle
        if (allowRomEdit_)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.25f, 0.0f, 1.0f));
        if (ImGui::SmallButton(allowRomEdit_ ? "ROM: Editable" : "ROM: Read-Only")) {
            allowRomEdit_ = !allowRomEdit_;
            machine_.setRomsWritable(allowRomEdit_);
        }
        if (allowRomEdit_) ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 20.0f);

        // Colour legend
        ImGui::ColorButton("##rom_legend",  {0.39f, 0.16f, 0.0f, 0.9f},
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, {10,10});
        ImGui::SameLine(0.0f, 3.0f); ImGui::TextDisabled("ROM");
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::ColorButton("##io_legend",   {0.0f, 0.12f, 0.39f, 0.9f},
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, {10,10});
        ImGui::SameLine(0.0f, 3.0f); ImGui::TextDisabled("I/O");
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::ColorButton("##pc_legend",   {1.0f, 1.0f, 0.2f, 0.9f},
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, {10,10});
        ImGui::SameLine(0.0f, 3.0f); ImGui::TextDisabled("PC");

        ImGui::Separator();
        if (memViewFollowPC_)
            memEditor_.GotoAddrAndHighlight(pc, pc);
        memEditor_.DrawContents(nullptr, 0x10000);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void Application::rebuildMemRegionColors() {
    std::memset(memRegionColors_, 0, sizeof(memRegionColors_));

    // Fill in reverse priority order so highest-priority device wins
    const auto& devs = machine_.bus().devices();
    for (int i = static_cast<int>(devs.size()) - 1; i >= 0; --i) {
        const auto& entry = devs[i];
        const char* name  = entry.device ? entry.device->deviceName() : nullptr;

        ImU32 color = 0;
        if (!name) {
            color = IM_COL32(0, 30, 100, 90);  // CHAR_OUT sentinel → I/O blue
        } else if (std::strcmp(name, "ROM") == 0) {
            color = IM_COL32(100, 40, 0, 90);  // ROM → amber
        } else if (std::strcmp(name, "RAM") == 0 || std::strcmp(name, "Memory") == 0) {
            color = 0;                          // RAM → no tint
        } else {
            color = IM_COL32(0, 30, 100, 90);  // everything else → I/O blue
        }

        for (uint32_t a = entry.start; a <= entry.end; ++a)
            memRegionColors_[a] = color;
    }

    memColorsDirty_ = false;
}
