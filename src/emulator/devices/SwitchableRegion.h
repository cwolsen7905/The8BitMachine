#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SwitchableRegion — proxy device that forwards reads/writes to one of N
// registered child devices.  select(n) swaps the active child at runtime.
//
// Options can be any IBusDevice* (RAM, ROM, I/O, nullptr for open bus).
// Typically controlled by a companion BankController.
// ---------------------------------------------------------------------------
class SwitchableRegion : public IBusDevice {
public:
    struct Option {
        IBusDevice* device;
        std::string label;
    };

    explicit SwitchableRegion(std::string label = "SwitchableRegion");

    // Add a bank option.  Options are indexed in insertion order.
    void addOption(IBusDevice* device, const std::string& optionLabel);

    // Select the active bank by index (clamped to valid range).
    void select(uint8_t index);

    uint8_t              selectedIndex() const { return active_; }
    size_t               optionCount()   const { return options_.size(); }
    const Option&        activeOption()  const { return options_[active_]; }
    const std::vector<Option>& options() const { return options_; }

    // IBusDevice
    const char* deviceName() const override { return "SwitchableRegion"; }
    void        reset()      override;
    uint8_t     read (uint16_t offset)          const override;
    void        write(uint16_t offset, uint8_t value) override;
    std::string statusLine()                    const override;

private:
    std::string          label_;
    std::vector<Option>  options_;
    uint8_t              active_ = 0;
};
