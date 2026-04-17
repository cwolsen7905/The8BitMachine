#pragma once

#include "emulator/core/Bus.h"
#include "emulator/core/ICPU.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/devices/BankedMemory.h"
#include "emulator/devices/BankController.h"
#include "emulator/devices/BankSelectPort.h"
#include "emulator/devices/ROM.h"
#include "emulator/devices/SwitchableRegion.h"
#include "emulator/devices/SID6581.h"
#include "emulator/devices/VIC6566.h"
#include "emulator/cpu/CPU6510.h"
#include "emulator/cpu/CPU8502.h"
#include "emulator/cpu/CPU65C02.h"
#include "emulator/devices/Memory.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    Bus&     bus()    { return bus_; }
    Memory&  ram()    { return ram_; }
    CIA6526& cia1()   { return cia1_; }
    CIA6526& cia2()   { return cia2_; }
    VIC6566& vic()    { return vic_; }
    SID6581& sid()    { return sid_; }
    CPU6510& cpu6510(){ return cpu6510_; }

    const CIA6526& cia1() const { return cia1_; }
    const VIC6566& vic()  const { return vic_;  }
    const SID6581& sid()  const { return sid_;  }

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

    void resetAddressMap();   // restore default device wiring

    // Dynamic ROM management
    // mountROM loads a file, owns the ROM object, and maps it on the bus.
    // Returns nullptr if the file cannot be opened.
    ROM* mountROM(uint16_t start, uint16_t end,
                  const std::string& label, const std::string& filePath);

    // unmountAt removes the bus entry at busIndex and, if the device is
    // dynamically owned (e.g. a ROM), frees it once no other entries reference it.
    void unmountAt(size_t busIndex);

    // mountBankedMemory creates N banks of bankSize bytes mapped at primaryStart–primaryEnd,
    // with a BankSelectPort at bankSelectAddr.  Writing to bankSelectAddr selects the bank.
    // Returns the BankedMemory pointer, or nullptr if the range is degenerate.
    BankedMemory* mountBankedMemory(uint16_t primaryStart, uint16_t primaryEnd,
                                    uint16_t bankSelectAddr, uint8_t numBanks);

    // Advanced bank switching ------------------------------------------------

    // Create a SwitchableRegion proxy at the given address range.
    // Use addRegionOption() to register child devices, then wire to a
    // BankController or call select() directly.
    SwitchableRegion* mountSwitchableRegion(uint16_t start, uint16_t end,
                                            const std::string& label);

    // Add a child device option to an existing SwitchableRegion.
    // dev may be any IBusDevice* already owned by the Machine, or nullptr (open bus).
    void addRegionOption(SwitchableRegion* region, IBusDevice* dev,
                         const std::string& optionLabel);

    // Create a BankController I/O byte at addr.
    BankController* mountBankController(uint16_t addr, const std::string& label);

    // Register a mapping: when ctrl receives value, call region->select(bankIndex).
    void addControllerMapping(BankController* ctrl, uint8_t value,
                              SwitchableRegion* region, uint8_t bankIndex);

    // Look up a device pointer by its config ID ("vic", "sid", "cia1", etc.)
    // Returns nullptr for "char_out" and unknown IDs.
    IBusDevice* deviceForId(const std::string& id);
    const char* idForDevice(const IBusDevice* dev) const;

private:
    Memory  ram_;
    CIA6526 cia1_;
    CIA6526 cia2_;
    VIC6566 vic_;
    SID6581 sid_;

    CPU6510  cpu6510_;
    CPU8502  cpu8502_;
    CPU65C02 cpu65c02_;
    ICPU*    activeCpu_ = &cpu8502_;

    Bus bus_;

    void buildDefaultMap();

    std::vector<std::unique_ptr<IBusDevice>> dynamicDevices_;
};
