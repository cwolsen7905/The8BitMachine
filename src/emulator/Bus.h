#pragma once

#include "CIA6526.h"
#include "Memory.h"
#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// System bus — routes reads/writes between the CPU and attached devices.
//
// Address map (Commodore 128 inspired; fully decoded even before peripherals
// are implemented so future stubs slot in without touching routing logic):
//
//   $0000–$DFFF   96 KB  RAM  (flat for now; bank switching later)
//   $E000–$EFFF    4 KB  RAM  (reserved for future I/O expansion / KERNAL)
//   $F000          1 B   CHAR_OUT — write a byte to send it to the terminal
//   $F001–$F0FF         Reserved I/O space
//   $F100–$F1FF         Future: CIA1  (MOS 6526)
//   $F200–$F2FF         Future: CIA2  (MOS 6526)
//   $F300–$F3FF         Future: SID   (MOS 6581/8580)
//   $F400–$FBFF         Reserved
//   $FC00–$FFFF    1 KB  Vectors / ROM  (reset/IRQ/NMI vectors in RAM for now)
//
// All unimplemented I/O reads return $FF (open bus).
// ---------------------------------------------------------------------------
class Bus {
public:
    // -----------------------------------------------------------------------
    // I/O address ranges
    // -----------------------------------------------------------------------
    static constexpr uint16_t CHAR_OUT_ADDR = 0xF000;
    static constexpr uint16_t CIA1_START    = 0xF100;
    static constexpr uint16_t CIA1_END      = 0xF1FF;
    static constexpr uint16_t CIA2_START    = 0xF200;
    static constexpr uint16_t CIA2_END      = 0xF2FF;
    static constexpr uint16_t IO_START      = 0xF000;
    static constexpr uint16_t IO_END        = 0xFBFF;

    // -----------------------------------------------------------------------
    // Callbacks installed by Application
    // -----------------------------------------------------------------------
    std::function<void(uint8_t)> onCharOut;

    // -----------------------------------------------------------------------
    // Devices
    // -----------------------------------------------------------------------
    CIA6526 cia1;  // $F100–$F1FF
    CIA6526 cia2;  // $F200–$F2FF  (stub — IRQ not yet connected)

    // -----------------------------------------------------------------------
    // Interface
    // -----------------------------------------------------------------------
    Bus() = default;

    void    reset();
    void    clock();   // tick all bus-attached devices one cycle
    uint8_t read (uint16_t addr) const;
    void    write(uint16_t addr, uint8_t value);

    // -----------------------------------------------------------------------
    // Raw RAM — used by ROM loader and memory viewer (bypasses I/O decode)
    // -----------------------------------------------------------------------
    Memory ram;

private:
    static constexpr bool isIO(uint16_t addr) {
        return addr >= IO_START && addr <= IO_END;
    }
};
