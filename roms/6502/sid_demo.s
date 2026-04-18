;
; sid_demo.s — MOS 6581 SID feature demonstration
;              for The 8-Bit Machine
;
; Three sections, each looping through the C major scale:
;
;   Section 1 — Plain voices
;     V1: sawtooth melody   V2: triangle bass drone
;
;   Section 2 — LP filter sweep
;     V1: sawtooth melody routed through LP filter, cutoff sweeping up each note
;     V2: triangle bass drone (unfiltered)
;     Resonance set high so the filter peak is audible
;
;   Section 3 — Hard sync + ring mod
;     V1: sawtooth at melody frequency
;     V3: pulse wave at 1.5× V1's frequency, SYNC'd to V1 (produces classic
;         hard-sync overtones that track the melody)
;     V2: triangle with RING mod driven by V3 oscillator (metallic timbre)
;
; After section 3 the demo loops back to section 1.
;
; Delay loops are calibrated for the ~60 kHz debug speed.
;

CHAR_OUT = $F000
SID      = $D400

; Voice offsets
V1_FREQL  = $00
V1_FREQH  = $01
V1_PWL    = $02
V1_PWH    = $03
V1_CTRL   = $04
V1_AD     = $05
V1_SR     = $06
V2_FREQL  = $07
V2_FREQH  = $08
V2_PWL    = $09
V2_PWH    = $0A
V2_CTRL   = $0B
V2_AD     = $0C
V2_SR     = $0D
V3_FREQL  = $0E
V3_FREQH  = $0F
V3_PWL    = $10
V3_PWH    = $11
V3_CTRL   = $12
V3_AD     = $13
V3_SR     = $14

; Filter registers
FC_LO     = $15
FC_HI     = $16
RES_FILT  = $17
MODE_VOL  = $18

; Control register bits
GATE   = $01
SYNC   = $02
RING   = $04
TRI    = $10
SAW    = $20
PULSE  = $40

; Zero page
NOTE_IDX  = $00
SECT_CTR  = $01   ; section repeat counter
FILT_HI   = $02   ; current filter cutoff hi byte (section 2)

        .code

; ============================================================
; reset — print banner, init SID, run sections
; ============================================================
reset:
        sei

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

        ; Master volume = 15, no filter modes yet
        lda #$0F
        sta SID + MODE_VOL

        ; Bass voice 2: C3 triangle drone — used in all sections
        lda #$B4
        sta SID + V2_FREQL
        lda #$08
        sta SID + V2_FREQH
        lda #$00
        sta SID + V2_AD
        lda #$F0
        sta SID + V2_SR

; ============================================================
; Section 1 — plain sawtooth melody + triangle bass
; ============================================================
section1:
        lda #'1'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

        ; Silence voice 3
        lda #$00
        sta SID + V3_CTRL

        ; No filter routing
        lda #$00
        sta SID + RES_FILT
        lda #$0F
        sta SID + MODE_VOL

        ; Gate bass on
        lda #(TRI | GATE)
        sta SID + V2_CTRL

        ; Voice 1: sawtooth melody
        lda #$02
        sta SID + V1_AD
        lda #$60
        sta SID + V1_SR

        lda #0
        sta NOTE_IDX
        lda #2                  ; play scale twice
        sta SECT_CTR

s1_loop:
        ldx NOTE_IDX
        lda note_hi,x
        beq s1_next_pass

        lda note_lo,x
        sta SID + V1_FREQL
        lda note_hi,x
        sta SID + V1_FREQH
        lda #(SAW | GATE)
        sta SID + V1_CTRL
        jsr note_delay
        lda #SAW
        sta SID + V1_CTRL
        jsr gap_delay

        inc NOTE_IDX
        jmp s1_loop

s1_next_pass:
        lda #0
        sta NOTE_IDX
        dec SECT_CTR
        bne s1_loop

; ============================================================
; Section 2 — LP filter sweep
; ============================================================
section2:
        lda #'2'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

        ; Route voice 1 through filter, resonance = 12
        lda #$C1
        sta SID + RES_FILT      ; res=12, FILT1
        ; LP mode on, volume = 15
        lda #$1F
        sta SID + MODE_VOL

        ; Start filter at low cutoff
        lda #$00
        sta SID + FC_LO
        lda #$04
        sta SID + FC_HI
        sta FILT_HI

        ; Gate bass on (unfiltered)
        lda #(TRI | GATE)
        sta SID + V2_CTRL

        lda #0
        sta NOTE_IDX
        lda #2
        sta SECT_CTR

s2_loop:
        ldx NOTE_IDX
        lda note_hi,x
        beq s2_next_pass

        lda note_lo,x
        sta SID + V1_FREQL
        lda note_hi,x
        sta SID + V1_FREQH
        lda #(SAW | GATE)
        sta SID + V1_CTRL
        jsr note_delay
        lda #SAW
        sta SID + V1_CTRL
        jsr gap_delay

        ; Sweep filter cutoff up each note
        lda FILT_HI
        clc
        adc #$10
        cmp #$FF
        bcc s2_no_wrap
        lda #$04                ; reset to low
s2_no_wrap:
        sta FILT_HI
        sta SID + FC_HI

        inc NOTE_IDX
        jmp s2_loop

s2_next_pass:
        lda #0
        sta NOTE_IDX
        dec SECT_CTR
        bne s2_loop

        ; Clear filter routing
        lda #$00
        sta SID + RES_FILT
        lda #$0F
        sta SID + MODE_VOL

; ============================================================
; Section 3 — hard sync (V3→V1) + ring mod (V3→V2)
; ============================================================
section3:
        lda #'3'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

        ; Gate bass off — voice 2 will become ring-mod target
        lda #TRI
        sta SID + V2_CTRL

        ; Voice 2: triangle with RING mod (source = V1 oscillator)
        lda #$00
        sta SID + V2_AD
        lda #$F0
        sta SID + V2_SR
        ; Set V2 freq to C3 as before; ring mod XOR's its fold with V1 MSB
        lda #$B4
        sta SID + V2_FREQL
        lda #$08
        sta SID + V2_FREQH
        lda #(TRI | RING | GATE)
        sta SID + V2_CTRL

        ; Voice 3: pulse wave at ~1.5× melody freq, SYNC'd to V1
        ; Will be set in the note loop; ADSR fast
        lda #$00
        sta SID + V3_AD
        lda #$F0
        sta SID + V3_SR
        ; 50% pulse width
        lda #$00
        sta SID + V3_PWL
        lda #$08
        sta SID + V3_PWH

        ; Voice 1: sawtooth melody
        lda #$02
        sta SID + V1_AD
        lda #$60
        sta SID + V1_SR

        lda #0
        sta NOTE_IDX
        lda #2
        sta SECT_CTR

s3_loop:
        ldx NOTE_IDX
        lda note_hi,x
        beq s3_next_pass

        ; V1 melody frequency
        lda note_lo,x
        sta SID + V1_FREQL
        lda note_hi,x
        sta SID + V1_FREQH

        ; V3: approximately 1.5× V1 frequency (add half of freq word)
        ; Use a lookup for the same notes shifted up a fifth (G D A E B F# C# G#)
        lda fifth_lo,x
        sta SID + V3_FREQL
        lda fifth_hi,x
        sta SID + V3_FREQH

        ; Gate both V1 and V3 on
        lda #(SAW | GATE)
        sta SID + V1_CTRL
        lda #(PULSE | SYNC | GATE)
        sta SID + V3_CTRL

        jsr note_delay

        lda #SAW
        sta SID + V1_CTRL
        lda #(PULSE | SYNC)
        sta SID + V3_CTRL
        jsr gap_delay

        inc NOTE_IDX
        jmp s3_loop

s3_next_pass:
        lda #0
        sta NOTE_IDX
        dec SECT_CTR
        bne s3_loop

        ; Gate all voices off, loop back to section 1
        lda #$00
        sta SID + V1_CTRL
        sta SID + V2_CTRL
        sta SID + V3_CTRL
        jmp reset

; ============================================================
; Delays (~60 kHz debug speed)
; note_delay ≈ 0.5 s, gap_delay ≈ 0.1 s
; ============================================================
note_delay:
        ldy #40
nd_out: ldx #150
nd_in:  dex
        bne nd_in
        dey
        bne nd_out
        rts

gap_delay:
        ldy #8
gd_out: ldx #150
gd_in:  dex
        bne gd_in
        dey
        bne gd_out
        rts

; ============================================================
; Note tables — C major scale up and down (15 notes + sentinel)
; PAL SID clock (985,248 Hz): freq_word = hz * 16777216 / 985248
; ============================================================
;           C4    D4    E4    F4    G4    A4    B4    C5
note_lo: .byte $67, $89, $ED, $3B, $13, $45, $DA, $CE
;           B4    A4    G4    F4    E4    D4    C4   end
         .byte $DA, $45, $13, $3B, $ED, $89, $67, $00

note_hi: .byte $11, $13, $15, $17, $1A, $1D, $20, $22
         .byte $20, $1D, $1A, $17, $15, $13, $11, $00

; Perfect fifth above each note (multiply freq by 3/2):
;           G4    A4    B4    C5    D5    E5    F#5   G5
fifth_lo:.byte $13, $45, $DA, $CE, $93, $8A, $C6, $26
fifth_hi:.byte $1A, $1D, $20, $22, $26, $2B, $2F, $34
;           F#5   E5    D5    C5    B4    A4    G4   end
         .byte $C6, $8A, $93, $CE, $DA, $45, $13, $00
fifth_hi_b:
         .byte $2F, $2B, $26, $22, $20, $1D, $1A, $00
