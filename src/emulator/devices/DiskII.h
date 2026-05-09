#pragma once
#include "emulator/core/IBusDevice.h"
#include "emulator/core/IHasPanel.h"
#include "emulator/core/IPeripheral.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DiskII — Apple Disk II controller (slot 6)
//
// Mapped at $C0E0-$C0EF (16 soft switches for slot 6).
// The slot boot ROM at $C600-$C6FF is already provided by the Apple IIe ROM
// image the user supplies, so no separate ROM device is needed.
//
// Supports .dsk / .do (DOS 3.3 ordered, 140 KB) disk images.
// Nibble tracks are pre-encoded from the disk image on mount using the
// standard 6-and-2 GCR scheme so that the real Apple II RWTS in ROM works
// without modification.
// ---------------------------------------------------------------------------
class DiskII : public IBusDevice, public IPeripheral, public IHasPanel {
public:
    // IBusDevice — mapped at $C0E0-$C0EF
    const char* deviceName() const override { return "Disk II"; }
    void    reset()                              override;
    uint8_t read (uint16_t offset) const         override;
    void    write(uint16_t offset, uint8_t val)  override;

    // IPeripheral
    const char*        peripheralName()  const override { return "Disk II"; }
    const std::string& mountedImage()    const override { return imagePath_; }
    bool               mount(const std::string& path) override;
    void               eject()                        override;
    const std::string& mountError()      const override { return mountError_; }

    // IHasPanel
    void drawPanel(const char* title, bool* open) override;

private:
    // -----------------------------------------------------------------------
    // Stepper motor and head position (all mutable — read() has side effects)
    // -----------------------------------------------------------------------
    mutable uint8_t phases_    = 0;   // active phase bits (bits 0-3)
    mutable int     halfTrack_ = 0;   // 0-69 (track = halfTrack_/2)
    mutable int     lastPhase_ = -1;  // last phase that turned on

    // -----------------------------------------------------------------------
    // Controller state
    // -----------------------------------------------------------------------
    mutable bool motorOn_  = false;
    mutable bool q6_       = false;   // Q6 latch
    mutable bool q7_       = false;   // Q7 latch (false=read, true=write)

    // -----------------------------------------------------------------------
    // Nibble stream
    // -----------------------------------------------------------------------
    static constexpr int kTracks = 35;
    std::array<std::vector<uint8_t>, kTracks> nibs_;
    mutable int  nibPos_ = 0;
    bool         loaded_ = false;

    // -----------------------------------------------------------------------
    // Media
    // -----------------------------------------------------------------------
    std::string imagePath_;
    std::string mountError_;

    // -----------------------------------------------------------------------
    // Encoding
    // -----------------------------------------------------------------------
    void buildNibTracks(const std::vector<uint8_t>& dsk);
    void encodeTrack(int trackNum, const uint8_t* sectors16);
    void stepMotor(int phase) const;
    void activateSwitch(uint16_t offset) const;

    // 6-and-2 GCR encode table (62 valid nibble values for 6-bit inputs 0-61)
    static constexpr uint8_t kGCR62[64] = {
        0x96,0x97,0x9A,0x9B, 0x9D,0x9E,0x9F,0xA6,
        0xA7,0xAB,0xAC,0xAD, 0xAE,0xAF,0xB2,0xB3,
        0xB4,0xB5,0xB6,0xB7, 0xB9,0xBA,0xBB,0xBC,
        0xBD,0xBE,0xBF,0xCB, 0xCF,0xD3,0xD6,0xD7,
        0xD9,0xDA,0xDB,0xDC, 0xDD,0xDE,0xDF,0xE5,
        0xE6,0xE7,0xE9,0xEA, 0xEB,0xEC,0xED,0xEE,
        0xEF,0xF2,0xF3,0xF4, 0xF5,0xF6,0xF7,0xF9,
        0xFA,0xFB,0xFC,0xFD, 0xFE,0xFF,0xFF,0xFF
    };

    // DOS 3.3 physical-sector → logical-sector interleave
    static constexpr uint8_t kInterleave[16] = {
        0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15
    };
};
