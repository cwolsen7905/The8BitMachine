#!/usr/bin/env python3
"""
basic_prg.py — C64 BASIC tokenizer

Reads a plain-text BASIC source file and writes a .prg binary with a
2-byte load-address header ($01 $08) followed by tokenized BASIC lines.

Usage:
    python3 basic_prg.py input.bas output.prg [--load-addr 0x0801]

Source format:
    <line-number> <statement>
    10 PRINT "HELLO"
    20 FOR I=1 TO 5
    30 PRINT I
    40 NEXT I
    50 END

Supports all standard C64 BASIC V2 keywords.
"""

import sys
import struct
import argparse

# ── C64 BASIC V2 keyword table (token → byte) ─────────────────────────────
# Reference: C64 BASIC V2 token table, $0080–$00CB
KEYWORDS = {
    # Statements
    "END":      0x80, "FOR":      0x81, "NEXT":     0x82, "DATA":     0x83,
    "INPUT#":   0x84, "INPUT":    0x85, "DIM":      0x86, "READ":     0x87,
    "LET":      0x88, "GOTO":     0x89, "RUN":      0x8A, "IF":       0x8B,
    "RESTORE":  0x8C, "GOSUB":    0x8D, "RETURN":   0x8E, "REM":      0x8F,
    "STOP":     0x90, "ON":       0x91, "WAIT":     0x92, "LOAD":     0x93,
    "SAVE":     0x94, "VERIFY":   0x95, "DEF":      0x96, "POKE":     0x97,
    "PRINT#":   0x98, "PRINT":    0x99, "CONT":     0x9A, "LIST":     0x9B,
    "CLR":      0x9C, "CMD":      0x9D, "SYS":      0x9E, "OPEN":     0x9F,
    "CLOSE":    0xA0, "GET":      0xA1, "NEW":      0xA2, "TAB(":     0xA3,
    "TO":       0xA4, "FN":       0xA5, "SPC(":     0xA6, "THEN":     0xA7,
    "NOT":      0xA8, "STEP":     0xA9,
    # Operators ($AA–$B3) — all tokenized; stored as token bytes, never ASCII
    "+":        0xAA, "-":        0xAB, "*":        0xAC, "/":        0xAD,
    "^":        0xAE, "AND":      0xAF, "OR":       0xB0,
    ">":        0xB1, "=":        0xB2, "<":        0xB3,
    # Math / string functions ($B4–$CB)
    "SGN":      0xB4, "INT":      0xB5, "ABS":      0xB6, "USR":      0xB7,
    "FRE":      0xB8, "POS":      0xB9, "SQR":      0xBA, "RND":      0xBB,
    "LOG":      0xBC, "EXP":      0xBD, "COS":      0xBE, "SIN":      0xBF,
    "TAN":      0xC0, "ATN":      0xC1, "PEEK":     0xC2, "LEN":      0xC3,
    "STR$":     0xC4, "VAL":      0xC5, "ASC":      0xC6, "CHR$":     0xC7,
    "LEFT$":    0xC8, "RIGHT$":   0xC9, "MID$":     0xCA, "GO":       0xCB,
}

# Keywords sorted longest-first so longer tokens match before shorter ones
_KW_SORTED = sorted(
    [(k, v) for k, v in KEYWORDS.items() if v is not None],
    key=lambda x: -len(x[0])
)


def tokenize_line(text: str) -> bytes:
    """Tokenize one BASIC line body (everything after the line number)."""
    out = bytearray()
    i = 0
    in_string = False
    in_rem = False

    while i < len(text):
        c = text[i]

        # Inside a string literal — pass through verbatim
        if in_string:
            out.append(ord(c) & 0xFF)
            if c == '"':
                in_string = False
            i += 1
            continue

        # After REM — pass through rest of line verbatim
        if in_rem:
            out.append(ord(c) & 0xFF)
            i += 1
            continue

        if c == '"':
            in_string = True
            out.append(ord(c))
            i += 1
            continue

        # Try to match a keyword at position i
        matched = False
        for kw, token in _KW_SORTED:
            if text[i:i+len(kw)].upper() == kw:
                out.append(token)
                i += len(kw)
                if kw == "REM":
                    in_rem = True
                matched = True
                break

        if not matched:
            out.append(ord(c) & 0xFF)
            i += 1

    return bytes(out)


def tokenize(source: str, load_addr: int = 0x0801) -> bytes:
    """Tokenize a full BASIC program and return PRG bytes (with load addr header)."""
    lines = []
    for raw in source.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        parts = raw.split(None, 1)
        if not parts[0].isdigit():
            continue
        line_num = int(parts[0])
        body = parts[1] if len(parts) > 1 else ""
        lines.append((line_num, tokenize_line(body)))

    # Build the BASIC program in memory
    # Each line: [next_lo][next_hi][linenum_lo][linenum_hi][tokens...][0x00]
    addr = load_addr
    segments = []
    for line_num, tokens in lines:
        line_size = 2 + 2 + len(tokens) + 1   # next_ptr + linenum + tokens + null
        next_addr = addr + line_size
        seg = struct.pack('<HH', next_addr, line_num) + tokens + b'\x00'
        segments.append(seg)
        addr += line_size

    program = b''.join(segments) + b'\x00\x00'
    return struct.pack('<H', load_addr) + program


def main():
    ap = argparse.ArgumentParser(description="C64 BASIC tokenizer → .prg")
    ap.add_argument("input",  help="Input .bas source file")
    ap.add_argument("output", help="Output .prg file")
    ap.add_argument("--load-addr", default="0x0801",
                    help="Load address (default 0x0801)")
    args = ap.parse_args()

    load_addr = int(args.load_addr, 0)
    with open(args.input, "r") as f:
        source = f.read()

    prg = tokenize(source, load_addr)

    with open(args.output, "wb") as f:
        f.write(prg)

    print(f"Wrote {len(prg)} bytes to {args.output}  (load addr ${load_addr:04X})")


if __name__ == "__main__":
    main()
