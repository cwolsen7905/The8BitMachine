#pragma once

#include "emulator/core/IKeyMapper.h"
#include "emulator/devices/CIA6526.h"

// ---------------------------------------------------------------------------
// C64StandardKeyMapper
//
// Maps SDL keycodes to CIA1 keyboard matrix positions for the standard
// Commodore 64 KERNAL ROM.
//
// The switch table in keyEvent() resolves each SDL key to its physical matrix
// position: col = keyboard matrix column (0–7), row = keyboard matrix row (0–7).
//
// CIA1 wiring for the stock C64 KERNAL:
//   PA (output) = row select  — KERNAL pulls one PA bit low to select a row.
//   PB (input)  = column data — bit low means a key in that column is pressed.
//
// Therefore applyKey calls setKey(row, col) — row is the PA bit index and
// col is the PB bit index — so the CIA scan matches the KERNAL's convention.
// ---------------------------------------------------------------------------
class C64StandardKeyMapper : public IKeyMapper {
public:
    explicit C64StandardKeyMapper(CIA6526& cia1) : cia1_(cia1) {}

    void keyEvent(int sdlSym, bool pressed) override;
    void clearKeys() override { cia1_.clearAllKeys(); }

protected:
    CIA6526& cia1_;

    // Stock C64 KERNAL: PA=row select, PB=column data → pass (row, col).
    virtual void applyKey(int col, int row, bool pressed) {
        cia1_.setKey(row, col, pressed);
    }
};

// ---------------------------------------------------------------------------
// MEGA65KeyMapper
//
// MEGA65 OpenROMs wire the matrix with PA=cols and PB=rows (opposite of the
// stock KERNAL).  applyKey passes (col, row) directly so the CIA matrix is
// stored in the orientation the MEGA65 KERNAL expects.
// ---------------------------------------------------------------------------
class MEGA65KeyMapper : public C64StandardKeyMapper {
public:
    explicit MEGA65KeyMapper(CIA6526& cia1) : C64StandardKeyMapper(cia1) {}

protected:
    void applyKey(int col, int row, bool pressed) override {
        cia1_.setKey(col, row, pressed);  // PA=col select, PB=row data
    }
};
