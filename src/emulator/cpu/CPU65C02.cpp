#include "emulator/cpu/CPU65C02.h"

// ============================================================================
// CPU65C02 constructor
//
// Fills the NMOS table via buildNMOSTable(), then patches the entries that
// differ on the WDC 65C02.  nmosBug_ = false so IND() uses the correct
// (non-wrapping) address calculation.
//
// Note: pointer-to-member expressions must use the derived type (CPU65C02::)
// so the compiler can verify access to the inherited protected members.
// The resulting pointers are implicitly narrowed to CPU6502Base::* when
// stored in the Instruction table — this conversion is well-defined.
// ============================================================================

CPU65C02::CPU65C02() {
    buildNMOSTable();
    nmosBug_ = false;

    // clang-format off

    // $04  TSB zp
    lookup_[0x04] = {"TSB", &CPU65C02::TSB, &CPU65C02::ZP0, 5};
    // $0C  TSB abs
    lookup_[0x0C] = {"TSB", &CPU65C02::TSB, &CPU65C02::ABS, 6};
    // $12  ORA (zp)
    lookup_[0x12] = {"ORA", &CPU65C02::ORA, &CPU65C02::IZP, 5};
    // $14  TRB zp
    lookup_[0x14] = {"TRB", &CPU65C02::TRB, &CPU65C02::ZP0, 5};
    // $1A  INA
    lookup_[0x1A] = {"INA", &CPU65C02::INA, &CPU65C02::IMP, 2};
    // $1C  TRB abs
    lookup_[0x1C] = {"TRB", &CPU65C02::TRB, &CPU65C02::ABS, 6};
    // $32  AND (zp)
    lookup_[0x32] = {"AND", &CPU65C02::AND, &CPU65C02::IZP, 5};
    // $34  BIT zp,X
    lookup_[0x34] = {"BIT", &CPU65C02::BIT, &CPU65C02::ZPX, 4};
    // $3A  DEA
    lookup_[0x3A] = {"DEA", &CPU65C02::DEA, &CPU65C02::IMP, 2};
    // $3C  BIT abs,X
    lookup_[0x3C] = {"BIT", &CPU65C02::BIT, &CPU65C02::ABX, 4};
    // $52  EOR (zp)
    lookup_[0x52] = {"EOR", &CPU65C02::EOR, &CPU65C02::IZP, 5};
    // $5A  PHY
    lookup_[0x5A] = {"PHY", &CPU65C02::PHY, &CPU65C02::IMP, 3};
    // $64  STZ zp
    lookup_[0x64] = {"STZ", &CPU65C02::STZ, &CPU65C02::ZP0, 3};
    // $72  ADC (zp)
    lookup_[0x72] = {"ADC", &CPU65C02::ADC, &CPU65C02::IZP, 5};
    // $74  STZ zp,X
    lookup_[0x74] = {"STZ", &CPU65C02::STZ, &CPU65C02::ZPX, 4};
    // $7A  PLY
    lookup_[0x7A] = {"PLY", &CPU65C02::PLY, &CPU65C02::IMP, 4};
    // $7C  JMP (abs,X)
    lookup_[0x7C] = {"JMP", &CPU65C02::JMP, &CPU65C02::AIIX, 6};
    // $80  BRA rel
    lookup_[0x80] = {"BRA", &CPU65C02::BRA, &CPU65C02::REL, 2};
    // $89  BIT #imm  (sets Z only, not N/V)
    lookup_[0x89] = {"BIT", &CPU65C02::BIT_IMM, &CPU65C02::IMM, 2};
    // $92  STA (zp)
    lookup_[0x92] = {"STA", &CPU65C02::STA, &CPU65C02::IZP, 5};
    // $9C  STZ abs
    lookup_[0x9C] = {"STZ", &CPU65C02::STZ, &CPU65C02::ABS, 4};
    // $9E  STZ abs,X
    lookup_[0x9E] = {"STZ", &CPU65C02::STZ, &CPU65C02::ABX, 5};
    // $B2  LDA (zp)
    lookup_[0xB2] = {"LDA", &CPU65C02::LDA, &CPU65C02::IZP, 5};
    // $D2  CMP (zp)
    lookup_[0xD2] = {"CMP", &CPU65C02::CMP, &CPU65C02::IZP, 5};
    // $DA  PHX
    lookup_[0xDA] = {"PHX", &CPU65C02::PHX, &CPU65C02::IMP, 3};
    // $F2  SBC (zp)
    lookup_[0xF2] = {"SBC", &CPU65C02::SBC, &CPU65C02::IZP, 5};
    // $FA  PLX
    lookup_[0xFA] = {"PLX", &CPU65C02::PLX, &CPU65C02::IMP, 4};

    // clang-format on
}
