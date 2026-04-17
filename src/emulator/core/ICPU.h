#pragma once

#include <cstdint>
#include <string>

class Bus;

// ---------------------------------------------------------------------------
// ICPU — interface every CPU implementation must satisfy.
//
// The Bus is injected via connectBus() after construction so that CPU and Bus
// can both be value-constructed without circular dependencies.
// ---------------------------------------------------------------------------
class ICPU {
public:
    virtual ~ICPU() = default;

    virtual const char* cpuName()    const = 0;

    virtual void connectBus(Bus* bus)      = 0;
    virtual void reset()                   = 0;
    virtual void clock()                   = 0;
    virtual void irq()                     = 0;
    virtual void nmi()                     = 0;

    virtual bool        complete()    const = 0;
    virtual uint16_t    getPC()       const = 0;
    virtual std::string stateString() const = 0;

    // Register accessors — allows the UI to work through ICPU* for any CPU
    virtual uint8_t regA()  const = 0;
    virtual uint8_t regX()  const = 0;
    virtual uint8_t regY()  const = 0;
    virtual uint8_t regSP() const = 0;
    virtual uint8_t regP()  const = 0;
};
