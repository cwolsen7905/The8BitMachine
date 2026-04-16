;
; test.s  —  Character-write test ROM for The 8-Bit Machine
;
; Writes a '*' followed by a newline to the terminal on every iteration of
; the main loop (~1 emulated second per iteration at the default 60 kHz
; emulated clock rate).  A one-shot "HELLO" message is printed at startup.
;
; Design notes:
;   • No .zeropage segment — avoiding the ca65 side-effect where .org inside
;     a non-CODE segment switches the assembler into absolute mode, which
;     corrupts JSR/JMP target addresses in the CODE segment.
;   • No subroutines — eliminates JSR/RTS stack interactions during initial
;     bring-up, making the ROM as easy to trace as possible.
;   • All I/O through the single CHAR_OUT port at $F000.
;
; Memory-mapped I/O:
;   $F000  CHAR_OUT — write one byte here; $0A (LF) flushes the line in
;                     the Terminal panel.  $0D (CR) is silently ignored.
;
; Load address: $0200  (embedded in the .prg header by the build script)
;
; Build (from project root):
;   ./build.sh
;

CHAR_OUT = $F000

        .code

; ============================================================
; Entry point
; Reset vector ($FFFC/$FFFD) points here.
; ============================================================
reset:

; ---- one-shot startup message: "HELLO\n" -------------------
        lda     #'H'
        sta     CHAR_OUT
        lda     #'E'
        sta     CHAR_OUT
        lda     #'L'
        sta     CHAR_OUT
        lda     #'L'
        sta     CHAR_OUT
        lda     #'O'
        sta     CHAR_OUT
        lda     #$0A            ; LF — flushes "HELLO" to the terminal
        sta     CHAR_OUT

; ============================================================
; Main loop — write '*' + newline, then delay ~1 s, repeat.
; ============================================================
main_loop:
        lda     #'*'
        sta     CHAR_OUT
        lda     #$0A            ; LF — flushes the line
        sta     CHAR_OUT

        ; ~61 000-cycle delay
        ; Outer × inner = $30 × 256 iterations
        ; Each inner iteration: DEY (2) + BNE (3 taken, 2 not) ≈ 5 cycles
        ; Total ≈ $30 × (256 × 5 + 5) ≈ 61 680 cycles ≈ 1 s at 60 kHz
        ldx     #$30
delay_outer:
        ldy     #$00
delay_inner:
        dey
        bne     delay_inner
        dex
        bne     delay_outer

        jmp     main_loop
