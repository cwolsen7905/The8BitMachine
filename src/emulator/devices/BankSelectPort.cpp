#include "emulator/devices/BankSelectPort.h"
#include <sstream>

BankSelectPort::BankSelectPort(BankedMemory* target) : target_(target) {}

uint8_t BankSelectPort::read(uint16_t) const {
    return target_->currentBank();
}

void BankSelectPort::write(uint16_t, uint8_t value) {
    target_->selectBank(value);
}

std::string BankSelectPort::statusLine() const {
    std::ostringstream ss;
    ss << "Selected bank: " << static_cast<int>(target_->currentBank())
       << " / " << static_cast<int>(target_->numBanks());
    return ss.str();
}
