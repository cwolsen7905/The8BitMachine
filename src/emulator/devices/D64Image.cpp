#include "D64Image.h"
#include <algorithm>
#include <cctype>
#include <fstream>

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

int D64Image::sectorsPerTrack(int track) {
    if (track >= 1  && track <= 17) return 21;
    if (track >= 18 && track <= 24) return 19;
    if (track >= 25 && track <= 30) return 18;
    if (track >= 31 && track <= 35) return 17;
    return 0;
}

int D64Image::sectorOffset(int track, int sector) {
    // Sum all sectors on preceding tracks (1-indexed).
    int off = 0;
    for (int t = 1; t < track; ++t)
        off += sectorsPerTrack(t);
    off += sector;
    return off * 256;
}

// ---------------------------------------------------------------------------
// Load / unload
// ---------------------------------------------------------------------------

bool D64Image::load(const std::string& path) {
    unload();
    path_ = path;

    std::ifstream f(path, std::ios::binary);
    if (!f) { error_ = "Cannot open: " + path; return false; }

    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    f.seekg(0);

    // Standard D64: 174848 bytes (35 tracks, no error bytes).
    // Extended: 175531 bytes (+ 683 error bytes).  We accept both.
    if (sz < 174848) {
        error_ = "File too small to be a .d64 image";
        return false;
    }

    data_.resize(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data_.data()), sz);

    parseDirectory();
    return true;
}

void D64Image::unload() {
    data_.clear();
    dir_.clear();
    path_.clear();
    error_.clear();
}

// ---------------------------------------------------------------------------
// Sector access
// ---------------------------------------------------------------------------

bool D64Image::readSector(int track, int sector, uint8_t* buf) const {
    if (track < 1 || track > 35) return false;
    if (sector < 0 || sector >= sectorsPerTrack(track)) return false;
    int off = sectorOffset(track, sector);
    if (off + 256 > static_cast<int>(data_.size())) return false;
    std::copy(data_.begin() + off, data_.begin() + off + 256, buf);
    return true;
}

std::vector<uint8_t> D64Image::readFile(int track, int sector) const {
    std::vector<uint8_t> out;
    int visited = 0;
    uint8_t buf[256];

    while (track != 0 && visited < 683) {
        if (!readSector(track, sector, buf)) break;
        int nextTrack  = buf[0];
        int nextSector = buf[1];
        if (nextTrack == 0) {
            // Last sector: nextSector holds the number of valid data bytes (1-based).
            int count = nextSector - 1;
            out.insert(out.end(), buf + 2, buf + 2 + count);
        } else {
            out.insert(out.end(), buf + 2, buf + 256);
        }
        track  = nextTrack;
        sector = nextSector;
        ++visited;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Directory
// ---------------------------------------------------------------------------

std::string D64Image::petsciiToAscii(const uint8_t* buf, int len) {
    std::string s;
    for (int i = 0; i < len; ++i) {
        uint8_t c = buf[i];
        if (c == 0xA0) break;  // PETSCII shift-space = padding → stop
        // PETSCII upper-case letters are $41-$5A; map to ASCII lower
        if (c >= 0x41 && c <= 0x5A) c = c - 0x41 + 'a';
        else if (c >= 0x61 && c <= 0x7A) c = c - 0x61 + 'A';
        s += static_cast<char>(c);
    }
    // Strip trailing spaces
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void D64Image::parseDirectory() {
    dir_.clear();
    uint8_t buf[256];

    // Directory starts at track 18, sector 1 (sector 0 is the BAM).
    int t = 18, s = 1;
    while (t != 0) {
        if (!readSector(t, s, buf)) break;
        // Each sector contains 8 directory entries of 32 bytes each.
        for (int e = 0; e < 8; ++e) {
            const uint8_t* entry = buf + e * 32;
            uint8_t ftype = entry[2];
            if (ftype == 0x00) continue;  // scratch/deleted

            DirEntry de;
            de.type   = ftype;
            de.track  = entry[3];
            de.sector = entry[4];
            de.name   = petsciiToAscii(entry + 5, 16);
            de.blocks = static_cast<uint16_t>(entry[30] | (entry[31] << 8));
            dir_.push_back(de);
        }
        t = buf[0];
        s = buf[1];
    }
}

// ---------------------------------------------------------------------------
// Convenience accessors
// ---------------------------------------------------------------------------

std::vector<uint8_t> D64Image::findPRG(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& de : dir_) {
        if (!de.isPRG()) continue;
        std::string entLower = de.name;
        std::transform(entLower.begin(), entLower.end(), entLower.begin(), ::tolower);
        if (entLower == lower)
            return readFile(de.track, de.sector);
    }
    return {};
}

std::vector<uint8_t> D64Image::firstPRG() const {
    for (const auto& de : dir_) {
        if (de.isPRG())
            return readFile(de.track, de.sector);
    }
    return {};
}

std::string D64Image::diskName() const {
    if (data_.empty()) return {};
    uint8_t buf[256];
    if (!readSector(18, 0, buf)) return {};
    return petsciiToAscii(buf + 0x90, 16);
}

int D64Image::freeBlocks() const {
    if (data_.empty()) return 0;
    uint8_t buf[256];
    if (!readSector(18, 0, buf)) return 0;
    int free = 0;
    for (int t = 1; t <= 35; ++t) {
        if (t == 18) continue;  // directory track
        free += buf[t * 4];     // BAM entry byte 0 = free sector count
    }
    return free;
}
