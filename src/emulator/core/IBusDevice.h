#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// IBusDevice — interface every address-space device must implement.
//
// The Bus passes an *offset* from the device's mapped base address, not the
// raw bus address.  For example, a CIA mounted at $F100 receives offset 0x04
// when the CPU accesses $F104.
//
// clock() is optional (default no-op) — only devices with internal timing
// (CIA timers, future SID envelopes, …) need to override it.
// ---------------------------------------------------------------------------
class IBusDevice {
public:
    virtual ~IBusDevice() = default;

    virtual const char* deviceName()  const = 0;
    virtual void        reset()             = 0;
    virtual void        clock()             {}   // optional — timing devices override
    virtual uint8_t     read (uint16_t offset) const = 0;
    virtual void        write(uint16_t offset, uint8_t value) = 0;

    // One-line status for the Machine Designer / debug panel.
    virtual std::string statusLine() const { return ""; }

    // Optional full ImGui panel.  Return true from hasPanel() to opt in.
    // drawPanel() is called each frame while the panel is open; title comes
    // from the bus entry label so two CIAs get distinct window titles.
    virtual bool hasPanel() const { return false; }
    virtual void drawPanel(const char* title, bool* open) { (void)title; (void)open; }

    // Sub-device address lookup — container devices (e.g. C64IOSpace) override
    // this to report where a contained chip lives relative to their own base.
    // Returns {-1,-1} when the device is not contained here.
    struct SubRange { int start = -1; int end = -1; bool valid() const { return start >= 0; } };
    virtual SubRange findSubDevice(const IBusDevice*) const { return {}; }
};
