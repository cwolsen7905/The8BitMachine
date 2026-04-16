#include "Machine.h"

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
