#include "Application.h"
#include "emulator/CIA6526.h"
#include "emulator/Disassembler.h"
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
}

Application::~Application() {
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
        "The 8-Bit Machine  |  8502 Emulator",
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

    // Mild style tweaks for a retro-tool feel
    ImGuiStyle& style       = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.TabRounding       = 2.0f;

    ImGui_ImplSDL2_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- Emulator ---
    cpu_.connectBus(&bus_);

    // CIA1 IRQ → CPU IRQ line
    bus_.cia1.onIRQ = [this]() { cpu_.irq(); };

    // Route CPU character output ($F000) to the terminal line buffer.
    // CR ($0D) is silently dropped; LF ($0A) flushes the line.
    // Any other printable byte is appended to the current line.
    bus_.onCharOut = [this](uint8_t c) {
        if (c == '\r') return;
        if (c == '\n') {
            termPrint(ioLineBuf_.empty() ? "" : ioLineBuf_);
            ioLineBuf_.clear();
        } else {
            ioLineBuf_ += static_cast<char>(c);
        }
    };

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

            // F-keys are emulator controls — always handled regardless of
            // whether ImGui has keyboard focus (e.g. the terminal input box).
            switch (sym) {
                case SDLK_F5:  emulatorRunning_ = true;  termPrint("[Emulator] Running..."); break;
                case SDLK_F6:  emulatorRunning_ = false; termPrint("[Emulator] Paused.");
                               termPrint(cpu_.stateString());                                break;
                case SDLK_F8:  emulatorReset();                                              break;
                case SDLK_F10: emulatorStep();                                               break;
                default:       break;
            }
        }
    }

    if (emulatorRunning_) {
        for (int i = 0; i < cyclesPerFrame_; ++i) {
            bus_.clock();   // tick CIA timers (and future devices) each cycle
            cpu_.clock();
            ++cycleCount_;

            // Check breakpoints only at instruction boundaries.
            if (cpu_.complete() && !breakpoints_.empty()
                    && breakpoints_.count(cpu_.PC)) {
                emulatorRunning_ = false;
                std::ostringstream bpMsg;
                bpMsg << "[Break] $" << std::uppercase << std::hex
                      << std::setfill('0') << std::setw(4) << (unsigned)cpu_.PC;
                termPrint(bpMsg.str());
                termPrint(cpu_.stateString());
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

        if (ImGui::MenuItem("Load State", "Ctrl+L"))
            termPrint("[File] Load State — not yet implemented");
        if (ImGui::MenuItem("Save State", "Ctrl+S"))
            termPrint("[File] Save State — not yet implemented");

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
            termPrint(cpu_.stateString());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Step",  "F10")) emulatorStep();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset", "F8"))  emulatorReset();
        ImGui::Separator();

        // Speed presets — cycles per frame × 60 fps = effective Hz
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
        ImGui::MenuItem("Screen",    nullptr, &showScreen_);
        ImGui::MenuItem("Terminal",  nullptr, &showTerminal_);
        ImGui::MenuItem("CPU State", nullptr, &showCpuState_);
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
            termPrint("The 8-Bit Machine  v0.1");
            termPrint("MOS 8502 CPU Emulator (Commodore 128)");
            termPrint("---");
        }
        ImGui::EndMenu();
    }

    // Inline status
    ImGui::Separator();
    const double effMHz = (cyclesPerFrame_ * 60.0) / 1'000'000.0;
    ImGui::TextDisabled("  Cycles: %llu   PC: $%04X   %.2f MHz   %s",
        (unsigned long long)cycleCount_,
        (unsigned)cpu_.PC,
        effMHz,
        emulatorRunning_ ? "RUNNING" : "PAUSED");

    ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------------------
// Screen panel  (black canvas — will become a texture render target)
// ---------------------------------------------------------------------------

void Application::drawScreen() {
    ImGui::Begin("Screen", &showScreen_);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail  = ImGui::GetContentRegionAvail();

    // Maintain 8:5 aspect ratio (320×200)
    float scale = std::min(avail.x / 320.0f, avail.y / 200.0f);
    if (scale < 1.0f) scale = 1.0f;
    const ImVec2 sz{ 320.0f * scale, 200.0f * scale };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, { origin.x + sz.x, origin.y + sz.y },
                      IM_COL32(0, 0, 0, 255));
    dl->AddRect(origin, { origin.x + sz.x, origin.y + sz.y },
                IM_COL32(55, 55, 55, 255));

    const char* label = "[ No Signal ]";
    const ImVec2 ts   = ImGui::CalcTextSize(label);
    dl->AddText({ origin.x + (sz.x - ts.x) * 0.5f,
                  origin.y + (sz.y - ts.y) * 0.5f },
                IM_COL32(40, 40, 40, 255), label);

    ImGui::Dummy(sz);
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

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.4f, 1.0f));  // green
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
    ImGui::SetNextWindowSizeConstraints({ 200.0f, 180.0f }, { 400.0f, 400.0f });
    ImGui::Begin("CPU State", &showCpuState_);

    ImGui::TextUnformatted("MOS 8502");
    ImGui::Separator();

    ImGui::Text("PC   $%04X", (unsigned)cpu_.PC);
    ImGui::Text("A    $%02X    (%3d)", (unsigned)cpu_.A, (unsigned)cpu_.A);
    ImGui::Text("X    $%02X    (%3d)", (unsigned)cpu_.X, (unsigned)cpu_.X);
    ImGui::Text("Y    $%02X    (%3d)", (unsigned)cpu_.Y, (unsigned)cpu_.Y);
    ImGui::Text("SP   $%02X", (unsigned)cpu_.SP);
    ImGui::Text("P    $%02X", (unsigned)cpu_.P);
    ImGui::Separator();

    // Flag display: bright green = set, dim = clear
    auto flagCell = [&](const char* name, bool set) {
        if (set) ImGui::TextColored({ 0.0f, 1.0f, 0.4f, 1.0f }, "%s", name);
        else     ImGui::TextDisabled("%s", name);
        ImGui::SameLine(0.0f, 4.0f);
    };

    ImGui::TextUnformatted("Flags  ");
    ImGui::SameLine(0.0f, 0.0f);
    flagCell("N", cpu_.P & CPU8502::N);
    flagCell("V", cpu_.P & CPU8502::V);
    flagCell("-", cpu_.P & CPU8502::U);
    flagCell("B", cpu_.P & CPU8502::B);
    flagCell("D", cpu_.P & CPU8502::D);
    flagCell("I", cpu_.P & CPU8502::I);
    flagCell("Z", cpu_.P & CPU8502::Z);
    flagCell("C", cpu_.P & CPU8502::C);

    ImGui::Separator();
    ImGui::Text("Cycles  %llu", (unsigned long long)cycleCount_);

    // CIA1 summary
    ImGui::Separator();
    ImGui::TextUnformatted("CIA1");
    ImGui::Separator();

    const CIA6526& c = bus_.cia1;
    const uint8_t  cra   = c.read(CIA6526::REG_CRA);
    const uint16_t taLo  = c.read(CIA6526::REG_TALO);
    const uint16_t taHi  = c.read(CIA6526::REG_TAHI);
    const uint16_t ta    = (taHi << 8) | taLo;
    const uint8_t  icr   = c.read(CIA6526::REG_ICR);

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

    // --- Controls bar ---
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

    // --- Build line list ---
    // When following PC: start ~40 bytes before PC so it lands in the upper third.
    // 40 bytes back = ~16 instructions worst-case (2.5 bytes avg).
    // We disassemble 60 instructions total which covers the window and beyond.
    const uint16_t viewStart = disasmFollowPC_
        ? static_cast<uint16_t>(cpu_.PC > 40 ? cpu_.PC - 40 : 0)
        : disasmViewAddr_;

    const auto lines = Disassembler::disassemble(bus_, viewStart, 60);

    // --- Table ---
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
    ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed,   58.0f);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed,   76.0f);
    ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const auto& line : lines) {
        const bool isPC = (line.addr == cpu_.PC);
        const bool hasBP = breakpoints_.count(line.addr) > 0;

        ImGui::TableNextRow();

        // Breakpoint row background
        if (hasBP)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   IM_COL32(90, 20, 20, 255));

        // -- Address column: invisible spanning selectable for click-to-toggle --
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

        // Breakpoint dot / PC arrow
        if (isPC && hasBP)
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "►$%04X", line.addr);
        else if (isPC)
            ImGui::TextColored({ 0.2f, 1.0f, 0.4f, 1.0f }, "► $%04X", line.addr);
        else if (hasBP)
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "● $%04X", line.addr);
        else
            ImGui::TextDisabled("  $%04X", line.addr);

        // -- Raw bytes column --
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

        // -- Mnemonic + operand column --
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

        // Auto-scroll: when following PC, keep it ~25% from the top
        if (isPC && disasmFollowPC_)
            ImGui::SetScrollHereY(0.25f);
    }

    ImGui::EndTable();
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
    // Finish any in-progress instruction first, then run exactly one more.
    do {
        cpu_.clock();
        ++cycleCount_;
    } while (!cpu_.complete());
    termPrint(cpu_.stateString());
}

void Application::emulatorReset() {
    bus_.reset();
    cpu_.reset();
    cycleCount_      = 0;
    emulatorRunning_ = false;

    termLines_.clear();
    termPrint("The 8-Bit Machine  |  8502 Emulator  v0.1");
    termPrint("==========================================");
    termPrint("CPU:    MOS 8502  (CMOS 6502 variant, 2 MHz)");
    termPrint("Memory: 64 KB RAM  (flat, no banking yet)");
    termPrint("");
    termPrint("System reset.");
    termPrint(cpu_.stateString());
    termPrint("");
    termPrint("Keyboard shortcuts:");
    termPrint("  F5   Run       F6  Pause");
    termPrint("  F8   Reset     F10 Step");
    termPrint("");
}

// ---------------------------------------------------------------------------
// Memory viewer panel  — full 64 KB scrollable hex editor
// ---------------------------------------------------------------------------

// Returns a short region label for the address gutter, or nullptr.
static const char* memRegionLabel(uint16_t addr) {
    if (addr == 0x0000) return "ZP ";   // zero page
    if (addr == 0x0100) return "STK";   // stack
    if (addr == 0x0200) return "RAM";   // general RAM
    if (addr == 0xF000) return "I/O";   // CHAR_OUT
    if (addr == 0xF100) return "CI1";   // CIA1
    if (addr == 0xF200) return "CI2";   // CIA2
    if (addr == 0xF300) return "SID";   // future SID
    if (addr == 0xFFFA) return "VEC";   // NMI/RST/IRQ vectors
    return nullptr;
}

void Application::drawMemoryViewer() {
    ImGui::SetNextWindowSize({ 620.0f, 500.0f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory Viewer", &showMemView_);

    // -----------------------------------------------------------------------
    // Controls bar
    // -----------------------------------------------------------------------
    ImGui::Checkbox("Follow PC", &memViewFollowPC_);
    ImGui::SameLine(0.0f, 16.0f);

    ImGui::TextUnformatted("Go to:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(52.0f);
    const bool gotoEnter = ImGui::InputText(
        "##memaddr", memViewInput_, sizeof(memViewInput_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool gotoClick = ImGui::Button("Go");

    if (gotoEnter || gotoClick) {
        if (memViewInput_[0] != '\0') {
            memViewAddr_     = static_cast<uint16_t>(std::stoul(memViewInput_, nullptr, 16));
            memViewFollowPC_ = false;
        }
    }

    if (memViewFollowPC_)
        memViewAddr_ = cpu_.PC;

    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextDisabled("PC=$%04X", (unsigned)cpu_.PC);

    ImGui::Separator();

    // -----------------------------------------------------------------------
    // Column layout constants
    // -----------------------------------------------------------------------
    constexpr int   kCols       = 16;
    constexpr int   kTotalRows  = 65536 / kCols;   // 4 096 rows for full 64 KB
    constexpr float kRegionW    = 34.0f;  // region label gutter
    constexpr float kAddrW      = 52.0f;  // "$XXXX" address
    constexpr float kByteW      = 22.0f;  // one hex byte cell
    constexpr float kGapW       =  8.0f;  // gap before ASCII
    constexpr float kAsciiW     = 10.0f;  // one ASCII char cell (monospace)

    // -----------------------------------------------------------------------
    // Column header (drawn above the scrolling child)
    // -----------------------------------------------------------------------
    {
        ImGui::Dummy({ kRegionW + kAddrW, ImGui::GetTextLineHeight() });  // gutter spacer
        for (int c = 0; c < kCols; ++c) {
            ImGui::SameLine(kRegionW + kAddrW + c * kByteW);
            ImGui::TextDisabled("%02X", c);
        }
        ImGui::SameLine(kRegionW + kAddrW + kCols * kByteW + kGapW);
        ImGui::TextDisabled("0123456789ABCDEF");
    }
    ImGui::Separator();

    // -----------------------------------------------------------------------
    // Scrollable body — ImGuiListClipper renders only visible rows
    // -----------------------------------------------------------------------
    ImGui::BeginChild("##membody", { 0.0f, 0.0f }, false, ImGuiWindowFlags_NoNav);

    // Scroll to the target row when Follow PC is active or after a Go jump
    const int targetRow = (memViewAddr_ & 0xFFF0u) / kCols;

    if (memViewFollowPC_)
        ImGui::SetScrollY(static_cast<float>(targetRow) * ImGui::GetTextLineHeightWithSpacing()
                          - ImGui::GetWindowHeight() * 0.25f);

    // One-shot scroll after a Go button press (detect by checking if the
    // current scroll position is far from the target)
    {
        const float targetY  = static_cast<float>(targetRow) * ImGui::GetTextLineHeightWithSpacing();
        const float currentY = ImGui::GetScrollY();
        const float windowH  = ImGui::GetWindowHeight();
        if (targetY < currentY || targetY > currentY + windowH - windowH * 0.25f) {
            if (!memViewFollowPC_ && (gotoEnter || gotoClick))
                ImGui::SetScrollY(targetY - windowH * 0.25f);
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(kTotalRows);

    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const uint16_t rowAddr = static_cast<uint16_t>(row * kCols);

            // -- Region label gutter --
            const char* lbl = memRegionLabel(rowAddr);
            if (lbl)
                ImGui::TextColored({ 0.5f, 0.8f, 1.0f, 1.0f }, "%-3s", lbl);
            else
                ImGui::TextDisabled("   ");

            // -- Address --
            const bool rowHasPC = (cpu_.PC >= rowAddr &&
                                   cpu_.PC <  static_cast<uint16_t>(rowAddr + kCols));
            ImGui::SameLine(kRegionW);
            if (rowHasPC)
                ImGui::TextColored({ 1.0f, 1.0f, 0.3f, 1.0f }, "$%04X", (unsigned)rowAddr);
            else
                ImGui::TextDisabled("$%04X", (unsigned)rowAddr);

            // -- Hex bytes --
            for (int col = 0; col < kCols; ++col) {
                const uint16_t addr = static_cast<uint16_t>(rowAddr + col);
                const uint8_t  val  = bus_.read(addr);
                const bool     isPC = (addr == cpu_.PC);

                ImGui::SameLine(kRegionW + kAddrW + col * kByteW);
                if (isPC)
                    ImGui::TextColored({ 1.0f, 1.0f, 0.3f, 1.0f }, "%02X", val);
                else if (val == 0x00)
                    ImGui::TextDisabled("%02X", val);
                else
                    ImGui::Text("%02X", val);
            }

            // -- ASCII --
            ImGui::SameLine(kRegionW + kAddrW + kCols * kByteW + kGapW);
            char ascii[kCols + 1];
            for (int col = 0; col < kCols; ++col) {
                const uint8_t v = bus_.read(static_cast<uint16_t>(rowAddr + col));
                ascii[col] = (v >= 0x20 && v < 0x7F) ? static_cast<char>(v) : '.';
            }
            ascii[kCols] = '\0';
            ImGui::TextDisabled("%s", ascii);
        }
    }
    clipper.End();

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// ROM loading
// ---------------------------------------------------------------------------

void Application::loadRomDialog() {
    const std::string path = FileDialog::openFile(
        "Load ROM", {"prg", "PRG", "bin", "BIN", "rom", "ROM"});

    if (path.empty()) return;  // user cancelled

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

    // -----------------------------------------------------------------------
    // Detect format from extension
    // -----------------------------------------------------------------------
    auto toLower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const size_t dot     = path.rfind('.');
    const std::string ext = dot != std::string::npos ? toLower(path.substr(dot + 1)) : "";
    const bool isPrg     = (ext == "prg");

    uint16_t loadAddr  = 0x0200;  // default for raw binary
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

    // -----------------------------------------------------------------------
    // Write bytes into RAM through the bus
    // -----------------------------------------------------------------------
    uint16_t addr       = loadAddr;
    size_t   bytesLoaded = 0;

    for (size_t i = 0; i < dataBytes; ++i) {
        const int c = file.get();
        if (c == EOF) break;
        bus_.write(addr++, static_cast<uint8_t>(c));
        ++bytesLoaded;
    }

    // -----------------------------------------------------------------------
    // Update the reset vector so F8 re-runs the loaded program
    // -----------------------------------------------------------------------
    bus_.write(0xFFFC, loadAddr & 0x00FF);
    bus_.write(0xFFFD, (loadAddr >> 8) & 0x00FF);

    // -----------------------------------------------------------------------
    // Reset CPU and sync UI
    // -----------------------------------------------------------------------
    cpu_.reset();
    cycleCount_      = 0;
    emulatorRunning_ = false;
    disasmViewAddr_  = loadAddr;
    disasmFollowPC_  = true;
    showDisasm_      = true;   // open the disassembler automatically

    // -----------------------------------------------------------------------
    // Terminal feedback
    // -----------------------------------------------------------------------
    const size_t slash    = path.find_last_of("/\\");
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
    termPrint(cpu_.stateString());
    termPrint("---");
}
