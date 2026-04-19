#include "emulator/devices/CIA6526.h"
#include <imgui.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cstdio>

// ---------------------------------------------------------------------------
// Keyboard matrix
// ---------------------------------------------------------------------------

void CIA6526::setKey(int col, int row, bool pressed) {
    if (col < 0 || col > 7 || row < 0 || row > 7) return;
    if (pressed) keyMatrix_[col] &= ~static_cast<uint8_t>(1 << row);
    else         keyMatrix_[col] |=  static_cast<uint8_t>(1 << row);
}

void CIA6526::clearAllKeys() {
    std::memset(keyMatrix_, 0xFF, sizeof(keyMatrix_));
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void CIA6526::reset() {
    pra_  = 0xFF;  prb_  = 0xFF;
    ddra_ = 0x00;  ddrb_ = 0x00;
    for (auto& col : keyMatrix_) col = 0xFF;

    timerALatch_   = 0xFFFF;
    timerACounter_ = 0xFFFF;
    cra_           = 0x00;

    timerBLatch_   = 0xFFFF;
    timerBCounter_ = 0xFFFF;
    crb_           = 0x00;

    tod10_  = 0x00;  todSec_ = 0x00;
    todMin_ = 0x00;  todHr_  = 0x01;

    todAlarm10_  = 0x00;  todAlarmSec_ = 0x00;
    todAlarmMin_ = 0x00;  todAlarmHr_  = 0x00;

    todLatch10_  = 0x00;  todLatchSec_ = 0x00;
    todLatchMin_ = 0x00;  todLatchHr_  = 0x01;
    todLatched_  = false;

    todCycleAcc_ = 0;

    sdr_      = 0x00;
    icrMask_  = 0x00;
    icrFlags_ = 0x00;
}

// ---------------------------------------------------------------------------
// Clock — called every ϕ2 cycle
// ---------------------------------------------------------------------------

void CIA6526::clock() {
    // --- Timer A ---
    if (cra_ & CRA_START) {
        if (timerACounter_ == 0)
            timerAUnderflow();
        else
            --timerACounter_;
    }

    // --- Timer B ---
    if (crb_ & CRB_START) {
        // INMODE bit: 0 = count ϕ2 cycles, 1 = count Timer A underflows
        // Timer A underflow increments happen inside timerAUnderflow().
        if (!(crb_ & CRB_INMODE)) {
            if (timerBCounter_ == 0)
                timerBUnderflow();
            else
                --timerBCounter_;
        }
    }

    // --- TOD advancement ---
    if (++todCycleAcc_ >= todCyclePeriod_) {
        todCycleAcc_ = 0;
        tickTOD();
    }
}

void CIA6526::timerAUnderflow() {
    timerACounter_ = timerALatch_;
    if (cra_ & CRA_ONESHOT) cra_ &= ~CRA_START;

    icrFlags_ |= ICR_TA;
    if (icrMask_ & ICR_TA) {
        icrFlags_ |= ICR_IR;
        if (onIRQ) onIRQ();
    }

    // Drive Timer B if in INMODE=1 (count TA underflows)
    if ((crb_ & CRB_START) && (crb_ & CRB_INMODE)) {
        if (timerBCounter_ == 0)
            timerBUnderflow();
        else
            --timerBCounter_;
    }
}

void CIA6526::timerBUnderflow() {
    timerBCounter_ = timerBLatch_;
    if (crb_ & CRB_ONESHOT) crb_ &= ~CRB_START;

    icrFlags_ |= ICR_TB;
    if (icrMask_ & ICR_TB) {
        icrFlags_ |= ICR_IR;
        if (onIRQ) onIRQ();
    }
}

// ---------------------------------------------------------------------------
// TOD helpers
// ---------------------------------------------------------------------------

// BCD increment with wraparound.  max is the BCD ceiling (e.g. 0x09, 0x59).
uint8_t CIA6526::bcdInc(uint8_t bcd, uint8_t max) {
    uint8_t lo = (bcd & 0x0F) + 1;
    uint8_t hi = (bcd >> 4) & 0x0F;
    if (lo > 9) { lo = 0; ++hi; }
    uint8_t result = static_cast<uint8_t>((hi << 4) | lo);
    return (result > max) ? 0x00 : result;
}

void CIA6526::tickTOD() {
    // Advance tenths
    uint8_t newTenths = (tod10_ & 0x0F) + 1;
    if (newTenths <= 9) {
        tod10_ = newTenths;
    } else {
        tod10_ = 0;
        // Advance seconds
        uint8_t newSec = bcdInc(todSec_, 0x59);
        if (newSec == 0x00 && todSec_ == 0x59) {
            todSec_ = 0x00;
            // Advance minutes
            uint8_t newMin = bcdInc(todMin_, 0x59);
            if (newMin == 0x00 && todMin_ == 0x59) {
                todMin_ = 0x00;
                // Advance hours (1–12 with AM/PM toggle)
                uint8_t pm   = todHr_ & TOD_PM;
                uint8_t hrs  = todHr_ & TOD_HR_MASK;
                uint8_t next = bcdInc(hrs, 0x12);
                if (next == 0x00) next = 0x01;
                if (hrs == 0x11) pm ^= TOD_PM;   // toggle AM/PM at 11→12
                todHr_ = static_cast<uint8_t>(pm | next);
            } else {
                todMin_ = newMin;
            }
        } else {
            todSec_ = newSec;
        }
    }

    checkTODAlarm();
}

void CIA6526::checkTODAlarm() {
    if ((tod10_  & 0x0F) == (todAlarm10_  & 0x0F) &&
        todSec_  == todAlarmSec_  &&
        todMin_  == todAlarmMin_  &&
        (todHr_ & TOD_HR_WRITE_MASK) == (todAlarmHr_ & TOD_HR_WRITE_MASK)) {

        icrFlags_ |= ICR_TOD;
        if (icrMask_ & ICR_TOD) {
            icrFlags_ |= ICR_IR;
            if (onIRQ) onIRQ();
        }
    }
}

// ---------------------------------------------------------------------------
// Register read
// ---------------------------------------------------------------------------

uint8_t CIA6526::read(uint16_t offset) const {
    switch (offset & 0x0F) {
        case REG_PRA: {
            // Poll live IEC lines so tight KERNAL read loops see asynchronous changes
            const_cast<CIA6526*>(this)->updateIECInputBits();

            uint8_t out = pra_ & ddra_;
            uint8_t in  = iecInputBits_ & ~ddra_;

            // Removed hack simulating ack when CLK high

            uint8_t val = out | in;
            if (!iecDevices_.empty() && val != iecLastReadPRA_) {
                iecLastReadPRA_ = val;
                char buf[64];
                snprintf(buf, sizeof(buf),
                    "R PRA=$%02X  CLK-in=%d DATA-in=%d",
                    (unsigned)val,
                    (int)!!(val & 0x40), (int)!!(val & 0x80));
                logIEC(buf);
            }
            return val;
        }
        case REG_PRB: {
            // C64 keyboard scan: KERNAL writes one PA bit low to select a column,
            // then reads PB to find which rows are active (active-low, key=0).
            // pra_ holds the last value written — bit N=0 means column N is selected.
            uint8_t result = 0xFF;
            for (int col = 0; col < 8; ++col)
                if (!(pra_ & (1 << col)))
                    result &= keyMatrix_[col];
            return result;
        }
        case REG_DDRA: return ddra_;
        case REG_DDRB: return ddrb_;

        case REG_TALO: return static_cast<uint8_t>(timerACounter_ & 0xFF);
        case REG_TAHI: return static_cast<uint8_t>(timerACounter_ >> 8);
        case REG_TBLO: return static_cast<uint8_t>(timerBCounter_ & 0xFF);
        case REG_TBHI: return static_cast<uint8_t>(timerBCounter_ >> 8);

        // TOD: reading HR latches the time; reading 10THS releases the latch.
        case REG_TOD_HR: {
            auto* self = const_cast<CIA6526*>(this);
            self->todLatch10_  = tod10_;
            self->todLatchSec_ = todSec_;
            self->todLatchMin_ = todMin_;
            self->todLatchHr_  = todHr_;
            self->todLatched_  = true;
            return todHr_;
        }
        case REG_TOD_MIN: return todLatched_ ? todLatchMin_ : todMin_;
        case REG_TOD_SEC: return todLatched_ ? todLatchSec_ : todSec_;
        case REG_TOD_10: {
            auto* self = const_cast<CIA6526*>(this);
            self->todLatched_ = false;
            return todLatch10_;  // return frozen snapshot, not live counter
        }

        case REG_SDR: return sdr_;

        case REG_ICR: {
            // Reading ICR is destructive: returns the current flags then clears them.
            // The UI panel must use icrFlags_() / icrMask_() accessors, never read().
            uint8_t val = icrFlags_;
            const_cast<CIA6526*>(this)->icrFlags_ = 0x00;
            return val;
        }
        case REG_CRA: return cra_;
        case REG_CRB: return crb_ & ~CRB_ALARM;  // bit 7 is write-only
        default:      return 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Register write
// ---------------------------------------------------------------------------

void CIA6526::write(uint16_t offset, uint8_t value) {
    switch (offset & 0x0F) {
        case REG_PRA:
            pra_ = value;
            notifyIEC();
            {
                uint8_t prb = 0xFF;
                for (int col = 0; col < 8; ++col)
                    if (!(pra_ & (1 << col)))
                        prb &= keyMatrix_[col];
                lastScanPRA_ = pra_;
                lastScanPRB_ = prb;
                if (prb != 0xFF) { lastActivePRA_ = pra_; lastActivePRB_ = prb; }
            }
            break;
        case REG_PRB:  prb_  = value; break;
        case REG_DDRA:
            ddra_ = value;
            notifyIEC();  // DDRA direction change can release/assert IEC lines
            break;
        case REG_DDRB: ddrb_ = value; break;

        case REG_TALO:
            timerALatch_ = (timerALatch_ & 0xFF00) | value;
            break;
        case REG_TAHI:
            timerALatch_ = (timerALatch_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            if (!(cra_ & CRA_START))
                timerACounter_ = timerALatch_;
            break;

        case REG_TBLO:
            timerBLatch_ = (timerBLatch_ & 0xFF00) | value;
            break;
        case REG_TBHI:
            timerBLatch_ = (timerBLatch_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            if (!(crb_ & CRB_START))
                timerBCounter_ = timerBLatch_;
            break;

        // TOD writes: CRB bit 7 = 1 → write alarm; bit 7 = 0 → write time
        case REG_TOD_10:
            if (crb_ & CRB_ALARM) todAlarm10_  = value & 0x0F;
            else                  tod10_        = value & 0x0F;
            break;
        case REG_TOD_SEC:
            if (crb_ & CRB_ALARM) todAlarmSec_ = value & TOD_HR_MASK;
            else                  todSec_       = value & TOD_HR_MASK;
            break;
        case REG_TOD_MIN:
            if (crb_ & CRB_ALARM) todAlarmMin_ = value & TOD_HR_MASK;
            else                  todMin_       = value & TOD_HR_MASK;
            break;
        case REG_TOD_HR:
            if (crb_ & CRB_ALARM) todAlarmHr_  = value & TOD_HR_WRITE_MASK;
            else                  todHr_        = value & TOD_HR_WRITE_MASK;
            break;

        case REG_SDR: sdr_ = value; break;

        case REG_ICR:
            if (value & ICR_SET_BIT) icrMask_ |=  (value & ICR_SOURCES);
            else                     icrMask_ &= ~(value & ICR_SOURCES);
            break;

        case REG_CRA:
            cra_ = value;
            if (value & CRA_LOAD) {
                timerACounter_ = timerALatch_;
                cra_ &= ~CRA_LOAD;
            }
            break;

        case REG_CRB:
            crb_ = value;
            if (value & CRB_LOAD) {
                timerBCounter_ = timerBLatch_;
                crb_ &= ~CRB_LOAD;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// statusLine
// ---------------------------------------------------------------------------

std::string CIA6526::statusLine() const {
    std::ostringstream s;
    s << std::uppercase << std::hex << std::setfill('0');
    s << "TA=$" << std::setw(4) << (unsigned)timerACounter_
      << ((cra_ & CRA_START) ? " RUN" : " STP")
      << "  TB=$" << std::setw(4) << (unsigned)timerBCounter_
      << ((crb_ & CRB_START) ? " RUN" : " STP");
    s << std::dec
      << "  TOD=" << bcdToInt(todHr_ & TOD_HR_MASK) << ":"
      << std::setfill('0') << std::setw(2) << bcdToInt(todMin_) << ":"
      << std::setw(2) << bcdToInt(todSec_) << "."
      << (int)(tod10_ & 0x0F)
      << ((todHr_ & TOD_PM) ? "pm" : "am");
    return s.str();
}

// ---------------------------------------------------------------------------
// ImGui panel
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// IEC bus notification
// ---------------------------------------------------------------------------

void CIA6526::notifyIEC() {
    if (iecDevices_.empty()) return;

    // CIA2 PA3/4/5 are active-HIGH outputs driving open-collector transistors.
    // Writing 1 pulls the line LOW (asserted); writing 0 releases (line HIGH via pull-up).
    // IECLines convention: true = released/HIGH, false = asserted/LOW.
    auto lineReleased = [&](int bit) -> bool {
        if (!(ddra_ & (1 << bit))) return true;    // input pin → not driving → released
        return (pra_ & (1 << bit)) == 0;           // output: bit=0 → line HIGH (released)
    };

    bool oldAtn = iecDriven_.atn;
    iecDriven_.atn  = lineReleased(3);
    iecDriven_.clk  = lineReleased(4);
    iecDriven_.data = lineReleased(5);

    {
        char buf[64];
        snprintf(buf, sizeof(buf),
            "W PRA=$%02X DDRA=$%02X  ATN=%d CLK=%d DATA=%d",
            (unsigned)pra_, (unsigned)ddra_,
            (int)iecDriven_.atn, (int)iecDriven_.clk, (int)iecDriven_.data);
        logIEC(buf);
    }

    for (IIECDevice* dev : iecDevices_)
        dev->setIECLines(iecDriven_);

    if (!iecDevices_.empty()) {
        auto devLines = iecDevices_[0]->getIECLines();
        iecDriven_.atn &= devLines.atn;
        iecDriven_.clk &= devLines.clk;
        iecDriven_.data &= devLines.data;
    }

    updateIECInputBits();

}

void CIA6526::updateIECInputBits() {
    if (iecDevices_.empty()) return;

    // Compute wired-AND bus state: CIA's own output AND all IEC device outputs.
    // A line is released (true) if DDRA bit is 0 (input) OR PRA bit is 0 (output low = released).
    auto lineOut = [&](int bit) -> bool {
        if (!(ddra_ & (1 << bit))) return true;  // input → not driving → released
        return !(pra_ & (1 << bit));             // output: pra_=1 asserts (low=false)
    };
    bool busAtn  = iecDriven_.atn;  // PA3: ATN
    bool busClk  = lineOut(4);      // PA4: CLK
    bool busData = lineOut(5);      // PA5: DATA

    bool prevBusAtn  = iecBusAtn_;
    bool prevBusClk  = iecBusClk_;
    bool prevBusData = iecBusData_;

    for (IIECDevice* dev : iecDevices_) {
        IECLines d = dev->getIECLines();
        busAtn  = busAtn  && d.atn;
        busClk  = busClk  && d.clk;
        busData = busData && d.data;
    }

    if (prevBusAtn && !busAtn) {
        atnCaptureActive_ = true;
        atnCaptureShiftReg_ = 0;
        atnCaptureBitCount_ = 0;
        logIEC("ATN command capture start");
    }

    if (atnCaptureActive_) {
        bool clkFell = prevBusClk && !busClk;
        if (clkFell) {
            uint8_t bit = busData ? 0u : 1u;
            atnCaptureShiftReg_ |= bit << atnCaptureBitCount_;
            char buf[64];
            snprintf(buf, sizeof(buf),
                "ATN bit %d = %d  sr=$%02X",
                atnCaptureBitCount_, (int)bit, (unsigned)atnCaptureShiftReg_);
            logIEC(buf);
            ++atnCaptureBitCount_;
        }
    }

    if (!prevBusAtn && busAtn && atnCaptureActive_) {
        char buf[64];
        snprintf(buf, sizeof(buf),
            "ATN command complete (%d bits) sr=$%02X",
            atnCaptureBitCount_, (unsigned)atnCaptureShiftReg_);
        logIEC(buf);
        if (atnCaptureBitCount_ != 8)
            logIEC("ATN command aborted before 8 bits");
        atnCaptureActive_ = false;
    }

    if (busAtn != iecBusAtn_ || busClk != iecBusClk_ || busData != iecBusData_) {
        char buf[64];
        snprintf(buf, sizeof(buf),
            "BUS ATN=%d CLK=%d DATA=%d  ->  ATN=%d CLK=%d DATA=%d",
            (int)iecBusAtn_, (int)iecBusClk_, (int)iecBusData_,
            (int)busAtn, (int)busClk, (int)busData);
        logIEC(buf);
        iecBusAtn_  = busAtn;
        iecBusClk_  = busClk;
        iecBusData_ = busData;
    }

    // Feed bus state back as PA6 (CLK-in) and PA7 (DATA-in).
    // The C64 hardware does NOT use inverters on these inputs.
    // They are connected directly to the bus:
    //   line LOW (asserted, false) → PA bit = 0
    //   line HIGH (released, true) → PA bit = 1
    iecInputBits_ = 0;
    if (busClk)  iecInputBits_ |= 0x40;  // PA6
    if (busData) iecInputBits_ |= 0x80;  // PA7
}

void CIA6526::drawPanel(const char* title, bool* open) {
    ImGui::SetNextWindowSize({ 360.0f, 380.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    // Helper: coloured bit indicator
    auto bitLabel = [](const char* label, bool set) {
        if (set) ImGui::TextColored({ 0.2f, 1.0f, 0.3f, 1.0f }, "%s", label);
        else     ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
    };

    if (ImGui::CollapsingHeader("Timer A", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Counter  $%04X    Latch $%04X",
            (unsigned)timerACounter_, (unsigned)timerALatch_);
        ImGui::Text("CRA      $%02X", (unsigned)cra_);
        ImGui::SameLine(120);
        bitLabel("START",   cra_ & CRA_START);
        bitLabel("ONESHOT", cra_ & CRA_ONESHOT);
        ImGui::NewLine();
        if (ImGui::SmallButton("Fire Now##ta")) timerAUnderflow();
    }

    if (ImGui::CollapsingHeader("Timer B", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Counter  $%04X    Latch $%04X",
            (unsigned)timerBCounter_, (unsigned)timerBLatch_);
        ImGui::Text("CRB      $%02X", (unsigned)crb_);
        ImGui::SameLine(120);
        bitLabel("START",   crb_ & CRB_START);
        bitLabel("ONESHOT", crb_ & CRB_ONESHOT);
        bitLabel((crb_ & CRB_INMODE) ? "cnt:TA" : "cnt:ϕ2", true);
        ImGui::NewLine();
        if (ImGui::SmallButton("Fire Now##tb")) timerBUnderflow();
    }

    if (ImGui::CollapsingHeader("ICR", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Flags $%02X  ", (unsigned)icrFlags_);
        ImGui::SameLine();
        bitLabel("TA",  icrFlags_ & ICR_TA);
        bitLabel("TB",  icrFlags_ & ICR_TB);
        bitLabel("TOD", icrFlags_ & ICR_TOD);
        bitLabel("IR",  icrFlags_ & ICR_IR);
        ImGui::NewLine();
        ImGui::Text("Mask  $%02X  ", (unsigned)icrMask_);
        ImGui::SameLine();
        bitLabel("TA",  icrMask_ & ICR_TA);
        bitLabel("TB",  icrMask_ & ICR_TB);
        bitLabel("TOD", icrMask_ & ICR_TOD);
        ImGui::NewLine();
    }

    if (ImGui::CollapsingHeader("TOD Clock", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Time   %02d:%02d:%02d.%d %s",
            bcdToInt(todHr_ & TOD_HR_MASK),
            bcdToInt(todMin_), bcdToInt(todSec_),
            (int)(tod10_ & 0x0F),
            (todHr_ & TOD_PM) ? "PM" : "AM");
        ImGui::Text("Alarm  %02d:%02d:%02d.%d %s",
            bcdToInt(todAlarmHr_ & TOD_HR_MASK),
            bcdToInt(todAlarmMin_), bcdToInt(todAlarmSec_),
            (int)(todAlarm10_ & 0x0F),
            (todAlarmHr_ & TOD_PM) ? "PM" : "AM");
    }

    if (ImGui::CollapsingHeader("Ports")) {
        ImGui::Text("PRA $%02X  DDRA $%02X", (unsigned)pra_,  (unsigned)ddra_);
        ImGui::Text("PRB $%02X  DDRB $%02X", (unsigned)prb_,  (unsigned)ddrb_);
        ImGui::Text("SDR $%02X", (unsigned)sdr_);
    }

    if (ImGui::CollapsingHeader("Keyboard Matrix Debugger")) {
        // Recompute PRB exactly as the read() path does
        uint8_t scanPRB = 0xFF;
        for (int col = 0; col < 8; ++col)
            if (!(pra_ & (1 << col)))
                scanPRB &= keyMatrix_[col];

        ImGui::Text("Current  PRA $%02X  ->  PRB $%02X", (unsigned)pra_, (unsigned)scanPRB);
        ImGui::Text("Last     PRA $%02X  ->  PRB $%02X", (unsigned)lastScanPRA_, (unsigned)lastScanPRB_);
        if (lastActivePRB_ != 0xFF)
            ImGui::TextColored({0.2f, 1.0f, 0.3f, 1.0f},
                "Key hit PRA $%02X  ->  PRB $%02X", (unsigned)lastActivePRA_, (unsigned)lastActivePRB_);
        else
            ImGui::TextDisabled("Key hit  (none yet)");
        ImGui::Separator();

        // Column headers
        ImGui::TextDisabled("    ");
        for (int col = 0; col < 8; ++col) {
            ImGui::SameLine();
            bool scanned = !(pra_ & (1 << col));
            if (scanned)
                ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, " c%d", col);
            else
                ImGui::TextDisabled(" c%d", col);
        }

        for (int row = 0; row < 8; ++row) {
            ImGui::TextDisabled("r%d  ", row);
            for (int col = 0; col < 8; ++col) {
                ImGui::SameLine();
                bool pressed = !(keyMatrix_[col] & (1 << row));
                if (pressed)
                    ImGui::TextColored({0.2f, 1.0f, 0.3f, 1.0f}, "  * ");
                else
                    ImGui::TextDisabled("  . ");
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("* = pressed   yellow col = KERNAL scanning");
    }

    if (!iecDevices_.empty() && ImGui::CollapsingHeader("IEC Log", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Clear##ieclog")) iecLog_.clear();

        std::string logText;
        logText.reserve(iecLog_.size() * 64);
        for (auto& e : iecLog_) {
            logText += e;
            logText += '\n';
        }
        if (logText.empty())
            logText = "(IEC log is empty)\n";
        logText.push_back('\0');

        ImGui::InputTextMultiline("##ieclogtext", logText.data(), logText.size(), {0, 140},
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput);
    }

    ImGui::End();
}
