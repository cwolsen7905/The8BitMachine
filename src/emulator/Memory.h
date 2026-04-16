#pragma once

#include "IBusDevice.h"
#include <array>
#include <cstdint>

// Flat 64 KB RAM — implements IBusDevice so it can be mounted anywhere in
// the address space via the Bus device map.
class Memory : public IBusDevice {
public:
    static constexpr size_t SIZE = 64 * 1024;

    Memory();

    const char* deviceName() const override { return "RAM"; }
    std::string statusLine() const override { return "64 KB flat RAM"; }

    void    reset() override;
    uint8_t read (uint16_t offset) const override;
    void    write(uint16_t offset, uint8_t value) override;

    // Direct pointer access for ROM loader (bypasses bus routing overhead).
    uint8_t* data() { return data_.data(); }

private:
    std::array<uint8_t, SIZE> data_{};
};
