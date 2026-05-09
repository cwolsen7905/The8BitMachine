#include "Drive1541.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>

static std::string normalizeFileName(std::string name) {
    auto trim = [&](std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    };
    trim(name);
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
        name = name.substr(1, name.size() - 2);
        trim(name);
    }
    // Strip CBM DOS drive prefix: "0:" .. "9:"
    if (name.size() >= 2 && std::isdigit(static_cast<unsigned char>(name[0])) && name[1] == ':')
        name = name.substr(2);
    // Strip CBM DOS type/mode suffix: ",P,R" etc. — anything after the first comma
    auto comma = name.find(',');
    if (comma != std::string::npos)
        name = name.substr(0, comma);
    trim(name);
    return name;
}

// ---------------------------------------------------------------------------
// CBM serial secondary-address command nibbles (upper nibble of SA byte)
// ---------------------------------------------------------------------------
static constexpr uint8_t SA_DATA  = 0x60;  // 0x60–0x6F: data channel
static constexpr uint8_t SA_CLOSE = 0xE0;  // 0xE0–0xEF: close channel
static constexpr uint8_t SA_OPEN  = 0xF0;  // 0xF0–0xFF: open channel

// CLK hold time (cycles) before releasing for non-EOI byte.  Must be <200µs.
static constexpr int kNormalReadyCycles = 50;


// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Drive1541::Drive1541(int deviceNumber)
    : deviceNumber_(deviceNumber)
{
    peripheralName_ = "Drive " + std::to_string(deviceNumber_) + " (1541)";
}

// ---------------------------------------------------------------------------
// IPeripheral
// ---------------------------------------------------------------------------

const char* Drive1541::peripheralName() const {
    return peripheralName_.c_str();
}

static bool hasExtension(const std::string& path, const char* ext) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pos = lower.rfind('.');
    return pos != std::string::npos && lower.substr(pos) == ext;
}

bool Drive1541::mount(const std::string& path) {
    mountError_.clear();
    if (hasExtension(path, ".t64")) {
        image_.unload();
        if (!t64_.load(path)) { mountError_ = t64_.error(); return false; }
    } else {
        t64_.unload();
        if (!image_.load(path)) { mountError_ = image_.error(); return false; }
    }
    return true;
}

void Drive1541::eject() {
    image_.unload();
    t64_.unload();
    mountError_.clear();
    reset();
}

std::vector<uint8_t> Drive1541::loadFile(const std::string& name) {
    if (t64_.isLoaded()) {
        if (name == "*" || name.empty()) return t64_.firstPRG();
        auto data = t64_.findPRG(name);
        return data.empty() ? t64_.firstPRG() : data;
    }
    if (image_.isLoaded()) {
        if (name == "*" || name.empty()) return image_.firstPRG();
        auto data = image_.findPRG(name);
        return data.empty() ? image_.firstPRG() : data;
    }
    return {};
}

void Drive1541::reset() {
    for (auto& ch : channels_) ch = Channel{};
    txBuf_.clear(); txPos_ = 0;
    rxBuf_.clear();
    state_         = State::Idle;
    listening_     = false;
    talking_       = false;
    channel_       = -1;
    shiftReg_      = 0;
    bitCount_      = 0;
    waitCycles_    = 0;
    eoiTimer_      = 0;
    talkEoiCycles_ = 0;
    txEOI_         = false;
    clkRoseFlag_   = false;
    prevClk_       = true;
    prevAtn_       = true;
    hostIn_        = { true, true, true };
    releaseAll();
    // Channel 15 is the 1541 error/status channel — always open, pre-filled with
    // the DOS version string that a real drive returns after power-on or reset.
    static const char kDosVersion[] = "73,CBM DOS V2.6 1541,00,00\r";
    channels_[15].data.assign(kDosVersion, kDosVersion + sizeof(kDosVersion) - 1);
    channels_[15].pos  = 0;
    channels_[15].open = true;
    logEvent("IEC reset");
}

std::vector<uint8_t> Drive1541::getDirectoryData() {
    // Build a BASIC program representing the disk directory.
    // Load address = $0801; link pointers are absolute from $0801.
    // Format mirrors what a real 1541 sends over IEC.

    struct BasicLine {
        uint16_t lineNum;
        std::vector<uint8_t> content;
    };
    std::vector<BasicLine> lines;

    auto appendStr = [](std::vector<uint8_t>& v, const char* s) {
        while (*s) v.push_back(static_cast<uint8_t>(*s++));
    };
    auto toUpper = [](char c) -> uint8_t {
        return (c >= 'a' && c <= 'z') ? static_cast<uint8_t>(c - 'a' + 'A')
                                      : static_cast<uint8_t>(c);
    };
    auto addNameField = [&](std::vector<uint8_t>& v, const std::string& name) {
        v.push_back('"');
        int written = 0;
        for (char c : name) { v.push_back(toUpper(c)); ++written; }
        for (; written < 16; ++written) v.push_back(' ');
        v.push_back('"');
    };

    if (image_.isLoaded()) {
        BasicLine hdr;
        hdr.lineNum = 0;
        hdr.content.push_back(0x12);  // reverse-on
        addNameField(hdr.content, image_.diskName());
        appendStr(hdr.content, " 00 2A");
        lines.push_back(std::move(hdr));

        for (const auto& de : image_.directory()) {
            BasicLine fl;
            fl.lineNum = de.blocks;
            addNameField(fl.content, de.name);
            fl.content.push_back(' ');
            appendStr(fl.content, de.isPRG() ? "PRG" : "SEQ");
            lines.push_back(std::move(fl));
        }

        BasicLine ftr;
        ftr.lineNum = static_cast<uint16_t>(image_.freeBlocks());
        appendStr(ftr.content, "BLOCKS FREE.");
        lines.push_back(std::move(ftr));

    } else if (t64_.isLoaded()) {
        BasicLine hdr;
        hdr.lineNum = 0;
        hdr.content.push_back(0x12);
        addNameField(hdr.content, t64_.tapeName());
        appendStr(hdr.content, " T64  ");
        lines.push_back(std::move(hdr));

        for (const auto& e : t64_.entries()) {
            if (!e.isPRG()) continue;
            BasicLine fl;
            fl.lineNum = 0;
            addNameField(fl.content, e.name);
            appendStr(fl.content, " PRG");
            lines.push_back(std::move(fl));
        }

        BasicLine ftr;
        ftr.lineNum = 0;
        appendStr(ftr.content, "BLOCKS FREE.");
        lines.push_back(std::move(ftr));
    }

    if (lines.empty()) return {};

    // Serialize: [load addr][BASIC lines...][0x00 0x00]
    const uint16_t base = 0x0801;
    std::vector<uint8_t> out;
    out.push_back(base & 0xFF);
    out.push_back(base >> 8);

    uint16_t addr = base;
    for (const auto& line : lines) {
        uint16_t lineSize = static_cast<uint16_t>(2 + 2 + line.content.size() + 1);
        uint16_t next = addr + lineSize;
        out.push_back(next & 0xFF);
        out.push_back(next >> 8);
        out.push_back(line.lineNum & 0xFF);
        out.push_back(line.lineNum >> 8);
        for (uint8_t b : line.content) out.push_back(b);
        out.push_back(0x00);
        addr = next;
    }
    out.push_back(0x00);
    out.push_back(0x00);
    return out;
}

// ---------------------------------------------------------------------------
// IIECDevice — line update from CIA2
//
// Design: ALL state transitions happen on ATN edges here.
// handleAtnByte() only sets flags; it never changes state_.
// ---------------------------------------------------------------------------

void Drive1541::setIECLines(IECLines host) {
    IECLines prevHost = hostIn_;
    const bool atnFell = (!host.atn && prevHost.atn);
    const bool atnRose = ( host.atn && !prevHost.atn);

    hostIn_  = host;
    prevAtn_ = host.atn;
    if (host.clk && !prevClk_) clkRoseFlag_ = true;
    prevClk_ = host.clk;

    // Log raw IEC line transitions — suppress during bit-transfer states to avoid flooding.
    if (host.atn != prevHost.atn || host.clk != prevHost.clk || host.data != prevHost.data) {
        bool isBitTransfer = (state_ == State::TalkSendBit        ||
                              state_ == State::TalkHoldBit        ||
                              state_ == State::TalkBitSettle      ||
                              state_ == State::TalkByteACK        ||
                              state_ == State::TalkNormalReady    ||
                              state_ == State::TalkWaitClkHigh    ||
                              state_ == State::TalkWaitHostDataHigh ||
                              state_ == State::TalkEOI            ||
                              state_ == State::AtnReceiveBit      ||
                              state_ == State::ListenReceiveBit   ||
                              state_ == State::ListenBitSettle);
        if (!isBitTransfer || host.atn != prevHost.atn) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                "IEC lines %d%d%d -> %d%d%d  state=%d",
                prevHost.atn ? 1 : 0, prevHost.clk ? 1 : 0, prevHost.data ? 1 : 0,
                host.atn ? 1 : 0, host.clk ? 1 : 0, host.data ? 1 : 0,
                (int)state_);
            logEvent(buf);
        }
    }

    if (atnFell) {
        state_ = State::AtnWaitClkLow;
        bitCount_ = 0;
        shiftReg_ = 0;
        clkRoseFlag_ = false; // discard any CLK rise that occurred before ATN fell
        driven_.data = false; // Acknowledge ATN (hold DATA low)
        logEvent("ATN fell → DATA asserted");
    }

    if (atnRose) {
        logEvent("ATN rose → listen=" + std::to_string(listening_) +
                 " talk=" + std::to_string(talking_) + " ch=" + std::to_string(channel_) +
                 " bits=" + std::to_string(bitCount_) + " sr=$" +
                 [&]{ char b[4]; snprintf(b,4,"%02X",shiftReg_); return std::string(b); }());

        if (talking_) {
            driven_.data = true;  // release DATA so bus is free for talker role
            if (channel_ >= 0 && channel_ < 16 && channels_[channel_].open) {
                auto& c = channels_[channel_];
                txBuf_    = std::vector<uint8_t>(c.data.begin() + c.pos, c.data.end());
                txPos_    = 0;
                bitCount_ = 0;
                state_    = State::TalkStart;
                logEvent("TalkStart " + std::to_string(txBuf_.size()) + " bytes");
            } else {
                logEvent("Talk: ch " + std::to_string(channel_) + " not open → Idle");
                talking_ = false;
                state_   = State::Idle;
            }
        } else if (listening_) {
            // Keep DATA LOW — the KERNAL verifies device presence by releasing DATA and
            // checking the bus still reads LOW (i.e., the device is holding it).
            // ListenWaitClkHigh will release DATA once CLK goes HIGH (talker's ready signal).
            logEvent("→ ListenWaitClkHigh ch=" + std::to_string(channel_));
            state_ = State::ListenWaitClkHigh;
        } else {
            driven_.data = true;  // not addressed — release lines, go idle
            state_ = State::Idle;
        }
    }
}

// ---------------------------------------------------------------------------
// clock() — bit-banging state machine
// ---------------------------------------------------------------------------

void Drive1541::clock() {
    if (waitCycles_ > 0) { --waitCycles_; return; }

    const bool clk  = hostIn_.clk;
    const bool data = hostIn_.data;

    switch (state_) {

    // ------------------------------------------------------------------
    case State::Idle:
        releaseAll();
        break;

    // ------------------------------------------------------------------
    // ATN command bytes — host is talker, we listen and decode.
    // Data is valid on CLK rising edge: talker sets DATA, pulses CLK low, releases CLK.
    // ------------------------------------------------------------------
    case State::AtnWaitClkLow:
        // Hold DATA low (ATN ack). KERNAL may assert ATN before asserting CLK;
        // wait here until CLK goes low so we don't release DATA on a stale CLK-high.
        if (!clk) state_ = State::AtnWaitClkHigh;
        break;

    case State::AtnWaitClkHigh:
        // Wait for a NEW CLK rising edge (clkRoseFlag_), not just "CLK is currently high".
        // Using clk directly would fire on the bit7 sample CLK that's still high while we
        // transition through AtnBitSettle — causing a premature return to AtnReceiveBit.
        if (clkRoseFlag_) {
            clkRoseFlag_ = false; // discard this rise — it's the "ready" signal, not bit0
            driven_.data = true;  // release DATA, ready to receive
            state_ = State::AtnReceiveBit;
            bitCount_ = 0;
            shiftReg_ = 0;
        }
        break;

    case State::AtnReceiveBit: {
        if (clkRoseFlag_) {
            clkRoseFlag_ = false;
            uint8_t bit = data ? 1u : 0u;  // DATA HIGH = bit 1, DATA LOW = bit 0
            shiftReg_ |= bit << bitCount_;
            char b[48];
            snprintf(b, sizeof(b), "[CLKr] bit%d DATA=%d sr=$%02X", bitCount_, (int)data, shiftReg_);
            logEvent(b);
            ++bitCount_;
            if (bitCount_ == 8) {
                driven_.data = false; // Acknowledge byte
                handleAtnByte(shiftReg_);
                shiftReg_ = 0;
                bitCount_ = 0;
                state_ = State::AtnBitSettle;
            }
        }
        break;
    }

    case State::AtnBitSettle:
        driven_.data = false;
        state_ = State::AtnWaitClkHigh;
        break;

    // ------------------------------------------------------------------
    // Listener — receive data bytes from host after ATN is released.
    // ------------------------------------------------------------------
    case State::ListenWaitClkHigh:
        // Same edge-detection rationale as AtnWaitClkHigh: require a new rising edge.
        if (clkRoseFlag_) {
            clkRoseFlag_ = false; // discard this "ready" CLK rise; next rise carries bit0
            driven_.data = true;  // release DATA (HIGH) = ready to receive
            state_    = State::ListenReceiveBit;
            bitCount_ = 0;
            shiftReg_ = 0;
            eoiTimer_ = 0;
        }
        break;

    case State::ListenReceiveBit: {
        // EOI detection: the talker holds CLK HIGH for >200 cycles before sending the
        // last byte.  DATA state is irrelevant — the trigger is CLK staying high.
        if (clk) {
            if (eoiTimer_ < 255) eoiTimer_++;
            if (eoiTimer_ == 200) {
                logEvent("EOI ack (DATA low)");
                driven_.data = false;  // acknowledge EOI — C64 $ED55-$ED58 detects DATA LOW
            } else if (eoiTimer_ == 220) {
                logEvent("EOI ack released");
                driven_.data = true;   // release DATA HIGH — C64 $ED5A-$ED5D exits, proceeds to send bits
            }
        } else {
            // CLK went low (talker starting a bit).  If we just acked EOI, release
            // DATA so the bus is clear for bit sampling.
            if (eoiTimer_ >= 200) {
                driven_.data = true;
            }
            eoiTimer_ = 0;
        }

        if (clkRoseFlag_) {
            clkRoseFlag_ = false;
            driven_.data = true; // Ensure DATA is released to read it
            eoiTimer_ = 0;
            shiftReg_ |= (data ? 1u : 0u) << bitCount_;  // DATA HIGH = bit 1, DATA LOW = bit 0
            {
                char b[48];
                snprintf(b, sizeof(b), "[LRx] bit%d DATA=%d sr=$%02X", bitCount_, (int)data, shiftReg_);
                logEvent(b);
            }
            ++bitCount_;
            if (bitCount_ == 8) {
                driven_.data = false;  // pull DATA low = byte acknowledged
                handleListenByte(shiftReg_);
                shiftReg_ = 0;
                bitCount_ = 0;
                state_    = State::ListenBitSettle;
            }
        }
        break;
    }

    case State::ListenBitSettle:
        // Hold DATA low for one full cycle so the host sees the byte ack.
        driven_.data = false;
        state_        = State::ListenWaitClkHigh;
        break;

    // ------------------------------------------------------------------
    // Talker — send data bytes to host.
    // ------------------------------------------------------------------
    case State::TalkStart:
        // Talker Step 0: drive holds CLK LOW to signal "I am the new talker".
        // TKSA's BMI loop (which loops while CLK HIGH) exits when it sees CLK LOW.
        // Stay here until CIA releases its CLK (TKSA will do this after ATN).
        driven_.clk = false;
        driven_.data = true;
        if (txPos_ >= txBuf_.size()) {
            state_ = State::Idle;
            talking_ = false;
        } else if (hostIn_.clk) {
            // CIA released CLK; hand off to TalkWaitClkHigh which drives the byte.
            state_ = State::TalkWaitClkHigh;
        }
        break;

    case State::TalkWaitClkHigh:
        // Wait for C64 to release CLK (ACPTR $EE18); then signal "ready-to-send".
        // Entered after role reversal (TalkStart) or after a frame ACK (TalkByteACK).
        // txPos_ always points to the next byte to send here.
        driven_.clk = true;
        driven_.data = true;
        if (clk) {
            shiftReg_ = txBuf_[txPos_];
            bitCount_ = 0;
            txEOI_ = (txPos_ == txBuf_.size() - 1);
            // Both EOI and normal bytes: hold CLK LOW for kNormalReadyCycles so the
            // KERNAL has time to release DATA (it holds DATA while confirming the device
            // took CLK).  TalkNormalReady releases CLK HIGH ("ready"), then
            // TalkWaitHostDataHigh waits for DATA=1, then routes to TalkEOI or TalkSendBit.
            talkEoiCycles_ = 0;
            driven_.clk = false;
            waitCycles_ = kNormalReadyCycles;
            state_ = State::TalkNormalReady;
        }
        break;

    case State::TalkEOI:
        // CLK stays HIGH here (set in TalkWaitClkHigh, unchanged).
        // KERNAL's CIA1 Timer B (511 cycles) fires while CLK is HIGH; KERNAL then
        // acks EOI by pulling DATA LOW briefly.  Watch for that pulse.
        driven_.clk = true;
        driven_.data = true;
        if (++talkEoiCycles_ % 5000 == 0) {
            char b[64];
            snprintf(b, sizeof(b), "TalkEOI: %d cycles data=%d", talkEoiCycles_, (int)data);
            logEvent(b);
        }
        if (!data) {
            char b[64];
            snprintf(b, sizeof(b), "EOI ack from C64 after %d cycles", talkEoiCycles_);
            logEvent(b);
            talkEoiCycles_ = 0;
            txEOI_ = false;
            state_ = State::TalkWaitHostDataHigh;
        }
        break;

    case State::TalkNormalReady:
        // CLK was pulled low in TalkWaitClkHigh and held for kNormalReadyCycles via
        // the waitCycles_ countdown.  Now release CLK and wait for DATA to go high.
        driven_.clk = true;
        state_ = State::TalkWaitHostDataHigh;
        break;

    case State::TalkWaitHostDataHigh:
        // Wait for C64 to release DATA.
        // txEOI_=true  → byte-ACK just cleared; go hold CLK HIGH for EOI detection.
        // txEOI_=false → EOI ack just cleared (or normal byte); go send bits.
        if (data) {
            waitCycles_ = 0;
            state_ = txEOI_ ? State::TalkEOI : State::TalkSendBit;
        }
        break;

    case State::TalkSendBit:
        if (waitCycles_ == 0) {
            driven_.clk = false; // CLK low
            driven_.data = (bool)((shiftReg_ >> bitCount_) & 1); // bit 1 → DATA HIGH, bit 0 → DATA LOW
            waitCycles_ = 60;
            state_ = State::TalkHoldBit;
        }
        break;

    case State::TalkHoldBit:
        // Countdown already done by early-return; release CLK so C64 samples DATA.
        driven_.clk = true;
        waitCycles_ = 60;
        state_ = State::TalkBitSettle;
        break;

    case State::TalkBitSettle:
        // Countdown done; CLK was high for 60 cycles. Advance to next bit.
        bitCount_++;
        if (bitCount_ == 8) {
            txPos_++;
            driven_.data = true;
            if (txPos_ % 1000 == 0 || txPos_ >= txBuf_.size() - 2) {
                char b[48]; snprintf(b, sizeof(b), "TX pos %zu/%zu", txPos_, txBuf_.size());
                logEvent(b);
            }
            // After bit 7, go to TalkByteACK which pulls CLK=0 and waits for the
            // host's DATA=0 frame acknowledgment (VICE P_DONE0 / P_DONE1).
            // This is required after every byte — without it the host never gets
            // a chance to ACK and the load hangs permanently.
            talkEoiCycles_ = 0;
            state_ = State::TalkByteACK;
        } else {
            waitCycles_ = 0;
            state_ = State::TalkSendBit;
        }
        break;

    case State::TalkByteACK:
        // Spec step 5 (VICE P_DONE0 / P_DONE1): pull CLK=0 DATA=1 to signal
        // "byte complete", then wait for the host to pull DATA=0 (frame ACK).
        // The KERNAL does this at the end of ACPTR before returning the byte.
        // Without this wait the host never acknowledges and the load hangs.
        driven_.clk  = false;
        driven_.data = true;
        if (!data) {
            // Persist position so re-TALK sessions resume from here.
            if (channel_ >= 0 && channel_ < 16)
                channels_[channel_].pos =
                    channels_[channel_].data.size() - txBuf_.size() + txPos_;
            if (txPos_ < txBuf_.size()) {
                state_ = State::TalkWaitClkHigh;
            } else {
                logEvent("TX complete → Idle");
                releaseAll();
                talking_ = false;
                state_ = State::Idle;
            }
        } else if (++talkEoiCycles_ >= 1000) {
            logEvent("ByteACK timeout — no frame ACK from host");
            releaseAll();
            talking_ = false;
            state_ = State::Idle;
        }
        break;

    default:
        break;
    }

    prevClk_ = clk;
}

// ---------------------------------------------------------------------------
// ATN command dispatch — ONLY sets flags, NEVER changes state_.
// State transitions happen exclusively in setIECLines() on ATN edges.
// ---------------------------------------------------------------------------

void Drive1541::handleAtnByte(uint8_t byte) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ATN byte $%02X", byte);
    logEvent(buf);

    // UNLISTEN / UNTALK terminators
    if (byte == 0x3F) {  // UNLISTEN
        {
            char dbg[64];
            snprintf(dbg, sizeof(dbg),
                "UNLISTEN: listen=%d ch=%d rx=%zu",
                (int)listening_, channel_, rxBuf_.size());
            logEvent(dbg);
        }
        if (listening_ && channel_ >= 0 && !rxBuf_.empty()) {
            std::string name(rxBuf_.begin(), rxBuf_.end());
            logEvent("openChannel(" + std::to_string(channel_) + ", \"" + name + "\")");
            rxBuf_.clear();
            openChannel(channel_, name);
        }
        listening_ = false;
        return;
    }
    if (byte == 0x5F) {  // UNTALK
        talking_ = false;
        return;
    }

    uint8_t cmd  = byte & 0xF0;
    uint8_t addr = byte & 0x0F;

    // LISTEN (0x20–0x3E) or TALK (0x40–0x5E): check device number
    if (cmd == 0x20) {
        if (addr == (uint8_t)deviceNumber_) listening_ = true;
        return;
    }
    if (cmd == 0x40) {
        if (addr == (uint8_t)deviceNumber_) talking_ = true;
        return;
    }

    // Secondary address bytes (only meaningful if we are the addressed device)
    if (!listening_ && !talking_) return;

    if (cmd == SA_OPEN) {
        channel_ = addr;
        // rxBuf_ accumulation starts when ATN rises (ListenWaitClkHigh)
    } else if (cmd == SA_DATA) {
        channel_ = addr;
        // ATN rising will load txBuf_ for this channel
    } else if (cmd == SA_CLOSE) {
        if (addr >= 0 && addr < 16) channels_[addr] = Channel{};
    }
}

void Drive1541::handleListenByte(uint8_t byte) {
    char dbg[24];
    snprintf(dbg, sizeof(dbg), "RX byte $%02X '%c'", byte,
             (byte >= 0x20 && byte < 0x7F) ? (char)byte : '.');
    logEvent(dbg);
    rxBuf_.push_back(byte);
}

// ---------------------------------------------------------------------------
// Channel / file loading
// ---------------------------------------------------------------------------

void Drive1541::openChannel(int ch, const std::string& name) {
    if (ch < 0 || ch >= 16) return;

    // Channel 15 is always open (error/status channel); no file open needed.
    if (ch == 15) return;

    if (!image_.isLoaded() && !t64_.isLoaded()) {
        logEvent("openChannel: no image mounted");
        return;
    }

    std::string normalized = normalizeFileName(name);
    std::vector<uint8_t> data;

    if (normalized == "*") {
        data = t64_.isLoaded() ? t64_.firstPRG() : image_.firstPRG();
        if (data.empty())
            logEvent("openChannel: no PRG file found");
    } else if (!normalized.empty() && normalized[0] == '$') {
        data = getDirectoryData();
        if (data.empty())
            logEvent("openChannel: no image mounted for directory");
    } else {
        data = t64_.isLoaded() ? t64_.findPRG(normalized) : image_.findPRG(normalized);
        if (data.empty())
            logEvent("openChannel: PRG \"" + normalized + "\" not found");
    }

    // Update channel 15 error status.
    static const char kOK[]       = "00,OK,00,00\r";
    static const char kNotFound[] = "62,FILE NOT FOUND,00,00\r";
    const char* status = data.empty() ? kNotFound : kOK;
    channels_[15].data.assign(status, status + std::strlen(status));
    channels_[15].pos  = 0;

    if (data.empty()) return;

    channels_[ch].data = std::move(data);
    channels_[ch].pos  = 0;
    channels_[ch].open = true;
    logEvent("openChannel(" + std::to_string(ch) + ") OK, " +
             std::to_string(channels_[ch].data.size()) + " bytes");

    // Mirror to both ch0 and ch1: KERNAL may open with SA=0 or SA=1 and
    // talk with the same or the other, depending on the LOAD form used.
    if (normalized == "*" && (ch == 0 || ch == 1)) {
        int other = ch ^ 1;
        channels_[other].data = channels_[ch].data;
        channels_[other].pos  = 0;
        channels_[other].open = true;
        logEvent("Mirrored ch" + std::to_string(ch) + " -> ch" + std::to_string(other));
    }
}

// ---------------------------------------------------------------------------
// IHasPanel
// ---------------------------------------------------------------------------

void Drive1541::drawPanel(const char* title, bool* open) {
    ImGui::SetNextWindowSize({ 420.f, 320.f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    ImGui::TextUnformatted(peripheralName_.c_str());
    ImGui::Separator();

    if (image_.isLoaded()) {
        ImGui::Text("Image : %s", image_.path().c_str());
        ImGui::Text("Disk  : %s", image_.diskName().c_str());
        ImGui::Text("Free  : %d blocks", image_.freeBlocks());
        ImGui::Separator();

        if (ImGui::BeginTable("dir", 3,
                ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type",   ImGuiTableColumnFlags_WidthFixed, 40.f);
            ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_WidthFixed, 50.f);
            ImGui::TableHeadersRow();
            for (const auto& de : image_.directory()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(de.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(de.isPRG() ? "PRG" : "---");
                ImGui::TableSetColumnIndex(2); ImGui::Text("%d", de.blocks);
            }
            ImGui::EndTable();
        }
    } else if (t64_.isLoaded()) {
        ImGui::Text("Image : %s", t64_.path().c_str());
        ImGui::Text("Tape  : %s", t64_.tapeName().c_str());
        ImGui::Separator();

        if (ImGui::BeginTable("t64dir", 2,
                ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.f);
            ImGui::TableHeadersRow();
            for (const auto& e : t64_.entries()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(e.isPRG() ? "PRG" : "---");
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("No image mounted");
    }

    ImGui::Spacing();
    bool prev = warpEnabled_;
    ImGui::Checkbox("Warp load", &warpEnabled_);
    if (warpEnabled_ != prev && onWarpToggle) onWarpToggle(warpEnabled_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Intercept KERNAL LOAD at $F533 and inject\n"
                          "file bytes directly — no IEC bus activity.");

    ImGui::Separator();
    static const char* kStateNames[] = {
        "Idle",
        "ATN Wait CLK Low", "ATN Wait CLK High", "ATN Receive Bit", "ATN Bit Settle",
        "Listen Wait CLK", "Listen Bit Settle", "Listen Bit", "Listen EOI",
        "Talk Start", "Talk Wait CLK High", "Talk EOI", "Talk Normal Ready",
        "Talk Wait Host DATA", "Talk Send Bit", "Talk Hold Bit", "Talk Bit Settle",
    };
    int si = (int)state_;
    ImGui::Text("IEC state : %s", (si >= 0 && si < (int)(sizeof(kStateNames)/sizeof(kStateNames[0]))) ? kStateNames[si] : "?");
    ImGui::Text("Listening : %s  Talking : %s  Ch : %d",
        listening_ ? "yes" : "no", talking_ ? "yes" : "no", channel_);
    if (!txBuf_.empty())
        ImGui::Text("TX : %zu bytes, pos %zu", txBuf_.size(), txPos_);
    if (!rxBuf_.empty())
        ImGui::Text("RX buf : %zu bytes", rxBuf_.size());
    ImGui::Text("Lines in  — ATN:%d CLK:%d DATA:%d",
        !hostIn_.atn ? 1 : 0, !hostIn_.clk ? 1 : 0, !hostIn_.data ? 1 : 0);
    ImGui::Text("Lines out — ATN:%d CLK:%d DATA:%d",
        !driven_.atn ? 1 : 0, !driven_.clk ? 1 : 0, !driven_.data ? 1 : 0);

    ImGui::Separator();
    ImGui::TextDisabled("IEC Event Log");

    std::string logText;
    logText.reserve(log_.size() * 64);
    for (const auto& e : log_) {
        logText += e.msg;
        logText += '\n';
    }
    if (logText.empty())
        logText = "(IEC event log is empty)\n";
    logText.push_back('\0');

    ImGui::InputTextMultiline("##ieclog", logText.data(), logText.size(), {0, 120},
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("Clear log")) log_.clear();

    ImGui::End();
}
