#include "emulator/devices/SwitchableRegion.h"
#include <sstream>

SwitchableRegion::SwitchableRegion(std::string label)
    : label_(std::move(label))
{}

void SwitchableRegion::addOption(IBusDevice* device, const std::string& optionLabel) {
    options_.push_back({ device, optionLabel });
}

void SwitchableRegion::select(uint8_t index) {
    if (options_.empty()) return;
    active_ = index % static_cast<uint8_t>(options_.size());
}

void SwitchableRegion::reset() {
    active_ = 0;
    for (auto& opt : options_)
        if (opt.device) opt.device->reset();
}

uint8_t SwitchableRegion::read(uint16_t offset) const {
    if (options_.empty() || !options_[active_].device) return 0xFF;
    return options_[active_].device->read(offset);
}

void SwitchableRegion::write(uint16_t offset, uint8_t value) {
    if (options_.empty() || !options_[active_].device) return;
    options_[active_].device->write(offset, value);
}

std::string SwitchableRegion::statusLine() const {
    if (options_.empty()) return "no options";
    std::ostringstream ss;
    ss << "Bank " << static_cast<int>(active_)
       << "/" << options_.size()
       << ": " << options_[active_].label;
    return ss.str();
}
