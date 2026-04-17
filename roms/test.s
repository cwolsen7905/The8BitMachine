;
; test.s  —  VIC colour-cycle + CIA1 Timer A interrupt demo
;            for The 8-Bit Machine
;
; Layout (load address $0200):
;
;   $0200  reset:       entry point
;                       · patches IRQ vector
;                       · sets VIC initial colours (blue bg, light-blue border)
;                       · programs CIA1 Timer A
;                       · prints banner then spins in loop:
;   <after loop>
;          irq_handler: CIA1 Timer A IRQ handler
;                       · advances BG colour through all 16 VIC palette entries
;                       · writes the colour index as a hex digit to CHAR_OUT
;
; Timer latch = $3000 (12 288 cycles):
;   ~4.9 colour changes/second at the 60 kHz debug speed.
;   Raise speed to ~500 kHz or ~1 MHz to make it strobe faster.
;
; Zero page:
;   $00  COLOR_CTR — current VIC background colour index (0–15)
;

VIC_CTRL1  = $D011   ; control register 1 (DEN, RSEL, YSCROLL …)
VIC_BORDER = $D020   ; border colour
VIC_BG0    = $D021   ; background colour 0

CIA1_TALO  = $F104
CIA1_TAHI  = $F105
CIA1_ICR   = $F10D
CIA1_CRA   = $F10E
CHAR_OUT   = $F000

COLOR_CTR  = $00     ; zero-page counter — current bg colour index

TIMER_LO   = $00     ; latch = $3000 (12 288 cycles)
TIMER_HI   = $30

        .code

; ============================================================
; reset — entry point ($0200, pointed to by reset vector)
; ============================================================
reset:
        sei                     ; block IRQs during setup

        ; ---- Patch IRQ vector to point at irq_handler ----
        lda     #<irq_handler
        sta     $FFFE
        lda     #>irq_handler
        sta     $FFFF

        ; ---- Initialise VIC-IIe colours ----
        lda     #$0E            ; light blue border
        sta     VIC_BORDER
        lda     #$06            ; blue background (palette index 6)
        sta     VIC_BG0
        sta     COLOR_CTR       ; keep counter in sync

        ; ---- Program CIA1 Timer A ----
        lda     #TIMER_LO
        sta     CIA1_TALO
        lda     #TIMER_HI
        sta     CIA1_TAHI       ; writing TAHI loads counter while stopped

        ; Enable Timer A IRQ: bit 7 = set mask, bit 0 = TA source
        lda     #$81
        sta     CIA1_ICR

        ; CRA = $01: start timer, continuous mode
        lda     #$01
        sta     CIA1_CRA

        ; ---- Print startup banner: "VIC+CIA COLOUR DEMO\n" ----
        lda     #'V'
        sta     CHAR_OUT
        lda     #'I'
        sta     CHAR_OUT
        lda     #'C'
        sta     CHAR_OUT
        lda     #'+'
        sta     CHAR_OUT
        lda     #'C'
        sta     CHAR_OUT
        lda     #'I'
        sta     CHAR_OUT
        lda     #'A'
        sta     CHAR_OUT
        lda     #' '
        sta     CHAR_OUT
        lda     #'C'
        sta     CHAR_OUT
        lda     #'O'
        sta     CHAR_OUT
        lda     #'L'
        sta     CHAR_OUT
        lda     #'O'
        sta     CHAR_OUT
        lda     #'U'
        sta     CHAR_OUT
        lda     #'R'
        sta     CHAR_OUT
        lda     #' '
        sta     CHAR_OUT
        lda     #'D'
        sta     CHAR_OUT
        lda     #'E'
        sta     CHAR_OUT
        lda     #'M'
        sta     CHAR_OUT
        lda     #'O'
        sta     CHAR_OUT
        lda     #$0A
        sta     CHAR_OUT        ; LF — flush line to Terminal

        cli                     ; enable interrupts — CIA1 IRQs will now fire

; ---- Spin forever; all output comes from irq_handler ----
loop:
        jmp     loop

; ============================================================
; irq_handler — entered on every CIA1 Timer A underflow
;
; Advances the VIC background colour by one step and reports
; the new colour index as a hex digit in the Terminal panel.
; ============================================================
irq_handler:
        pha                     ; save A (PC/P saved automatically)

        lda     CIA1_ICR        ; reading ICR clears flags + de-asserts IRQ

        ; Advance background colour (0 → 1 → … → 15 → 0)
        lda     COLOR_CTR
        clc
        adc     #1
        and     #$0F            ; wrap at 16
        sta     COLOR_CTR
        sta     VIC_BG0         ; update VIC register — visible immediately

        ; Write colour index as ASCII hex digit to Terminal
        cmp     #$0A
        bcc     digit           ; 0–9: add '0'
        adc     #$06            ; A–F: skip gap between '9' and 'A' (carry set)
digit:  adc     #'0'
        sta     CHAR_OUT
        lda     #$0A
        sta     CHAR_OUT        ; LF — flush

        pla                     ; restore A
        rti
