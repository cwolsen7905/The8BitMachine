#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Bus;

// ---------------------------------------------------------------------------
// A single disassembled instruction
// ---------------------------------------------------------------------------
struct DisasmLine {
    uint16_t addr      = 0;
    uint8_t  bytes[3]  = {};
    int      byteCount = 0;
    std::string mnemonic;   // e.g. "LDA"
    std::string operand;    // e.g. "#$42"
};

// ---------------------------------------------------------------------------
// Stateless disassembler — walks memory forward from a given address.
//
// Backward disassembly is inherently ambiguous on the 6502 (variable-length
// instructions, data may look like valid opcodes).  The standard approach
// used here is to start a fixed number of bytes before the point of interest
// and walk forward; misalignments usually self-correct within a few opcodes.
// ---------------------------------------------------------------------------
class Disassembler {
public:
    // Disassemble `count` instructions starting at `startAddr`.
    static std::vector<DisasmLine> disassemble(
        const Bus& bus, uint16_t startAddr, int count);
};
