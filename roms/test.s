;
; test.s  —  CIA1 Timer A interrupt test ROM for The 8-Bit Machine
;
; Layout (load address $0200):
;
;   $0200  reset:       entry point (reset vector → here)
;                       sets up CIA1 Timer A and patches the IRQ vector,
;                       then spins in `loop:` forever.
;   <after loop>
;          irq_handler: jumped to by the CPU on every CIA1 Timer A IRQ.
;                       reads CIA1 ICR to acknowledge, writes '*'+LF to
;                       CHAR_OUT ($F000), then RTI.
;
; The IRQ vector ($FFFE/$FFFF) is patched at runtime so it points to
; irq_handler regardless of its exact assembled address.
;
; Timer latch = $3000 (12 288 cycles):
;   ~4.9 IRQs/second at the default 60 kHz debug speed.
;   Use Emulator → Speed → ~60 kHz if output scrolls too fast at 2 MHz.
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
; reset: — entry point ($0200, pointed to by reset vector)
; ============================================================
reset:
        sei                     ; block IRQs while setting up

        ; ---- Patch IRQ vector to point at irq_handler ----
        lda     #<irq_handler
        sta     $FFFE
        lda     #>irq_handler
        sta     $FFFF

        ; ---- Program CIA1 Timer A ----
        lda     #TIMER_LO
        sta     CIA1_TALO
        lda     #TIMER_HI
        sta     CIA1_TAHI       ; writing TAHI loads counter while timer is stopped

        ; Enable Timer A IRQ: bit 7=set-mask, bit 0=TA source
        lda     #$81
        sta     CIA1_ICR

        ; CRA = $01: start timer, continuous mode
        lda     #$01
        sta     CIA1_CRA

        ; ---- Print startup banner ----
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
        lda     #$0A            ; LF — flush "CIA1 TIMER" to terminal
        sta     CHAR_OUT

        cli                     ; enable interrupts — CIA1 IRQs will now fire

; ---- Spin forever; all output comes from irq_handler below ----
loop:
        jmp     loop

; ============================================================
; irq_handler — entered on every CIA1 Timer A underflow
; ============================================================
irq_handler:
        pha                     ; save A (PC and P saved automatically by CPU)

        lda     CIA1_ICR        ; reading ICR clears all flags and de-asserts IRQ
        lda     #'*'
        sta     CHAR_OUT
        lda     #$0A            ; LF — flushes the line in the Terminal panel
        sta     CHAR_OUT

        pla                     ; restore A
        rti
