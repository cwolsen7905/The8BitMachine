#pragma once

#include <array>
#include <cstdint>

// Flat 64KB address space — later this will be banked (Commodore 128 style)
class Memory {
public:
    static constexpr size_t SIZE = 64 * 1024;

    Memory();

    uint8_t  read(uint16_t addr) const;
    void     write(uint16_t addr, uint8_t value);
    void     reset();

private:
    std::array<uint8_t, SIZE> data_{};
};
