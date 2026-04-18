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
            setDisasmLabels("");
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

// ---------------------------------------------------------------------------
// scanPresets — find all *.json files in presets/ next to the executable
// ---------------------------------------------------------------------------

void Application::scanPresets() {
    presets_.clear();

    char* basePath = SDL_GetBasePath();
    if (!basePath) return;
    const std::string presetsDir = std::string(basePath) + "presets";
    SDL_free(basePath);

    // Iterate directory entries using C++17 filesystem
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
        info.keyMatrixTranspose = root.value("key_matrix_transpose", true);

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

    // Sort alphabetically by name
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

    // Keyboard matrix option (shown for presets that declare it)
    if (preset.keyMatrixTranspose) {
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
// buildActivePreset — dispatches to the right C++ builder
// ---------------------------------------------------------------------------

void Application::buildActivePreset() {
    if (activePresetIdx_ < 0 || activePresetIdx_ >= (int)presets_.size()) return;
    const PresetInfo& preset = presets_[activePresetIdx_];

    MachineConfigResult result;

    if (preset.presetType == "c64") {
        result = machine_.buildC64Preset(
            presetRomPaths_["kernal"],
            presetRomPaths_["basic"],
            presetRomPaths_["char"],
            keyMatrixTranspose_);
    } else if (preset.presetType == "spectrum48") {
        result = machine_.buildSpectrumPreset(presetRomPaths_["rom"]);
        screenTexW_ = screenTexH_ = 0;
    } else if (preset.presetType == "apple2e") {
        result = machine_.buildAppleIIePreset(presetRomPaths_["rom"]);
        screenTexW_ = screenTexH_ = 0;
    } else {
        presetMsg_ = "[Preset] Unknown preset type: " + preset.presetType;
        return;
    }

    presetMsg_ = result.message;
    if (result.ok) {
        cyclesPerFrame_  = preset.cyclesPerFrame > 0 ? preset.cyclesPerFrame : 16'667;
        cycleCount_      = 0;
        emulatorRunning_ = false;
        setDisasmLabels(preset.presetType);
        allowRomEdit_   = false;
        memColorsDirty_ = true;
        termPrint(result.message);
        termPrint(machine_.cpu().stateString());
        showPresetDialog_ = false;
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void Application::setDisasmLabels(const std::string& presetType) {
    disasmLabels_.clear();
    disasmLabels_[Bus::CHAR_OUT_ADDR] = "CHAR_OUT";

    if (presetType == "c64") {
        disasmLabels_[0x0314] = "IRQ-Vec";
        disasmLabels_[0x0316] = "NMI-Vec";
        disasmLabels_[0xFF81] = "CINT";
        disasmLabels_[0xFF84] = "IOINIT";
        disasmLabels_[0xFF87] = "RAMTAS";
        disasmLabels_[0xFF8A] = "RESTOR";
        disasmLabels_[0xFF8D] = "VECTOR";
        disasmLabels_[0xFFD2] = "CHROUT";
        disasmLabels_[0xFFE4] = "GETIN";
        disasmLabels_[0xFFFA] = "NMI";
        disasmLabels_[0xFFFC] = "RESET";
        disasmLabels_[0xFFFE] = "IRQ";
    } else if (presetType == "spectrum48") {
        disasmLabels_[0x0000] = "RESET";
        disasmLabels_[0x0038] = "IRQ";
        disasmLabels_[0x0066] = "NMI";
        disasmLabels_[0x4000] = "BITMAP";
        disasmLabels_[0x5800] = "ATTRS";
    } else if (presetType == "apple2e") {
        disasmLabels_[0xC000] = "KBD";
        disasmLabels_[0xC010] = "KBDSTRB";
        disasmLabels_[0xC050] = "GRAPHICS";
        disasmLabels_[0xC051] = "TEXT";
        disasmLabels_[0xC052] = "FULLSCR";
        disasmLabels_[0xC053] = "MIXED";
        disasmLabels_[0xC054] = "PAGE1";
        disasmLabels_[0xC055] = "PAGE2";
        disasmLabels_[0xC056] = "LORES";
        disasmLabels_[0xC057] = "HIRES";
        disasmLabels_[0xFFFA] = "NMI";
        disasmLabels_[0xFFFC] = "RESET";
        disasmLabels_[0xFFFE] = "IRQ";
    }
}

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
