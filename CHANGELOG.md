# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.9.0] - 2026-04-16

### Added
- **Machine config save / load** (File → Save Machine Config / Load Machine Config) — persists the address-space wiring as a JSON file so machines can be recalled and shared
- `FileDialog::saveFile()` — native macOS NSSavePanel wrapper
- nlohmann/json v3.11.3 added as a CMake FetchContent dependency (header-only, fetched automatically)

### Config format (version 1)
```json
{ "version": 1, "cpu": "MOS 8502",
  "devices": [{ "id": "cia1", "label": "CIA1 $F100–$F1FF", "start": "F100", "end": "F1FF" }, …] }
```
Device IDs: `cia1`, `cia2`, `ram`, `char_out`.

---

## [0.8.0] - 2026-04-16

### Changed — Architecture pivot: general-purpose 8-bit machine designer

The project pivoted from a Commodore 128-specific emulator to a general-purpose
8-bit machine designer where you pick a CPU, add devices, and wire them into the
address space.

### Added
- `IBusDevice` interface — any peripheral implements `reset()`, `clock()`, `read(offset)`, `write(offset, value)`, and an optional `statusLine()` for the designer panel
- `ICPU` interface — any CPU implements `reset()`, `clock()`, `irq()`, `nmi()`, `complete()`, `stateString()`
- `Machine` class — owns all device instances and builds the default address map; exposes typed accessors for the UI
- **Machine Designer panel** (View → Machine Designer) — live table of all mounted devices showing address ranges and per-device status lines

### Changed
- `Bus` refactored from hardcoded CIA/RAM members to a priority-ordered `vector<DeviceEntry>`; first matching device wins on every read/write
- `CIA6526`, `Memory` now implement `IBusDevice` (`read`/`write` take `uint16_t offset`)
- `CPU8502` now implements `ICPU`
- `Application` uses `Machine` instead of bare `Bus` + `CPU8502` members
- Window title updated to reflect new app identity

---

## [0.7.0] - 2026-04-15

### Added
- **CIA1 (MOS 6526)** at `$F100–$F1FF` — Timer A with continuous/one-shot countdown, ICR interrupt mask and flags, data ports PRA/PRB; CIA1 IRQ wired to CPU IRQ line
- **CIA2 stub** at `$F200–$F2FF` — registers readable/writable, no side effects
- CIA1 Timer A counter, CRA, and ICR displayed live in the CPU State panel
- **Breakpoints** — click any disassembler row to toggle; run loop halts automatically when PC hits a breakpoint
- **Configurable clock speed** — Emulator → Speed presets: ~60 kHz (debug), ~500 kHz, ~1 MHz, ~2 MHz; effective MHz shown in the menu bar status line
- `Bus::clock()` — ticks all bus-attached devices once per emulated cycle
- Decoded address bus with reserved I/O ranges (`$F000–$FBFF`)

### Fixed
- IRQ fires only once bug — `setFlag(I, true)` was called before `stackPush(P)`, so RTI restored I=1 and blocked all subsequent IRQs; fixed by pushing P first (with original I=0) then setting I
- F10 now steps a full instruction rather than a single clock tick
- F-key shortcuts (F5/F6/F8/F10) now work regardless of ImGui keyboard focus

### Added — ROM tooling
- `roms/test.s` — CIA1 Timer A interrupt demo: patches the IRQ vector at runtime, starts Timer A, prints `CIA1 TIMER`, then streams `*` lines from the IRQ handler at ~5 Hz (at 60 kHz debug speed)
- `build.sh` assembles all `roms/*.s` files via ca65/ld65 before building

---

## [0.1.0] - 2026-04-14  *(initial commit)*

### Added
- MOS 8502 CPU — all 56 legal opcodes, all 13 addressing modes, cycle-accurate timing
- IRQ, NMI, BRK / RTI with correct flag and stack handling
- NMOS 6502 indirect JMP page-wrap bug reproduced
- Illegal opcodes mapped to extended NOP
- SDL2 + Dear ImGui (docking branch v1.91.6) GUI with dockable panels
- Screen panel (320×200 placeholder canvas)
- Terminal panel — green-on-black scrollable log, command input
- CPU State panel — live register and flag display, cycle counter
- Disassembler panel — live disassembly, Follow PC, Go To address, current instruction highlight
- Memory Viewer panel — 16-column hex+ASCII grid, full 64 KB scrollable via `ImGuiListClipper`, PC highlighted
- ROM loading (File → Load ROM) — native macOS file dialog, raw `.bin` and Commodore `.prg` support
- CHAR_OUT port at `$F000` — CPU writes routed to Terminal panel (line-buffered, flushed on LF)
- 64 KB flat RAM with reset vector pointing to NOP stub at `$0200`
- CMake build system; Dear ImGui fetched automatically via `FetchContent`
- macOS `NSOpenPanel` file dialog (`FileDialog.mm`)
