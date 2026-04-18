;
; keyboard_test.s  —  CIA1 keyboard matrix scanner
;                     for The 8-Bit Machine
;
; Layout (load address $0200):
;
;   $0200  reset:   prints banner "KEYBOARD SCAN", then enters scan loop
;   scan:           iterates all 8 columns via col_masks table
;                   for each newly-pressed key prints "CcRr " (column, row)
;                   RETURN key (col 0, row 0) additionally prints a newline
;
; Zero page ($00–$0F):
;   $00–$07  PREV0–PREV7  previous PRB byte per column (all $FF = no key)
;   $08      COL          current column index (0–7)
;   $09      NEWP         newly-pressed bits for current column
;   $0A      ROWCTR       row bit counter (0–7)
;

CIA1_PRA = $F100
CIA1_PRB = $F101
CHAR_OUT = $F000

PREV0    = $00
COL      = $08
NEWP     = $09
ROWCTR   = $0A

        .code

; ============================================================
; reset — entry point
; ============================================================
reset:
        sei

        ; Initialise previous-state table: all $FF (no keys pressed)
        ldx #7
init:
        lda #$FF
        sta PREV0,x
        dex
        bpl init

        ; Print banner "KEYBOARD SCAN\n"
        lda #'K'
        sta CHAR_OUT
        lda #'E'
        sta CHAR_OUT
        lda #'Y'
        sta CHAR_OUT
        lda #'B'
        sta CHAR_OUT
        lda #'O'
        sta CHAR_OUT
        lda #'A'
        sta CHAR_OUT
        lda #'R'
        sta CHAR_OUT
        lda #'D'
        sta CHAR_OUT
        lda #' '
        sta CHAR_OUT
        lda #'S'
        sta CHAR_OUT
        lda #'C'
        sta CHAR_OUT
        lda #'A'
        sta CHAR_OUT
        lda #'N'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

        cli

; ============================================================
; scan — main loop: walk all 8 columns each pass
; ============================================================
scan:
        lda #0
        sta COL

next_col:
        ; Select column: write column mask to PRA
        ldx COL
        lda col_masks,x
        sta CIA1_PRA

        ; Read current row state
        lda CIA1_PRB

        ; Compute newly-pressed bits: ~current & previous
        eor #$FF                ; invert: 1 = pressed
        and PREV0,x             ; AND with prev (1 = was up): 1 = edge
        sta NEWP

        ; Update previous state with raw PRB value
        lda CIA1_PRB
        sta PREV0,x

        ; Walk all 8 row bits and report each newly-pressed key
        lda #0
        sta ROWCTR

check_row:
        lda NEWP
        lsr                     ; shift bit 0 into carry; A = remaining bits
        sta NEWP                ; store shifted value back
        bcc no_press

        ; ---- Key press detected: print "CcRr " ----
        lda #'C'
        sta CHAR_OUT
        lda COL
        clc
        adc #'0'
        sta CHAR_OUT
        lda #'R'
        sta CHAR_OUT
        lda ROWCTR
        clc
        adc #'0'
        sta CHAR_OUT
        lda #$0A
        sta CHAR_OUT

no_press:
        inc ROWCTR
        lda ROWCTR
        cmp #8
        bne check_row

        ; Advance to next column
        inc COL
        lda COL
        cmp #8
        bne next_col

        jmp scan

; ============================================================
; Column-select masks (active-low: one bit cleared per column)
; ============================================================
col_masks:
        .byte $FE,$FD,$FB,$F7,$EF,$DF,$BF,$7F
