#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// D64Image — parses a standard .d64 disk image (35 tracks, 683 sectors).
//
// Sector geometry follows the original 1541 layout:
//   Tracks  1–17 : 21 sectors/track
//   Tracks 18–24 : 19 sectors/track
//   Tracks 25–30 : 18 sectors/track
//   Tracks 31–35 : 17 sectors/track
//
// The directory is on track 18; sector 0 holds the BAM, sectors 1+ are dir.
// PRG files are stored as linked-sector chains; each 256-byte sector uses
// byte 0 as the next-track pointer, byte 1 as the next-sector (or byte count
// when track == 0).
// ---------------------------------------------------------------------------
class D64Image {
public:
    // A single directory entry (file name and starting t/s).
    struct DirEntry {
        std::string name;       // PETSCII converted to ASCII, padded spaces stripped
        uint8_t     type  = 0;  // file type byte ($82 = PRG, etc.)
        uint8_t     track = 0;
        uint8_t     sector= 0;
        uint16_t    blocks= 0;

        bool isPRG() const { return (type & 0x07) == 2; }
    };

    D64Image() = default;

    // Load image from file.  Returns false on error; call error() for details.
    bool load(const std::string& path);
    void unload();
    bool isLoaded() const { return !data_.empty(); }

    const std::string& path()  const { return path_;  }
    const std::string& error() const { return error_; }

    // Directory — populated after load().
    const std::vector<DirEntry>& directory() const { return dir_; }

    // Read a raw 256-byte sector into buf[256].  Returns false if t/s invalid.
    bool readSector(int track, int sector, uint8_t* buf) const;

    // Follow the linked-sector chain starting at (track, sector) and return
    // the full file data.  Returns empty vector on error.
    std::vector<uint8_t> readFile(int track, int sector) const;

    // Convenience: find first PRG matching name (PETSCII/ASCII, case-insensitive,
    // wildcards not yet supported) and return its data.  Returns empty on miss.
    std::vector<uint8_t> findPRG(const std::string& name) const;

    // First PRG in directory (used for LOAD"*",8,1).
    std::vector<uint8_t> firstPRG() const;

    // Disk name from BAM sector.
    std::string diskName() const;

    // Number of free blocks reported in the BAM.
    int freeBlocks() const;

    // Sectors per track for tracks 1-35.
    static int sectorsPerTrack(int track);

    // Byte offset of sector (track, sector) in the flat .d64 data.
    static int sectorOffset(int track, int sector);

private:
    std::string              path_;
    std::string              error_;
    std::vector<uint8_t>     data_;
    std::vector<DirEntry>    dir_;

    void parseDirectory();
    static std::string petsciiToAscii(const uint8_t* buf, int len);
};
