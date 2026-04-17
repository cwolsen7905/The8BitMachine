# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.21.0] - 2026-04-17

### Added
- **MOS 6510 CPU** — NMOS 6502 subclass with built-in 6-bit I/O port at `$00`/`$01`; selectable in Machine Designer CPU dropdown alongside MOS 8502 and WDC 65C02
  - `$00` — direction register (DDR): 1=output, 0=input per bit; power-on default `$2F`
  - `$01` — data register; power-on default `$37` (LORAM=1, HIRAM=1, CHAREN=1)
  - Reads/writes to `$00`/`$01` are intercepted before reaching the bus; all other accesses pass through normally
  - `onIOWrite(data, dir)` callback fires on every port write so a `BankController` or custom handler can switch `SwitchableRegion`s — the foundation for the C64 memory map
  - `CPU6502Base::busRead` / `busWrite` made `virtual` to enable the override; no other behaviour changed
- `Machine::cpu6510()` accessor for direct access to the 6510 I/O port from machine-preset code

### Changed
- Machine Designer CPU selector now lists "MOS 8502", "MOS 6510", and "WDC 65C02"

---

## [0.20.0] - 2026-04-17

### Added
- **SwitchableRegion device** (`src/emulator/devices/SwitchableRegion`) — proxy `IBusDevice` that forwards reads/writes to one of N registered child devices; `select(n)` swaps the active child at runtime; options can be any device type (RAM, ROM, I/O, open bus); `statusLine()` shows current bank and option label
- **BankController device** (`src/emulator/devices/BankController`) — 1-byte I/O register; `addMapping(value, region, bankIndex)` registers actions that fire when a value is written; a single write can switch multiple `SwitchableRegion`s simultaneously (e.g. C64 `$01` port); optional `onWrite` callback for machine-specific logic; `statusLine()` shows current value, region count, and mapping count
- **Machine Designer → Add Switchable Region** — Start/End address fields; "Add Switchable Region" button creates an empty proxy; tooltip explains wiring via config/preset; bank-selector dropdown appears live in the Status column for any `SwitchableRegion` on the bus
- **Machine Designer → Add Bank Controller** — Address field; "Add Bank Controller" button mounts the controller; tooltip explains mapping via config/preset
- `Machine::mountSwitchableRegion(start, end, label)` — creates and maps a `SwitchableRegion`
- `Machine::addRegionOption(region, dev, label)` — appends a child device option to a region
- `Machine::mountBankController(addr, label)` — creates and maps a `BankController`
- `Machine::addControllerMapping(ctrl, value, region, bankIndex)` — wires one mapping entry

---

## [0.19.0] - 2026-04-17

### Added
- **BankedMemory device** (`src/emulator/devices/BankedMemory`) — N equal banks of RAM mapped at one address window; `selectBank(n)` switches the visible bank; reads/writes go to the active bank; `statusLine()` shows current bank and size
- **BankSelectPort device** (`src/emulator/devices/BankSelectPort`) — single-byte I/O port companion to `BankedMemory`; CPU write selects the bank, read returns the current bank number
- **Machine Designer → Add Banked RAM** — Start, End, BankSel address, and Banks count fields; "Add Banked RAM" button mounts both devices in one step; success shown in green, errors in red
- `Machine::mountBankedMemory(primaryStart, primaryEnd, bankSelectAddr, numBanks)` — creates both devices, takes ownership via `dynamicDevices_`, maps them on the bus; returns `nullptr` on degenerate range
- Machine config save/load extended: `banked_ram` entries serialise with `num_banks` and `bank_select_addr` fields and are reconstructed via `mountBankedMemory` on load; `bank_select` entries are reconstructed automatically (not saved separately)

---

## [0.18.0] - 2026-04-17

### Added
- **ROM device** (`src/emulator/devices/ROM`) — read-only `IBusDevice`; writes silently ignored; reads beyond loaded data return `$FF`; `.prg` files have their 2-byte Commodore load-address header stripped automatically
- **Machine Designer → Load ROM File** — "Start" address field + "Browse..." button; file size determines the end address automatically (clamped to `$FFFF`); success shown in green, errors in red
- `Machine::mountROM(start, end, label, path)` — loads a file into a new `ROM`, takes ownership via `dynamicDevices_`, and maps it on the bus; returns `nullptr` on file error
- `Machine::unmountAt(busIndex)` — removes a bus entry and frees the dynamic device if no other entries reference it; replaces the previous `bus_.removeAt()` calls in the Machine Designer so ROM objects are properly released
- Machine config save/load extended: ROM entries serialise with a `"path"` field and are reloaded from file on config load
- `dynamicDevices_` (`vector<unique_ptr<IBusDevice>>`) added to `Machine` to own runtime-created devices

### Changed
- `Machine::resetAddressMap()` now clears `dynamicDevices_` in addition to the bus entries

---

## [0.17.0] - 2026-04-17

### Changed
- **Machine Designer panel** — fully interactive address-space editor replacing the previous read-only table
  - **Add Device** — dropdown of all known devices (VIC, SID, CIA1, CIA2, RAM, CHAR_OUT) with editable hex address fields; defaults auto-fill on selection change
  - **Remove** — per-row `×` button unmaps a device from the address space without destroying it
  - **Inline address editing** — click any Start or End cell to edit in place; Enter or focus-loss commits, Escape cancels
  - **Drag-to-reorder** — drag the `=` handle to change bus priority order; row highlights on hover during drag
  - **Sort by Address** — sorts entries by start address (stable sort preserving relative order of equal entries)
  - **Reset to Defaults** — clears the bus and rebuilds the standard default map
  - **Validation warnings** — red row tint for invalid ranges (Start > End), orange row tint for unreachable entries fully shadowed by a higher-priority entry, yellow text warning when no catch-all `$0000–$FFFF` entry is present; tooltip on drag handle explains the issue
- `Bus` — added `removeAt(index)`, `modifyAt(index, start, end)`, `moveEntry(from, to)`, `sortByAddress()`
- `Machine` — added `resetAddressMap()`, made `deviceForId()` / `idForDevice()` public for use by the designer panel

---

## [0.16.1] - 2026-04-17

### Added
- **SID filter routing** — Chamberlin state-variable filter (LP/BP/HP modes); `$D417` routes voices through the filter; `$D418` bits 4–6 select LP/BP/HP output mix; resonance applied as damping
- **Ring modulation** — `RING` control bit XORs the triangle fold bit with the modulator oscillator MSB; voice N modulated by voice (N+2)%3
- **Hard sync** — `SYNC` control bit resets target oscillator phase when source oscillator overflows; source routing matches real 6581 (`kSyncSrc[]`)
- **OSC3 / ENV3 readback** — `$D41B` and `$D41C` return live voice 3 oscillator and envelope values via `std::atomic<uint8_t>`
- **`roms/sid_playground.s`** — interactive SID synthesizer: keys QWERTYUI play notes C4–C5 on voice 1; VIC screen shows active key indicators and note name; border colour changes per note; loads at `$0800` to avoid screen RAM overlap
- **`roms/sid_demo.s`** updated to three sections: plain sawtooth+triangle → LP filter sweep (cutoff rises each note) → hard sync (V3 pulse at 1.5× melody) + ring mod (V2 triangle modulated by V1)

### Changed
- SID phase accumulator corrected to 24-bit (`& 0xFFFFFF`); fixes clicks/pops and off-pitch notes introduced in 0.16.0
- `generateSamples()` takes a register snapshot under mutex then synthesizes without holding the lock

---

## [0.16.0] - 2026-04-16

### Added
- **SID audio synthesis** — SDL audio device opened at 44100 Hz mono float32; SID6581 generates samples in the SDL audio callback
  - 32-bit phase accumulator per voice; SID PAL clock (985248 Hz) used for pitch accuracy
  - All four waveforms: triangle, sawtooth, pulse (12-bit pulse width), noise (23-bit LFSR with correct 6581 tap positions)
  - Full ADSR envelope per voice with datasheet-accurate attack/decay/release time tables (2 ms – 8 s); sustain level 0–15
  - Gate edge detection starts attack / triggers release
  - TEST bit resets oscillator phase and freezes LFSR
  - Master volume register (`$D418` bits 3–0) applied to final mix
  - Register snapshot at start of each buffer avoids holding mutex during synthesis

### Added (ROM)
- `roms/sid_demo.s` — two-voice SID demo: sawtooth melody (C major scale, up and down) on voice 1, triangle bass drone on voice 2; ADSR configured for a punchy melody attack with a held bass

### Changed
- `SID6581::write()` and `statusLine()` take a `std::mutex` lock to protect register state shared with the audio thread
- `Application::init()` now calls `SDL_INIT_AUDIO`; audio device closed in destructor

---

## [0.15.0] - 2026-04-16

### Added
- **SID6581 stub** (`$D400–$D7FF`) — all 29 MOS 6581/8580 registers modelled; writes stored, reads return `$FF` (write-only behaviour matches real hardware); read-only registers PotX/PotY return `$FF`, OSC3/ENV3 return `$00`
- `statusLine()` shows master volume, filter cutoff frequency, and voice 1 frequency word in the Machine Designer panel
- Config save/load recognises device id `"sid"`

---

## [0.14.0] - 2026-04-16

### Added
- **CIA1 keyboard matrix input** — SDL key events are translated to CIA1 column/row coordinates and forwarded to `CIA6526::setKey()`; 6502 programs can scan the matrix via PRA (column select) and PRB (row read) exactly as on real hardware
- **Keyboard capture focus model** — click the Screen panel to capture keyboard input; Escape releases capture; a green border overlay and "KEYBOARD ACTIVE" label indicate active capture
- **`roms/keyboard_test.s`** — CIA1 keyboard scanner ROM; scans all 8 columns using active-low column masks, edge-detects newly pressed keys with a PREV table in zero page, and prints `CcRr` + newline to the terminal for each key press

### Changed
- `CIA6526` keyboard matrix (`keyMatrix_[8]`) added — active-low 8×8 grid; `setKey(col, row, pressed)` sets/clears individual bits; `read(REG_PRB)` ANDs all selected column bytes for correct multi-key scanning
- `CIA6526::reset()` now clears `keyMatrix_` to `0xFF` (all keys released)

---

## [0.13.0] - 2026-04-16

### Added
- **VIC border colour** — framebuffer expanded to 400×280 (400×ACTIVE + 40px border each side); border region painted from `$D020` independently of the 320×200 active area
- **Character mode rendering** — 40×25 text mode; reads screen codes from screen RAM (default `$0400`), renders 8×8 glyphs using an embedded open clean-room font (`CharROM.h`); screen codes `$80–$FF` rendered in reverse video; foreground defaults to white (colour RAM `$D800` not yet implemented)
- **`CharROM.h`** — 256-entry 8×8 open bitmap font covering `@`, A–Z, 0–9, punctuation, and common graphics blocks; clean-room, no copyright concerns

### Changed
- `VIC6566::WIDTH/HEIGHT` now 400×280 (was 320×200); `ACTIVE_W/ACTIVE_H/BORDER_X/BORDER_Y` constants added
- Screen panel OpenGL texture automatically picks up new dimensions via `VIC6566::WIDTH/HEIGHT`

---

## [0.12.0] - 2026-04-16

### Changed
- **Memory Viewer** now uses `imgui_memory_editor` (single-header, from imgui_club) — click any byte to edit RAM live, built-in data preview and column options, PC highlighted in yellow, Follow PC toggle retained

---

## [0.11.0] - 2026-04-16

### Added
- **VIC6566 (MOS 6566/6567/8566) stub** — Video Interface Controller device (`$D000–$D3FF`)
  - Full 64-register file with correct read/write behaviour; collision and ISR registers clear on read
  - 320×200 RGBA framebuffer; cycle-accurate raster counter (65 cycles/line, 263 lines/frame NTSC)
  - Raster compare IRQ — fires when raster line matches `$D011`/`$D012` and IRQ is enabled (`$D01A`)
  - `renderFrame()` fills the active display area with the background colour (`$D021 & $0F`)
  - Power-on defaults: DEN=1, blue background, light-blue border — screen shows immediately without ROM setup
  - `statusLine()` shows current raster line, background colour, and DEN state in the Machine Designer panel
- **Screen panel** now displays the live VIC framebuffer via an OpenGL texture (nearest-neighbour scaled)
- VIC IRQ wired to the CPU IRQ line alongside CIA1

### Changed
- `Machine::buildDefaultMap()` registers VIC at `$D000–$D3FF` (before RAM in priority order)
- Machine config save/load recognises device id `"vic"`

### Planned (future revisions)
- Character-mode rendering (reads screen matrix from RAM, renders with embedded font)
- Border colour and border area in framebuffer
- Sprite rendering, bitmap mode, ECM/MCM text modes

---

## [0.10.0] - 2026-04-16

### Added
- **WDC 65C02 CPU** — second CPU implementation, selectable at runtime via the Machine Designer panel
  - All 27 CMOS opcode patches: BRA, STZ, TRB, TSB, INA, DEA, PHX, PHY, PLX, PLY, BIT immediate, zero-page indirect `($zp)`, absolute indexed indirect `($abs,X)`
  - JMP indirect page-wrap bug fixed (`nmosBug_ = false`)
- **`CPU6502Base`** — shared base class for all MOS 6502-family CPUs; `buildNMOSTable()` fills the 256-entry NMOS dispatch table; subclasses patch CMOS entries without duplication
- **CPU selector dropdown** in Machine Designer panel — switch between "MOS 8502" and "WDC 65C02" live; CPU resets on switch
- `Machine::selectCPU(name)` — switches the active CPU by name, resets it, and wires it to the bus
- Both CPUs are always connected to the bus; only the active one receives `clock()` / `reset()` calls from the Application

### Changed
- `Machine::cpu()` now returns `ICPU&` instead of `CPU8502&` — Application works through the interface for any CPU
- All direct register field accesses in Application (`cpu.PC`, `cpu.A`, etc.) replaced with ICPU getters (`getPC()`, `regA()`, …)
- Flag display in CPU State panel uses raw bit masks on `regP()` instead of `CPU8502::Flags` enum values

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
