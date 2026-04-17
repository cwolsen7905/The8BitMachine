#pragma once

#include "emulator/core/Bus.h"
#include "emulator/core/ICPU.h"
#include "emulator/devices/CIA6526.h"
#include "emulator/devices/BankedMemory.h"
#include "emulator/devices/BankController.h"
#include "emulator/devices/C64IOSpace.h"
#include "emulator/devices/BankSelectPort.h"
#include "emulator/devices/ROM.h"
#include "emulator/devices/SwitchableRegion.h"
#include "emulator/devices/SID6581.h"
#include "emulator/devices/ULA.h"
#include "emulator/devices/VIC6566.h"
#include "emulator/cpu/CPU6510.h"
#include "emulator/cpu/CPU8502.h"
#include "emulator/cpu/CPU65C02.h"
#include "emulator/cpu/CPUZ80.h"
#include "emulator/devices/Memory.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Active screen framebuffer descriptor — returned by Machine::screenInfo()
struct ScreenInfo {
    int           width  = 0;
    int           height = 0;
    const uint8_t* pixels = nullptr;   // RGBA, width*height*4 bytes
};

// Config save/load result
struct MachineConfigResult {
    bool        ok                 = false;
    std::string message;
    // Populated on load when a preset was restored; Application should apply these.
    bool        hasPreset          = false;
    bool        keyMatrixTranspose = true;
    int         cyclesPerFrame     = 0;  // 0 = not stored in file (use caller's default)
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
    ULA&     ula()    { return ula_; }
    CPU6510& cpu6510(){ return cpu6510_; }
    CPUZ80&  cpuZ80() { return cpuZ80_; }

    const CIA6526& cia1() const { return cia1_; }
    const VIC6566& vic()  const { return vic_;  }
    const SID6581& sid()  const { return sid_;  }
    const ULA&     ula()  const { return ula_;  }

    // Active screen (switches between VIC and ULA depending on preset)
    ScreenInfo screenInfo() const { return activeScreen_; }

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
    MachineConfigResult saveConfig(const std::string& path, int cyclesPerFrame = 0) const;
    MachineConfigResult loadConfig(const std::string& path);

    void resetAddressMap();   // restore default device wiring

    // Build a Commodore 64 memory map.  Loads kernal, BASIC, and char ROMs
    // from the given paths.  Switches the CPU to MOS 6510 and wires the I/O
    // port to the three SwitchableRegions.  Returns an error if any ROM fails.
    // keyMatrixTranspose: true = MEGA65 OpenROMs (PA=rows/PB=cols),
    //                    false = standard C64 KERNAL (PA=cols/PB=rows).
    // The value is stored in the preset and round-tripped through saveConfig/loadConfig.
    // Build a ZX Spectrum 48K memory map.  Loads the 16 KB ROM from romPath.
    // Switches the CPU to Zilog Z80, wires the ULA port handlers, and resets.
    MachineConfigResult buildSpectrumPreset(const std::string& romPath);

    MachineConfigResult buildC64Preset(const std::string& kernalPath,
                                       const std::string& basicPath,
                                       const std::string& charPath,
                                       bool keyMatrixTranspose = true);

    // Dynamic ROM management
    // mountROM loads a file, owns the ROM object, and maps it on the bus.
    // Returns nullptr if the file cannot be opened.
    ROM* mountROM(uint16_t start, uint16_t end,
                  const std::string& label, const std::string& filePath);

    // loadROM loads ROM data into dynamicDevices_ but does NOT register it
    // on the bus.  Used by presets that add the ROM as a SwitchableRegion
    // option rather than a direct bus entry.
    ROM* loadROM(const std::string& label, const std::string& filePath);

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

    // All devices that have debug panels, regardless of bus layout.
    // Always includes the fixed chips (VIC, SID, CIA1/2) even when they are
    // not direct bus entries (e.g. inside C64IOSpace in preset mode).
    struct PanelEntry { std::string label; IBusDevice* device; };
    std::vector<PanelEntry> panelDevices();

    // Look up a device pointer by its config ID ("vic", "sid", "cia1", etc.)
    // Returns nullptr for "char_out" and unknown IDs.
    IBusDevice* deviceForId(const std::string& id);
    const char* idForDevice(const IBusDevice* dev) const;

private:
    Memory     ram_;
    CIA6526    cia1_;
    CIA6526    cia2_;
    VIC6566    vic_;
    SID6581    sid_;
    ULA        ula_;
    C64IOSpace c64IOSpace_;  // pre-wired to the four fixed chips above

    CPU6510  cpu6510_;
    CPU8502  cpu8502_;
    CPU65C02 cpu65c02_;
    CPUZ80   cpuZ80_;
    ICPU*    activeCpu_ = &cpu8502_;

    Bus bus_;

    ScreenInfo activeScreen_;

    void buildDefaultMap();

    std::vector<std::unique_ptr<IBusDevice>> dynamicDevices_;

    // Active preset — populated by buildC64Preset, consumed by saveConfig.
    struct PresetState {
        std::string name;           // "c64"
        std::string kernalPath;
        std::string basicPath;
        std::string charPath;
        bool        keyMatrixTranspose = true;
    };
    bool        hasPreset_ = false;
    PresetState preset_;

    // Fixed devices that are active in the current preset — clocked, reset,
    // and shown in panelDevices().  Set by each preset builder / buildDefaultMap.
    std::vector<IBusDevice*> activeFixedDevices_;
};
