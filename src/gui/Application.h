#pragma once

#include <SDL.h>
#include <imgui.h>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef __APPLE__
#  include <OpenGL/gl3.h>
#else
#  include <SDL_opengl.h>
#endif

#include "emulator/core/Machine.h"
#include "imgui_memory_editor.h"

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
    GLuint             screenTex_   = 0;  // VIC framebuffer texture

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

    // -----------------------------------------------------------------------
    // UI visibility toggles
    // -----------------------------------------------------------------------
    bool showScreen_        = true;
    bool keyboardCaptured_  = false;
    bool showTerminal_ = true;
    bool showCpuState_ = true;
    bool showDisasm_   = true;
    bool showMemView_  = false;
    bool showDesigner_ = false;

    // -----------------------------------------------------------------------
    // Disassembler
    // -----------------------------------------------------------------------
    bool     disasmFollowPC_  = true;
    uint16_t disasmViewAddr_  = 0x0200;
    char     disasmGotoInput_[5] = "0200";

    // -----------------------------------------------------------------------
    // Memory viewer
    // -----------------------------------------------------------------------
    MemoryEditor memEditor_;
    bool         memViewFollowPC_ = false;

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
    void drawMemoryViewer();
    void drawMachineDesigner();

    void termPrint(const std::string& line);

    void emulatorStep();
    void emulatorReset();
    void loadRomDialog();
    void saveMachineConfigDialog();
    void loadMachineConfigDialog();
};
