# Contributor Guide

This document covers what you need to know to add a new chip, CPU, peripheral, or preset machine to The 8-Bit Machine without modifying any core logic.

---

## Table of contents

1. [Architecture overview](#architecture-overview)
2. [IBusDevice — adding a chip](#ibusdevice--adding-a-chip)
3. [IHasPanel — adding a debug panel](#ihaspanel--adding-a-debug-panel)
4. [ICPU — adding a CPU](#icpu--adding-a-cpu)
5. [IPeripheral — adding an attachable peripheral](#iperipheral--adding-an-attachable-peripheral)
6. [IIECDevice — connecting to the IEC serial bus](#iiecdevice--connecting-to-the-iec-serial-bus)
7. [Preset authoring](#preset-authoring)
8. [Bus routing rules](#bus-routing-rules)
9. [Machine lifetime guarantees](#machine-lifetime-guarantees)

---

## Architecture overview

```
Application  ←→  Machine  ←→  Bus  ←→  IBusDevice implementations
                    ↕
                  ICPU implementations
```

`Machine` owns all device instances and the active CPU. `Bus` is a priority-ordered list of `(start, end, IBusDevice*)` entries; on every CPU read or write it walks the list and delegates to the first matching entry, passing `addr - entry.start` as the *offset*.

Interfaces are the only coupling between layers:

| Interface | Purpose |
|-----------|---------|
| `IBusDevice` | Any chip or device mapped into the address space |
| `IHasPanel` | Optional — devices that expose an ImGui debug panel |
| `ICPU` | Any CPU implementation |
| `IPeripheral` | External devices the user can mount media onto |
| `IIECDevice` | Devices that connect to the IEC serial bus |

---

## IBusDevice — adding a chip

**File:** `src/emulator/core/IBusDevice.h`

```cpp
class IBusDevice {
public:
    virtual const char* deviceName()  const = 0;
    virtual void        reset()             = 0;
    virtual void        clock()             {}   // optional
    virtual uint8_t     read (uint16_t offset) const = 0;
    virtual void        write(uint16_t offset, uint8_t value) = 0;

    virtual std::string statusLine() const { return ""; }
    virtual SubRange    findSubDevice(const IBusDevice*) const { return {}; }
};
```

### Contract

| Method | Contract |
|--------|----------|
| `deviceName()` | Short constant string, e.g. `"CIA6526"`. Used in the Machine Designer and debug output. |
| `reset()` | Restore the device to power-on state. Called on CPU reset and on preset load. Must be idempotent. |
| `clock()` | Called once per emulated CPU cycle if the device is in the active preset's clock list. Default no-op is fine for purely memory-mapped devices. |
| `read(offset)` | Return the byte at `offset` from the device's base address. Must be `const` — the Bus calls it from the CPU's read path. Do not mutate observable state from `read()` unless the hardware genuinely has side effects on read (e.g., ICR flags clear on read). |
| `write(offset, value)` | Store or act on a register write. |
| `statusLine()` | One-line human-readable status for the Machine Designer panel. Keep it short — it appears in a table cell. |
| `findSubDevice(dev)` | Container devices (e.g., `C64IOSpace`) override this to report where a contained chip lives within their address window. Return `{-1, -1}` (the default) if `dev` is not a sub-device of yours. |

### Key rules

- **Offsets, not addresses.** A CIA mounted at `$F100` receives offset `0x04` for a CPU access to `$F104`. Never assume a base address inside a device.
- **`read()` must be `const`.** The bus calls it from a `const` path. If you need to clear a flag on read, declare the flag `mutable`.
- **`clock()` is optional.** Pure register banks (ROM, simple RAM) should leave the default no-op in place. Only override it if your device has internal counters or timers.
- **`reset()` must fully initialize state.** It is called both at startup and when the user presses F8 or loads a preset. Do not rely on constructor-only initialization.

### Minimal example

```cpp
// src/emulator/devices/MyCounter.h
#pragma once
#include "emulator/core/IBusDevice.h"

class MyCounter : public IBusDevice {
public:
    const char* deviceName() const override { return "MyCounter"; }
    void reset() override { count_ = 0; }
    void clock() override { ++count_; }
    uint8_t read(uint16_t offset) const override {
        return (offset == 0) ? (count_ & 0xFF) : 0xFF;
    }
    void write(uint16_t offset, uint8_t value) override {
        if (offset == 0) count_ = value;
    }
    std::string statusLine() const override {
        return "count=" + std::to_string(count_);
    }
private:
    mutable uint16_t count_ = 0;
};
```

Mount it from a preset builder:

```cpp
auto* ctr = machine.mountDevice<MyCounter>(0xC000, 0xC0FF, "MyCounter");
```

Or from the Machine Designer at runtime — add a device ID to the dropdown in `Application_Designer.cpp`.

---

## IHasPanel — adding a debug panel

**File:** `src/emulator/core/IHasPanel.h`

```cpp
class IHasPanel {
public:
    virtual void drawPanel(const char* title, bool* open) = 0;
};
```

Inherit both `IBusDevice` and `IHasPanel` to get a dockable ImGui window listed in the View menu.

```cpp
class MyCounter : public IBusDevice, public IHasPanel { … };
```

### Contract

- `drawPanel()` is called every ImGui frame while the panel is open. `title` comes from the bus entry label (so two of the same device type get distinct titles). Pass `title` to `ImGui::Begin`.
- `open` is the standard ImGui close-button pointer — pass it directly to `ImGui::Begin`.
- `Machine::panelDevices()` uses `dynamic_cast<IHasPanel*>` to discover panel-capable devices. You do not need to register anywhere else.
- Devices that do not implement `IHasPanel` need zero ImGui knowledge.

### Minimal example

```cpp
void MyCounter::drawPanel(const char* title, bool* open) {
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }
    ImGui::Text("Count: %u", count_);
    ImGui::End();
}
```

---

## ICPU — adding a CPU

**File:** `src/emulator/core/ICPU.h`

```cpp
class ICPU {
public:
    virtual const char* cpuName()    const = 0;
    virtual void connectBus(Bus* bus)      = 0;
    virtual void reset()                   = 0;
    virtual void clock()                   = 0;
    virtual void irq()                     = 0;
    virtual void nmi()                     = 0;
    virtual bool        complete()    const = 0;
    virtual uint16_t    getPC()       const = 0;
    virtual std::string stateString() const = 0;
    virtual uint8_t regA()  const = 0;
    virtual uint8_t regX()  const = 0;
    virtual uint8_t regY()  const = 0;
    virtual uint8_t regSP() const = 0;
    virtual uint8_t regP()  const = 0;
};
```

### Contract

| Method | Contract |
|--------|----------|
| `cpuName()` | Short string shown in the Machine Designer CPU dropdown, e.g. `"Zilog Z80"`. |
| `connectBus(bus)` | Store the bus pointer. Called once after construction, before `reset()`. |
| `reset()` | Load the reset vector, clear registers, set up power-on state. |
| `clock()` | Execute one sub-cycle (tick). The application calls this in a loop until `complete()` returns true, which marks instruction boundary. |
| `irq()` | Assert the IRQ line. Typically sets an internal flag; the CPU samples it at instruction boundaries. |
| `nmi()` | Assert the NMI line (edge-triggered). |
| `complete()` | Return true when the current instruction has finished executing. The application uses this for single-step (F10) and breakpoint detection. |
| `getPC()` | Return the current program counter. |
| `stateString()` | One-line register dump for the CPU State panel, e.g. `"A=00 X=00 Y=00 SP=FD PC=FCE2"`. |
| `regA/X/Y/SP/P()` | Individual register accessors used by the CPU State panel. For CPUs without these exact registers (e.g. Z80), map to the closest architectural equivalent or return 0 for registers that have no analogue. |

### Registering the new CPU

Add an entry to the CPU selector dropdown in `Application_Designer.cpp` (search for the existing `"MOS 8502"` / `"Zilog Z80"` entries) and add a `selectCPU("My CPU Name")` branch in `Machine::selectCPU()`.

---

## IPeripheral — adding an attachable peripheral

**File:** `src/emulator/core/IPeripheral.h`

Implement `IPeripheral` on any device the user should be able to attach media to (disk drives, tape decks, printers). The Application's Peripherals menu is driven entirely by this interface.

```cpp
class IPeripheral {
public:
    virtual const char*        peripheralName() const = 0;
    virtual const std::string& mountedImage()   const = 0;
    virtual bool               mount(const std::string& path) = 0;
    virtual void               eject() = 0;
    virtual const std::string& mountError()     const = 0;
};
```

### Contract

- `mount()` returns `true` on success and `false` on failure. On failure, `mountError()` must return a human-readable description until the next `mount()` call.
- `mountedImage()` returns the path of the currently mounted image, or an empty string if nothing is mounted.
- The Peripherals menu calls `mount()` with the path selected by the native file browser. The acceptable file extensions are not enforced by the menu — validate inside `mount()`.

Peripherals are registered with `Machine::registerPeripheral()` from the preset builder or from `rewirePeripherals()`. See `Drive1541` for a full example.

---

## IIECDevice — connecting to the IEC serial bus

**File:** `src/emulator/core/IIECDevice.h`

The IEC bus is an open-collector serial bus used between a Commodore 64 and drives/printers. Three lines: ATN, CLK, DATA. Any device can pull a line low; the bus value is the logical AND of all drivers.

```cpp
struct IECLines {
    bool atn  = true;   // true = released (high), false = asserted (low)
    bool clk  = true;
    bool data = true;
};

class IIECDevice {
public:
    virtual void     setIECLines(IECLines host) = 0;
    virtual IECLines getIECLines() const        = 0;
};
```

### How the bus update cycle works

1. CIA2 changes its output bits (PA3=ATN, PA4=CLK, PA5=DATA via open-collector inverter).
2. CIA2 calls `setIECLines(hostLines)` on every registered `IIECDevice`.
3. CIA2 calls `getIECLines()` on each device and ANDs all results together to form the wired-AND bus state.
4. CIA2 feeds the resulting CLK-in (PA6) and DATA-in (PA7) back into `$DD00` reads.

### Contract

- `true` means **released** (line high); `false` means **asserted** (line pulled low). This is active-low convention — match it exactly or the KERNAL will misread every bit.
- `setIECLines()` receives the current state CIA2 is driving. Your device should update its internal state machine accordingly (e.g., detect ATN falling edge, sample CLK transitions).
- `getIECLines()` must return the lines your device is **currently pulling low**. Return `true` on any line you are not driving.
- The update cycle runs synchronously on every CIA2 Port A write that touches bits 3–5. Do not spin or block in these methods.

Register with `CIA2::connectIEC(IIECDevice*)`. See `Drive1541` for the full state-machine implementation.

---

## Preset authoring

A preset is a JSON file in `presets/` plus a `buildXxxPreset()` method in `Machine` and one entry in `kPresetDrivers[]`.

### JSON schema

```json
{
  "version": 1,
  "preset_type": "my_machine",
  "name": "My Machine",
  "description": "One-line description shown in the preset picker",
  "cpu": "MOS 6510",
  "cycles_per_frame": 16667,
  "key_matrix_transpose": false,
  "roms": [
    { "key": "rom", "label": "ROM", "description": "16 KB main ROM" }
  ]
}
```

| Field | Required | Notes |
|-------|----------|-------|
| `version` | yes | Always `1`. |
| `preset_type` | yes | Unique snake_case key. Must match the key in `kPresetDrivers[]`. |
| `name` | yes | Shown as the submenu entry under File → Load Preset. |
| `description` | yes | Shown in the ROM picker dialog. |
| `cpu` | yes | Must match a `cpuName()` string: `"MOS 8502"`, `"MOS 6510"`, `"WDC 65C02"`, `"Zilog Z80"`. |
| `cycles_per_frame` | yes | Target CPU cycles per video frame. Sets emulator speed. Examples: 16667 (C64 ~1 MHz @ 60 Hz), 69888 (Spectrum 3.5 MHz @ 50 Hz). |
| `key_matrix_transpose` | no | Default `false`. Set `true` for MEGA65 OpenROMs C64 keyboard layout. |
| `roms` | no | Omit if the preset needs no ROM files. Each entry produces one Browse button in the picker dialog. |

`roms[].key` is passed to `buildXxxPreset()` inside a `std::map<std::string, std::string>` of `{key → file path}`.

### Implementing the preset builder

Add a method to `Machine`:

```cpp
// Machine.h
std::string buildMyMachinePreset(const std::map<std::string, std::string>& roms);

// Machine.cpp
std::string Machine::buildMyMachinePreset(const std::map<std::string, std::string>& roms) {
    clearForPreset();                        // tears down current machine

    // 1. Mount RAM
    mountRAM(0x0000, 0xFFFF, "64K RAM");

    // 2. Load ROM(s)
    auto it = roms.find("rom");
    if (it == roms.end()) return "ROM path missing";
    if (!mountROM(0xC000, 0xFFFF, "ROM", it->second))
        return "Failed to load ROM: " + it->second;

    // 3. Select CPU
    selectCPU("WDC 65C02");

    // 4. Set clock speed (matches cycles_per_frame in JSON)
    setCyclesPerFrame(17030);

    // 5. Wire keyboard handler if needed
    installMyMachineKeyHandler();

    // 6. Reset
    reset();
    return "";   // empty string = success
}
```

`clearForPreset()` must be called first — it removes all dynamic devices, clears the bus, and disconnects peripherals.

### Registering in kPresetDrivers[]

In `Application.cpp`, add one entry to `kPresetDrivers[]`:

```cpp
{ "my_machine",
  [](Machine& m, const auto& roms) { return m.buildMyMachinePreset(roms); },
  myMachineDisasmLabels   // std::unordered_map<uint16_t, std::string>, or {} for none
},
```

That is the only change required outside of `Machine`. The File → Load Preset submenu, the ROM picker dialog, and session restore all discover your preset automatically.

### Disasm labels

`disasmLabels` is a `std::unordered_map<uint16_t, std::string>` of known addresses that appear as dim `; LABEL` annotations in the Disassembler panel. Add entries for ROM entry points, KERNAL jump table addresses, and important vectors. An empty map is fine.

---

## Bus routing rules

- Devices are matched in **registration order** (index 0 first). The first entry whose `[start, end]` range covers the address wins.
- Add your device **before** the catch-all RAM entry, or it will never be reached.
- The CHAR_OUT debug port (`$F000`) is intercepted before the device list walk and is always available regardless of what is mounted at that address.
- Unmatched reads return `$FF` (open bus). Unmatched writes are silently dropped.
- `Bus::clock()` ticks every device **not** in `noAutoClk_`. Fixed preset chips (VIC, SID, CIA) are added to `noAutoClk_` and clocked directly by `Machine::clock()` to avoid double-clocking. Dynamic devices you mount are clocked by `Bus::clock()` automatically.

---

## Machine lifetime guarantees

- Device instances must outlive their `Bus` registration. `Machine` owns all built-in devices; dynamically created devices go into `dynamicDevices_` (`vector<unique_ptr<IBusDevice>>`) which is cleaned up by `clearForPreset()`.
- `reset()` is called on every device before the CPU starts. Your `reset()` must leave the device in a fully defined state.
- `clock()` is called once per emulated cycle for devices in the active preset's clock list. Do not assume a fixed wall-clock rate — the user can change emulator speed at runtime.
- The Application renders on the main thread. If your device produces audio or video output asynchronously (e.g., via an SDL audio callback like SID), protect shared state with a mutex.
