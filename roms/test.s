;
; test.s  —  CIA1 Timer A interrupt test ROM for The 8-Bit Machine
;
; Demonstrates the full CIA1 IRQ pipeline:
;
;   1. Writes the 6502 IRQ vector ($FFFE/$FFFF) at runtime to point to
;      irq_handler (the vector addresses are in plain RAM, so a simple STA
;      is enough — no linker tricks needed).
;   2. Programs CIA1 Timer A latch to $3000 (12 288 cycles):
;        ~4.9 IRQs / second  at the default 60 kHz debug speed
;        ~163 IRQs / second  at real 2 MHz — use a slower speed preset
;         if the terminal scrolls too fast.
;   3. Enables the Timer A IRQ via the ICR ($81 = set-mask + TA bit).
;   4. Starts the timer (CRA = $01) and enables CPU interrupts (CLI).
;   5. Main loop spins forever — every '*' line in the terminal is one IRQ.
;
; IRQ handler:
;   - Reads CIA1 ICR to acknowledge and clear the interrupt flag.
;   - Writes '*' + LF to CHAR_OUT ($F000), which appears as a new line
;     in the Terminal panel.
;   - RTI.
;
; CIA1 register addresses (base $F100):
;   $F104  TALO — Timer A latch low byte
;   $F105  TAHI — Timer A latch high byte
;   $F10D  ICR  — Interrupt Control Register
;   $F10E  CRA  — Control Register A
;
; Load address: $0200  (embedded in .prg header by build.sh)
;
; Build:  ./build.sh
;

CIA1_TALO = $F104
CIA1_TAHI = $F105
CIA1_ICR  = $F10D
CIA1_CRA  = $F10E
CHAR_OUT  = $F000

TIMER_LO  = $00       ; latch = $3000 (12 288 cycles)
TIMER_HI  = $30

        .code

; ============================================================
; IRQ handler  (placed first so its address is known when we
; patch the vector in reset)
; ============================================================
irq_handler:
        pha                     ; save A — PC and P are saved by the CPU

        lda     CIA1_ICR        ; reading ICR clears all flags and de-asserts IRQ
        lda     #'*'
        sta     CHAR_OUT
        lda     #$0A            ; LF — flushes the line in the Terminal panel
        sta     CHAR_OUT

        pla
        rti

; ============================================================
; Reset entry point
; ($FFFC/$FFFD reset vector is seeded to $0200 by Memory::reset)
; ============================================================
reset:
        sei                     ; block IRQs while we set up

        ; ---- Patch IRQ vector at runtime ----
        ; $FFFE/$FFFF are plain RAM (above the I/O window $F000-$FBFF)
        ; so a direct STA works.
        lda     #<irq_handler   ; = $00  (irq_handler is at $0200)
        sta     $FFFE
        lda     #>irq_handler   ; = $02
        sta     $FFFF

        ; ---- Program CIA1 Timer A ----
        lda     #TIMER_LO
        sta     CIA1_TALO
        lda     #TIMER_HI
        sta     CIA1_TAHI       ; writing TAHI also loads counter when stopped

        ; Enable Timer A IRQ: bit 7 = 1 (set), bit 0 = TA source
        lda     #$81
        sta     CIA1_ICR

        ; CRA = $01: start timer, continuous mode
        lda     #$01
        sta     CIA1_CRA

        ; ---- Print startup banner: "CIA1 TIMER\n" ----
        lda     #'C'
        sta     CHAR_OUT
        lda     #'I'
        sta     CHAR_OUT
        lda     #'A'
        sta     CHAR_OUT
        lda     #'1'
        sta     CHAR_OUT
        lda     #' '
        sta     CHAR_OUT
        lda     #'T'
        sta     CHAR_OUT
        lda     #'I'
        sta     CHAR_OUT
        lda     #'M'
        sta     CHAR_OUT
        lda     #'E'
        sta     CHAR_OUT
        lda     #'R'
        sta     CHAR_OUT
        lda     #$0A
        sta     CHAR_OUT

        cli                     ; enable interrupts — CIA1 will now fire IRQs

        ; ---- Spin forever ----
        ; All terminal output is produced by irq_handler above.
loop:
        jmp     loop
