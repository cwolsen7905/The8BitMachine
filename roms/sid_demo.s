;
; sid_demo.s — MOS 6581 SID audio demonstration
;              for The 8-Bit Machine
;
; Plays a two-voice arrangement:
;   Voice 1 (sawtooth) — C major scale melody, up and down, looping forever
;   Voice 2 (triangle) — C3 bass drone
;
; ADSR (voice 1): attack 0 = 2 ms, decay 2 = 48 ms, sustain 6/15, release 0
; ADSR (voice 2): sustain held at full level (bass never gates off)
;
; Delay loops are calibrated for the ~60 kHz debug speed.
; At higher speeds the tempo will increase proportionally.
;
; Load address: $0200
;

CHAR_OUT = $F000
SID      = $D400

; SID register offsets (relative to SID base)
V1_FREQL = $00
V1_FREQH = $01
V1_CTRL  = $04
V1_AD    = $05
V1_SR    = $06

V2_FREQL = $07
V2_FREQH = $08
V2_CTRL  = $0B
V2_AD    = $0C
V2_SR    = $0D

SID_VOL  = $18

; Control register bits
GATE  = $01
TRI   = $10
SAW   = $20
PULSE = $40
NOISE = $80

; Zero page
NOTE_IDX = $00

        .code

; ============================================================
; reset — entry point
; ============================================================
reset:
        sei

        ; Print banner "SID DEMO\n"
        lda #'S'
        sta CHAR_OUT
        lda #'I'
        sta CHAR_OUT
        lda #'D'
        sta CHAR_OUT
        lda #' '
        sta CHAR_OUT
        lda #'D'
        sta CHAR_OUT
        lda #'E'
        sta CHAR_OUT
        lda #'M'
        sta CHAR_OUT
        lda #'O'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

        cli

        ; Master volume = 15
        lda #$0F
        sta SID + SID_VOL

        ; Voice 2 — triangle bass drone on C3 (held forever)
        lda #$B4
        sta SID + V2_FREQL
        lda #$08
        sta SID + V2_FREQH
        lda #$00
        sta SID + V2_AD         ; attack=0 (2 ms), decay=0 (6 ms)
        lda #$F0
        sta SID + V2_SR         ; sustain=15, release=0
        lda #(TRI | GATE)
        sta SID + V2_CTRL       ; gate on — stays on throughout

        ; Voice 1 — sawtooth melody
        lda #$02
        sta SID + V1_AD         ; attack=0 (2 ms), decay=2 (48 ms)
        lda #$60
        sta SID + V1_SR         ; sustain=6, release=0

        lda #0
        sta NOTE_IDX

; ============================================================
; note loop
; ============================================================
next_note:
        ldx NOTE_IDX
        lda note_hi,x
        beq wrap                ; sentinel: hi=$00 marks end of table

        lda note_lo,x
        sta SID + V1_FREQL
        lda note_hi,x
        sta SID + V1_FREQH

        lda #(SAW | GATE)
        sta SID + V1_CTRL       ; gate ON

        jsr note_delay

        lda #SAW
        sta SID + V1_CTRL       ; gate OFF → release

        jsr gap_delay

        inc NOTE_IDX
        jmp next_note

wrap:
        lda #0
        sta NOTE_IDX
        jmp next_note

; ============================================================
; Delays — calibrated for ~60 kHz debug speed
;
; note_delay ≈ 0.5 s:  40 × 150 × 5 cycles ≈ 30,000 cycles
; gap_delay  ≈ 0.1 s:   8 × 150 × 5 cycles ≈  6,000 cycles
; ============================================================
note_delay:
        ldy #40
nd_outer:
        ldx #150
nd_inner:
        dex
        bne nd_inner
        dey
        bne nd_outer
        rts

gap_delay:
        ldy #8
gd_outer:
        ldx #150
gd_inner:
        dex
        bne gd_inner
        dey
        bne gd_outer
        rts

; ============================================================
; Note tables — C major scale up and down (15 notes + sentinel)
; Frequency words for PAL SID clock (985,248 Hz)
;   freq_word = note_hz * 16,777,216 / 985,248
; ============================================================
;                 C4    D4    E4    F4    G4    A4    B4    C5
note_lo:  .byte $67, $89, $ED, $3B, $13, $45, $DA, $CE
;                 B4    A4    G4    F4    E4    D4    C4   (end)
          .byte $DA, $45, $13, $3B, $ED, $89, $67, $00

;                 C4    D4    E4    F4    G4    A4    B4    C5
note_hi:  .byte $11, $13, $15, $17, $1A, $1D, $20, $22
;                 B4    A4    G4    F4    E4    D4    C4   (end)
          .byte $20, $1D, $1A, $17, $15, $13, $11, $00
