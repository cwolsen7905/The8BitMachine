#include "Bus.h"

void Bus::reset() {
    ram.reset();
    cia1.reset();
    cia2.reset();
}

void Bus::clock() {
    cia1.clock();
    cia2.clock();
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------
uint8_t Bus::read(uint16_t addr) const {
    if (!isIO(addr))
        return ram.read(addr);

    if (addr >= CIA1_START && addr <= CIA1_END)
        return cia1.read(static_cast<uint8_t>(addr - CIA1_START));

    if (addr >= CIA2_START && addr <= CIA2_END)
        return cia2.read(static_cast<uint8_t>(addr - CIA2_START));

    // CHAR_OUT is write-only; all other unimplemented I/O reads open bus
    return 0xFF;
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------
void Bus::write(uint16_t addr, uint8_t value) {
    if (!isIO(addr)) {
        ram.write(addr, value);
        return;
    }

    if (addr == CHAR_OUT_ADDR) {
        if (onCharOut) onCharOut(value);
        return;
    }

    if (addr >= CIA1_START && addr <= CIA1_END) {
        cia1.write(static_cast<uint8_t>(addr - CIA1_START), value);
        return;
    }

    if (addr >= CIA2_START && addr <= CIA2_END) {
        cia2.write(static_cast<uint8_t>(addr - CIA2_START), value);
        return;
    }

    // Unimplemented I/O write — silently ignored
}
