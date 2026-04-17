#pragma once

#include "emulator/core/IBusDevice.h"
#include "emulator/devices/BankedMemory.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// BankSelectPort — single-byte I/O port that switches a BankedMemory bank.
//
// Write any value to select the corresponding bank (clamped by BankedMemory).
// Read returns the current bank number.
// ---------------------------------------------------------------------------
class BankSelectPort : public IBusDevice {
public:
    explicit BankSelectPort(BankedMemory* target);

    BankedMemory* targetMemory() const { return target_; }

    // IBusDevice
    const char* deviceName() const override { return "BankSelectPort"; }
    void    reset()                       override { target_->selectBank(0); }
    uint8_t read (uint16_t offset) const  override;
    void    write(uint16_t offset, uint8_t value) override;
    std::string statusLine() const        override;

private:
    BankedMemory* target_;
};
