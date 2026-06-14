#pragma once

#include <cctype>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Shared Commodore text helpers used by the disk/tape image parsers
// (D64Image, T64Image).  Header-only so no extra translation unit is needed.
// ---------------------------------------------------------------------------
namespace cbm {

// CBM wildcard match: '?' = any one char, '*' = match rest of name.
inline bool match(const std::string& pattern, const std::string& name) {
    size_t pi = 0, ni = 0;
    while (pi < pattern.size()) {
        if (pattern[pi] == '*') return true;
        if (ni >= name.size()) return false;
        char p = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[pi])));
        char n = static_cast<char>(std::tolower(static_cast<unsigned char>(name[ni])));
        if (p != '?' && p != n) return false;
        ++pi; ++ni;
    }
    return ni == name.size();
}

// PETSCII → ASCII for directory/file names.  Stops at the 0xA0 shift-space
// padding sentinel (and at a NUL), swaps the PETSCII upper/lower case ranges,
// and strips trailing spaces.
inline std::string petsciiToAscii(const uint8_t* buf, int len) {
    std::string s;
    for (int i = 0; i < len; ++i) {
        uint8_t c = buf[i];
        if (c == 0xA0 || c == 0x00) break;             // padding sentinel
        if (c >= 0x41 && c <= 0x5A) c = c - 0x41 + 'a';
        else if (c >= 0x61 && c <= 0x7A) c = c - 0x61 + 'A';
        s += static_cast<char>(c);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace cbm
