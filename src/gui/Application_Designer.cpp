#include "Application.h"
#include "gui/FileDialog.h"

#include <cstring>
#include <fstream>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Machine Designer panel — known devices table (file-scope so helpers can use it)
// ---------------------------------------------------------------------------

namespace {
    struct DesignerKnownDev { const char* id; const char* name; const char* defStart; const char* defEnd; };
    static constexpr DesignerKnownDev kDesignerKnown[] = {
        { "vic",          "VIC-IIe (MOS 6566)",         "D000", "D3FF" },
        { "sid",          "SID (MOS 6581)",              "D400", "D7FF" },
        { "cia1",         "CIA1 (MOS 6526)",             "F100", "F1FF" },
        { "cia2",         "CIA2 (MOS 6526)",             "F200", "F2FF" },
        { "ula",          "ULA (ZX Spectrum)",           "0000", "3FFF" },
        { "c64_io_space", "C64 I/O Space (VIC+SID+CIA)", "D000", "DFFF" },
        { "ram",          "RAM (64 KB flat)",            "0000", "FFFF" },
        { "char_out",     "CHAR_OUT debug port",         "F000", "F000" },
    };
    static constexpr int kDesignerKnownCount = (int)(sizeof(kDesignerKnown) / sizeof(kDesignerKnown[0]));
} // namespace

// ---------------------------------------------------------------------------
// Machine Designer panel
// ---------------------------------------------------------------------------

void Application::drawMachineDesigner() {
    if (designerAddStart_[0] == '\0') {
        std::strncpy(designerAddStart_, kDesignerKnown[0].defStart, 5);
        std::strncpy(designerAddEnd_,   kDesignerKnown[0].defEnd,   5);
    }

    ImGui::SetNextWindowSize({ 560.0f, 440.0f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("Machine Designer", &showDesigner_);

    drawDesignerCpuSection();

    int removeIdx = -1, moveFrom = -1, moveTo = -1;
    drawDesignerDeviceTable(removeIdx, moveFrom, moveTo);
    if (removeIdx >= 0)
        machine_.unmountAt(static_cast<size_t>(removeIdx));
    if (moveFrom >= 0 && moveTo >= 0)
        machine_.bus().moveEntry(static_cast<size_t>(moveFrom), static_cast<size_t>(moveTo));

    drawDesignerContainedDevices();
    drawDesignerAddDevice();
    drawDesignerLoadRom();
    drawDesignerAddBankedRam();
    drawDesignerAddSwitchableRegion();
    drawDesignerAddBankController();

    ImGui::End();
}

// ---------------------------------------------------------------------------

void Application::drawDesignerCpuSection() {
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "CPU");
    ImGui::SameLine(80.0f);
    const char* cpuNames[] = { "MOS 8502", "MOS 6510", "WDC 65C02", "Zilog Z80" };
    const char* currentCPU = machine_.cpu().cpuName();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##cpu", currentCPU)) {
        for (const char* name : cpuNames) {
            const bool selected = (std::strcmp(name, currentCPU) == 0);
            if (ImGui::Selectable(name, selected))
                machine_.selectCPU(name);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();
}

// ---------------------------------------------------------------------------

void Application::drawDesignerDeviceTable(int& removeIdx, int& moveFrom, int& moveTo) {
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Address Space");
    if (ImGui::SmallButton("Sort by Address"))
        machine_.bus().sortByAddress();
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset to Defaults"))
        machine_.resetAddressMap();

    constexpr ImGuiTableFlags tf =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;

    struct RowWarn { uint32_t color = 0; const char* msg = nullptr; };
    const auto& allDevs = machine_.bus().devices();
    std::vector<RowWarn> rowWarns(allDevs.size());
    bool hasCatchAll = false;

    for (int i = 0; i < (int)allDevs.size(); ++i) {
        const auto& e = allDevs[i];
        if (e.start == 0x0000 && e.end == 0xFFFF)
            hasCatchAll = true;
        if (e.start > e.end) {
            rowWarns[i] = { ImGui::GetColorU32(ImVec4(0.85f, 0.2f, 0.2f, 0.35f)),
                            "Invalid range: Start > End" };
            continue;
        }
        for (int j = 0; j < i; ++j) {
            const auto& p = allDevs[j];
            if (p.start <= p.end && p.start <= e.start && p.end >= e.end) {
                rowWarns[i] = { ImGui::GetColorU32(ImVec4(0.85f, 0.55f, 0.1f, 0.35f)),
                                "Unreachable: fully shadowed by a higher-priority entry" };
                break;
            }
        }
    }

    if (ImGui::BeginTable("##devices", 6, tf)) {
        ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthFixed,  16.0f);
        ImGui::TableSetupColumn("Start",  ImGuiTableColumnFlags_WidthFixed,  56.0f);
        ImGui::TableSetupColumn("End",    ImGuiTableColumnFlags_WidthFixed,  56.0f);
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthFixed,  24.0f);
        ImGui::TableHeadersRow();

        if (designerEditRow_ >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape))
            designerEditRow_ = designerEditCol_ = -1;

        for (int i = 0; i < (int)allDevs.size(); ++i) {
            const auto& e = allDevs[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            if (rowWarns[i].color)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowWarns[i].color);

            // col 0: drag handle + warning tooltip
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f,0.4f,0.4f,0.3f));
            ImGui::Selectable("=", false, 0, ImVec2(12.0f, 0.0f));
            ImGui::PopStyleColor(2);
            if (rowWarns[i].msg && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", rowWarns[i].msg);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                ImGui::SetDragDropPayload("BUS_ROW", &i, sizeof(int));
                ImGui::TextUnformatted(e.label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.25f));
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("BUS_ROW")) {
                    moveFrom = *(const int*)pl->Data;
                    moveTo   = i;
                }
                ImGui::EndDragDropTarget();
            }

            // cols 1 & 2: editable address cells (semanticCol: 0=start, 1=end)
            auto addrCell = [&](int tableCol, int semanticCol, uint16_t val) {
                ImGui::TableSetColumnIndex(tableCol);
                if (designerEditRow_ == i && designerEditCol_ == semanticCol) {
                    if (designerEditFocus_) {
                        ImGui::SetKeyboardFocusHere();
                        designerEditFocus_ = false;
                    }
                    char* ep;
                    unsigned long v     = std::strtoul(designerEditBuf_, &ep, 16);
                    const bool validHex = (ep != designerEditBuf_ && v <= 0xFFFF);
                    const uint16_t ns   = (semanticCol == 0 && validHex) ? (uint16_t)v : e.start;
                    const uint16_t ne   = (semanticCol == 1 && validHex) ? (uint16_t)v : e.end;
                    const bool invalid  = !validHex || ns > ne;

                    if (invalid)
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.55f, 0.1f, 0.1f, 1.0f));

                    ImGui::SetNextItemWidth(52.0f);
                    constexpr ImGuiInputTextFlags kEditFlags =
                        ImGuiInputTextFlags_CharsHexadecimal |
                        ImGuiInputTextFlags_CharsUppercase   |
                        ImGuiInputTextFlags_EnterReturnsTrue;
                    const bool tabPressed = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
                    bool entered = ImGui::InputText("##ec", designerEditBuf_, 5, kEditFlags);
                    bool lost    = !entered && ImGui::IsItemDeactivated();
                    const bool commit = entered || (lost && tabPressed);

                    if (invalid) ImGui::PopStyleColor();

                    if (commit) {
                        if (invalid) {
                            designerEditFocus_ = true;
                        } else {
                            if (designerInvalidRow_ == i && designerInvalidCol_ == semanticCol)
                                designerInvalidRow_ = designerInvalidCol_ = -1;
                            machine_.bus().modifyAt((size_t)i, ns, ne);
                            designerEditRow_ = designerEditCol_ = -1;
                        }
                    } else if (lost) {
                        if (invalid) {
                            designerInvalidRow_ = i;
                            designerInvalidCol_ = semanticCol;
                            std::strncpy(designerInvalidBuf_, designerEditBuf_, 5);
                        } else {
                            if (designerInvalidRow_ == i && designerInvalidCol_ == semanticCol)
                                designerInvalidRow_ = designerInvalidCol_ = -1;
                        }
                        designerEditRow_ = designerEditCol_ = -1;
                    }
                } else {
                    const bool hasInvalid = (designerInvalidRow_ == i &&
                                             designerInvalidCol_ == semanticCol);
                    char buf[8];
                    if (hasInvalid)
                        std::snprintf(buf, sizeof(buf), "%s", designerInvalidBuf_);
                    else
                        std::snprintf(buf, sizeof(buf), "$%04X", (unsigned)val);

                    if (hasInvalid)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                        ImVec4(0.26f, 0.59f, 0.98f, 0.25f));
                    if (ImGui::Selectable(buf, false,
                            ImGuiSelectableFlags_None, ImVec2(52.0f, 0.0f))) {
                        designerEditRow_   = i;
                        designerEditCol_   = semanticCol;
                        designerEditFocus_ = true;
                        if (hasInvalid)
                            std::strncpy(designerEditBuf_, designerInvalidBuf_, 5);
                        else
                            std::snprintf(designerEditBuf_, 5, "%04X", (unsigned)val);
                    }
                    ImGui::PopStyleColor();
                    if (hasInvalid) ImGui::PopStyleColor();
                }
            };

            addrCell(1, 0, e.start);
            addrCell(2, 1, e.end);

            // col 3: device label
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(e.label.c_str());

            // col 4: status / live bank selector for SwitchableRegion
            ImGui::TableSetColumnIndex(4);
            auto* sr = dynamic_cast<SwitchableRegion*>(e.device);
            if (sr && sr->optionCount() > 0) {
                ImGui::SetNextItemWidth(-1.0f);
                char comboId[16];
                std::snprintf(comboId, sizeof(comboId), "##sr%d", i);
                const char* activeLabel = sr->activeOption().label.c_str();
                if (ImGui::BeginCombo(comboId, activeLabel)) {
                    for (size_t k = 0; k < sr->optionCount(); ++k) {
                        const bool sel = (k == sr->selectedIndex());
                        if (ImGui::Selectable(sr->options()[k].label.c_str(), sel))
                            sr->select(static_cast<uint8_t>(k));
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else if (e.device) {
                ImGui::TextDisabled("%s", e.device->statusLine().c_str());
            } else {
                ImGui::TextDisabled("—");
            }

            // col 5: remove
            ImGui::TableSetColumnIndex(5);
            if (ImGui::SmallButton("×"))
                removeIdx = i;

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!hasCatchAll)
        ImGui::TextColored({ 1.0f, 0.85f, 0.2f, 1.0f },
            "No catch-all entry ($0000-$FFFF) -- unmapped reads return $FF");
}

// ---------------------------------------------------------------------------

void Application::drawDesignerContainedDevices() {
    std::unordered_set<const IBusDevice*> directBus;
    for (const auto& e : machine_.bus().devices())
        if (e.device) directBus.insert(e.device);

    std::vector<Machine::PanelEntry> indirect;
    for (const auto& pe : machine_.panelDevices())
        if (!directBus.count(pe.device))
            indirect.push_back(pe);

    if (indirect.empty()) return;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Contained Devices");
    ImGui::SameLine();
    ImGui::TextDisabled("(via container — read only)");
    ImGui::Spacing();

    if (ImGui::BeginTable("##indirect", 3,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Device",  ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Status",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& pe : indirect) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", pe.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(pe.device->deviceName());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", pe.device->statusLine().c_str());
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------

void Application::drawDesignerAddDevice() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Add Device");
    ImGui::Spacing();

    const int prevIdx = designerAddDevIdx_;
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##adddev", kDesignerKnown[designerAddDevIdx_].name)) {
        for (int i = 0; i < kDesignerKnownCount; ++i) {
            if (ImGui::Selectable(kDesignerKnown[i].name, designerAddDevIdx_ == i))
                designerAddDevIdx_ = i;
            if (designerAddDevIdx_ == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (designerAddDevIdx_ != prevIdx) {
        std::strncpy(designerAddStart_, kDesignerKnown[designerAddDevIdx_].defStart, 5);
        std::strncpy(designerAddEnd_,   kDesignerKnown[designerAddDevIdx_].defEnd,   5);
        designerAddError_.clear();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##as", designerAddStart_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::TextDisabled("–");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##ae", designerAddEnd_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();

    if (ImGui::Button("Add")) {
        char* ep = nullptr;
        unsigned long s  = std::strtoul(designerAddStart_, &ep, 16);
        unsigned long e2 = std::strtoul(designerAddEnd_,   &ep, 16);
        if (s <= e2 && e2 <= 0xFFFF) {
            const DesignerKnownDev& kd = kDesignerKnown[designerAddDevIdx_];
            IBusDevice* dev = machine_.deviceForId(kd.id);
            char label[64];
            std::snprintf(label, sizeof(label), "%s $%04lX–$%04lX", kd.name, s, e2);
            machine_.bus().addDevice(
                static_cast<uint16_t>(s), static_cast<uint16_t>(e2), dev, label);
            designerAddError_.clear();
        } else {
            designerAddError_ = "Invalid range";
        }
    }

    if (!designerAddError_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", designerAddError_.c_str());
    }
}

// ---------------------------------------------------------------------------

void Application::drawDesignerLoadRom() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Load ROM File");
    ImGui::Spacing();

    if (designerRomStart_[0] == '\0')
        std::strncpy(designerRomStart_, "E000", 5);

    ImGui::TextUnformatted("Start:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##rs", designerRomStart_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();

    if (ImGui::Button("Browse...")) {
        const std::string path = FileDialog::openFile(
            "Select ROM file", { "bin", "prg", "rom" });
        if (!path.empty()) {
            char* ep = nullptr;
            unsigned long s = std::strtoul(designerRomStart_, &ep, 16);
            if (ep == designerRomStart_ || s > 0xFFFF) {
                designerRomMsg_.setErr("Invalid start address");
            } else {
                std::ifstream probe(path, std::ios::binary | std::ios::ate);
                if (!probe) {
                    designerRomMsg_.setErr("Cannot open file");
                } else {
                    size_t fileSize = static_cast<size_t>(probe.tellg());
                    auto ext = path.size() >= 4 ? path.substr(path.size() - 4) : "";
                    for (auto& c : ext)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (ext == ".prg" && fileSize >= 2) fileSize -= 2;

                    if (fileSize == 0) {
                        designerRomMsg_.setErr("File has no data");
                    } else {
                        unsigned long e2 = s + fileSize - 1;
                        if (e2 > 0xFFFF) e2 = 0xFFFF;

                        auto slash = path.find_last_of("/\\");
                        std::string fname = (slash != std::string::npos)
                            ? path.substr(slash + 1) : path;
                        char label[80];
                        std::snprintf(label, sizeof(label), "%s $%04lX–$%04lX",
                                      fname.c_str(), s, e2);

                        ROM* rom = machine_.mountROM(
                            static_cast<uint16_t>(s), static_cast<uint16_t>(e2),
                            label, path);

                        if (rom) {
                            char msg[120];
                            std::snprintf(msg, sizeof(msg),
                                "Loaded %zu bytes at $%04lX–$%04lX",
                                rom->dataSize(), s, e2);
                            designerRomMsg_.setOk(msg);
                        } else {
                            designerRomMsg_.setErr("Failed to load file");
                        }
                    }
                }
            }
        }
    }

    if (!designerRomMsg_.empty()) {
        ImGui::SameLine();
        if (designerRomMsg_.isErr)
            ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", designerRomMsg_.text.c_str());
        else
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", designerRomMsg_.text.c_str());
    }
}

// ---------------------------------------------------------------------------

void Application::drawDesignerAddBankedRam() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Add Banked RAM");
    ImGui::Spacing();

    if (designerBankStart_[0]   == '\0') std::strncpy(designerBankStart_,   "8000", 5);
    if (designerBankEnd_[0]     == '\0') std::strncpy(designerBankEnd_,     "BFFF", 5);
    if (designerBankSelAddr_[0] == '\0') std::strncpy(designerBankSelAddr_, "DFFF", 5);
    if (designerBankCount_[0]   == '\0') std::strncpy(designerBankCount_,   "4",    4);

    ImGui::TextUnformatted("Start:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##bks", designerBankStart_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::TextUnformatted("End:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##bke", designerBankEnd_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::TextUnformatted("BankSel:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##bkp", designerBankSelAddr_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::TextUnformatted("Banks:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(36.0f);
    ImGui::InputText("##bkn", designerBankCount_, 4, ImGuiInputTextFlags_CharsDecimal);

    ImGui::Spacing();
    if (ImGui::Button("Add Banked RAM")) {
        char* ep = nullptr;
        unsigned long s  = std::strtoul(designerBankStart_,   &ep, 16);
        unsigned long e2 = std::strtoul(designerBankEnd_,     &ep, 16);
        unsigned long p  = std::strtoul(designerBankSelAddr_, &ep, 16);
        int           n  = std::atoi(designerBankCount_);

        if (s > 0xFFFF || e2 > 0xFFFF || e2 < s || p > 0xFFFF || n < 1 || n > 256) {
            designerBankMsg_.setErr("Invalid parameters");
        } else {
            BankedMemory* bm = machine_.mountBankedMemory(
                static_cast<uint16_t>(s), static_cast<uint16_t>(e2),
                static_cast<uint16_t>(p), static_cast<uint8_t>(n));
            if (bm) {
                char msg[80];
                std::snprintf(msg, sizeof(msg), "Mounted %d banks × %lu KB at $%04lX–$%04lX",
                              n, (e2 - s + 1) / 1024, s, e2);
                designerBankMsg_.setOk(msg);
            } else {
                designerBankMsg_.setErr("Failed to create banked RAM");
            }
        }
    }

    if (!designerBankMsg_.empty()) {
        ImGui::SameLine();
        if (designerBankMsg_.isErr)
            ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", designerBankMsg_.text.c_str());
        else
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", designerBankMsg_.text.c_str());
    }
}

// ---------------------------------------------------------------------------

void Application::drawDesignerAddSwitchableRegion() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Add Switchable Region");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "A proxy device that forwards reads/writes to one of N child devices.\n"
            "After adding, wire options via JSON config or a machine preset.\n"
            "The Status column shows a live bank-selector dropdown.");
    ImGui::Spacing();

    if (designerSRStart_[0] == '\0') std::strncpy(designerSRStart_, "A000", 5);
    if (designerSREnd_[0]   == '\0') std::strncpy(designerSREnd_,   "BFFF", 5);

    ImGui::TextUnformatted("Start:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##srs", designerSRStart_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::TextUnformatted("End:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##sre", designerSREnd_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

    ImGui::Spacing();
    if (ImGui::Button("Add Switchable Region")) {
        char* ep = nullptr;
        unsigned long s  = std::strtoul(designerSRStart_, &ep, 16);
        unsigned long e2 = std::strtoul(designerSREnd_,   &ep, 16);
        if (s > 0xFFFF || e2 > 0xFFFF || e2 < s) {
            designerSRMsg_.setErr("Invalid range");
        } else {
            char label[32];
            std::snprintf(label, sizeof(label), "SwitchableRegion $%04lX-$%04lX", s, e2);
            machine_.mountSwitchableRegion(
                static_cast<uint16_t>(s), static_cast<uint16_t>(e2), label);
            char msg[48];
            std::snprintf(msg, sizeof(msg), "Added at $%04lX-$%04lX", s, e2);
            designerSRMsg_.setOk(msg);
        }
    }

    if (!designerSRMsg_.empty()) {
        ImGui::SameLine();
        if (designerSRMsg_.isErr)
            ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", designerSRMsg_.text.c_str());
        else
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", designerSRMsg_.text.c_str());
    }
}

// ---------------------------------------------------------------------------

void Application::drawDesignerAddBankController() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Add Bank Controller");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "A 1-byte I/O register.  Writing a value triggers bank switches\n"
            "on connected SwitchableRegions per the mapping table.\n"
            "Wire mappings via JSON config or a machine preset.");
    ImGui::Spacing();

    if (designerBCAddr_[0] == '\0') std::strncpy(designerBCAddr_, "0001", 5);

    ImGui::TextUnformatted("Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::InputText("##bca", designerBCAddr_, 5,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

    ImGui::Spacing();
    if (ImGui::Button("Add Bank Controller")) {
        char* ep = nullptr;
        unsigned long a = std::strtoul(designerBCAddr_, &ep, 16);
        if (ep == designerBCAddr_ || a > 0xFFFF) {
            designerBCMsg_.setErr("Invalid address");
        } else {
            char label[32];
            std::snprintf(label, sizeof(label), "BankController $%04lX", a);
            machine_.mountBankController(static_cast<uint16_t>(a), label);
            char msg[40];
            std::snprintf(msg, sizeof(msg), "Added at $%04lX", a);
            designerBCMsg_.setOk(msg);
        }
    }

    if (!designerBCMsg_.empty()) {
        ImGui::SameLine();
        if (designerBCMsg_.isErr)
            ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", designerBCMsg_.text.c_str());
        else
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", designerBCMsg_.text.c_str());
    }
}

// ---------------------------------------------------------------------------
// Keyboard matrix — injected as a collapsible section into the owning device panel
// ---------------------------------------------------------------------------

void Application::injectC64KeyMatrix(const char* title, bool* open) {
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader("Keyboard Matrix")) {
        static const char* kLabel[8][8] = {
          //  row0      row1    row2    row3    row4    row5    row6    row7
            {"DEL",    "3",    "5",    "7",    "9",    "+",    "\xC2\xA3", "1"   },
            {"RETURN", "W",    "R",    "Y",    "I",    "P",    "*",    "\xE2\x86\x90"},
            {"CUR\xE2\x86\x93", "A", "D", "G", "J", "L",    ";",    "CTRL"},
            {"F7",     "4",    "6",    "8",    "0",    "-",    "HOME", "2"   },
            {"F1",     "Z",    "C",    "B",    "M",    ".",    "^",    "SPACE"},
            {"F3",     "S",    "F",    "H",    "K",    ":",    "=",    "CBM" },
            {"F5",     "E",    "T",    "U",    "O",    "@",    "\xE2\x86\x91", "Q"},
            {"CUR\xE2\x86\x92", "LSHF", "X", "V",    "N",    ",",    "/",    "STOP"},
        };

        {
            bool standard = !keyMatrixTranspose_;
            if (ImGui::RadioButton("Standard C64 KERNAL", standard)) keyMatrixTranspose_ = false;
            ImGui::SameLine();
            if (ImGui::RadioButton("MEGA65 OpenROMs", !standard))    keyMatrixTranspose_ = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("MEGA65 OpenROMs wire PA=rows/PB=cols (transposed vs stock C64).");
        }

        if (lastKeyCol_ >= 0)
            ImGui::Text("Last: %-18s  col %d, row %d  (%s)",
                lastKeyName_.c_str(), lastKeyCol_, lastKeyRow_,
                kLabel[lastKeyCol_][lastKeyRow_]);
        else
            ImGui::TextDisabled("Press a key (with keyboard capture) or click a cell.");

        ImGui::Spacing();
        ImGui::TextDisabled("     ");
        for (int c = 0; c < 8; ++c) { ImGui::SameLine(); ImGui::TextDisabled("  Col%-2d  ", c); }

        const ImVec2 cellSz{ 62.0f, 20.0f };
        for (int row = 0; row < 8; ++row) {
            ImGui::TextDisabled("Row%d ", row);
            for (int col = 0; col < 8; ++col) {
                ImGui::SameLine();
                int kCol = col, kRow = row;
                if (keyMatrixTranspose_) std::swap(kCol, kRow);
                const bool held   = machine_.cia1().keyState(kCol, kRow);
                const bool isLast = (lastKeyCol_ == col && lastKeyRow_ == row);
                ImVec4 bg = held   ? ImVec4(0.1f,0.7f,0.2f,0.6f)
                          : isLast ? ImVec4(0.2f,0.4f,0.8f,0.4f)
                                   : ImVec4(0.15f,0.15f,0.15f,1.0f);
                ImVec4 fg = (held || isLast) ? ImVec4(1,1,1,1) : ImVec4(0.6f,0.6f,0.6f,1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f,0.7f,0.2f,0.8f));
                ImGui::PushStyleColor(ImGuiCol_Text,          fg);
                char id[16]; std::snprintf(id, sizeof(id), "##k%d%d", col, row);
                ImGui::Button((std::string(kLabel[col][row]) + id).c_str(), cellSz);
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemActivated()) {
                    int ciaCol = col, ciaRow = row;
                    if (keyMatrixTranspose_) std::swap(ciaCol, ciaRow);
                    machine_.cia1().setKey(ciaCol, ciaRow, true);
                    lastKeyCol_ = col; lastKeyRow_ = row; lastKeyName_ = kLabel[col][row];
                }
                if (ImGui::IsItemDeactivated()) {
                    int ciaCol = col, ciaRow = row;
                    if (keyMatrixTranspose_) std::swap(ciaCol, ciaRow);
                    machine_.cia1().setKey(ciaCol, ciaRow, false);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("col %d, row %d = %s\nClick to inject keypress",
                                      col, row, kLabel[col][row]);
            }
        }
    }
    ImGui::End();
}

void Application::injectSpectrumKeyMatrix(const char* title, bool* open) {
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader("Keyboard Matrix")) {
        static const char* kLabel[8][5] = {
            {"CS",  "Z", "X", "C", "V" },  // row 0 (0xFEFE)
            {"A",   "S", "D", "F", "G" },  // row 1 (0xFDFE)
            {"Q",   "W", "E", "R", "T" },  // row 2 (0xFBFE)
            {"1",   "2", "3", "4", "5" },  // row 3 (0xF7FE)
            {"0",   "9", "8", "7", "6" },  // row 4 (0xEFFE)
            {"P",   "O", "I", "U", "Y" },  // row 5 (0xDFFE)
            {"EN",  "L", "K", "J", "H" },  // row 6 (0xBFFE)
            {"SP",  "SS","M", "N", "B" },  // row 7 (0x7FFE)
        };

        if (lastKeyCol_ >= 0)
            ImGui::Text("Last: %-8s  row %d, bit %d  (%s)",
                lastKeyName_.c_str(), lastKeyCol_, lastKeyRow_,
                kLabel[lastKeyCol_][lastKeyRow_]);
        else
            ImGui::TextDisabled("Press a key (with keyboard capture) or click a cell.");

        ImGui::Spacing();
        ImGui::TextDisabled("     ");
        for (int b = 0; b < 5; ++b) { ImGui::SameLine(); ImGui::TextDisabled("  Bit%-2d  ", b); }

        const ImVec2 cellSz{ 52.0f, 20.0f };
        for (int row = 0; row < 8; ++row) {
            ImGui::TextDisabled("Row%d ", row);
            for (int bit = 0; bit < 5; ++bit) {
                ImGui::SameLine();
                const bool held   = machine_.ula().keyState(row, bit);
                const bool isLast = (lastKeyCol_ == row && lastKeyRow_ == bit);
                ImVec4 bg = held   ? ImVec4(0.1f,0.7f,0.2f,0.6f)
                          : isLast ? ImVec4(0.2f,0.4f,0.8f,0.4f)
                                   : ImVec4(0.15f,0.15f,0.15f,1.0f);
                ImVec4 fg = (held || isLast) ? ImVec4(1,1,1,1) : ImVec4(0.6f,0.6f,0.6f,1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f,0.7f,0.2f,0.8f));
                ImGui::PushStyleColor(ImGuiCol_Text,          fg);
                char id[16]; std::snprintf(id, sizeof(id), "##s%d%d", row, bit);
                ImGui::Button((std::string(kLabel[row][bit]) + id).c_str(), cellSz);
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemActivated()) {
                    machine_.ula().setKey(row, bit, true);
                    lastKeyCol_ = row; lastKeyRow_ = bit; lastKeyName_ = kLabel[row][bit];
                }
                if (ImGui::IsItemDeactivated())
                    machine_.ula().setKey(row, bit, false);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("row %d, bit %d = %s\nClick to inject keypress",
                                      row, bit, kLabel[row][bit]);
            }
        }
    }
    ImGui::End();
}
