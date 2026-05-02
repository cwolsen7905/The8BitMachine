#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// T64Image — parses a .t64 tape container image.
//
// Format overview (all multi-byte values are little-endian):
//   Offset  0–63 : 64-byte header
//     [0–31]  magic string "C64 tape image file" + padding
//     [32–33] version (0x100 or 0x101)
//     [34–35] total file record slots
//     [36–37] records in use (unreliable; may be 0 even when files exist)
//     [40–63] PETSCII tape description (24 bytes, space-padded)
//   Offset 64+   : 32-byte file records, one per slot
//     [0]    entry type (0=free, 1=normal/PRG, others=skip)
//     [1]    CBM file type ($82 = PRG)
//     [2–3]  load address (start)
//     [4–5]  end address (exclusive)
//     [8–11] byte offset into T64 where file data begins
//     [16–31] PETSCII filename (16 bytes, space-padded)
//   File data : raw bytes; does NOT include the 2-byte load address
//
// getFile() / firstPRG() / findPRG() return [lo(loadAddr), hi(loadAddr)]
// followed by the raw file bytes, matching the layout that D64Image returns
// so that Drive1541::openChannel() can serve them identically.
// ---------------------------------------------------------------------------
class T64Image {
public:
    struct Entry {
        std::string name;           // PETSCII converted to ASCII, trailing spaces stripped
        uint8_t     cbmType   = 0;  // CBM file type byte ($82 = PRG)
        uint16_t    startAddr = 0;  // load (start) address
        uint16_t    endAddr   = 0;  // exclusive end address
        uint32_t    dataOffset= 0;  // byte offset of file data inside the T64

        bool isPRG() const { return (cbmType & 0x07) == 2; }
        uint32_t dataSize() const {
            return (endAddr > startAddr) ? (endAddr - startAddr) : 0;
        }
    };

    T64Image() = default;

    bool load(const std::string& path);
    void unload();
    bool isLoaded() const { return !data_.empty(); }

    const std::string& path()  const { return path_;  }
    const std::string& error() const { return error_; }

    // Tape description from header (ASCII).
    std::string tapeName() const;

    // All parsed entries (includes only non-free records).
    const std::vector<Entry>& entries() const { return entries_; }

    // Return file data for entry at index as [lo, hi, bytes...].
    // Returns empty vector on out-of-range or zero-size entry.
    std::vector<uint8_t> getFile(int index) const;

    // First PRG entry.  Returns empty on miss.
    std::vector<uint8_t> firstPRG() const;

    // Find PRG by name (case-insensitive, PETSCII/ASCII).  Returns empty on miss.
    std::vector<uint8_t> findPRG(const std::string& name) const;

private:
    std::string           path_;
    std::string           error_;
    std::vector<uint8_t>  data_;
    std::vector<Entry>    entries_;

    void parseEntries(int numSlots);
    static std::string petsciiToAscii(const uint8_t* buf, int len);
};
