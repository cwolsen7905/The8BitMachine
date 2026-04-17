#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Bus — address-space router.
//
// Devices are registered with addDevice() at a [start, end] address range.
// On read/write the Bus finds the matching entry and calls
// device->read(addr - start) / device->write(addr - start, value).
//
// CHAR_OUT ($F000) is a special write-only debug port; writes are forwarded
// to the onCharOut callback.  It is listed in the device table as a sentinel
// so the machine designer panel can show it.
//
// Unmatched reads return $FF (open bus).  Unmatched writes are silently
// dropped.
// ---------------------------------------------------------------------------
class Bus {
public:
    // -----------------------------------------------------------------------
    // Device map entry — one per mounted device
    // -----------------------------------------------------------------------
    struct DeviceEntry {
        uint16_t    start;
        uint16_t    end;
        IBusDevice* device;
        std::string label;   // human-readable name shown in the designer panel
    };

    // -----------------------------------------------------------------------
    // Special debug port: CPU writes here → forwarded to this callback
    // -----------------------------------------------------------------------
    static constexpr uint16_t CHAR_OUT_ADDR = 0xF000;
    std::function<void(uint8_t)> onCharOut;

    // -----------------------------------------------------------------------
    // Device management
    // -----------------------------------------------------------------------
    void addDevice(uint16_t start, uint16_t end,
                   IBusDevice* device, const std::string& label = "");
    void removeDevice(IBusDevice* device);
    void clearDevices();

    const std::vector<DeviceEntry>& devices() const { return devices_; }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    void reset();   // reset all registered devices
    void clock();   // clock all registered devices one cycle

    // -----------------------------------------------------------------------
    // CPU-facing read / write
    // -----------------------------------------------------------------------
    uint8_t read (uint16_t addr) const;
    void    write(uint16_t addr, uint8_t value);

private:
    std::vector<DeviceEntry> devices_;
};
