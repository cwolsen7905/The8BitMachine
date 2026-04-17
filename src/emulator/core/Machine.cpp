#include "emulator/core/Machine.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>

Machine::Machine() {
    buildDefaultMap();
    cpu6510_.connectBus(&bus_);
    cpu8502_.connectBus(&bus_);
    cpu65c02_.connectBus(&bus_);
    vic_.connectBus(&bus_);
}

MachineConfigResult Machine::buildC64Preset(const std::string& kernalPath,
                                              const std::string& basicPath,
                                              const std::string& charPath) {
    bus_.clearDevices();
    dynamicDevices_.clear();

    // --- ROMs ---
    ROM* kernal = mountROM(0xE000, 0xFFFF, "KERNAL $E000-$FFFF", kernalPath);
    if (!kernal)
        return { false, "Cannot load kernal ROM: " + kernalPath };

    ROM* basic = mountROM(0xA000, 0xBFFF, "BASIC $A000-$BFFF", basicPath);
    if (!basic)
        return { false, "Cannot load BASIC ROM: " + basicPath };

    ROM* charRom = mountROM(0xD000, 0xDFFF, "CHAR $D000-$DFFF", charPath);
    if (!charRom)
        return { false, "Cannot load char ROM: " + charPath };

    // --- C64 I/O space dispatcher ---
    auto ioSpace = std::make_unique<C64IOSpace>(&vic_, &sid_, &cia1_, &cia2_);
    C64IOSpace* ioPtr = ioSpace.get();
    dynamicDevices_.push_back(std::move(ioSpace));

    // --- Three switchable regions ---
    // $A000–$BFFF: option 0=RAM, 1=BASIC ROM
    SwitchableRegion* regionA = mountSwitchableRegion(
        0xA000, 0xBFFF, "BASIC/RAM $A000-$BFFF");
    regionA->addOption(&ram_,  "RAM");
    regionA->addOption(basic,  "BASIC ROM");

    // $D000–$DFFF: option 0=RAM, 1=CHAR ROM, 2=I/O
    SwitchableRegion* regionD = mountSwitchableRegion(
        0xD000, 0xDFFF, "IO/CHAR/RAM $D000-$DFFF");
    regionD->addOption(&ram_,   "RAM");
    regionD->addOption(charRom, "CHAR ROM");
    regionD->addOption(ioPtr,   "I/O (VIC+SID+CIA)");

    // $E000–$FFFF: option 0=RAM, 1=KERNAL ROM
    SwitchableRegion* regionE = mountSwitchableRegion(
        0xE000, 0xFFFF, "KERNAL/RAM $E000-$FFFF");
    regionE->addOption(&ram_,   "RAM");
    regionE->addOption(kernal,  "KERNAL ROM");

    // $0000–$FFFF catch-all RAM
    bus_.addDevice(0x0000, 0xFFFF, &ram_, "RAM $0000-$FFFF");

    // --- 6510 I/O port → bank controller ---
    // C64 banking truth table (bits 2,1,0 = CHAREN, HIRAM, LORAM of effective data):
    //   $A000: BASIC when HIRAM && LORAM, else RAM
    //   $D000: I/O   when (HIRAM||LORAM) && CHAREN
    //          CHAR  when (HIRAM||LORAM) && !CHAREN
    //          RAM   when !HIRAM && !LORAM
    //   $E000: KERNAL when HIRAM, else RAM
    cpu6510_.onIOWrite = [regionA, regionD, regionE](uint8_t data, uint8_t dir) {
        // Output pins use data; input pins float high (pull-ups)
        const uint8_t eff    = (data & dir) | (~dir & 0xFF);
        const bool    loram  = (eff & 0x01) != 0;
        const bool    hiram  = (eff & 0x02) != 0;
        const bool    charen = (eff & 0x04) != 0;

        regionA->select((hiram && loram) ? 1 : 0);
        regionE->select(hiram ? 1 : 0);
        if (hiram || loram)
            regionD->select(charen ? 2 : 1);
        else
            regionD->select(0);
    };

    // --- Switch to 6510 and apply initial bank state ---
    activeCpu_ = &cpu6510_;
    cpu6510_.connectBus(&bus_);
    cpu6510_.reset();   // fires onIOWrite with power-on state ($37/$2F → BASIC+IO+KERNAL)

    return { true, "[C64] Machine ready — press F8 to reset, F5 to run" };
}

void Machine::buildDefaultMap() {
    // Bus iterates entries in registration order — higher-priority devices
    // must be registered first so they shadow the catch-all RAM entry.
    bus_.addDevice(0xD000, 0xD3FF, &vic_,  "VIC-IIe $D000–$D3FF");
    bus_.addDevice(0xD400, 0xD7FF, &sid_,  "SID $D400–$D7FF");
    bus_.addDevice(0xF100, 0xF1FF, &cia1_, "CIA1 $F100–$F1FF");
    bus_.addDevice(0xF200, 0xF2FF, &cia2_, "CIA2 $F200–$F2FF");
    // CHAR_OUT is a special case handled directly in Bus before the loop;
    // this sentinel entry (nullptr device) exists only for the designer panel.
    bus_.addDevice(0xF000, 0xF000, nullptr, "CHAR_OUT $F000 (debug port)");
    // RAM covers everything else — vectors at $FFFC/$FFFD live here too.
    bus_.addDevice(0x0000, 0xFFFF, &ram_,  "RAM  $0000–$FFFF");
}

void Machine::resetAddressMap() {
    bus_.clearDevices();
    dynamicDevices_.clear();
    buildDefaultMap();
}

ROM* Machine::mountROM(uint16_t start, uint16_t end,
                       const std::string& label, const std::string& filePath) {
    auto rom = std::make_unique<ROM>(label);
    if (!rom->loadFromFile(filePath)) return nullptr;
    ROM* ptr = rom.get();
    dynamicDevices_.push_back(std::move(rom));
    bus_.addDevice(start, end, ptr, label);
    return ptr;
}

BankedMemory* Machine::mountBankedMemory(uint16_t primaryStart, uint16_t primaryEnd,
                                          uint16_t bankSelectAddr, uint8_t numBanks) {
    if (primaryEnd < primaryStart) return nullptr;
    const uint32_t bankSize = static_cast<uint32_t>(primaryEnd - primaryStart) + 1;

    auto mem  = std::make_unique<BankedMemory>(numBanks, bankSize);
    auto port = std::make_unique<BankSelectPort>(mem.get());

    BankedMemory* memPtr  = mem.get();
    BankSelectPort* portPtr = port.get();

    dynamicDevices_.push_back(std::move(mem));
    dynamicDevices_.push_back(std::move(port));

    char memLabel[64], portLabel[64];
    std::snprintf(memLabel,  sizeof(memLabel),  "BankedRAM $%04X–$%04X (%d banks)",
                  primaryStart, primaryEnd, numBanks);
    std::snprintf(portLabel, sizeof(portLabel), "BankSelect $%04X", bankSelectAddr);

    bus_.addDevice(primaryStart,   primaryEnd,    memPtr,  memLabel);
    bus_.addDevice(bankSelectAddr, bankSelectAddr, portPtr, portLabel);
    return memPtr;
}

SwitchableRegion* Machine::mountSwitchableRegion(uint16_t start, uint16_t end,
                                                  const std::string& label) {
    auto reg = std::make_unique<SwitchableRegion>(label);
    SwitchableRegion* ptr = reg.get();
    dynamicDevices_.push_back(std::move(reg));
    bus_.addDevice(start, end, ptr, label);
    return ptr;
}

void Machine::addRegionOption(SwitchableRegion* region, IBusDevice* dev,
                               const std::string& optionLabel) {
    region->addOption(dev, optionLabel);
}

BankController* Machine::mountBankController(uint16_t addr, const std::string& label) {
    auto ctrl = std::make_unique<BankController>(label);
    BankController* ptr = ctrl.get();
    dynamicDevices_.push_back(std::move(ctrl));
    bus_.addDevice(addr, addr, ptr, label);
    return ptr;
}

void Machine::addControllerMapping(BankController* ctrl, uint8_t value,
                                    SwitchableRegion* region, uint8_t bankIndex) {
    ctrl->addMapping(value, region, bankIndex);
}

void Machine::unmountAt(size_t busIndex) {
    if (busIndex >= bus_.devices().size()) return;
    IBusDevice* dev = bus_.devices()[busIndex].device;
    bus_.removeAt(busIndex);
    if (!dev) return;
    // Free dynamic device only if no other bus entries still reference it
    bool stillMapped = false;
    for (const auto& e : bus_.devices())
        if (e.device == dev) { stillMapped = true; break; }
    if (!stillMapped) {
        auto it = std::find_if(dynamicDevices_.begin(), dynamicDevices_.end(),
            [dev](const auto& p) { return p.get() == dev; });
        if (it != dynamicDevices_.end())
            dynamicDevices_.erase(it);
    }
}

void Machine::reset() {
    bus_.reset();   // resets RAM + CIA1 + CIA2 (skips nullptr sentinel)
    activeCpu_->reset();
}

void Machine::clock() {
    bus_.clock();   // ticks CIA1 + CIA2 (skips nullptr sentinel)
}

bool Machine::selectCPU(const std::string& name) {
    ICPU* next = nullptr;
    if      (name == cpu6510_.cpuName())  next = &cpu6510_;
    else if (name == cpu8502_.cpuName())  next = &cpu8502_;
    else if (name == cpu65c02_.cpuName()) next = &cpu65c02_;
    if (!next) return false;
    activeCpu_ = next;
    activeCpu_->reset();
    return true;
}

void Machine::setCharOutCallback(std::function<void(uint8_t)> cb) {
    bus_.onCharOut = std::move(cb);
}

void Machine::setIRQCallback(std::function<void()> cb) {
    cia1_.onIRQ = cb;
    vic_.onIRQ  = std::move(cb);
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

const char* Machine::idForDevice(const IBusDevice* dev) const {
    if (dev == &vic_)   return "vic";
    if (dev == &sid_)   return "sid";
    if (dev == &cia1_)  return "cia1";
    if (dev == &cia2_)  return "cia2";
    if (dev == &ram_)   return "ram";
    if (dev == nullptr) return "char_out";
    for (const auto& d : dynamicDevices_) {
        if (d.get() != dev) continue;
        if (dynamic_cast<const ROM*>(dev))              return "rom";
        if (dynamic_cast<const BankedMemory*>(dev))    return "banked_ram";
        if (dynamic_cast<const BankSelectPort*>(dev))  return "bank_select";
        if (dynamic_cast<const SwitchableRegion*>(dev)) return "switchable_region";
        if (dynamic_cast<const BankController*>(dev))  return "bank_controller";
    }
    return "unknown";
}

IBusDevice* Machine::deviceForId(const std::string& id) {
    if (id == "vic")      return &vic_;
    if (id == "sid")      return &sid_;
    if (id == "cia1")     return &cia1_;
    if (id == "cia2")     return &cia2_;
    if (id == "ram")      return &ram_;
    if (id == "char_out") return nullptr;
    return nullptr;
}

// ---------------------------------------------------------------------------
// saveConfig
// ---------------------------------------------------------------------------

MachineConfigResult Machine::saveConfig(const std::string& path) const {
    using json = nlohmann::json;

    json root;
    root["version"] = 1;
    root["cpu"]     = activeCpu_->cpuName();

    json devArray = json::array();
    for (const auto& e : bus_.devices()) {
        json entry;
        entry["id"]    = idForDevice(e.device);
        entry["label"] = e.label;

        std::ostringstream ss;
        ss << std::uppercase << std::hex << std::setfill('0');
        ss << std::setw(4) << e.start;
        entry["start"] = ss.str();
        ss.str("");
        ss << std::setw(4) << e.end;
        entry["end"]   = ss.str();

        const std::string devId = idForDevice(e.device);

        // Persist file path for ROM entries so they can be reloaded
        if (devId == "rom") {
            const auto* rom = static_cast<const ROM*>(e.device);
            entry["path"] = rom->filePath();
        }

        // Persist bank count + companion port address for banked_ram entries
        if (devId == "banked_ram") {
            const auto* bm = static_cast<const BankedMemory*>(e.device);
            entry["num_banks"] = bm->numBanks();
            // Find the companion BankSelectPort on the bus
            for (const auto& e2 : bus_.devices()) {
                const auto* bsp = dynamic_cast<const BankSelectPort*>(e2.device);
                if (bsp && bsp->targetMemory() == bm) {
                    entry["bank_select_addr"] = e2.start;
                    break;
                }
            }
        }

        // bank_select entries are reconstructed via banked_ram's bank_select_addr on load.
        if (devId == "bank_select") continue;

        devArray.push_back(entry);
    }
    root["devices"] = devArray;

    std::ofstream f(path);
    if (!f)
        return { false, "[Config] Error: cannot write to " + path };

    f << root.dump(2);
    return { true, "[Config] Saved: " + path };
}

// ---------------------------------------------------------------------------
// loadConfig
// ---------------------------------------------------------------------------

MachineConfigResult Machine::loadConfig(const std::string& path) {
    using json = nlohmann::json;

    std::ifstream f(path);
    if (!f)
        return { false, "[Config] Error: cannot open " + path };

    json root;
    try {
        root = json::parse(f);
    } catch (const json::exception& e) {
        return { false, std::string("[Config] JSON parse error: ") + e.what() };
    }

    if (!root.contains("devices") || !root["devices"].is_array())
        return { false, "[Config] Error: missing 'devices' array" };

    bus_.clearDevices();

    for (const auto& entry : root["devices"]) {
        const std::string id    = entry.value("id",    "");
        const std::string label = entry.value("label", "");
        const std::string sStr  = entry.value("start", "0000");
        const std::string eStr  = entry.value("end",   "0000");

        const uint16_t start = static_cast<uint16_t>(std::stoul(sStr, nullptr, 16));
        const uint16_t end   = static_cast<uint16_t>(std::stoul(eStr, nullptr, 16));

        if (id == "rom") {
            const std::string filePath = entry.value("path", "");
            if (!filePath.empty())
                mountROM(start, end, label, filePath);
        } else if (id == "banked_ram") {
            const uint8_t  numBanks      = static_cast<uint8_t>(entry.value("num_banks", 4));
            const uint16_t bankSelectAddr = static_cast<uint16_t>(
                entry.value("bank_select_addr", 0xDFFF));
            mountBankedMemory(start, end, bankSelectAddr, numBanks);
        } else {
            IBusDevice* dev = deviceForId(id);
            if (id != "unknown")
                bus_.addDevice(start, end, dev, label);
        }
    }

    const std::string cpuName = root.value("cpu", "");
    if (!cpuName.empty()) selectCPU(cpuName);
    return { true, "[Config] Loaded: " + path + "  (CPU: " + activeCpu_->cpuName() + ")" };
}
