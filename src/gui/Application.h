#pragma once

#include <SDL.h>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// One ROM slot required by a preset (e.g. KERNAL, BASIC, CHAR).
struct PresetRomEntry {
    std::string key;          // machine-readable id used as JSON key
    std::string label;        // display name shown in the dialog
    std::string description;  // tooltip / sub-label
};

// Metadata loaded from a bundled preset JSON file.
struct PresetInfo {
    std::string              name;
    std::string              description;
    std::string              presetType;   // "c64", "spectrum", …
    std::string              path;         // absolute path to the .json file
    std::string              cpu;
    int                      cyclesPerFrame    = 0;
    bool                     keyMatrixTranspose = true;
    std::vector<PresetRomEntry> roms;
};

#ifdef __APPLE__
#  include <OpenGL/gl3.h>
#else
#  include <SDL_opengl.h>
#endif

#include "emulator/core/IBusDevice.h"
#include "emulator/core/Machine.h"
#include "imgui_memory_editor.h"
#include <functional>

// ---------------------------------------------------------------------------
// PresetDriver — bundles everything Application needs to activate one preset
// type.  Add a new entry to kPresetDrivers[] in Application.cpp to support a
// new machine without touching any existing dispatch logic.
// ---------------------------------------------------------------------------
struct PresetDriver {
    std::string presetType;    // must match PresetInfo::presetType from JSON
    bool        resetScreenTex = false;  // true when the screen texture dimensions change
    std::function<MachineConfigResult(
        Machine&,
        const std::unordered_map<std::string, std::string>& romPaths,
        bool keyMatrixTranspose)> build;
    std::unordered_map<uint16_t, std::string> disasmLabels;
};

class Application {
public:
    Application();
    ~Application();

    bool init();
    void run();

private:
    // -----------------------------------------------------------------------
    // Window / context
    // -----------------------------------------------------------------------
    SDL_Window*        window_      = nullptr;
    SDL_GLContext      glContext_   = nullptr;
    SDL_AudioDeviceID  audioDevice_ = 0;
    bool               running_     = false;
    GLuint             screenTex_   = 0;  // active screen texture
    int                screenTexW_  = 0;  // dimensions of the allocated texture
    int                screenTexH_  = 0;

    // -----------------------------------------------------------------------
    // Emulator — Machine owns CPU, Bus, and all devices
    // -----------------------------------------------------------------------
    Machine  machine_;
    bool     emulatorRunning_ = false;
    uint64_t cycleCount_      = 0;
    int      cyclesPerFrame_  = 1000;   // ~60 kHz at 60 fps (debug default)

    // -----------------------------------------------------------------------
    // Breakpoints
    // -----------------------------------------------------------------------
    std::unordered_set<uint16_t> breakpoints_;
    bool showBreakpoints_ = false;
    char bpAddInput_[5]   = "";

    // -----------------------------------------------------------------------
    // Watchpoints
    // -----------------------------------------------------------------------
    struct Watchpoint {
        uint16_t addr    = 0;
        bool     onRead  = true;
        bool     onWrite = true;
    };
    std::vector<Watchpoint> watchpoints_;
    bool     showWatchpoints_    = false;
    char     wpAddInput_[5]      = "";
    bool     watchpointHit_      = false;
    uint16_t watchpointHitAddr_  = 0;
    bool     watchpointHitWrite_ = false;

    // -----------------------------------------------------------------------
    // Per-device panel visibility  (keyed by IBusDevice*)
    // -----------------------------------------------------------------------
    std::unordered_map<const IBusDevice*, bool> devicePanelVisible_;

    // -----------------------------------------------------------------------
    // Bundled presets — scanned from presets/ at startup
    // -----------------------------------------------------------------------
    std::vector<PresetInfo>              presets_;
    int                                  activePresetIdx_  = -1;  // preset being configured
    bool                                 showPresetDialog_ = false;
    std::unordered_map<std::string, std::string> presetRomPaths_;  // key → file path

    // -----------------------------------------------------------------------
    // UI visibility toggles
    // -----------------------------------------------------------------------
    bool showScreen_        = true;
    bool keyboardCaptured_  = false;
    bool showTerminal_ = true;
    bool showCpuState_ = true;
    bool showDisasm_   = true;
    bool showMemView_   = false;
    bool allowRomEdit_  = false;
    bool memColorsDirty_ = true;
    ImU32 memRegionColors_[0x10000] = {};
    bool showDesigner_ = false;

    // -----------------------------------------------------------------------
    // Keyboard matrix state (shared by C64 and Spectrum inject panels)
    // -----------------------------------------------------------------------
    std::string lastKeyName_;
    int         lastKeyCol_ = -1;
    int         lastKeyRow_ = -1;

    // When true, col and row are swapped before calling setKey — required for
    // MEGA65 OpenROMs which wire PA=rows/PB=cols (opposite of stock C64 KERNAL).
    bool keyMatrixTranspose_ = true;

    // -----------------------------------------------------------------------
    // Disassembler
    // -----------------------------------------------------------------------
    bool     disasmFollowPC_  = true;
    uint16_t disasmViewAddr_  = 0x0200;
    char     disasmGotoInput_[5] = "0200";
    std::unordered_map<uint16_t, std::string> disasmLabels_;

    // -----------------------------------------------------------------------
    // Memory viewer
    // -----------------------------------------------------------------------
    MemoryEditor memEditor_;
    bool         memViewFollowPC_ = false;

    // -----------------------------------------------------------------------
    // Machine Designer
    // -----------------------------------------------------------------------
    struct DesignerMsg {
        std::string text;
        bool        isErr = false;
        void setOk(std::string s)  { text = std::move(s); isErr = false; }
        void setErr(std::string s) { text = std::move(s); isErr = true;  }
        void clear()               { text.clear(); isErr = false; }
        bool empty() const         { return text.empty(); }
    };

    int         designerAddDevIdx_  = 0;
    char        designerAddStart_[5]{};
    char        designerAddEnd_[5]{};
    std::string designerAddError_;  // add-device error (simple string; shown only in red)

    // ROM loading
    char        designerRomStart_[5]{};
    DesignerMsg designerRomMsg_;

    // Banked RAM
    char        designerBankStart_[5]{};
    char        designerBankEnd_[5]{};
    char        designerBankSelAddr_[5]{};
    char        designerBankCount_[4]{};
    DesignerMsg designerBankMsg_;

    // Preset dialog status message
    std::string presetMsg_;

    // Switchable Region
    char        designerSRStart_[5]{};
    char        designerSREnd_[5]{};
    DesignerMsg designerSRMsg_;

    // Bank Controller
    char        designerBCAddr_[5]{};
    DesignerMsg designerBCMsg_;

    // Inline address editing
    int         designerEditRow_    = -1;
    int         designerEditCol_    = -1;  // 0=start, 1=end
    char        designerEditBuf_[5]{};
    bool        designerEditFocus_  = false;

    // When the user leaves an address field with an invalid value, we persist
    // the typed text here so it stays visible in red and pre-fills on re-entry.
    int         designerInvalidRow_ = -1;
    int         designerInvalidCol_ = -1;
    char        designerInvalidBuf_[5]{};

    // -----------------------------------------------------------------------
    // Terminal
    // -----------------------------------------------------------------------
    std::vector<std::string> termLines_;
    bool                     termScrollToBottom_ = false;
    char                     termInput_[256];

    // Line buffer for characters arriving from the CPU via CHAR_OUT ($F000).
    std::string              ioLineBuf_;

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------
    void processEvents();
    void render();

    void drawMenuBar();
    void drawScreen();
    void drawTerminal();
    void drawCpuState();
    void drawDisassembler();
    void drawBreakpoints();
    void drawWatchpoints();
    void rebuildMemRegionColors();
    void drawMemoryViewer();
    void drawMachineDesigner();
    void drawDesignerCpuSection();
    void drawDesignerDeviceTable(int& removeIdx, int& moveFrom, int& moveTo);
    void drawDesignerContainedDevices();
    void drawDesignerAddDevice();
    void drawDesignerLoadRom();
    void drawDesignerAddBankedRam();
    void drawDesignerAddSwitchableRegion();
    void drawDesignerAddBankController();

    void termPrint(const std::string& line);

    void emulatorStep();
    void emulatorReset();
    void loadRomDialog();
    void saveMachineConfigDialog();
    void loadMachineConfigDialog();
    void scanPresets();
    std::string sessionFilePath() const;
    void drawPresetDialog();
    void buildActivePreset();
    void injectC64KeyMatrix(const char* title, bool* open);
    void injectSpectrumKeyMatrix(const char* title, bool* open);
};
