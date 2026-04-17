#include "emulator/devices/Memory.h"

Memory::Memory() {
    reset();
}

void Memory::reset() {
    data_.fill(0x00);

    // Reset vector at $FFFC/$FFFD — point to stub program at $0200
    data_[0xFFFC] = 0x00;  // lo
    data_[0xFFFD] = 0x02;  // hi  →  PC = $0200

    // Stub program: 64 NOPs ($EA) followed by JMP $0200
    for (int i = 0; i < 64; ++i)
        data_[0x0200 + i] = 0xEA;   // NOP

    data_[0x0240] = 0x4C;            // JMP abs
    data_[0x0241] = 0x00;            // lo
    data_[0x0242] = 0x02;            // hi  →  JMP $0200
}

uint8_t Memory::read(uint16_t addr) const {
    return data_[addr];
}

void Memory::write(uint16_t addr, uint8_t value) {
    data_[addr] = value;
}
