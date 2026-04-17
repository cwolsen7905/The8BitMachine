#pragma once

#include "emulator/core/Bus.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/cpu/CPU8502.h"
#include "emulator/devices/Memory.h"

#include <functional>
#include <string>

// Config save/load result
struct MachineConfigResult {
    bool        ok      = false;
    std::string message;   // human-readable status for the terminal
};

// ---------------------------------------------------------------------------
// Machine — owns all emulator components and wires them together.
//
// The default machine models a MOS 8502 system with:
//   $0000–$EFFF   64 KB flat RAM  (Memory)
//   $F000          1 B   CHAR_OUT debug port  (Bus::onCharOut callback)
//   $F100–$F1FF   CIA1  (MOS 6526) — Timer A + IRQ
//   $F200–$F2FF   CIA2  (MOS 6526) — stub
//   $FC00–$FFFF   upper RAM (vectors; included in the Memory range above)
//
// The machine designer panel in Application can inspect devices() to render
// the address map, and future work will let users add/remove devices at
// runtime.
// ---------------------------------------------------------------------------
class Machine {
public:
    Machine();

    // -----------------------------------------------------------------------
    // Core components — direct typed access for Application UI panels
    // -----------------------------------------------------------------------
    CPU8502& cpu()  { return cpu_; }
    Bus&     bus()  { return bus_; }
    Memory&  ram()  { return ram_; }
    CIA6526& cia1() { return cia1_; }
    CIA6526& cia2() { return cia2_; }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    void reset();    // reset all devices + CPU
    void clock();    // tick all devices + (caller ticks CPU separately)

    // -----------------------------------------------------------------------
    // Wiring — called once by Application::init() to inject callbacks
    // -----------------------------------------------------------------------
    void setCharOutCallback(std::function<void(uint8_t)> cb);
    void setIRQCallback(std::function<void()> cb);   // CIA1 → CPU IRQ

    // -----------------------------------------------------------------------
    // Config — save / load the address map as JSON
    // -----------------------------------------------------------------------
    MachineConfigResult saveConfig(const std::string& path) const;
    MachineConfigResult loadConfig(const std::string& path);

private:
    // Device instances (Machine owns them; Bus holds non-owning pointers)
    Memory  ram_;
    CIA6526 cia1_;
    CIA6526 cia2_;

    CPU8502 cpu_;
    Bus     bus_;

    void buildDefaultMap();

    // Map between a device pointer and its stable string ID for JSON
    const char*  idForDevice(const IBusDevice* dev) const;
    IBusDevice*  deviceForId(const std::string& id);
};
