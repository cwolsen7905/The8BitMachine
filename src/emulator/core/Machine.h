#pragma once

#include "emulator/core/Bus.h"
#include "emulator/core/ICPU.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/cpu/CPU8502.h"
#include "emulator/cpu/CPU65C02.h"
#include "emulator/devices/Memory.h"

#include <functional>
#include <string>

// Config save/load result
struct MachineConfigResult {
    bool        ok      = false;
    std::string message;
};

// ---------------------------------------------------------------------------
// Machine — owns all emulator components and wires them together.
//
// Two CPUs are available: MOS 8502 (NMOS) and WDC 65C02 (CMOS).
// selectCPU() switches the active CPU; cpu() always returns the active one.
//
// Default machine:
//   $0000–$FFFF   64 KB flat RAM  (Memory, catch-all)
//   $F000          CHAR_OUT debug port  (Bus::onCharOut callback)
//   $F100–$F1FF   CIA1  (MOS 6526) — Timer A + IRQ
//   $F200–$F2FF   CIA2  (MOS 6526) — stub
// ---------------------------------------------------------------------------
class Machine {
public:
    Machine();

    // -----------------------------------------------------------------------
    // CPU — active CPU exposed through ICPU interface
    // -----------------------------------------------------------------------
    ICPU& cpu()               { return *activeCpu_; }
    const ICPU& cpu() const   { return *activeCpu_; }

    // Switch active CPU by name ("MOS 8502" or "WDC 65C02").
    // Returns true if the name matched a known CPU.
    bool selectCPU(const std::string& name);

    // -----------------------------------------------------------------------
    // Devices — direct typed access for Application UI panels
    // -----------------------------------------------------------------------
    Bus&     bus()  { return bus_; }
    Memory&  ram()  { return ram_; }
    CIA6526& cia1() { return cia1_; }
    CIA6526& cia2() { return cia2_; }

    const CIA6526& cia1() const { return cia1_; }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    void reset();
    void clock();

    // -----------------------------------------------------------------------
    // Wiring
    // -----------------------------------------------------------------------
    void setCharOutCallback(std::function<void(uint8_t)> cb);
    void setIRQCallback(std::function<void()> cb);

    // -----------------------------------------------------------------------
    // Config
    // -----------------------------------------------------------------------
    MachineConfigResult saveConfig(const std::string& path) const;
    MachineConfigResult loadConfig(const std::string& path);

private:
    Memory  ram_;
    CIA6526 cia1_;
    CIA6526 cia2_;

    CPU8502  cpu8502_;
    CPU65C02 cpu65c02_;
    ICPU*    activeCpu_ = &cpu8502_;

    Bus bus_;

    void buildDefaultMap();

    const char*  idForDevice(const IBusDevice* dev) const;
    IBusDevice*  deviceForId(const std::string& id);
};
