;
; sid_playground.s — Interactive SID synthesizer with VIC display
;                    for The 8-Bit Machine
;
; Keys QWERTYUI play notes C4–C5 on SID voice 1.
; The VIC screen shows which keys are pressed and the active note.
; Border colour changes with each note.
;
; Keyboard → CIA1 matrix positions scanned here:
;   Col 6 ($BF): Q=row7  E=row1  T=row2  U=row3
;   Col 1 ($FD): W=row1  R=row2  Y=row3  I=row4
;
; Key index assignment (piano order L→R):
;   0=Q(C4)  1=W(D4)  2=E(E4)  3=R(F4)
;   4=T(G4)  5=Y(A4)  6=U(B4)  7=I(C5)
;
; Screen RAM at $0400 (40×25 chars).  Screen codes used:
;   $01-$1A = A-Z   $20 = space   $30-$39 = 0-9
;   $51     = solid block (key pressed indicator)
;
; Load address: $0200
;

CIA1_PRA = $F100
CIA1_PRB = $F101
CHAR_OUT = $F000

VIC_BORD = $D020
VIC_BG   = $D021

SID      = $D400
V1_FREQL = $00
V1_FREQH = $01
V1_CTRL  = $04
V1_AD    = $05
V1_SR    = $06
SID_VOL  = $18

GATE = $01
SAW  = $20

; Screen row start addresses ($0400 + row*40)
ROW1 = $0428    ; title
ROW3 = $0478    ; instruction
ROW5 = $04C8    ; "NOTE:" live display

; Key indicators: row 8 ($0540), stride 5, start col 4
; Addresses: $0544 $0549 $054E $0553 $0558 $055D $0562 $0567  (all page $05)
ROW8 = $0540

; Note labels: row 9 ($0568), same stride
; 2-char note name per slot: e.g. C4 at $056C/$056D
ROW9 = $0568

; Zero page
KEY_STATE   = $00   ; bit0=Q bit1=W bit2=E bit3=R bit4=T bit5=Y bit6=U bit7=I
PREV_KEYS   = $01
ACTIVE_NOTE = $02   ; 0-7 = note index, $FF = silent
TEMP        = $03

        .code

; ============================================================
; reset
; ============================================================
reset:
        sei

        ; Terminal banner
        lda #'S'
        sta CHAR_OUT
        lda #'I'
        sta CHAR_OUT
        lda #'D'
        sta CHAR_OUT
        lda #' '
        sta CHAR_OUT
        lda #'P'
        sta CHAR_OUT
        lda #'L'
        sta CHAR_OUT
        lda #'A'
        sta CHAR_OUT
        lda #'Y'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

        ; Keep interrupts disabled — no IRQ handler needed, we poll CIA directly

        ; SID init — voice 1 sawtooth, fast response
        lda #$0F
        sta SID + SID_VOL       ; master volume = 15
        lda #$00                ; attack=0 (2ms), decay=0 (6ms)
        sta SID + V1_AD
        lda #$F0                ; sustain=15, release=0
        sta SID + V1_SR
        lda #SAW                ; waveform only, gate off
        sta SID + V1_CTRL

        ; VIC init
        lda #$06                ; blue border
        sta VIC_BORD
        lda #$00                ; black background
        sta VIC_BG

        ; Zero page init
        lda #$00
        sta KEY_STATE
        sta PREV_KEYS
        lda #$FF
        sta ACTIVE_NOTE

        ; Clear screen RAM ($0400–$07E7 = 1000 bytes)
        lda #$20
        ldx #0
clr0:   sta $0400,x
        inx
        bne clr0
clr1:   sta $0500,x
        inx
        bne clr1
clr2:   sta $0600,x
        inx
        bne clr2
        ldx #0
clr3:   sta $0700,x             ; $0700–$07E7 = 232 bytes
        inx
        cpx #$E8
        bne clr3

        ; ---- Static screen text ----

        ; Row 1: "SID PLAYGROUND" centred at col 13
        lda #$13
        sta ROW1+13             ; S
        lda #$09
        sta ROW1+14             ; I
        lda #$04
        sta ROW1+15             ; D
        lda #$20
        sta ROW1+16             ; (space)
        lda #$10
        sta ROW1+17             ; P
        lda #$0C
        sta ROW1+18             ; L
        lda #$01
        sta ROW1+19             ; A
        lda #$19
        sta ROW1+20             ; Y
        lda #$07
        sta ROW1+21             ; G
        lda #$12
        sta ROW1+22             ; R
        lda #$0F
        sta ROW1+23             ; O
        lda #$15
        sta ROW1+24             ; U
        lda #$0E
        sta ROW1+25             ; N
        lda #$04
        sta ROW1+26             ; D

        ; Row 3: "PRESS QWERTUI TO PLAY" at col 10
        lda #$10
        sta ROW3+10             ; P
        lda #$12
        sta ROW3+11             ; R
        lda #$05
        sta ROW3+12             ; E
        lda #$13
        sta ROW3+13             ; S
        lda #$13
        sta ROW3+14             ; S
        lda #$20
        sta ROW3+15
        lda #$11
        sta ROW3+16             ; Q
        lda #$17
        sta ROW3+17             ; W
        lda #$05
        sta ROW3+18             ; E
        lda #$12
        sta ROW3+19             ; R
        lda #$14
        sta ROW3+20             ; T
        lda #$19
        sta ROW3+21             ; Y
        lda #$15
        sta ROW3+22             ; U
        lda #$09
        sta ROW3+23             ; I
        lda #$20
        sta ROW3+24
        lda #$14
        sta ROW3+25             ; T
        lda #$0F
        sta ROW3+26             ; O
        lda #$20
        sta ROW3+27
        lda #$10
        sta ROW3+28             ; P
        lda #$0C
        sta ROW3+29             ; L
        lda #$01
        sta ROW3+30             ; A
        lda #$19
        sta ROW3+31             ; Y

        ; Row 5: "NOTE" label at col 4
        lda #$0E
        sta ROW5+4              ; N
        lda #$0F
        sta ROW5+5              ; O
        lda #$14
        sta ROW5+6              ; T
        lda #$05
        sta ROW5+7              ; E

        ; Row 8 key indicators — stride 5, start col 1 → cols 1,6,11,16,21,26,31,36
        lda #$11
        sta $0541               ; Q  col 1
        lda #$17
        sta $0546               ; W  col 6
        lda #$05
        sta $054B               ; E  col 11
        lda #$12
        sta $0550               ; R  col 16
        lda #$14
        sta $0555               ; T  col 21
        lda #$19
        sta $055A               ; Y  col 26
        lda #$15
        sta $055F               ; U  col 31
        lda #$09
        sta $0564               ; I  col 36

        ; Row 9 note labels — same stride, 2-char names at col+1,col+2
        ; C4 D4 E4 F4 G4 A4 B4 C5  (cols 2-3, 7-8, 12-13, 17-18, 22-23, 27-28, 32-33, 37-38)
        lda #$03
        sta $0569               ; C
        lda #$34
        sta $056A               ; 4
        lda #$04
        sta $056E               ; D
        lda #$34
        sta $056F               ; 4
        lda #$05
        sta $0573               ; E
        lda #$34
        sta $0574               ; 4
        lda #$06
        sta $0578               ; F
        lda #$34
        sta $0579               ; 4
        lda #$07
        sta $057D               ; G
        lda #$34
        sta $057E               ; 4
        lda #$01
        sta $0582               ; A
        lda #$34
        sta $0583               ; 4
        lda #$02
        sta $0587               ; B
        lda #$34
        sta $0588               ; 4
        lda #$03
        sta $058C               ; C
        lda #$35
        sta $058D               ; 5

; ============================================================
; main loop
; ============================================================
main:
        ; ---- Scan keyboard matrix ----
        lda #$00
        sta KEY_STATE

        ; Col 6 ($BF): Q=bit7 E=bit1 T=bit2 U=bit3
        lda #$BF
        sta CIA1_PRA
        lda CIA1_PRB
        sta TEMP

        lda TEMP
        and #$80                ; bit7 = Q
        bne nq
        lda KEY_STATE
        ora #$01
        sta KEY_STATE
nq:
        lda TEMP
        and #$02                ; bit1 = E (row 1)
        bne ne
        lda KEY_STATE
        ora #$04
        sta KEY_STATE
ne:
        lda TEMP
        and #$04                ; bit2 = T (row 2)
        bne nt
        lda KEY_STATE
        ora #$10
        sta KEY_STATE
nt:
        lda TEMP
        and #$08                ; bit3 = U (row 3)
        bne nu
        lda KEY_STATE
        ora #$40
        sta KEY_STATE
nu:
        ; Col 1 ($FD): W=bit1 R=bit2 Y=bit3 I=bit4
        lda #$FD
        sta CIA1_PRA
        lda CIA1_PRB
        sta TEMP

        lda TEMP
        and #$02                ; bit1 = W (row 1)
        bne nw
        lda KEY_STATE
        ora #$02
        sta KEY_STATE
nw:
        lda TEMP
        and #$04                ; bit2 = R (row 2)
        bne nr
        lda KEY_STATE
        ora #$08
        sta KEY_STATE
nr:
        lda TEMP
        and #$08                ; bit3 = Y (row 3)
        bne ny
        lda KEY_STATE
        ora #$20
        sta KEY_STATE
ny:
        lda TEMP
        and #$10                ; bit4 = I (row 4)
        bne ni
        lda KEY_STATE
        ora #$80
        sta KEY_STATE
ni:
        ; ---- Find lowest-priority pressed key ----
        lda KEY_STATE
        beq no_keys

        ldy #0
find_key:
        lsr a
        bcs found_key
        iny
        bne find_key            ; safety (will always find before Y=8)

found_key:
        ; Y = active note index (0-7)
        cpy ACTIVE_NOTE
        beq update_indicators   ; same note, just refresh indicators

        ; Note changed — update SID
        sty ACTIVE_NOTE
        lda freq_lo,y
        sta SID + V1_FREQL
        lda freq_hi,y
        sta SID + V1_FREQH
        lda #(SAW | GATE)
        sta SID + V1_CTRL       ; gate ON

        ; Update border colour
        lda note_colors,y
        sta VIC_BORD

        ; Update active note display (row 5, col 9-10)
        lda note_letter,y
        sta ROW5+9
        lda note_octave,y
        sta ROW5+10

        jmp update_indicators

no_keys:
        ; No key pressed — gate off if we were playing
        lda ACTIVE_NOTE
        cmp #$FF
        beq update_indicators   ; already silent

        lda #$FF
        sta ACTIVE_NOTE
        lda #SAW                ; waveform, no gate
        sta SID + V1_CTRL

        ; Reset border to blue
        lda #$06
        sta VIC_BORD

        ; Clear note display
        lda #$20
        sta ROW5+9
        sta ROW5+10

; ---- Update key indicator row (always) ----
update_indicators:
        ; For each of 8 keys: if pressed write block ($51), else write key letter
        ldx #7
upd_loop:
        lda KEY_STATE
        and bit_masks,x         ; test bit X; non-zero = pressed
        bne key_pressed
        lda key_letters,x       ; not pressed: show letter
        jmp write_ind
key_pressed:
        lda #$51                ; pressed: solid block
write_ind:
        ldy scr_offsets,x      ; offset within page $05
        sta $0500,y
        dex
        bpl upd_loop

        jmp main

; ============================================================
; Data tables
; ============================================================

; PAL SID frequency words (985,248 Hz clock)
; freq_word = hz * 16,777,216 / 985,248
freq_lo: .byte $67,$89,$ED,$3B,$13,$45,$DA,$CE  ; C4 D4 E4 F4 G4 A4 B4 C5
freq_hi: .byte $11,$13,$15,$17,$1A,$1D,$20,$22

; Border colours per note (C=red D=orange E=yellow F=green G=cyan A=blue B=purple C5=white)
note_colors: .byte $02,$08,$07,$05,$03,$06,$04,$01

; Screen codes for note letter and octave digit
note_letter: .byte $03,$04,$05,$06,$07,$01,$02,$03   ; C D E F G A B C
note_octave: .byte $34,$34,$34,$34,$34,$34,$34,$35   ; 4 4 4 4 4 4 4 5

; Initial key letter screen codes Q W E R T Y U I
key_letters: .byte $11,$17,$05,$12,$14,$19,$15,$09

; Bit masks for testing KEY_STATE bits 0-7
bit_masks: .byte $01,$02,$04,$08,$10,$20,$40,$80

; Screen offset within page $05 for each key indicator (row 8 = $0540)
; stride 5 from col 1: $0541 $0546 $054B $0550 $0555 $055A $055F $0564
scr_offsets: .byte $41,$46,$4B,$50,$55,$5A,$5F,$64
