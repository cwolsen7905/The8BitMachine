# CBM IEC Serial Bus — Protocol Specification

Reference for the Drive1541 software state machine. Grounded in
VICE 3.10 `src/serial/serial-iec-device.c` and the Commodore
service manuals.

---

## 1. Signal Conventions

The IEC bus uses open-collector wired-AND logic: any device can pull a
line LOW; the line idles HIGH via pull-up resistors. No device can
actively drive HIGH.

| Symbol | Value | Meaning |
|--------|-------|---------|
| `true`  | HIGH | Released (not asserted) |
| `false` | LOW  | Asserted |

In code, `IECLines { atn, clk, data }` maps directly to this convention.

### CIA2 Inversion

The C64's CIA2 Port A controls the IEC lines with inverted polarity:

| CIA2 bit | Direction | Polarity |
|----------|-----------|---------|
| PA3 (ATN-out)  | output | bit=1 → bus LOW (assert ATN) |
| PA4 (CLK-out)  | output | bit=1 → bus LOW (assert CLK) |
| PA5 (DATA-out) | output | bit=1 → bus LOW (assert DATA) |
| PA6 (CLK-in)   | input  | bit=1 → bus HIGH (CLK released) |
| PA7 (DATA-in)  | input  | bit=1 → bus HIGH (DATA released) |

---

## 2. Timing Reference

All timings derived from VICE `serial-iec-device.c` `US2CYCLES()` values
at 1 MHz (1 cycle = 1 µs).

| Event | µs | Cycles |
|-------|----|--------|
| ATN stabilization (PRE0 hold) | 100 | 100 |
| Talker ready-delay (CLK=0 hold before CLK=1) | 80 | 80 |
| EOI timeout — listener waits for CLK↓ before declaring EOI | 200 | 200 |
| EOI acknowledgment pulse (listener DATA=0) | 60 | 60 |
| Bit CLK-low period (talker: CLK=0, DATA=bit value) | 60 | 60 |
| Bit CLK-high period (talker: CLK=1, listener samples) | 60 | 60 |
| Frame-ACK timeout (talker waits for listener DATA=0) | 1000 | 1000 |

---

## 3. ATN Sequence (Host Commands All Devices)

```
Host:  ATN LOW
All devices: DATA LOW within 1 ms ("I am here")

Host sends command bytes via CLK/DATA handshake (host is talker):
  PRE1: wait for CLK=0
  PRE2: wait for CLK=1 (ready-to-send); device sets DATA=1 (ready-for-data)
  READY: wait 200 µs; if no CLK↓ → EOI; if CLK↓ → receive bits
  BITn: wait CLK=1, sample DATA
  BITnw: wait CLK=0
  After bit 7 (CLK=0): device sets DATA=0 (frame ACK); repeat for next byte

Host: ATN HIGH (addressing complete)
Addressed device:
  LISTEN (0x20+dev): enter listener mode
  TALK   (0x40+dev): enter talker mode

Command bytes:
  0x20+dev  LISTEN    0x40+dev  TALK
  0x3F      UNLISTEN  0x5F      UNTALK
  0x60+ch   OPEN/DATA (secondary address)
  0x70+ch   CLOSE     0xE0+ch   CLOSE
  0xF0+ch   OPEN
```

---

## 4. TALK Sequence (Drive → C64)

This is the path taken by `LOAD"*",8,1`. The drive is the talker;
the C64 KERNAL ACPTR routine is the listener.

### 4.1 Role Reversal (once, after ATN)

```
ATN rises (host done addressing):
  Drive: CLK=0  ("I am now the talker; the bus has changed hands")
  Host (KERNAL $EE18): waits for CLK=1 before proceeding
  Host: releases CLK (CIA2 PA4=0)
  Drive: sees CLK released → after 80 µs, signals "ready-to-send"
```

### 4.2 Per-Byte Loop

Called by KERNAL ACPTR ($EE13) once per byte:

```
Step 1 — Ready-to-send
  Drive: CLK=1, DATA=1  ("I am ready to send a byte")

Step 2 — Ready-for-data  
  Host (KERNAL): releases DATA  (DATA=1)
  Drive: waits for DATA=1

Step 3 — EOI signaling (LAST byte only)
  Drive: does NOT pull CLK=0 after Step 2
  Host: if CLK stays HIGH >200 µs → CIA1 Timer B fires → EOI detected
  Host: pulls DATA=0 for 60 µs (EOI acknowledgment)
  Drive: sees DATA=0 → waits for DATA=1 (end of EOI ack) → proceed to Step 4

Step 4 — Send 8 bits (LSB first, bits 0–7)
  For each bit n (0..7):
    Drive: CLK=0, DATA=bit_n,  hold 60 µs
    Drive: CLK=1,              hold 60 µs  (host samples DATA on CLK rising edge)

Step 5 — Frame acknowledgment (EVERY byte, including last)
  Drive: CLK=0, DATA=1  ("byte complete")
  Host (KERNAL): pulls DATA=0 within 1000 µs  (frame ACK)
  Drive: sees DATA=0 from host
    • More bytes remain  → go to Step 1 (next byte)
    • This was last byte → release CLK=1 DATA=1, exit talking mode
```

> **Critical**: Step 5 (frame ACK wait) is required after EVERY byte.
> Skipping it causes the KERNAL to be in a frame-ACK state while the
> drive has already started sending the next byte — resulting in the
> load hanging permanently.

### 4.3 KERNAL ACPTR Internals (C64 ROM $EE13)

```
$EE13  SEI
$EE14  release CLK (CIA2 PA4=0)
$EE18  wait for CLK=1  (drive's "ready-to-send" signal)
$EE1B  start CIA1 Timer B, latch=$01FF (511 cycles), one-shot
$EE30  poll loop:
          read CIA1 ICR → if Timer B bit set → EOI path ($EE3E)
          read CIA2 PA  → if CLK=0           → bit-receive ($EE56)
          else loop
$EE56  bit-receive loop (8 iterations):
          $EE5A  wait CLK=1   (drive set DATA valid)
          $EE65  sample DATA  (ASL / ROR into A)
          $EE67  wait CLK=0   (BMI $EE67)
          $EE72  DEC bit counter
          $EE74  BNE $EE5A
       (exits after 8 bits)
$EE75+ pull DATA=0  (frame ACK to drive)
       release DATA
       return received byte in A
```

---

## 5. LISTEN Sequence (C64 → Drive)

Used for OPEN (filename), SAVE, and command channel writes.
The C64 is the talker; the drive is the listener.

```
After ATN LISTEN (0x20+dev) + secondary address:
  ATN rises
  Drive: keeps DATA=0 ("I am here")

Per-byte from host:
  PRE1: drive waits for CLK=0
  PRE2: drive waits for CLK=1; drive sets DATA=1 (ready-for-data)
  READY: drive waits 200 µs for CLK↓; if timeout → EOI detected
  BITn:  wait CLK=1, sample DATA
  BITnw: wait CLK=0
  After bit 7: drive sets DATA=0 (frame ACK); repeat for next byte

UNLISTEN (0x3F):
  ATN sequence with 0x3F; drive processes filename/command from rxBuf_
```

---

## 6. Drive1541 State Machine — Mapping to Protocol

### Talker states

| VICE state | Drive1541::State | Step |
|------------|------------------|------|
| P_PRE0 (initial) | `TalkStart` | role-reversal CLK=0 |
| P_PRE1 | `TalkWaitClkHigh` | ready-to-send CLK=1 |
| P_READY | `TalkWaitHostDataHigh` | wait DATA=1 |
| P_EOI / P_EOIw | `TalkEOI` | keep CLK=1, wait DATA=0 ack |
| P_BITn | `TalkSendBit` | CLK=0, set DATA, 60 cycles |
| P_BITnw | `TalkHoldBit` | CLK=1, 60 cycles |
| P_DONE0 + P_DONE1 | `TalkByteACK` | CLK=0, DATA=1, wait DATA=0 |

`TalkNormalReady` is a brief hold state for `kNormalReadyCycles` (50 cycles)
before `TalkWaitClkHigh` releases CLK — this corresponds to the 80 µs
ready-delay in VICE's P_PRE1 (only needed at the start of each new
"ready-to-send" cycle, not after the initial role reversal).

### Listener states

| VICE state | Drive1541::State |
|------------|------------------|
| P_PRE0 (ATN fall) | `AtnWaitClkLow` / `AtnWaitClkHigh` |
| P_PRE2 | `ListenWaitClkHigh` |
| P_BITn / P_BITnw | `ListenReceiveBit` |
| Frame ACK (DATA=0) | `AtnBitSettle` / `ListenBitSettle` |

---

## 7. Common Mistakes

1. **Missing frame ACK wait (Step 5)** — talker must wait for listener
   DATA=0 after every byte, including the last. Without this, the host
   has no chance to transition between bytes cleanly.

2. **Pulling CLK=0 before entering EOI hold** — if the talker pulls
   CLK=0 before keeping CLK=1 for the 200 µs EOI timeout, the KERNAL
   exits its poll loop ($EE30) into bit-receive mode immediately, missing
   EOI detection entirely.

3. **Bit timing too short** — the KERNAL's bit-receive loop takes ~15
   cycles per iteration. CLK periods shorter than ~30 cycles risk the
   KERNAL sampling in the wrong window.

4. **Not resetting drive state on host reset** — if the drive stays in
   a mid-transfer state after the C64 resets, subsequent loads will
   deadlock immediately.
