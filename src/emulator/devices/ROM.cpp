#include "emulator/devices/ROM.h"
#include <fstream>
#include <cstdio>
#include <algorithm>

ROM::ROM(std::string label) : label_(std::move(label)) {}

bool ROM::loadFromFile(const std::string& path, size_t skipBytes) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;

    auto fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);

    // Strip the 2-byte Commodore PRG load-address header for .prg files
    size_t skip = skipBytes;
    if (fileSize >= 2) {
        auto ext = path.size() >= 4 ? path.substr(path.size() - 4) : "";
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".prg") skip += 2;
    }

    if (skip >= fileSize) return false;
    f.seekg(static_cast<std::streamoff>(skip));
    size_t dataSize = fileSize - skip;
    data_.resize(dataSize);
    f.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(dataSize));

    filePath_ = path;
    return true;
}

void ROM::write(uint16_t offset, uint8_t value) {
    if (writable_ && offset < static_cast<uint16_t>(data_.size()))
        data_[offset] = value;
}

uint8_t ROM::read(uint16_t offset) const {
    if (offset < static_cast<uint16_t>(data_.size()))
        return data_[offset];
    return 0xFF;
}

std::string ROM::statusLine() const {
    if (data_.empty()) return "empty";
    auto slash = filePath_.find_last_of("/\\");
    std::string fname = (slash != std::string::npos)
        ? filePath_.substr(slash + 1) : filePath_;
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%zu bytes — %s",
                  data_.size(), fname.c_str());
    return buf;
}
