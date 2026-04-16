#include "Bus.h"

// ---------------------------------------------------------------------------
// Device management
// ---------------------------------------------------------------------------

void Bus::addDevice(uint16_t start, uint16_t end,
                    IBusDevice* device, const std::string& label) {
    devices_.push_back({ start, end, device, label });
}

void Bus::removeDevice(IBusDevice* device) {
    devices_.erase(
        std::remove_if(devices_.begin(), devices_.end(),
            [device](const DeviceEntry& e){ return e.device == device; }),
        devices_.end());
}

void Bus::clearDevices() {
    devices_.clear();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Bus::reset() {
    for (auto& e : devices_)
        if (e.device) e.device->reset();
}

void Bus::clock() {
    for (auto& e : devices_)
        if (e.device) e.device->clock();
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

uint8_t Bus::read(uint16_t addr) const {
    if (addr == CHAR_OUT_ADDR) return 0xFF;  // write-only port

    for (const auto& e : devices_) {
        if (e.device && addr >= e.start && addr <= e.end)
            return e.device->read(static_cast<uint16_t>(addr - e.start));
    }
    return 0xFF;  // open bus
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

void Bus::write(uint16_t addr, uint8_t value) {
    if (addr == CHAR_OUT_ADDR) {
        if (onCharOut) onCharOut(value);
        return;
    }

    for (auto& e : devices_) {
        if (e.device && addr >= e.start && addr <= e.end) {
            e.device->write(static_cast<uint16_t>(addr - e.start), value);
            return;
        }
    }
    // Unmatched write — silently dropped (open bus)
}
