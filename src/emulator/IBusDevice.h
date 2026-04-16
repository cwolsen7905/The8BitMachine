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
};
