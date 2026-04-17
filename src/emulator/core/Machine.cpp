#include "emulator/core/Machine.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>

Machine::Machine() : c64IOSpace_(&vic_, &sid_, &cia1_, &cia2_) {
    // Fixed chips are always clocked directly in Machine::clock(); the bus
    // must not also clock them or they'd run at double speed in default mode.
    bus_.setNoAutoClk(&vic_);
    bus_.setNoAutoClk(&sid_);
    bus_.setNoAutoClk(&cia1_);
    bus_.setNoAutoClk(&cia2_);
    bus_.setNoAutoClk(&ula_);

    buildDefaultMap();
    cpu6510_.connectBus(&bus_);
    cpu8502_.connectBus(&bus_);
    cpu65c02_.connectBus(&bus_);
    cpuZ80_.connectBus(&bus_);
    vic_.connectBus(&bus_);
    ula_.connectBus(&bus_);

    // Default screen shows VIC framebuffer
    activeScreen_ = { VIC6566::WIDTH, VIC6566::HEIGHT, vic_.framebuffer() };
}

MachineConfigResult Machine::buildC64Preset(const std::string& kernalPath,
                                              const std::string& basicPath,
                                              const std::string& charPath,
                                              bool               keyMatrixTranspose) {
    hasPreset_ = false;
    bus_.clearDevices();
    dynamicDevices_.clear();

    // --- Load ROMs into dynamicDevices_ WITHOUT adding to bus.
    // They must only be reachable through SwitchableRegions, which are
    // registered first.  If the ROMs were direct bus entries they would shadow
    // the SwitchableRegions (bus is first-match) and C64IOSpace would be
    // unreachable, breaking all VIC/SID/CIA register access.
    ROM* kernal = loadROM("KERNAL $E000-$FFFF", kernalPath);
    if (!kernal)
        return { false, "Cannot load kernal ROM: " + kernalPath };

    ROM* basic = loadROM("BASIC $A000-$BFFF", basicPath);
    if (!basic)
        return { false, "Cannot load BASIC ROM: " + basicPath };

    ROM* charRom = loadROM("CHAR $D000-$DFFF", charPath);
    if (!charRom)
        return { false, "Cannot load char ROM: " + charPath };

    // --- Three switchable regions — registered on bus BEFORE catch-all RAM ---
    // $A000–$BFFF: option 0=RAM, 1=BASIC ROM
    SwitchableRegion* regionA = mountSwitchableRegion(
        0xA000, 0xBFFF, "BASIC/RAM $A000-$BFFF");
    regionA->addOption(&ram_,  "RAM");
    regionA->addOption(basic,  "BASIC ROM");

    // $D000–$DFFF: option 0=RAM, 1=CHAR ROM, 2=I/O (uses fixed c64IOSpace_)
    SwitchableRegion* regionD = mountSwitchableRegion(
        0xD000, 0xDFFF, "IO/CHAR/RAM $D000-$DFFF");
    regionD->addOption(&ram_,        "RAM");
    regionD->addOption(charRom,      "CHAR ROM");
    regionD->addOption(&c64IOSpace_, "I/O (VIC+SID+CIA)");

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

    // Record preset so saveConfig can serialise it instead of the device list.
    hasPreset_ = true;
    preset_    = { "c64", kernalPath, basicPath, charPath, keyMatrixTranspose };

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

ROM* Machine::loadROM(const std::string& label, const std::string& filePath) {
    auto rom = std::make_unique<ROM>(label);
    if (!rom->loadFromFile(filePath)) return nullptr;
    ROM* ptr = rom.get();
    dynamicDevices_.push_back(std::move(rom));
    return ptr;  // intentionally NOT added to bus
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

// ---------------------------------------------------------------------------
// Spectrum 48K preset
// ---------------------------------------------------------------------------

MachineConfigResult Machine::buildSpectrumPreset(const std::string& romPath) {
    hasPreset_ = false;
    bus_.clearDevices();
    dynamicDevices_.clear();

    // 16 KB ROM at $0000–$3FFF
    ROM* rom = mountROM(0x0000, 0x3FFF, "Spectrum ROM $0000-$3FFF", romPath);
    if (!rom)
        return { false, "Cannot load Spectrum ROM: " + romPath };

    // 48 KB RAM at $4000–$FFFF (screen RAM lives in $4000–$57FF / attrs $5800–$5AFF)
    bus_.addDevice(0x4000, 0xFFFF, &ram_, "RAM  $4000–$FFFF");

    // Wire ULA port I/O to the Z80
    cpuZ80_.setPortHandlers(
        [this](uint16_t port) -> uint8_t { return ula_.portRead(port); },
        [this](uint16_t port, uint8_t val) { ula_.portWrite(port, val); }
    );

    // ULA fires interrupt → Z80 IRQ
    ula_.onIRQ = [this]() { cpuZ80_.irq(); };

    ula_.reset();
    ula_.connectBus(&bus_);

    // Switch to Z80 and reset
    activeCpu_ = &cpuZ80_;
    activeCpu_->reset();

    // Point the active screen at the ULA framebuffer
    activeScreen_ = { ULA::WIDTH, ULA::HEIGHT, ula_.framebuffer() };

    hasPreset_ = true;
    preset_.name  = "spectrum48";
    preset_.kernalPath = romPath;

    return { true, "Spectrum 48K preset built." };
}

void Machine::reset() {
    vic_.reset();
    sid_.reset();
    cia1_.reset();
    cia2_.reset();
    ula_.reset();
    bus_.reset();   // resets RAM and any dynamic devices
    activeCpu_->reset();
}

void Machine::clock() {
    vic_.clock();
    sid_.clock();
    cia1_.clock();
    cia2_.clock();
    ula_.clock();
    bus_.clock();   // ticks dynamic devices; skips fixed chips above
}

bool Machine::selectCPU(const std::string& name) {
    ICPU* next = nullptr;
    if      (name == cpu6510_.cpuName())  next = &cpu6510_;
    else if (name == cpu8502_.cpuName())  next = &cpu8502_;
    else if (name == cpu65c02_.cpuName()) next = &cpu65c02_;
    else if (name == cpuZ80_.cpuName())   next = &cpuZ80_;
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

std::vector<Machine::PanelEntry> Machine::panelDevices() {
    std::vector<PanelEntry> result;
    std::unordered_set<IBusDevice*> onBus;

    // Collect every bus entry that has a panel — no deduplication so that the
    // same chip mapped at two addresses produces two independently-labelled
    // entries (both open the same panel, but the menu is self-describing).
    for (const auto& e : bus_.devices()) {
        if (e.device && e.device->hasPanel()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s $%04X-$%04X",
                          e.device->deviceName(), e.start, e.end);
            result.push_back({ buf, e.device });
            onBus.insert(e.device);
        }
    }

    // Fixed chips not directly on the bus (e.g. inside C64IOSpace inside a
    // SwitchableRegion).  Walk the bus — including one level into any
    // SwitchableRegion's options — to find the chip and derive its address.
    auto findAddr = [&](IBusDevice* dev) -> std::string {
        auto fmtRange = [](const char* name, uint16_t lo, uint16_t hi) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s $%04X-$%04X", name, lo, hi);
            return std::string(buf);
        };
        for (const auto& e : bus_.devices()) {
            if (!e.device) continue;
            // Direct container
            auto r = e.device->findSubDevice(dev);
            if (r.valid())
                return fmtRange(dev->deviceName(),
                                static_cast<uint16_t>(e.start + r.start),
                                static_cast<uint16_t>(e.start + r.end));
            // One level deep through SwitchableRegion
            if (auto* sr = dynamic_cast<SwitchableRegion*>(e.device)) {
                for (const auto& opt : sr->options()) {
                    if (!opt.device) continue;
                    r = opt.device->findSubDevice(dev);
                    if (r.valid())
                        return fmtRange(dev->deviceName(),
                                        static_cast<uint16_t>(e.start + r.start),
                                        static_cast<uint16_t>(e.start + r.end));
                }
            }
        }
        return dev->deviceName();  // no address found — just the chip name
    };

    for (IBusDevice* dev : std::initializer_list<IBusDevice*>{&vic_, &sid_, &cia1_, &cia2_, &ula_}) {
        if (dev->hasPanel() && !onBus.count(dev))
            result.push_back({ findAddr(dev), dev });
    }

    return result;
}

const char* Machine::idForDevice(const IBusDevice* dev) const {
    if (dev == &vic_)        return "vic";
    if (dev == &sid_)        return "sid";
    if (dev == &cia1_)       return "cia1";
    if (dev == &cia2_)       return "cia2";
    if (dev == &ram_)        return "ram";
    if (dev == &ula_)         return "ula";
    if (dev == &c64IOSpace_) return "c64_io_space";
    if (dev == nullptr)      return "char_out";
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
    if (id == "vic")          return &vic_;
    if (id == "sid")          return &sid_;
    if (id == "cia1")         return &cia1_;
    if (id == "cia2")         return &cia2_;
    if (id == "ram")          return &ram_;
    if (id == "ula")          return &ula_;
    if (id == "c64_io_space") return &c64IOSpace_;
    if (id == "char_out")     return nullptr;
    return nullptr;
}

// ---------------------------------------------------------------------------
// saveConfig
// ---------------------------------------------------------------------------

MachineConfigResult Machine::saveConfig(const std::string& path, int cyclesPerFrame) const {
    using json = nlohmann::json;

    json root;

    // Preset machines serialise as a self-contained definition rather than a
    // raw device list, so that the banking wiring can be fully reconstructed.
    if (hasPreset_) {
        root["version"] = 2;
        root["preset"]  = preset_.name;
        json pc;
        pc["kernal"]              = preset_.kernalPath;
        pc["basic"]               = preset_.basicPath;
        pc["char"]                = preset_.charPath;
        pc["key_matrix_transpose"] = preset_.keyMatrixTranspose;
        root["preset_config"]     = pc;
        if (cyclesPerFrame > 0)
            root["cycles_per_frame"] = cyclesPerFrame;

        std::ofstream f(path);
        if (!f)
            return { false, "[Config] Error: cannot write to " + path };
        f << root.dump(2);
        return { true, "[Config] Saved: " + path };
    }

    root["version"] = 1;
    root["cpu"]     = activeCpu_->cpuName();
    if (cyclesPerFrame > 0)
        root["cycles_per_frame"] = cyclesPerFrame;

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

    // Version 2: preset block — reconstruct via the appropriate builder.
    if (root.contains("preset")) {
        const std::string preset = root["preset"].get<std::string>();
        if (preset == "c64") {
            const auto& pc   = root["preset_config"];
            const std::string kernal = pc.value("kernal", "");
            const std::string basic  = pc.value("basic",  "");
            const std::string chr    = pc.value("char",   "");
            const bool        xpose  = pc.value("key_matrix_transpose", true);
            auto result = buildC64Preset(kernal, basic, chr, xpose);
            if (!result.ok) return result;
            result.message = "[Config] Loaded: " + path + "  (preset: C64)";
            result.hasPreset          = true;
            result.keyMatrixTranspose = xpose;
            result.cyclesPerFrame     = root.value("cycles_per_frame", 0);
            return result;
        }
        return { false, "[Config] Error: unknown preset '" + preset + "'" };
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
    MachineConfigResult result;
    result.ok             = true;
    result.message        = "[Config] Loaded: " + path + "  (CPU: " + activeCpu_->cpuName() + ")";
    result.cyclesPerFrame = root.value("cycles_per_frame", 0);
    return result;
}
