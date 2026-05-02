#include "T64Image.h"
#include <algorithm>
#include <cctype>
#include <fstream>

// ---------------------------------------------------------------------------
// PETSCII → ASCII (same mapping as D64Image)
// ---------------------------------------------------------------------------

std::string T64Image::petsciiToAscii(const uint8_t* buf, int len) {
    std::string s;
    for (int i = 0; i < len; ++i) {
        uint8_t c = buf[i];
        if (c == 0xA0 || c == 0x00) break;  // padding sentinel
        if (c >= 0x41 && c <= 0x5A) c = c - 0x41 + 'a';
        else if (c >= 0x61 && c <= 0x7A) c = c - 0x61 + 'A';
        s += static_cast<char>(c);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// Load / unload
// ---------------------------------------------------------------------------

bool T64Image::load(const std::string& path) {
    unload();
    path_ = path;

    std::ifstream f(path, std::ios::binary);
    if (!f) { error_ = "Cannot open: " + path; return false; }

    f.seekg(0, std::ios::end);
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);

    if (sz < 64) {
        error_ = "File too small to be a .t64 image";
        return false;
    }

    data_.resize(sz);
    f.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(sz));

    // Read num_entries from header [34–35] (LE).
    int numSlots = static_cast<int>(data_[34]) | (static_cast<int>(data_[35]) << 8);
    if (numSlots == 0) {
        // num_entries is unreliable; infer from how many 32-byte records fit before
        // the first data offset (or cap at a reasonable maximum).
        numSlots = static_cast<int>((sz - 64) / 32);
        if (numSlots > 1024) numSlots = 1024;
    }

    parseEntries(numSlots);

    if (entries_.empty()) {
        error_ = "No file records found in .t64 image";
        return false;
    }
    return true;
}

void T64Image::unload() {
    data_.clear();
    entries_.clear();
    path_.clear();
    error_.clear();
}

// ---------------------------------------------------------------------------
// Header accessors
// ---------------------------------------------------------------------------

std::string T64Image::tapeName() const {
    if (data_.size() < 64) return {};
    return petsciiToAscii(data_.data() + 40, 24);
}

// ---------------------------------------------------------------------------
// Parse file record table
// ---------------------------------------------------------------------------

void T64Image::parseEntries(int numSlots) {
    entries_.clear();
    for (int i = 0; i < numSlots; ++i) {
        size_t recOff = 64 + static_cast<size_t>(i) * 32;
        if (recOff + 32 > data_.size()) break;

        const uint8_t* rec = data_.data() + recOff;
        uint8_t entryType = rec[0];
        if (entryType == 0) continue;  // free slot
        // Only T64_FILE_RECORD_NORMAL (1) contains loadable PRG data.
        // Skip snapshot / stream records; they don't map to a simple PRG load.
        if (entryType != 1) continue;

        Entry e;
        e.cbmType   = rec[1];
        e.startAddr = static_cast<uint16_t>(rec[2] | (rec[3] << 8));
        e.endAddr   = static_cast<uint16_t>(rec[4] | (rec[5] << 8));
        e.dataOffset= static_cast<uint32_t>(rec[8] | (rec[9]<<8) | (rec[10]<<16) | (rec[11]<<24));
        e.name      = petsciiToAscii(rec + 16, 16);
        entries_.push_back(e);
    }
}

// ---------------------------------------------------------------------------
// File data retrieval
// ---------------------------------------------------------------------------

std::vector<uint8_t> T64Image::getFile(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return {};
    const Entry& e = entries_[index];
    uint32_t size = e.dataSize();
    if (size == 0) return {};
    if (e.dataOffset + size > data_.size()) return {};

    // Prepend the 2-byte load address so callers get a standard PRG layout.
    std::vector<uint8_t> out;
    out.reserve(2 + size);
    out.push_back(static_cast<uint8_t>(e.startAddr & 0xFF));
    out.push_back(static_cast<uint8_t>(e.startAddr >> 8));
    out.insert(out.end(),
               data_.begin() + e.dataOffset,
               data_.begin() + e.dataOffset + size);
    return out;
}

std::vector<uint8_t> T64Image::firstPRG() const {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (entries_[i].isPRG())
            return getFile(i);
    }
    return {};
}

std::vector<uint8_t> T64Image::findPRG(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (!entries_[i].isPRG()) continue;
        std::string entLower = entries_[i].name;
        std::transform(entLower.begin(), entLower.end(), entLower.begin(), ::tolower);
        if (entLower == lower)
            return getFile(i);
    }
    return {};
}
