#pragma once

#include "emulator/core/IBusDevice.h"
#include "emulator/core/IHasPanel.h"
#include "emulator/core/IPeripheral.h"
#include "emulator/devices/Memory.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// EpyxFastLoad — emulates the Epyx FastLoad cartridge for the Commodore 64.
//
// Hardware behaviour (matches VICE epyxfastload.c):
//   - 8 KB ROM mapped at $8000–$9FFF (ROML) when enabled
//   - Capacitor-based toggle: ROM is enabled on reset and whenever the CPU
//     reads from $8000–$9FFF or $DE00–$DEFF (IO1).  If 512 cycles elapse
//     without such a read, the ROM auto-disables (capacitor charges up).
//   - IO2 ($DF00–$DFFF) always mirrors the last 256 bytes of the ROM
//     regardless of the capacitor state.
//   - Writes to ROML / IO1 / IO2 are ignored.
//
// Integration:
//   - Registered on the bus at $8000–$9FFF by Machine::buildC64Preset().
//     When no ROM is loaded, read() returns $FF (transparent open bus).
//   - C64IOSpace calls triggerIO1Read() / readIO2() for $DE00/$DF00 ranges.
//   - clock() must be called every machine cycle; Bus does this automatically
//     since the device is a direct bus entry.
// ---------------------------------------------------------------------------
class EpyxFastLoad : public IBusDevice, public IPeripheral, public IHasPanel {
public:
    // -----------------------------------------------------------------------
    // IBusDevice — bus entry covers $8000–$9FFF (8 KB ROML window)
    // -----------------------------------------------------------------------
    const char* deviceName() const override { return "Epyx FastLoad"; }
    void        reset()            override;
    void        clock()            override;
    uint8_t     read(uint16_t offset) const override;
    void        write(uint16_t, uint8_t) override {}  // writes ignored

    // -----------------------------------------------------------------------
    // Called by C64IOSpace for IO1 ($DE00–$DEFF) and IO2 ($DF00–$DFFF)
    // -----------------------------------------------------------------------
    void    triggerIO1Read();               // re-arms capacitor; no data returned
    uint8_t readIO2(uint8_t addrLow) const; // last 256 bytes of ROM, no capacitor

    // -----------------------------------------------------------------------
    // IPeripheral
    // -----------------------------------------------------------------------
    const char*        peripheralName() const override { return "Epyx FastLoad Cart"; }
    const std::string& mountedImage()   const override { return imagePath_; }
    bool               mount(const std::string& path) override;
    void               eject()          override;
    const std::string& mountError()     const override { return mountError_; }

    // -----------------------------------------------------------------------
    // IHasPanel
    // -----------------------------------------------------------------------
    void drawPanel(const char* title, bool* open) override;

    // Called by Machine::buildC64Preset so disabled-ROM reads fall through to RAM.
    void setRam(Memory* ram) { ram_ = ram; }

    bool isRomLoaded()  const { return !rom_.empty(); }
    bool isRomEnabled() const { return romEnabled_; }

private:
    static constexpr int kCapacitorCycles = 512;

    std::vector<uint8_t> rom_;
    std::string          imagePath_;
    std::string          mountError_;
    Memory*              ram_               = nullptr;
    mutable bool         romEnabled_        = false;
    mutable int          cyclesUntilDisable_= 0;

    void armCapacitor() const;
};
