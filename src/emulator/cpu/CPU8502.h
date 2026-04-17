#pragma once

#include "emulator/cpu/CPU6502Base.h"

// ---------------------------------------------------------------------------
// CPU8502 — MOS 8502 (Commodore 128).
// Thin subclass of CPU6502Base; uses the full NMOS 6502 dispatch table with
// the JMP-indirect page-wrap bug enabled.
// ---------------------------------------------------------------------------
class CPU8502 : public CPU6502Base {
public:
    CPU8502() {
        buildNMOSTable();
        nmosBug_ = true;
    }
    const char* cpuName() const override { return "MOS 8502"; }
};
