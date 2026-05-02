#pragma once

// ---------------------------------------------------------------------------
// IIECDevice — implemented by any device that connects to the IEC serial bus.
//
// The three IEC lines are open-collector: any device can pull a line low;
// the bus value is the AND of all drivers (false = asserted / pulled low).
//
// CIA2 on a C64 drives ATN, CLK, and DATA as outputs and reads them back as
// inputs.  On every Port A write that changes bits 3-5, CIA2 calls
// setIECLines() on each registered device and then queries getIECLines() to
// compose the wired-AND bus state it presents back to the CPU.
// ---------------------------------------------------------------------------
struct IECLines {
    bool atn  = true;   // true = released (high), false = asserted (low)
    bool clk  = true;
    bool data = true;
};

class IIECDevice {
public:
    virtual ~IIECDevice() = default;

    // Called by CIA2 after it drives its output lines onto the bus.
    // `host` reflects the current state CIA2 is driving (wired-AND so far).
    virtual void setIECLines(IECLines host) = 0;

    // CIA2 calls this to read back whatever lines the device is driving.
    // Return true on each line the device is *not* pulling low.
    virtual IECLines getIECLines() const = 0;
};
