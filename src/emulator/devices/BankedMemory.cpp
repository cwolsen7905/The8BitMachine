#include "emulator/devices/BankedMemory.h"
#include <algorithm>
#include <sstream>

BankedMemory::BankedMemory(uint8_t numBanks, uint32_t bankSize)
    : numBanks_(std::max<uint8_t>(1, numBanks))
    , bankSize_(bankSize)
    , data_(static_cast<size_t>(numBanks_) * bankSize_, 0x00)
{}

void BankedMemory::selectBank(uint8_t bank) {
    currentBank_ = bank % numBanks_;
}

void BankedMemory::reset() {
    currentBank_ = 0;
    std::fill(data_.begin(), data_.end(), 0x00);
}

uint8_t BankedMemory::read(uint16_t offset) const {
    const size_t idx = static_cast<size_t>(currentBank_) * bankSize_ + offset;
    if (idx >= data_.size()) return 0xFF;
    return data_[idx];
}

void BankedMemory::write(uint16_t offset, uint8_t value) {
    const size_t idx = static_cast<size_t>(currentBank_) * bankSize_ + offset;
    if (idx < data_.size())
        data_[idx] = value;
}

std::string BankedMemory::statusLine() const {
    std::ostringstream ss;
    ss << "Bank " << static_cast<int>(currentBank_)
       << " / " << static_cast<int>(numBanks_);
    if (bankSize_ >= 1024)
        ss << "  (" << (bankSize_ / 1024) << " KB each)";
    else
        ss << "  (" << bankSize_ << " B each)";
    return ss.str();
}
