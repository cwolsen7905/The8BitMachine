#include "Application.h"
#include "gui/FileDialog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

// ---------------------------------------------------------------------------
// Preset driver table — one entry per preset type.
// To add a new preset: implement Machine::buildXxxPreset(), then add an
// entry here.  No other dispatch code needs to change.
// ---------------------------------------------------------------------------

namespace {
    const PresetDriver kPresetDrivers[] = {
        {
            "c64",
            /*resetScreenTex=*/false,
            [](Machine& m, const auto& roms, bool transpose) {
                return m.buildC64Preset(
                    roms.count("kernal") ? roms.at("kernal") : "",
                    roms.count("basic")  ? roms.at("basic")  : "",
                    roms.count("char")   ? roms.at("char")   : "",
                    transpose);
            },
            {
                { 0x0314, "IRQ-Vec" },
                { 0x0316, "NMI-Vec" },
                { 0xFF81, "CINT"    },
                { 0xFF84, "IOINIT"  },
                { 0xFF87, "RAMTAS"  },
                { 0xFF8A, "RESTOR"  },
                { 0xFF8D, "VECTOR"  },
                { 0xFFD2, "CHROUT"  },
                { 0xFFE4, "GETIN"   },
                { 0xFFFA, "NMI"     },
                { 0xFFFC, "RESET"   },
                { 0xFFFE, "IRQ"     },
            }
        },
        {
            "spectrum48",
            /*resetScreenTex=*/true,
            [](Machine& m, const auto& roms, bool) {
                return m.buildSpectrumPreset(roms.count("rom") ? roms.at("rom") : "");
            },
            {
                { 0x0000, "RESET"  },
                { 0x0038, "IRQ"    },
                { 0x0066, "NMI"    },
                { 0x4000, "BITMAP" },
                { 0x5800, "ATTRS"  },
            }
        },
        {
            "apple2e",
            /*resetScreenTex=*/true,
            [](Machine& m, const auto& roms, bool) {
                return m.buildAppleIIePreset(roms.count("rom") ? roms.at("rom") : "");
            },
            {
                { 0xC000, "KBD"      },
                { 0xC010, "KBDSTRB"  },
                { 0xC050, "GRAPHICS" },
                { 0xC051, "TEXT"     },
                { 0xC052, "FULLSCR"  },
                { 0xC053, "MIXED"    },
                { 0xC054, "PAGE1"    },
                { 0xC055, "PAGE2"    },
                { 0xC056, "LORES"    },
                { 0xC057, "HIRES"    },
                { 0xFFFA, "NMI"      },
                { 0xFFFC, "RESET"    },
                { 0xFFFE, "IRQ"      },
            }
        },
    };
} // namespace

// ---------------------------------------------------------------------------
// scanPresets — find all *.json files in presets/ next to the executable
// ---------------------------------------------------------------------------

void Application::scanPresets() {
    presets_.clear();

    char* basePath = SDL_GetBasePath();
    if (!basePath) return;
    const std::string presetsDir = std::string(basePath) + "presets";
    SDL_free(basePath);

    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(presetsDir, ec)) {
        if (ec || entry.path().extension() != ".json") continue;

        std::ifstream f(entry.path());
        if (!f) continue;

        nlohmann::json root;
        try { root = nlohmann::json::parse(f); }
        catch (...) { continue; }

        PresetInfo info;
        info.path              = entry.path().string();
        info.name              = root.value("name", entry.path().stem().string());
        info.description       = root.value("description", "");
        info.presetType        = root.value("preset_type", "");
        info.cpu               = root.value("cpu", "");
        info.cyclesPerFrame    = root.value("cycles_per_frame", 0);
        info.keyMatrixTranspose = root.value("key_matrix_transpose", false);

        if (root.contains("roms") && root["roms"].is_array()) {
            for (const auto& r : root["roms"]) {
                PresetRomEntry re;
                re.key         = r.value("key", "");
                re.label       = r.value("label", re.key);
                re.description = r.value("description", "");
                info.roms.push_back(std::move(re));
            }
        }

        presets_.push_back(std::move(info));
    }

    std::sort(presets_.begin(), presets_.end(),
        [](const PresetInfo& a, const PresetInfo& b){ return a.name < b.name; });
}

// ---------------------------------------------------------------------------
// drawPresetDialog — generic ROM picker driven by the active PresetInfo
// ---------------------------------------------------------------------------

void Application::drawPresetDialog() {
    if (activePresetIdx_ < 0 || activePresetIdx_ >= (int)presets_.size()) return;
    const PresetInfo& preset = presets_[activePresetIdx_];

    const std::string title = preset.name + " Preset";
    ImGui::SetNextWindowSize({ 500.0f, 0.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

    if (!ImGui::Begin(title.c_str(), &showPresetDialog_,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    if (!preset.description.empty()) {
        ImGui::TextWrapped("%s", preset.description.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ROM browse rows — one per entry in preset.roms
    bool allReady = true;
    for (const auto& rom : preset.roms) {
        std::string& path = presetRomPaths_[rom.key];
        if (path.empty()) allReady = false;

        ImGui::TextUnformatted(rom.label.c_str());
        if (!rom.description.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", rom.description.c_str());
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(270.0f);
        char buf[512];
        std::strncpy(buf, path.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::PushID(rom.key.c_str());
        if (ImGui::InputText("##p", buf, sizeof(buf)))
            path = buf;
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            const std::string sel = FileDialog::openFile(
                ("Select " + rom.label).c_str(), {"bin", "rom", "prg", ""});
            if (!sel.empty()) path = sel;
        }
        ImGui::PopID();
    }

    // Keyboard matrix option (shown for C64 presets)
    if (preset.presetType == "c64") {
        ImGui::Spacing();
        ImGui::TextUnformatted("ROM target:");
        ImGui::SameLine();
        bool standard = !keyMatrixTranspose_;
        if (ImGui::RadioButton("Standard KERNAL", standard))  keyMatrixTranspose_ = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("MEGA65 OpenROMs", !standard)) keyMatrixTranspose_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("MEGA65 OpenROMs use PA=rows/PB=cols (transposed vs stock KERNAL).");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!allReady) ImGui::BeginDisabled();
    const std::string btnLabel = "Build " + preset.name;
    if (ImGui::Button(btnLabel.c_str(), { 180.0f, 0.0f }))
        buildActivePreset();
    if (!allReady) ImGui::EndDisabled();

    if (!presetMsg_.empty()) {
        ImGui::SameLine();
        const bool isErr = presetMsg_.find("[Config]") == std::string::npos ||
                           presetMsg_.find("Error") != std::string::npos;
        if (isErr)
            ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", presetMsg_.c_str());
        else
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", presetMsg_.c_str());
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// buildActivePreset — dispatches to the right C++ builder via kPresetDrivers
// ---------------------------------------------------------------------------

void Application::buildActivePreset() {
    if (activePresetIdx_ < 0 || activePresetIdx_ >= (int)presets_.size()) return;
    const PresetInfo& preset = presets_[activePresetIdx_];

    const PresetDriver* driver = nullptr;
    for (const auto& d : kPresetDrivers)
        if (d.presetType == preset.presetType) { driver = &d; break; }

    if (!driver) {
        presetMsg_ = "[Preset] Unknown preset type: " + preset.presetType;
        return;
    }

    if (driver->resetScreenTex)
        screenTexW_ = screenTexH_ = 0;

    MachineConfigResult result = driver->build(machine_, presetRomPaths_, keyMatrixTranspose_);

    presetMsg_ = result.message;
    if (result.ok) {
        cyclesPerFrame_  = preset.cyclesPerFrame > 0 ? preset.cyclesPerFrame : 16'667;
        cycleCount_      = 0;
        emulatorRunning_ = false;
        disasmLabels_.clear();
        disasmLabels_[Bus::CHAR_OUT_ADDR] = "CHAR_OUT";
        for (const auto& [addr, label] : driver->disasmLabels)
            disasmLabels_[addr] = label;
        allowRomEdit_   = false;
        memColorsDirty_ = true;

        rewirePeripherals(preset.presetType);

        termPrint(result.message);
        termPrint(machine_.cpu().stateString());
        showPresetDialog_ = false;
    }
}

// ---------------------------------------------------------------------------
// rewirePeripherals — connects peripheral devices to the machine for a given
// preset type.  Called after both UI-driven preset loads and session restores.
// ---------------------------------------------------------------------------

void Application::rewirePeripherals(const std::string& presetType) {
    peripherals_.clear();
    peripheralPanelVisible_.clear();

    if (presetType == "c64") {
        machine_.cia2().connectIEC(&drive1541_);
        peripherals_.push_back(&drive1541_);
        peripheralPanelVisible_[&drive1541_] = false;
    }
}
