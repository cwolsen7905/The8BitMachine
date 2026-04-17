#pragma once

#include "emulator/cpu/CPU6502Base.h"

// ---------------------------------------------------------------------------
// CPU65C02 — WDC 65C02 (CMOS 6502).
// Starts from the NMOS dispatch table then patches ~27 entries for CMOS:
//   - New instructions: BRA, STZ, TRB, TSB, INA, DEA, PHX, PHY, PLX, PLY
//   - New addressing modes: IZP ($zp), AIIX ($abs,X)
//   - JMP indirect bug fixed (nmosBug_ = false)
//   - BIT immediate sets Z only (not N/V)
// ---------------------------------------------------------------------------
class CPU65C02 : public CPU6502Base {
public:
    CPU65C02();
    const char* cpuName() const override { return "WDC 65C02"; }
};
