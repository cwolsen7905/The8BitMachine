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
    return name;
}

// ---------------------------------------------------------------------------
// CBM serial secondary-address command nibbles (upper nibble of SA byte)
// ---------------------------------------------------------------------------
static constexpr uint8_t SA_DATA  = 0x60;  // 0x60–0x6F: data channel
static constexpr uint8_t SA_CLOSE = 0xE0;  // 0xE0–0xEF: close channel
static constexpr uint8_t SA_OPEN  = 0xF0;  // 0xF0–0xFF: open channel

// Bit timing in C64 clock cycles (~1 MHz).
// Generous multiples of the real 1541 spec for reliability.
static constexpr int kBitHalfPeriod = 50;   // cycles per CLK half-period when talking
static constexpr int kByteGap       = 100;  // cycles between bytes when talking
static constexpr int kEOIHold       = 250;  // CLK HIGH hold before last byte (>200µs)

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

bool Drive1541::mount(const std::string& path) {
    mountError_.clear();
    if (!image_.load(path)) {
        mountError_ = image_.error();
        return false;
    }
    return true;
}

void Drive1541::eject() {
    image_.unload();
    mountError_.clear();
    for (auto& ch : channels_) ch = Channel{};
    txBuf_.clear(); txPos_ = 0;
    rxBuf_.clear();
    state_     = State::Idle;
    listening_ = false;
    talking_   = false;
    channel_   = -1;
    releaseAll();
}

std::vector<uint8_t> Drive1541::getDirectoryData() {
    std::vector<uint8_t> dir;
    // Load address 0x0801
    dir.push_back(0x01);
    dir.push_back(0x08);
    // BASIC line: 0 "DISK DIRECTORY"
    dir.push_back(0x00);
    dir.push_back(0x00);
    dir.push_back(0x20);
    dir.push_back('"');
    dir.push_back('D');
    dir.push_back('I');
    dir.push_back('S');
    dir.push_back('K');
    dir.push_back(' ');
    dir.push_back('D');
    dir.push_back('I');
    dir.push_back('R');
    dir.push_back('E');
    dir.push_back('C');
    dir.push_back('T');
    dir.push_back('O');
    dir.push_back('R');
    dir.push_back('Y');
    dir.push_back('"');
    dir.push_back(0x00);
    // Dummy file entry
    dir.push_back(0x00);
    dir.push_back(0x00);
    dir.push_back(0x20);
    dir.push_back(' ');
    dir.push_back('"');
    dir.push_back('T');
    dir.push_back('E');
    dir.push_back('S');
    dir.push_back('T');
    dir.push_back('"');
    dir.push_back(' ');
    dir.push_back('P');
    dir.push_back('R');
    dir.push_back('G');
    dir.push_back(0x00);
    // End of directory
    dir.push_back(0x00);
    dir.push_back(0x00);
    return dir;
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

    // Log raw IEC line transitions for correlation with CIA traces.
    if (host.atn != prevHost.atn || host.clk != prevHost.clk || host.data != prevHost.data) {
        char buf[96];
        snprintf(buf, sizeof(buf),
            "IEC lines %d%d%d -> %d%d%d  state=%d",
            prevHost.atn ? 1 : 0, prevHost.clk ? 1 : 0, prevHost.data ? 1 : 0,
            host.atn ? 1 : 0, host.clk ? 1 : 0, host.data ? 1 : 0,
            (int)state_);
        logEvent(buf);
    }

    // EOI acknowledge: C64 briefly pulls DATA low while we hold CLK high.
    // Clear waitCycles_ so clock() immediately runs TalkEOI Phase 2.
    if (state_ == State::TalkEOI && talkClkHigh_ && !host.data && prevHost.data)
        waitCycles_ = 0;

    if (atnFell) {
        state_ = State::AtnWaitClkHigh;
        bitCount_ = 0;
        shiftReg_ = 0;
        driven_.data = false; // Acknowledge ATN
        logEvent("ATN fell → DATA asserted");
    }

    if (atnRose) {
        driven_.data = true;  // release DATA
        logEvent("ATN rose → listen=" + std::to_string(listening_) +
                 " talk=" + std::to_string(talking_) + " ch=" + std::to_string(channel_) +
                 " bits=" + std::to_string(bitCount_) + " sr=$" +
                 [&]{ char b[4]; snprintf(b,4,"%02X",shiftReg_); return std::string(b); }());

        if (talking_) {
            if (channel_ >= 0 && channel_ < 16 && channels_[channel_].open) {
                auto& c = channels_[channel_];
                txBuf_       = std::vector<uint8_t>(c.data.begin() + c.pos, c.data.end());
                txPos_       = 0;
                bitCount_    = 0;
                talkClkHigh_ = false;
                state_       = State::TalkStart;
                logEvent("TalkStart " + std::to_string(txBuf_.size()) + " bytes");
            } else {
                logEvent("Talk: ch " + std::to_string(channel_) + " not open → Idle");
                talking_ = false;
                state_   = State::Idle;
            }
        } else if (listening_) {
            state_ = State::ListenWaitClkHigh;
        } else {
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
    // Bits are valid on CLK falling edge (talker sets DATA then pulses CLK low).
    // ------------------------------------------------------------------
    case State::AtnWaitClkHigh:
        if (clk) {
            driven_.data = true; // release DATA, ready to receive
            state_ = State::AtnReceiveBit;
            bitCount_ = 0;
            shiftReg_ = 0;
        }
        break;

    case State::AtnReceiveBit: {
        if (clkRoseFlag_) {
            clkRoseFlag_ = false;
            uint8_t bit = data ? 1u : 0u;
            shiftReg_ |= bit << bitCount_;
            char b[48]; snprintf(b, sizeof(b), "[CLKr] bit%d DATA=%d sr=$%02X", bitCount_, (int)data, shiftReg_);
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
        if (clk) {
            driven_.data = true;   // release DATA (HIGH) = ready to receive
            state_    = State::ListenReceiveBit;
            bitCount_ = 0;
            shiftReg_ = 0;
            eoiTimer_ = 0;
        }
        break;

    case State::ListenReceiveBit: {
        // EOI Detection: Host pulls DATA low while CLK is high.
        if (clk && !data) {
            if (eoiTimer_ < 255) eoiTimer_++;
            // If DATA held low for ~200us, acknowledge EOI by pulling DATA low.
            if (eoiTimer_ >= 200) {
                driven_.data = false;
            }
        } else {
            if (eoiTimer_ >= 200 && !clk) {
                // Host released DATA and pulled CLK low to send the first bit.
                // We must release DATA so we can read the bus properly!
                driven_.data = true;
                eoiTimer_ = 0;
            } else if (!clk) {
                // Just a normal CLK low (bit send)
                eoiTimer_ = 0;
            } else {
                // clk is high, data is high. Not an EOI or aborted EOI.
                if (eoiTimer_ < 200) eoiTimer_ = 0;
            }
        }

        if (clkRoseFlag_) {
            clkRoseFlag_ = false;
            driven_.data = true; // Ensure DATA is released to read it
            eoiTimer_ = 0;
            shiftReg_ |= (data ? 1u : 0u) << bitCount_;
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
        // When ATN rose, we entered TalkStart. C64 pulls CLK low to hold off drive.
        driven_.clk = true;
        driven_.data = true;
        if (txPos_ >= txBuf_.size()) {
            state_ = State::Idle;
            talking_ = false;
        } else {
            state_ = State::TalkWaitClkHigh;
        }
        break;

    case State::TalkWaitClkHigh:
        // Wait for C64 to release CLK (ACPTR EE18)
        driven_.clk = true;
        driven_.data = true;
        if (clk) {
            // C64 is ready! We pull CLK low to say we are starting the byte.
            driven_.clk = false;
            shiftReg_ = txBuf_[txPos_];
            bitCount_ = 0;
            if (txPos_ == txBuf_.size() - 1) {
                state_ = State::TalkEOI;
            } else {
                waitCycles_ = 0;
                state_ = State::TalkNormalReady;
            }
        }
        break;

    case State::TalkEOI:
        // Hold CLK low until C64 times out (255us) and pulls DATA low.
        driven_.clk = false;
        if (!data) {
            // C64 acknowledged EOI!
            driven_.clk = true; // Release CLK
            state_ = State::TalkWaitHostDataHigh;
        }
        break;

    case State::TalkNormalReady:
        // Hold CLK low for a short time (<200us) then release.
        driven_.clk = false;
        waitCycles_++;
        if (waitCycles_ >= 50) {
            driven_.clk = true; // Release CLK
            state_ = State::TalkWaitHostDataHigh;
        }
        break;

    case State::TalkWaitHostDataHigh:
        // Wait for C64 to release DATA (it does this at EE2A)
        if (data) {
            waitCycles_ = 0;
            state_ = State::TalkSendBit;
        }
        break;

    case State::TalkSendBit:
        if (waitCycles_ == 0) {
            driven_.clk = false; // CLK low
            driven_.data = !((shiftReg_ >> bitCount_) & 1); // active-low data
            waitCycles_ = 20;
            state_ = State::TalkHoldBit;
        }
        break;

    case State::TalkHoldBit:
        if (--waitCycles_ == 0) {
            driven_.clk = true; // CLK high (C64 samples here)
            waitCycles_ = 20;
            state_ = State::TalkBitSettle;
        }
        break;

    case State::TalkBitSettle:
        if (--waitCycles_ == 0) {
            bitCount_++;
            if (bitCount_ == 8) {
                txPos_++;
                driven_.data = true;
                if (txPos_ < txBuf_.size()) {
                    state_ = State::TalkWaitClkHigh;
                } else {
                    state_ = State::Idle;
                    talking_ = false;
                }
            } else {
                waitCycles_ = 0;
                state_ = State::TalkSendBit;
            }
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
    rxBuf_.push_back(byte);
}

// ---------------------------------------------------------------------------
// Channel / file loading
// ---------------------------------------------------------------------------

void Drive1541::openChannel(int ch, const std::string& name) {
    if (!image_.isLoaded()) return;
    if (ch < 0 || ch >= 16) return;

    std::string normalized = normalizeFileName(name);
    std::vector<uint8_t> data;

    if (normalized == "*") {
        data = image_.firstPRG();
    } else {
        if (!normalized.empty() && normalized[0] == '$') return;  // directory not yet supported
        data = image_.findPRG(normalized);
    }

    if (data.empty()) return;

    channels_[ch].data = std::move(data);
    channels_[ch].pos  = 0;
    channels_[ch].open = true;
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
    } else {
        ImGui::TextDisabled("No image mounted");
    }

    ImGui::Separator();
    static const char* kStateNames[] = {
        "Idle",
        "ATN Wait CLK High", "ATN Receive Bit", "ATN Bit Settle",
        "Listen Wait CLK", "Listen Bit Settle", "Listen Bit", "Listen EOI",
        "Talk Start", "Talk Bit Out", "Talk EOI", "Talk Turn"
    };
    int si = (int)state_;
    ImGui::Text("IEC state : %s", (si >= 0 && si < 12) ? kStateNames[si] : "?");
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
