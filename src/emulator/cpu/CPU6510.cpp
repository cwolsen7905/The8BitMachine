#include "emulator/cpu/CPU6510.h"

CPU6510::CPU6510() {
    buildNMOSTable();
    nmosBug_ = true;
}

void CPU6510::reset() {
    ioDir_  = 0x2F;
    ioData_ = 0x37;
    CPU6502Base::reset();
    if (onIOWrite) onIOWrite(ioData_, ioDir_);
}

uint8_t CPU6510::busRead(uint16_t addr) {
    if (addr == 0x0000) return ioDir_;
    if (addr == 0x0001) return ioData_;
    return CPU6502Base::busRead(addr);
}

void CPU6510::busWrite(uint16_t addr, uint8_t val) {
    if (addr == 0x0000) {
        ioDir_ = val;
        if (onIOWrite) onIOWrite(ioData_, ioDir_);
        return;
    }
    if (addr == 0x0001) {
        ioData_ = val;
        if (onIOWrite) onIOWrite(ioData_, ioDir_);
        return;
    }
    CPU6502Base::busWrite(addr, val);
}
