#include "Application.h"
#include "emulator/core/ICPU.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/devices/VIC6566.h"
#include "emulator/cpu/Disassembler.h"
#include "gui/FileDialog.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#ifdef __APPLE__
#  include <OpenGL/gl3.h>
#else
#  include <SDL_opengl.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Application::Application() {
    std::memset(termInput_, 0, sizeof(termInput_));

    memEditor_.UserData = &machine_;

    memEditor_.ReadFn = [](const ImU8* /*mem*/, size_t off, void* ud) -> ImU8 {
        return static_cast<Machine*>(ud)->bus().read(static_cast<uint16_t>(off));
    };
    memEditor_.WriteFn = [](ImU8* /*mem*/, size_t off, ImU8 d, void* ud) {
        static_cast<Machine*>(ud)->bus().write(static_cast<uint16_t>(off), d);
    };
    memEditor_.BgColorFn = [](const ImU8* /*mem*/, size_t off, void* ud) -> ImU32 {
        const uint16_t pc = static_cast<Machine*>(ud)->cpu().getPC();
        return (static_cast<uint16_t>(off) == pc) ? IM_COL32(255, 255, 50, 80) : 0;
    };
}

Application::~Application() {
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
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

    // --- VIC screen texture (320×200 RGBA, nearest-neighbour scaled) ---
    glGenTextures(1, &screenTex_);
    glBindTexture(GL_TEXTURE_2D, screenTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 VIC6566::WIDTH, VIC6566::HEIGHT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

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

    emulatorReset();

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

        if (e.type == SDL_KEYDOWN) {
            const SDL_Keycode sym = e.key.keysym.sym;

            switch (sym) {
                case SDLK_F5:  emulatorRunning_ = true;  termPrint("[Emulator] Running..."); break;
                case SDLK_F6:  emulatorRunning_ = false; termPrint("[Emulator] Paused.");
                               termPrint(machine_.cpu().stateString());                       break;
                case SDLK_F8:  emulatorReset();                                               break;
                case SDLK_F10: emulatorStep();                                                break;
                default:       break;
            }
        }
    }

    if (emulatorRunning_) {
        ICPU& cpu = machine_.cpu();
        for (int i = 0; i < cyclesPerFrame_; ++i) {
            machine_.clock();
            cpu.clock();
            ++cycleCount_;

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
    if (showDisasm_)   drawDisassembler();
    if (showMemView_)  drawMemoryViewer();
    if (showDesigner_) drawMachineDesigner();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void Application::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    // File ----------------------------------------------------------------
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Load ROM...", "Ctrl+O"))
            loadRomDialog();

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
        ImGui::MenuItem("Screen",           nullptr, &showScreen_);
        ImGui::MenuItem("Terminal",         nullptr, &showTerminal_);
        ImGui::MenuItem("CPU State",        nullptr, &showCpuState_);
        ImGui::MenuItem("Machine Designer", nullptr, &showDesigner_);
        ImGui::EndMenu();
    }

    // Debug ---------------------------------------------------------------
    if (ImGui::BeginMenu("Debug")) {
        ImGui::MenuItem("Disassembler",  nullptr, &showDisasm_);
        ImGui::MenuItem("Memory Viewer", nullptr, &showMemView_);
        ImGui::EndMenu();
    }

    // Help ----------------------------------------------------------------
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) {
            termPrint("---");
            termPrint("The 8-Bit Machine  v0.8");
            termPrint("Design your own 8-bit computer.");
            termPrint("Pick a CPU, add devices, wire the address space.");
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
    ImGui::Begin("Screen", &showScreen_);

    // Upload VIC framebuffer to GPU if a new frame is ready
    VIC6566& vic = machine_.vic();
    if (vic.frameDirty()) {
        glBindTexture(GL_TEXTURE_2D, screenTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        VIC6566::WIDTH, VIC6566::HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        vic.framebuffer());
        glBindTexture(GL_TEXTURE_2D, 0);
        vic.clearDirty();
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale  = std::min(avail.x / VIC6566::WIDTH, avail.y / VIC6566::HEIGHT);
    if (scale < 1.0f) scale = 1.0f;
    const ImVec2 sz{ VIC6566::WIDTH * scale, VIC6566::HEIGHT * scale };

    ImGui::Image(static_cast<ImTextureID>(screenTex_), sz);

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

    // CIA1 summary
    ImGui::Separator();
    ImGui::TextUnformatted("CIA1");
    ImGui::Separator();

    const CIA6526& c   = machine_.cia1();
    const uint8_t  cra  = c.read(CIA6526::REG_CRA);
    const uint16_t taLo = c.read(CIA6526::REG_TALO);
    const uint16_t taHi = c.read(CIA6526::REG_TAHI);
    const uint16_t ta   = (taHi << 8) | taLo;
    const uint8_t  icr  = c.read(CIA6526::REG_ICR);

    ImGui::Text("Timer A  $%04X", (unsigned)ta);
    ImGui::Text("CRA      $%02X  %s",
        (unsigned)cra,
        (cra & CIA6526::CRA_START) ? "RUN" : "STP");
    ImGui::Text("ICR      $%02X", (unsigned)icr);

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

        if (isPC && disasmFollowPC_)
            ImGui::SetScrollHereY(0.25f);
    }

    ImGui::EndTable();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Machine Designer panel
// ---------------------------------------------------------------------------

void Application::drawMachineDesigner() {
    ImGui::SetNextWindowSize({ 500.0f, 380.0f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("Machine Designer", &showDesigner_);

    // CPU selector
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "CPU");
    ImGui::SameLine(80.0f);
    const char* cpuNames[] = { "MOS 8502", "WDC 65C02" };
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

    // Address-space device table
    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Address Space");

    constexpr ImGuiTableFlags tf =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("##devices", 4, tf)) {
        ImGui::TableSetupColumn("Start",  ImGuiTableColumnFlags_WidthFixed,  60.0f);
        ImGui::TableSetupColumn("End",    ImGuiTableColumnFlags_WidthFixed,  60.0f);
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& e : machine_.bus().devices()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("$%04X", (unsigned)e.start);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("$%04X", (unsigned)e.end);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.label.c_str());
            ImGui::TableSetColumnIndex(3);
            if (e.device)
                ImGui::TextDisabled("%s", e.device->statusLine().c_str());
            else
                ImGui::TextDisabled("—");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Future: add / remove devices and rewire the address space at runtime.");

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
    const uint16_t pc = machine_.cpu().getPC();

    if (memViewFollowPC_)
        memEditor_.GotoAddrAndHighlight(pc, pc);

    // Prepend a "Follow PC" toggle above the editor's own toolbar
    ImGui::SetNextWindowSize({ 680.0f, 500.0f }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Viewer", &showMemView_)) {
        ImGui::Checkbox("Follow PC", &memViewFollowPC_);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("PC=$%04X", (unsigned)pc);
        ImGui::Separator();
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
// Machine config dialogs
// ---------------------------------------------------------------------------

void Application::saveMachineConfigDialog() {
    const std::string path = FileDialog::saveFile(
        "Save Machine Config", "machine.json", {"json"});
    if (path.empty()) return;

    const auto result = machine_.saveConfig(path);
    termPrint(result.message);
}

void Application::loadMachineConfigDialog() {
    const std::string path = FileDialog::openFile(
        "Load Machine Config", {"json"});
    if (path.empty()) return;

    const auto result = machine_.loadConfig(path);
    termPrint(result.message);

    if (result.ok) {
        machine_.reset();
        cycleCount_      = 0;
        emulatorRunning_ = false;
        termPrint("[Config] Machine reset with new address map.");
        termPrint(machine_.cpu().stateString());
    }
}
