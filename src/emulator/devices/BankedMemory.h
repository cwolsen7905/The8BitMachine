#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// BankedMemory — RAM split into N equal banks mapped at one address window.
//
// All banks share the same address window (primaryStart–primaryEnd on the bus).
// selectBank(n) switches which bank is visible; n is clamped to numBanks-1.
// A companion BankSelectPort device writes the bank number on CPU store.
// ---------------------------------------------------------------------------
class BankedMemory : public IBusDevice {
public:
    BankedMemory(uint8_t numBanks, uint32_t bankSize);

    void    selectBank(uint8_t bank);
    uint8_t currentBank() const { return currentBank_; }
    uint8_t numBanks()    const { return numBanks_; }

    // IBusDevice
    const char* deviceName() const override { return "BankedMemory"; }
    void    reset()                       override;
    uint8_t read (uint16_t offset) const  override;
    void    write(uint16_t offset, uint8_t value) override;
    std::string statusLine() const        override;

private:
    uint8_t              numBanks_;
    uint32_t             bankSize_;
    uint8_t              currentBank_ = 0;
    std::vector<uint8_t> data_;  // numBanks * bankSize bytes, contiguous
};
