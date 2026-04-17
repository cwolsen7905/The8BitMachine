#pragma once

#include "emulator/core/IBusDevice.h"
#include "emulator/devices/SwitchableRegion.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// BankController — 1-byte I/O register that drives one or more
// SwitchableRegions.
//
// addMapping(value, region, bankIndex) registers an action: "when value V is
// written, call region->select(bankIndex)".  Multiple regions can be updated
// by a single write (e.g. C64's $01 port simultaneously changes three regions).
//
// An optional write callback lets machine-specific code (e.g. the 6510 I/O
// port logic) react to every write without subclassing.
// ---------------------------------------------------------------------------
class BankController : public IBusDevice {
public:
    struct Action {
        SwitchableRegion* region;
        uint8_t           bankIndex;
    };

    explicit BankController(std::string label = "BankController");

    // Register an action for a specific written value.
    void addMapping(uint8_t value, SwitchableRegion* region, uint8_t bankIndex);

    // Optional callback fired on every write (after mappings are applied).
    std::function<void(uint8_t)> onWrite;

    uint8_t currentValue() const { return value_; }
    size_t  regionCount()  const { return regions_.size(); }

    // Force-apply the mappings for a given value without a bus write.
    void apply(uint8_t value);

    // IBusDevice
    const char* deviceName() const override { return "BankController"; }
    void        reset()      override;
    uint8_t     read (uint16_t offset)          const override;
    void        write(uint16_t offset, uint8_t value) override;
    std::string statusLine()                    const override;

private:
    std::string                                       label_;
    uint8_t                                           value_ = 0;
    std::unordered_map<uint8_t, std::vector<Action>>  mappings_;
    std::vector<SwitchableRegion*>                    regions_;  // unique list for status
};
