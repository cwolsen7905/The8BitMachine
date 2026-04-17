#include "emulator/devices/BankController.h"
#include <algorithm>
#include <sstream>

BankController::BankController(std::string label)
    : label_(std::move(label))
{}

void BankController::addMapping(uint8_t value, SwitchableRegion* region, uint8_t bankIndex) {
    mappings_[value].push_back({ region, bankIndex });
    if (std::find(regions_.begin(), regions_.end(), region) == regions_.end())
        regions_.push_back(region);
}

void BankController::apply(uint8_t value) {
    value_ = value;
    auto it = mappings_.find(value);
    if (it != mappings_.end())
        for (const auto& action : it->second)
            action.region->select(action.bankIndex);
    if (onWrite) onWrite(value);
}

void BankController::reset() {
    apply(0);
}

uint8_t BankController::read(uint16_t) const {
    return value_;
}

void BankController::write(uint16_t, uint8_t value) {
    apply(value);
}

std::string BankController::statusLine() const {
    std::ostringstream ss;
    ss << "Value=$" << std::hex << std::uppercase << static_cast<int>(value_)
       << "  regions=" << regions_.size()
       << "  mappings=" << mappings_.size();
    return ss.str();
}
