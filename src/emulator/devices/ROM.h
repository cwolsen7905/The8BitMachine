#pragma once

#include "emulator/core/IBusDevice.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ROM — read-only memory region loaded from a file.
//
// Writes are silently ignored.  Reads beyond the loaded data return $FF.
// .prg files (Commodore load-address format) have their 2-byte header
// stripped automatically.
// ---------------------------------------------------------------------------
class ROM : public IBusDevice {
public:
    explicit ROM(std::string label = "ROM");

    // Load from file.  Returns false if the file cannot be opened.
    // .prg files: the 2-byte load-address header is stripped; the raw
    // program bytes are stored.  skipBytes skips additional bytes before
    // the data (used e.g. to load only the upper half of a 32 KB ROM image).
    bool loadFromFile(const std::string& path, size_t skipBytes = 0);

    const std::string& filePath() const { return filePath_; }
    size_t             dataSize() const { return data_.size(); }

    void setWritable(bool w) { writable_ = w; }
    bool isWritable()  const { return writable_; }

    // IBusDevice
    const char* deviceName() const override { return "ROM"; }
    void        reset()      override {}
    uint8_t     read (uint16_t offset)          const override;
    void        write(uint16_t offset, uint8_t value) override;
    std::string statusLine()                    const override;

private:
    std::string          label_;
    std::string          filePath_;
    std::vector<uint8_t> data_;
    bool                 writable_ = false;
};
