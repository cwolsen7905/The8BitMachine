#pragma once

#include "emulator/cpu/CPU6502Base.h"
#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// CPU6510 — MOS 6510 (Commodore 64).
//
// Identical to the NMOS 6502 except for a built-in 6-bit I/O port mapped
// at the first two bytes of the address space:
//
//   $00  — I/O port direction register (DDR): 1=output, 0=input per bit
//   $01  — I/O port data register
//
// On the C64 power-on state: DDR=$2F (bits 0–3,5 are outputs), data=$37
// (LORAM=1, HIRAM=1, CHAREN=1).  The three LSBs of the data register control
// which ROMs are visible in the address map:
//
//   Bit 0 (LORAM)  — 1: BASIC ROM at $A000–$BFFF; 0: RAM
//   Bit 1 (HIRAM)  — 1: kernal ROM at $E000–$FFFF; 0: RAM
//   Bit 2 (CHAREN) — 1: I/O block at $D000–$DFFF; 0: char ROM
//
// onIOWrite is called whenever either port register is written so the
// Machine can update SwitchableRegions accordingly.  The callback receives
// (data, dir) — apply the DDR mask yourself if needed.
// ---------------------------------------------------------------------------
class CPU6510 : public CPU6502Base {
public:
    CPU6510();

    const char* cpuName() const override { return "MOS 6510"; }

    void reset() override;

    uint8_t ioData() const { return ioData_; }
    uint8_t ioDir()  const { return ioDir_;  }

    // Fired on every write to $00 or $01 with the new (data, dir) values.
    std::function<void(uint8_t data, uint8_t dir)> onIOWrite;

protected:
    uint8_t busRead (uint16_t addr)          override;
    void    busWrite(uint16_t addr, uint8_t val) override;

private:
    uint8_t ioDir_  = 0x2F;  // C64 power-on: bits 0-3,5 are outputs
    uint8_t ioData_ = 0x37;  // C64 power-on: LORAM=1, HIRAM=1, CHAREN=1
};
