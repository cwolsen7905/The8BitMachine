#include "emulator/core/Machine.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>

Machine::Machine() {
    buildDefaultMap();
    cpu_.connectBus(&bus_);
}

void Machine::buildDefaultMap() {
    // Bus iterates entries in registration order — higher-priority devices
    // must be registered first so they shadow the catch-all RAM entry.
    bus_.addDevice(0xF100, 0xF1FF, &cia1_, "CIA1 $F100–$F1FF");
    bus_.addDevice(0xF200, 0xF2FF, &cia2_, "CIA2 $F200–$F2FF");
    // CHAR_OUT is a special case handled directly in Bus before the loop;
    // this sentinel entry (nullptr device) exists only for the designer panel.
    bus_.addDevice(0xF000, 0xF000, nullptr, "CHAR_OUT $F000 (debug port)");
    // RAM covers everything else — vectors at $FFFC/$FFFD live here too.
    bus_.addDevice(0x0000, 0xFFFF, &ram_,  "RAM  $0000–$FFFF");
}

void Machine::reset() {
    bus_.reset();   // resets RAM + CIA1 + CIA2 (skips nullptr sentinel)
    cpu_.reset();
}

void Machine::clock() {
    bus_.clock();   // ticks CIA1 + CIA2 (skips nullptr sentinel)
}

void Machine::setCharOutCallback(std::function<void(uint8_t)> cb) {
    bus_.onCharOut = std::move(cb);
}

void Machine::setIRQCallback(std::function<void()> cb) {
    cia1_.onIRQ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

const char* Machine::idForDevice(const IBusDevice* dev) const {
    if (dev == &cia1_)    return "cia1";
    if (dev == &cia2_)    return "cia2";
    if (dev == &ram_)     return "ram";
    if (dev == nullptr)   return "char_out";
    return "unknown";
}

IBusDevice* Machine::deviceForId(const std::string& id) {
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
    root["cpu"]     = cpu_.cpuName();

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

        IBusDevice* dev = deviceForId(id);
        if (id != "unknown")
            bus_.addDevice(start, end, dev, label);
    }

    const std::string cpu = root.value("cpu", "");
    return { true, "[Config] Loaded: " + path + "  (CPU: " + cpu + ")" };
}
