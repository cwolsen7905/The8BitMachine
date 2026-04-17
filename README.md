# The 8-Bit Machine

A general-purpose **8-bit machine designer** written in C++17.

Design your own 8-bit computer: pick a CPU architecture, add components (RAM, timers, I/O ports, …), and wire them into the address space — all at runtime, with a live debugger watching every cycle.

The architecture is intentionally extensible: new CPUs and bus devices are defined by implementing a simple interface (`ICPU` / `IBusDevice`), so the community can contribute new chips without touching the core.

The default machine that ships out of the box is a **MOS 8502** system (the CPU from the Commodore 128) with 64 KB RAM, two CIA 6526 timer/IO chips, and a debug character-output port.

---

## Current State  (v0.8)

### Machine Designer
- **`IBusDevice` interface** — any chip or peripheral implements `reset()`, `clock()`, `read(offset)`, `write(offset, value)`, and an optional `statusLine()` for the designer panel
- **`ICPU` interface** — any CPU implements `reset()`, `clock()`, `irq()`, `nmi()`, `complete()`, `stateString()`
- **Dynamic address-space map** — devices are registered with `bus.addDevice(start, end, device)` in priority order; unmatched reads return `$FF` (open bus)
- **Machine class** — owns device instances, builds the default address map, exposes typed accessors for the UI
- **Machine Designer panel** (View menu) — live table of all mounted devices with address ranges and per-device status

### CPU  (MOS 8502)
- All 56 legal 6502/8502 opcodes, all 13 addressing modes
- Cycle-accurate timing with page-cross and branch penalties
- IRQ and NMI with full stack push and vector load
- BRK / RTI with correct flag handling
- NMOS 6502 indirect JMP page-wrap bug reproduced
- Illegal opcodes mapped to extended NOP

### GUI
- Dockable panel layout (Dear ImGui docking branch)
- Standard menu bar — File / Emulator / View / Debug / Help
- **Screen panel** — 320×200 canvas (placeholder; texture rendering not yet wired)
- **Terminal panel** — green-on-black scrollable log with command input
- **CPU State panel** — live register and flag display, CIA1 timer status, cycle counter
- **Disassembler panel** (Debug menu) — live disassembly with Follow PC, Go To address, highlighted current instruction; click any row to toggle a breakpoint (red `●`); emulator halts automatically when PC hits a breakpoint
- **Memory Viewer panel** (Debug menu) — 16-column hex+ASCII grid, full 64 KB scrollable, jump to any address, PC highlighted in yellow
- **Machine Designer panel** (View menu) — address map table with device names, ranges, and live status lines
- **ROM loading** (File → Load ROM) — native macOS file dialog; supports raw `.bin` and Commodore `.prg`; resets CPU and jumps disassembler to load address
- **Machine config save / load** (File → Save / Load Machine Config) — persists the address-space wiring as a JSON file so machines can be recalled and shared

### Emulator core
- 64 KB flat RAM; reset vector points to the loaded program or the built-in NOP stub at `$0200`
- **CIA1 (MOS 6526) at `$F100–$F1FF`** — Timer A with IRQ, ICR mask/flag, data ports PRA/PRB; CIA1 IRQ wired to CPU IRQ line
- **CIA2 stub at `$F200–$F2FF`** — registers readable/writable, no side effects yet
- **CHAR_OUT port at `$F000`** — CPU writes here appear in the Terminal panel (line-buffered; flushed on LF)
- **F10 instruction step** — runs the CPU until the current instruction completes
- **Configurable clock speed** — Emulator → Speed presets: ~60 kHz (debug), ~500 kHz, ~1 MHz, ~2 MHz; effective MHz shown in the menu bar

### ROM development
- `roms/test.s` — CIA1 Timer A interrupt demo: patches the IRQ vector at runtime, programs CIA1 Timer A to fire ~5× per second (at 60 kHz debug speed), then spins in a loop while the IRQ handler writes `*` + newline to the terminal on every timer tick
- `build.sh` assembles all `roms/*.s` files via ca65/ld65 before building the C++ emulator
- Load via **File → Load ROM → `roms/test.prg`**, press **F5** — `CIA1 TIMER` appears once, then `*` lines stream in driven purely by CIA1 IRQs

---

## Prerequisites

| Tool | Install |
|------|---------|
| CMake ≥ 3.20 | `brew install cmake` |
| SDL2 | `brew install sdl2` |
| cc65 (ca65/ld65) | `brew install cc65` |
| C++17 compiler | Xcode Command Line Tools (`xcode-select --install`) |

Dear ImGui (docking branch, v1.91.6) is fetched automatically by CMake.

cc65 is only needed to rebuild the 6502 assembly ROMs from source. The build script skips the assembly step with a warning if cc65 is not installed.

---

## Building

```bash
./build.sh             # Debug build (default)
./build.sh Release     # Release build
```

Or manually:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

The binary is placed at `build/the-8-bit-machine`.

---

## Running

```bash
./build/the-8-bit-machine
```

The UI layout is saved to `imgui_layout.ini` so panel positions persist between sessions.

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F5  | Run emulator |
| F6  | Pause emulator |
| F8  | Reset (reloads reset vector, clears registers) |
| F10 | Step one instruction |

---

## Project Structure

```
the-8-bit-machine/
├── CMakeLists.txt
├── build.sh
├── roms/
│   ├── test.s          CIA1 Timer A interrupt demo (6502 assembly)
│   ├── test.cfg        ld65 linker config (flat binary at $0200)
│   └── test.prg        assembled PRG (generated by build.sh)
└── src/
    ├── main.cpp
    ├── gui/
    │   ├── Application.h / .cpp    Window, event loop, all panel rendering
    │   ├── FileDialog.h            File-open dialog interface
    │   └── FileDialog.mm           macOS NSOpenPanel implementation
    └── emulator/
        ├── IBusDevice.h            Interface every bus device implements
        ├── ICPU.h                  Interface every CPU implements
        ├── Machine.h / .cpp        Owns CPU + devices; builds default address map
        ├── Bus.h / .cpp            Dynamic address-space router
        ├── CPU8502.h / .cpp        MOS 8502 (implements ICPU)
        ├── CIA6526.h / .cpp        MOS 6526 CIA (implements IBusDevice)
        ├── Memory.h / .cpp         64 KB flat RAM (implements IBusDevice)
        └── Disassembler.h / .cpp   Stateless 6502 disassembler
```

### How the address space works

`Bus` holds a priority-ordered list of `DeviceEntry` records (`start`, `end`, `IBusDevice*`, `label`).  On every read or write the bus walks the list and delegates to the first matching device, passing `addr - entry.start` as the offset.  The special CHAR_OUT port (`$F000`) is intercepted before the list walk.

Device instances are owned by `Machine`.  The default map is:

| Priority | Range | Device |
|----------|-------|--------|
| 1 | `$F100–$F1FF` | CIA1 (MOS 6526) |
| 2 | `$F200–$F2FF` | CIA2 (MOS 6526) |
| 3 | `$F000` | CHAR_OUT debug port |
| 4 | `$0000–$FFFF` | 64 KB RAM (catch-all) |

### Adding a new device

1. Create a class that inherits `IBusDevice`.
2. Override `deviceName()`, `reset()`, `read()`, `write()`, and optionally `clock()` / `statusLine()`.
3. Instantiate it in `Machine` (or anywhere with stable lifetime) and call `bus.addDevice(start, end, &myDevice, "label")` before the RAM catch-all entry.

### Adding a new CPU

1. Create a class that inherits `ICPU`.
2. Override all pure-virtual methods.
3. Inject the bus with `connectBus(&bus)` and pass IRQ/NMI signals from CIA / other sources.

---

## Roadmap

- [x] Full 8502 instruction set (all 56 legal opcodes + addressing modes)
- [x] Interrupt handling (IRQ, NMI, BRK / RTI)
- [x] Disassembler panel with breakpoints
- [x] ROM loading (`.bin` / `.prg` via File → Load ROM)
- [x] Memory-mapped I/O: CHAR_OUT port at `$F000`
- [x] 6502 ASM test ROM with build system integration (ca65/ld65)
- [x] Memory viewer (hex+ASCII, full 64 KB, jump to address, PC highlight)
- [x] F10 steps a full instruction
- [x] Configurable clock speed with real-MHz display
- [x] CIA1 (MOS 6526) — Timer A + IRQ + data ports
- [x] CIA2 stub at `$F200`
- [x] **`IBusDevice` / `ICPU` interfaces** — extensible plugin architecture
- [x] **Dynamic address-space map** — devices registered at runtime, priority routing
- [x] **Machine Designer panel** — live address map with device status
- [ ] Machine Designer: add / remove / rewire devices at runtime via UI
- [x] **JSON machine config** — save and load machine definitions (File → Save/Load Machine Config)
- [ ] Second CPU (Z80 or 65C02) to validate the ICPU abstraction
- [ ] VIC-IIe video stub → render to Screen panel texture
- [ ] SID audio stub (MOS 6581/8580)
- [ ] Keyboard input via CIA1 matrix
- [ ] Proper ROM regions and bank switching

---

## Dependencies

| Library | Version | Source |
|---------|---------|--------|
| [SDL2](https://libsdl.org) | ≥ 2.0.22 | Homebrew |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.6-docking | CMake FetchContent |
| OpenGL | 3.3 core | System (macOS) |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | CMake FetchContent |
| [cc65](https://cc65.github.io) (ca65/ld65) | ≥ 2.19 | Homebrew (optional) |
