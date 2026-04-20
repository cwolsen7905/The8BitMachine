#pragma once

#include "emulator/core/IIECDevice.h"
#include "emulator/core/IHasPanel.h"
#include "emulator/core/IPeripheral.h"
#include "emulator/devices/D64Image.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Drive1541 — CBM 1541 disk drive, software IEC state machine.
//
// Instead of emulating the 1541's internal 6502+VIA hardware, this class
// implements the CBM serial bus protocol directly in software.  It handles
// the standard KERNAL loading sequence (LOAD"name",8,1 / LOAD"*",8,1) and
// streams file bytes from a mounted .d64 image over the IEC bus.
//
// Fast loaders that bit-bang the IEC lines directly will not work; those
// require full VIA+ROM emulation (a future phase).
//
// IEC protocol summary (CBM serial, not IEEE-488):
//   ATN low  → host is sending a command byte
//   Byte transfer uses CLK/DATA handshake (CLK high = ready, DATA low = busy)
//   Commands: LISTEN (0x20+dev), TALK (0x40+dev), OPEN (0x60+ch),
//             DATA  (0x60+ch),  CLOSE(0x70+ch), UNLISTEN(0x3F), UNTALK(0x5F)
// ---------------------------------------------------------------------------
class Drive1541 : public IIECDevice, public IPeripheral, public IHasPanel {
public:
    explicit Drive1541(int deviceNumber = 8);

    // -----------------------------------------------------------------------
    // IIECDevice
    // -----------------------------------------------------------------------
    void     setIECLines(IECLines host) override;
    IECLines getIECLines() const override { return driven_; }

    // -----------------------------------------------------------------------
    // IPeripheral
    // -----------------------------------------------------------------------
    const char*        peripheralName() const override;
    const std::string& mountedImage()   const override { return image_.path(); }
    bool               mount(const std::string& path) override;
    void               eject()          override;
    const std::string& mountError()     const override { return mountError_; }

    // -----------------------------------------------------------------------
    // IHasPanel
    // -----------------------------------------------------------------------
    void drawPanel(const char* title, bool* open) override;

    // -----------------------------------------------------------------------
    // Clock — call once per C64 clock cycle (≈ 1 MHz).
    // Drives the IEC bit-bang handshake timing.
    // -----------------------------------------------------------------------
    void clock();

    int deviceNumber() const { return deviceNumber_; }

private:
    std::vector<uint8_t> getDirectoryData();
    int      deviceNumber_;
    D64Image image_;
    std::string mountError_;
    std::string peripheralName_;  // cached "Drive 8 (1541)" string

    // -----------------------------------------------------------------------
    // IEC line state we are driving (all released / high by default)
    // -----------------------------------------------------------------------
    IECLines driven_  = { true, true, true };
    IECLines hostIn_  = { true, true, true };  // last state received from host

    // -----------------------------------------------------------------------
    // Protocol state machine
    // -----------------------------------------------------------------------
    enum class State {
        Idle,
        // ATN sequence
        AtnWaitClkHigh,     // pulling DATA low, wait for host to release CLK
        AtnReceiveBit,      // sampling DATA on CLK falling edge
        AtnBitSettle,       // wait one cycle after acknowledging byte
        // Listener (host → drive)
        ListenWaitClkHigh,  // wait for host to release CLK (ready-to-send)
        ListenBitSettle,    // wait one cycle after DATA driven
        ListenReceiveBit,   // sample DATA on CLK falling edge
        ListenEOI,          // EOI handshake
        // Talker (drive → host)
        TalkStart,          // wait for C64 to pull CLK low
        TalkWaitClkHigh,    // wait for C64 to release CLK
        TalkEOI,            // signal EOI (hold CLK low)
        TalkNormalReady,    // hold CLK low briefly
        TalkWaitHostDataHigh, // wait for C64 to release DATA
        TalkSendBit,        // pull CLK low, set DATA
        TalkHoldBit,        // release CLK (C64 samples)
        TalkBitSettle,      // hold DATA briefly after CLK rises
    };

    State   state_     = State::Idle;
    bool    listening_ = false;   // this device is the active LISTENER
    bool    talking_   = false;   // this device is the active TALKER
    int     channel_   = -1;

    // Byte being shifted in/out
    uint8_t shiftReg_  = 0;
    int     bitCount_  = 0;

    // Previous line values (edge detection)
    bool prevClk_  = true;
    bool prevAtn_  = true;

    bool clkRoseFlag_ = false;

    // Transmit queue filled by command processing
    std::vector<uint8_t> txBuf_;
    size_t               txPos_ = 0;
    bool                 txEOI_ = false;  // next byte is the last

    // Receive accumulator (command / OPEN channel data)
    std::vector<uint8_t> rxBuf_;

    // Open channels: channel 0-15 are secondary addresses (15 is the command channel
    // used for OPEN/LOAD and TALK/DATA transfers).
    struct Channel {
        bool               open  = false;
        std::vector<uint8_t> data;
        size_t             pos   = 0;
    };
    Channel channels_[16];

    // Timing counters (in clock cycles)
    int  waitCycles_ = 0;
    int  eoiTimer_   = 0;
    bool talkClkHigh_= false;  // phase flag for TalkBitOut CLK pulse

    // Event log for debug panel
    struct LogEntry { std::string msg; };
    std::vector<LogEntry> log_;
    void logEvent(std::string msg) {
        log_.push_back({std::move(msg)});
        if (log_.size() > 64) log_.erase(log_.begin());
    }

    // -----------------------------------------------------------------------
    // Protocol helpers
    // -----------------------------------------------------------------------
    void handleAtnByte(uint8_t byte);
    void handleListenByte(uint8_t byte);
    void openChannel(int ch, const std::string& name);
    void loadChannelFromDisk(int ch);

    // Set all driven lines to released (high)
    void releaseAll() { driven_ = { true, true, true }; }
};
