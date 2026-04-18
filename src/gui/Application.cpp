#include "Application.h"
#include "emulator/core/ICPU.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/devices/SID6581.h"
#include "emulator/devices/VIC6566.h"
#include "emulator/cpu/Disassembler.h"
#include "gui/FileDialog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL_image.h>

#ifdef __APPLE__
#  include <OpenGL/gl3.h>
#else
#  include <SDL_opengl.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Application::Application() {
    std::memset(termInput_, 0, sizeof(termInput_));

    memEditor_.UserData = this;

    memEditor_.ReadFn = [](const ImU8* /*mem*/, size_t off, void* ud) -> ImU8 {
        return static_cast<Application*>(ud)->machine_.bus().read(static_cast<uint16_t>(off));
    };
    memEditor_.WriteFn = [](ImU8* /*mem*/, size_t off, ImU8 d, void* ud) {
        static_cast<Application*>(ud)->machine_.bus().write(static_cast<uint16_t>(off), d);
    };
    memEditor_.BgColorFn = [](const ImU8* /*mem*/, size_t off, void* ud) -> ImU32 {
        auto* app = static_cast<Application*>(ud);
        const uint16_t addr = static_cast<uint16_t>(off);
        if (addr == app->machine_.cpu().getPC()) return IM_COL32(255, 255, 50, 80);
        return app->memRegionColors_[addr];
    };

    machine_.bus().onAccess = [this](uint16_t addr, bool isWrite) {
        if (watchpointHit_) return;
        for (const auto& wp : watchpoints_) {
            if (wp.addr == addr && ((isWrite && wp.onWrite) || (!isWrite && wp.onRead))) {
                watchpointHit_      = true;
                watchpointHitAddr_  = addr;
                watchpointHitWrite_ = isWrite;
                return;
            }
        }
    };
}

Application::~Application() {
    if (audioDevice_) SDL_CloseAudioDevice(audioDevice_);
    if (screenTex_) glDeleteTextures(1, &screenTex_);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (glContext_) SDL_GL_DeleteContext(glContext_);
    if (window_)    SDL_DestroyWindow(window_);
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool Application::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,        0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8);

    window_ = SDL_CreateWindow(
        "The 8-Bit Machine  |  Machine Designer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    // Set window icon from assets/icon.png next to the executable
    if (char* base = SDL_GetBasePath()) {
        std::string iconPath = std::string(base) + "assets/icon.png";
        SDL_free(base);
        if (SDL_Surface* icon = IMG_Load(iconPath.c_str())) {
            SDL_SetWindowIcon(window_, icon);
            SDL_FreeSurface(icon);
        }
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);  // vsync

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "imgui_layout.ini";

    ImGui::StyleColorsDark();

    ImGuiStyle& style       = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.TabRounding       = 2.0f;

    ImGui_ImplSDL2_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- Screen texture — sized to the initial screen (VIC); recreated on preset change ---
    {
        auto info = machine_.screenInfo();
        screenTexW_ = info.width;
        screenTexH_ = info.height;
        glGenTextures(1, &screenTex_);
        glBindTexture(GL_TEXTURE_2D, screenTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     screenTexW_, screenTexH_,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // --- Wire machine callbacks ---
    machine_.setCharOutCallback([this](uint8_t c) {
        if (c == '\r') return;
        if (c == '\n') {
            termPrint(ioLineBuf_.empty() ? "" : ioLineBuf_);
            ioLineBuf_.clear();
        } else {
            ioLineBuf_ += static_cast<char>(c);
        }
    });

    machine_.setIRQCallback([this]() { machine_.cpu().irq(); });

    // --- SDL audio: mono float32 at 44100 Hz, 512-sample callback ---
    SDL_AudioSpec want{};
    want.freq     = 44100;
    want.format   = AUDIO_F32SYS;
    want.channels = 1;
    want.samples  = 512;
    want.callback = [](void* ud, Uint8* stream, int len) {
        static_cast<SID6581*>(ud)->generateSamples(
            reinterpret_cast<float*>(stream), len / sizeof(float), 44100.0f);
    };
    want.userdata = &machine_.sid();

    SDL_AudioSpec have{};
    audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!audioDevice_)
        std::fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
    else
        SDL_PauseAudioDevice(audioDevice_, 0);  // start playing immediately

    emulatorReset();
    scanPresets();

    // Restore last session if one was saved
    {
        const std::string sessionPath = sessionFilePath();
        if (!sessionPath.empty()) {
            const auto result = machine_.loadConfig(sessionPath);
            if (result.ok) {
                if (result.hasPreset)
                    keyMatrixTranspose_ = result.keyMatrixTranspose;
                else
                    machine_.reset();
                if (result.cyclesPerFrame > 0)
                    cyclesPerFrame_ = result.cyclesPerFrame;
                cycleCount_ = 0;
            }
        }
    }

    running_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void Application::run() {
    while (running_) {
        processEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        render();

        ImGui::Render();
        const ImGuiIO& io = ImGui::GetIO();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
    }

    // Save session on clean exit
    const std::string sessionPath = sessionFilePath();
    if (!sessionPath.empty())
        machine_.saveConfig(sessionPath, cyclesPerFrame_);
}

// ---------------------------------------------------------------------------
// Event handling  +  per-frame emulator tick
// ---------------------------------------------------------------------------

void Application::processEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);

        if (e.type == SDL_QUIT)
            running_ = false;

        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            const SDL_Keycode sym     = e.key.keysym.sym;
            const bool        pressed = (e.type == SDL_KEYDOWN);

            // F5/F6 are also C64 keyboard keys — only fire emulator action
            // when keyboard is NOT captured so they aren't consumed twice.
            if (pressed && !keyboardCaptured_) {
                switch (sym) {
                    case SDLK_F5: emulatorRunning_ = true;  termPrint("[Emulator] Running..."); break;
                    case SDLK_F6: emulatorRunning_ = false; termPrint("[Emulator] Paused.");
                                  termPrint(machine_.cpu().stateString());                      break;
                    default: break;
                }
            }
            // F8 / F10 are not C64 keys — always available
            if (pressed) {
                switch (sym) {
                    case SDLK_F8:  emulatorReset();  break;
                    case SDLK_F10: emulatorStep();   break;
                    default: break;
                }
            }

            // Route to the active machine's keyboard matrix when screen has focus
            if (keyboardCaptured_) {
                if (sym == SDLK_ESCAPE) {
                    if (pressed) { keyboardCaptured_ = false; machine_.clearKeys(); }
                } else {
                    machine_.keyEvent(sym, pressed);
                }
            }
        }
    }

    if (emulatorRunning_) {
        ICPU& cpu = machine_.cpu();
        watchpointHit_ = false;
        for (int i = 0; i < cyclesPerFrame_; ++i) {
            machine_.clock();
            cpu.clock();
            ++cycleCount_;

            if (watchpointHit_) {
                emulatorRunning_ = false;
                std::ostringstream msg;
                msg << "[Watchpoint] $" << std::uppercase << std::hex
                    << std::setfill('0') << std::setw(4) << (unsigned)watchpointHitAddr_
                    << (watchpointHitWrite_ ? " WRITE" : " READ");
                termPrint(msg.str());
                termPrint(cpu.stateString());
                watchpointHit_ = false;
                break;
            }

            if (cpu.complete() && !breakpoints_.empty()
                    && breakpoints_.count(cpu.getPC())) {
                emulatorRunning_ = false;
                std::ostringstream bpMsg;
                bpMsg << "[Break] $" << std::uppercase << std::hex
                      << std::setfill('0') << std::setw(4) << (unsigned)cpu.getPC();
                termPrint(bpMsg.str());
                termPrint(cpu.stateString());
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Top-level render
// ---------------------------------------------------------------------------

void Application::render() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);
    drawMenuBar();
    if (showScreen_)   drawScreen();
    if (showTerminal_) drawTerminal();
    if (showCpuState_) drawCpuState();
    if (showDisasm_)       drawDisassembler();
    if (showBreakpoints_)  drawBreakpoints();
    if (showWatchpoints_)  drawWatchpoints();
    if (showMemView_)      drawMemoryViewer();
    if (showDesigner_) drawMachineDesigner();
    if (showPresetDialog_) drawPresetDialog();

    // Per-device panels — uses panelDevices() so fixed chips are found even
    // when they are not direct bus entries (e.g. inside C64IOSpace).
    for (const auto& entry : machine_.panelDevices()) {
        auto it = devicePanelVisible_.find(entry.device);
        if (it == devicePanelVisible_.end() || !it->second) continue;
        bool open = true;
        entry.panel->drawPanel(entry.label.c_str(), &open);
        // Keyboard matrix injected as a collapsible section in the owning device panel.
        if (entry.device == &machine_.cia1())
            injectC64KeyMatrix(entry.label.c_str(), &open);
        else if (entry.device == &machine_.ula())
            injectSpectrumKeyMatrix(entry.label.c_str(), &open);
        if (!open) devicePanelVisible_[entry.device] = false;
    }
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void Application::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    // File ----------------------------------------------------------------
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Machine")) {
            emulatorRunning_ = false;
            machine_.resetAddressMap();
            machine_.reset();
            screenTexW_ = 0; screenTexH_ = 0;
            disasmLabels_.clear();
            disasmLabels_[Bus::CHAR_OUT_ADDR] = "CHAR_OUT";
            allowRomEdit_    = false;
            memColorsDirty_  = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Load ROM...", "Ctrl+O"))
            loadRomDialog();

        ImGui::Separator();

        if (ImGui::BeginMenu("Load Preset", !presets_.empty())) {
            for (int pi = 0; pi < (int)presets_.size(); ++pi) {
                if (ImGui::MenuItem(presets_[pi].name.c_str())) {
                    activePresetIdx_  = pi;
                    showPresetDialog_ = true;
                    presetMsg_.clear();
                    // Pre-populate ROM path slots for this preset
                    for (const auto& r : presets_[pi].roms)
                        if (presetRomPaths_.find(r.key) == presetRomPaths_.end())
                            presetRomPaths_[r.key] = "";
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Save Machine Config..."))
            saveMachineConfigDialog();
        if (ImGui::MenuItem("Load Machine Config..."))
            loadMachineConfigDialog();

        ImGui::Separator();

        if (ImGui::MenuItem("Quit", "Alt+F4"))
            running_ = false;

        ImGui::EndMenu();
    }

    // Emulator ------------------------------------------------------------
    if (ImGui::BeginMenu("Emulator")) {
        if (ImGui::MenuItem("Run",   "F5",  emulatorRunning_)) {
            emulatorRunning_ = true;
            termPrint("[Emulator] Running...");
        }
        if (ImGui::MenuItem("Pause", "F6", !emulatorRunning_)) {
            emulatorRunning_ = false;
            termPrint("[Emulator] Paused.");
            termPrint(machine_.cpu().stateString());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Step",  "F10")) emulatorStep();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset", "F8"))  emulatorReset();
        ImGui::Separator();

        if (ImGui::BeginMenu("Speed")) {
            struct { const char* label; int cycles; } presets[] = {
                { "~60 kHz  (debug)",    1'000 },
                { "~500 kHz",            8'333 },
                { "~1 MHz",             16'667 },
                { "~2 MHz  (real 8502)", 33'333 },
            };
            for (auto& p : presets) {
                if (ImGui::MenuItem(p.label, nullptr,
                                    cyclesPerFrame_ == p.cycles))
                    cyclesPerFrame_ = p.cycles;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    // View ----------------------------------------------------------------
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Screen",      nullptr, &showScreen_);
        ImGui::MenuItem("Terminal",    nullptr, &showTerminal_);
        ImGui::MenuItem("CPU State",   nullptr, &showCpuState_);
        ImGui::EndMenu();
    }

    // Machine -------------------------------------------------------------
    if (ImGui::BeginMenu("Machine")) {
        ImGui::MenuItem("Machine Designer", nullptr, &showDesigner_);

        // Device panels — fixed chips + any designer-added devices with panels.
        const auto panels = machine_.panelDevices();
        if (!panels.empty()) ImGui::Separator();
        for (const auto& entry : panels) {
            bool& vis = devicePanelVisible_[entry.device];
            ImGui::MenuItem(entry.label.c_str(), nullptr, &vis);
        }

        ImGui::EndMenu();
    }

    // Debug ---------------------------------------------------------------
    if (ImGui::BeginMenu("Debug")) {
        ImGui::MenuItem("Disassembler",    nullptr, &showDisasm_);
        ImGui::MenuItem("Breakpoints",     nullptr, &showBreakpoints_);
        ImGui::MenuItem("Watchpoints",     nullptr, &showWatchpoints_);
        ImGui::MenuItem("Memory Viewer",   nullptr, &showMemView_);
        ImGui::EndMenu();
    }

    // Help ----------------------------------------------------------------
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) {
            termPrint("---");
            termPrint("The 8-Bit Machine  v0.30.0");
            termPrint("Design your own 8-bit computer.");
            termPrint("Pick a CPU, add devices, wire the address space.");
            termPrint("Author: Christopher W. Olsen <cwolsen@brainchurts.com>");
            termPrint("Copyright (c) 2026 Christopher W. Olsen. All rights reserved.");
            termPrint("---");
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    const double effMHz = (cyclesPerFrame_ * 60.0) / 1'000'000.0;
    ImGui::TextDisabled("  Cycles: %llu   PC: $%04X   %.2f MHz   %s",
        (unsigned long long)cycleCount_,
        (unsigned)machine_.cpu().getPC(),
        effMHz,
        emulatorRunning_ ? "RUNNING" : "PAUSED");

    ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------------------
// Screen panel
// ---------------------------------------------------------------------------

void Application::drawScreen() {
    auto info = machine_.screenInfo();

    // Enforce a minimum window size of 1× native resolution so the image
    // always fits without a scrollbar.
    {
        const float titleH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
        const float minW   = info.width  > 0 ? float(info.width)  : 320.0f;
        const float minH   = info.height > 0 ? float(info.height) : 200.0f;
        ImGui::SetNextWindowSizeConstraints({ minW, minH + titleH }, { FLT_MAX, FLT_MAX });
    }

    ImGui::Begin("Screen", &showScreen_,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (info.pixels && info.width > 0 && info.height > 0) {
        // Recreate texture if screen dimensions changed (preset switch)
        if (info.width != screenTexW_ || info.height != screenTexH_) {
            glDeleteTextures(1, &screenTex_);
            glGenTextures(1, &screenTex_);
            glBindTexture(GL_TEXTURE_2D, screenTex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                         info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            screenTexW_ = info.width;
            screenTexH_ = info.height;
        }

        // VIC has a dirty flag; ULA and others always upload every frame
        VIC6566& vic = machine_.vic();
        bool vicActive = (info.pixels == vic.framebuffer());
        if (!vicActive || vic.frameDirty()) {
            glBindTexture(GL_TEXTURE_2D, screenTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            info.width, info.height,
                            GL_RGBA, GL_UNSIGNED_BYTE, info.pixels);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (vicActive) vic.clearDirty();
        }
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float  w     = info.width  > 0 ? float(info.width)  : 1.0f;
    float  h     = info.height > 0 ? float(info.height) : 1.0f;
    float  scale = std::min(avail.x / w, avail.y / h);
    const ImVec2 sz{ w * scale, h * scale };

    ImGui::Image(static_cast<ImTextureID>(screenTex_), sz);

    if (ImGui::IsItemClicked())
        keyboardCaptured_ = true;

    if (keyboardCaptured_) {
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(0, 255, 80, 220), 0.0f, 0, 2.5f);
        ImGui::TextColored({0.0f, 1.0f, 0.3f, 1.0f}, "KEYBOARD ACTIVE  (Esc to release)");
    } else {
        ImGui::TextDisabled("Click screen to capture keyboard");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Terminal panel
// ---------------------------------------------------------------------------

void Application::drawTerminal() {
    ImGui::Begin("Terminal", &showTerminal_);

    const float footerH = ImGui::GetStyle().ItemSpacing.y
                        + ImGui::GetFrameHeightWithSpacing();

    ImGui::BeginChild("##output", { 0.0f, -footerH }, false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.4f, 1.0f));
    for (const auto& line : termLines_)
        ImGui::TextUnformatted(line.c_str());
    ImGui::PopStyleColor();

    if (termScrollToBottom_) {
        ImGui::SetScrollHereY(1.0f);
        termScrollToBottom_ = false;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = ImGui::InputText(
        "##cmd", termInput_, sizeof(termInput_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetItemDefaultFocus();

    if (enter && termInput_[0] != '\0') {
        termPrint(std::string("> ") + termInput_);
        std::memset(termInput_, 0, sizeof(termInput_));
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// CPU State panel
// ---------------------------------------------------------------------------

void Application::drawCpuState() {
    ImGui::SetNextWindowSizeConstraints({ 200.0f, 180.0f }, { 400.0f, 500.0f });
    ImGui::Begin("CPU State", &showCpuState_);

    ICPU& cpu = machine_.cpu();
    const uint8_t p = cpu.regP();

    ImGui::TextUnformatted(cpu.cpuName());
    ImGui::Separator();

    ImGui::Text("PC   $%04X", (unsigned)cpu.getPC());
    ImGui::Text("A    $%02X    (%3d)", (unsigned)cpu.regA(), (unsigned)cpu.regA());
    ImGui::Text("X    $%02X    (%3d)", (unsigned)cpu.regX(), (unsigned)cpu.regX());
    ImGui::Text("Y    $%02X    (%3d)", (unsigned)cpu.regY(), (unsigned)cpu.regY());
    ImGui::Text("SP   $%02X", (unsigned)cpu.regSP());
    ImGui::Text("P    $%02X", (unsigned)p);
    ImGui::Separator();

    auto flagCell = [&](const char* name, bool set) {
        if (set) ImGui::TextColored({ 0.0f, 1.0f, 0.4f, 1.0f }, "%s", name);
        else     ImGui::TextDisabled("%s", name);
        ImGui::SameLine(0.0f, 4.0f);
    };

    ImGui::TextUnformatted("Flags  ");
    ImGui::SameLine(0.0f, 0.0f);
    flagCell("N", p & 0x80);
    flagCell("V", p & 0x40);
    flagCell("-", p & 0x20);
    flagCell("B", p & 0x10);
    flagCell("D", p & 0x08);
    flagCell("I", p & 0x04);
    flagCell("Z", p & 0x02);
    flagCell("C", p & 0x01);

    ImGui::Separator();
    ImGui::Text("Cycles  %llu", (unsigned long long)cycleCount_);


    ImGui::End();
}


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------


void Application::termPrint(const std::string& line) {
    termLines_.push_back(line);
    termScrollToBottom_ = true;
}

void Application::emulatorStep() {
    emulatorRunning_ = false;
    ICPU& cpu = machine_.cpu();
    do {
        machine_.clock();
        cpu.clock();
        ++cycleCount_;
    } while (!cpu.complete());
    termPrint(cpu.stateString());
}

void Application::emulatorReset() {
    machine_.reset();
    cycleCount_      = 0;
    emulatorRunning_ = false;

    termLines_.clear();
    termPrint("The 8-Bit Machine  |  Machine Designer  v0.8");
    termPrint("=============================================");
    termPrint("Design your own 8-bit computer.");
    termPrint("Pick a CPU, add devices, wire the address space.");
    termPrint("");
    termPrint("Default machine: MOS 8502 + CIA1 + CIA2 + 64 KB RAM");
    termPrint("System reset.");
    termPrint(machine_.cpu().stateString());
    termPrint("");
    termPrint("Keyboard shortcuts:");
    termPrint("  F5   Run       F6  Pause");
    termPrint("  F8   Reset     F10 Step");
    termPrint("");
}

// ---------------------------------------------------------------------------
// ROM loading
// ---------------------------------------------------------------------------

void Application::loadRomDialog() {
    const std::string path = FileDialog::openFile(
        "Load ROM", {"prg", "PRG", "bin", "BIN", "rom", "ROM"});

    if (path.empty()) return;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        termPrint("[ROM] Error: cannot open file.");
        return;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize == 0) {
        termPrint("[ROM] Error: file is empty.");
        return;
    }

    auto toLower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const size_t dot      = path.rfind('.');
    const std::string ext = dot != std::string::npos ? toLower(path.substr(dot + 1)) : "";
    const bool isPrg      = (ext == "prg");

    uint16_t loadAddr  = 0x0200;
    size_t   dataBytes = fileSize;

    if (isPrg) {
        if (fileSize < 3) {
            termPrint("[ROM] Error: PRG file too small (need at least 3 bytes).");
            return;
        }
        const uint8_t lo = static_cast<uint8_t>(file.get());
        const uint8_t hi = static_cast<uint8_t>(file.get());
        loadAddr  = static_cast<uint16_t>((hi << 8) | lo);
        dataBytes = fileSize - 2;
    }

    Bus& bus = machine_.bus();
    uint16_t addr       = loadAddr;
    size_t   bytesLoaded = 0;

    for (size_t i = 0; i < dataBytes; ++i) {
        const int c = file.get();
        if (c == EOF) break;
        bus.write(addr++, static_cast<uint8_t>(c));
        ++bytesLoaded;
    }

    bus.write(0xFFFC, loadAddr & 0x00FF);
    bus.write(0xFFFD, (loadAddr >> 8) & 0x00FF);

    machine_.cpu().reset();
    cycleCount_      = 0;
    emulatorRunning_ = false;
    disasmViewAddr_  = loadAddr;
    disasmFollowPC_  = true;
    showDisasm_      = true;

    const size_t slash     = path.find_last_of("/\\");
    const std::string filename = slash != std::string::npos ? path.substr(slash + 1) : path;

    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0');

    termPrint("---");
    termPrint("[ROM] " + filename);
    ss << "[ROM] Format: " << (isPrg ? "PRG (Commodore)" : "raw binary");
    termPrint(ss.str());  ss.str("");

    ss << "[ROM] Load address : $" << std::setw(4) << loadAddr;
    termPrint(ss.str());  ss.str("");

    ss << "[ROM] Bytes loaded : " << std::dec << bytesLoaded;
    termPrint(ss.str());  ss.str("");

    ss << "[ROM] End address  : $" << std::uppercase << std::hex
       << std::setw(4) << static_cast<uint16_t>(loadAddr + bytesLoaded - 1);
    termPrint(ss.str());

    termPrint("System reset — PC → $" + [&]{
        std::ostringstream t;
        t << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << loadAddr;
        return t.str();
    }());
    termPrint(machine_.cpu().stateString());
    termPrint("---");
}

// ---------------------------------------------------------------------------
// Session persistence
// ---------------------------------------------------------------------------

std::string Application::sessionFilePath() const {
    char* prefPath = SDL_GetPrefPath("the8bitmachine", "The8BitMachine");
    if (!prefPath) return "";
    std::string path = std::string(prefPath) + "last_session.json";
    SDL_free(prefPath);
    return path;
}

// ---------------------------------------------------------------------------
// Machine config dialogs
// ---------------------------------------------------------------------------

void Application::saveMachineConfigDialog() {
    const std::string path = FileDialog::saveFile(
        "Save Machine Config", "machine.json", {"json"});
    if (path.empty()) return;

    const auto result = machine_.saveConfig(path, cyclesPerFrame_);
    termPrint(result.message);
}

void Application::loadMachineConfigDialog() {
    const std::string path = FileDialog::openFile(
        "Load Machine Config", {"json"});
    if (path.empty()) return;

    const auto result = machine_.loadConfig(path);
    termPrint(result.message);

    if (result.ok) {
        if (result.hasPreset) {
            // Preset configs rebuild the machine internally — no extra reset needed.
            keyMatrixTranspose_ = result.keyMatrixTranspose;
        } else {
            machine_.reset();
        }
        if (result.cyclesPerFrame > 0)
            cyclesPerFrame_ = result.cyclesPerFrame;
        cycleCount_      = 0;
        emulatorRunning_ = false;
        termPrint(machine_.cpu().stateString());
    }
}
