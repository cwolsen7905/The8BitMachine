;
; hello.s  —  Hello World for The 8-Bit Machine (generic Z80)
;
; Targets the default machine map with Zilog Z80 selected:
;   CHAR_OUT  $F000   write any byte → appears in Terminal panel
;   CIA1      $F100   (unused)
;   RAM       $0000–$FFFF  (catch-all)
;
; How to run:
;   1. Machine Designer → CPU → Zilog Z80
;   2. File → Load ROM → select hello.prg  (loads at $0000)
;   3. F8 reset, F5 run
;
; What it does:
;   Prints a banner, counts 00–FF in hex via CHAR_OUT, then halts.
;

        org     $0000

; ---------------------------------------------------------------
; Reset — Z80 starts execution here
; ---------------------------------------------------------------
start:
        di                              ; interrupts off

        ld      hl, msg_banner
        call    puts

        ; count 00–FF, printing "XX\n" for each
        ld      b, 0
count_loop:
        ld      a, b
        call    print_hex_byte
        ld      a, $0A                  ; newline
        ld      ($F000), a
        inc     b
        jr      nz, count_loop          ; wraps 255→0 then exits

        ld      hl, msg_done
        call    puts

.halt:
        halt
        jr      .halt

; ---------------------------------------------------------------
; puts — print null-terminated string at HL to CHAR_OUT
; ---------------------------------------------------------------
puts:
        ld      a, (hl)
        or      a
        ret     z
        ld      ($F000), a
        inc     hl
        jr      puts

; ---------------------------------------------------------------
; print_hex_byte — print A as two uppercase hex digits to CHAR_OUT
; ---------------------------------------------------------------
print_hex_byte:
        push    af
        rrca
        rrca
        rrca
        rrca
        call    print_hex_nib
        pop     af
        ; fall through — print low nibble

print_hex_nib:
        and     $0F
        cp      $0A
        jr      c, .digit
        add     a, 'A' - $0A
        ld      ($F000), a
        ret
.digit:
        add     a, '0'
        ld      ($F000), a
        ret

; ---------------------------------------------------------------
; Strings
; ---------------------------------------------------------------
msg_banner:
        defb    "The 8-Bit Machine -- Z80 Hello World", $0A
        defb    "Counting 00-FF via CHAR_OUT:", $0A, 0

msg_done:
        defb    $0A
        defm    "Done - CPU halted"
        defb    $0A, 0
