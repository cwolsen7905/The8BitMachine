#!/usr/bin/env python3
"""
make_d64.py — Create a .d64 disk image from one or more .prg files

Usage:
    python3 make_d64.py -o output.d64 -n "DISK NAME" file1.prg [file2.prg ...]

Each PRG file is stored as a separate directory entry.  The filename used
in the directory is the stem of the input filename (uppercased, max 16 chars).
The first two bytes of the PRG are the load address (not stored separately —
they are the first bytes of the file data as per CBM convention).

D64 geometry (standard 35-track, no error bytes = 174848 bytes):
    Tracks  1–17:  21 sectors each
    Tracks 18–24:  19 sectors each
    Tracks 25–30:  18 sectors each
    Tracks 31–35:  17 sectors each
    Sector size:   256 bytes
    Total sectors: 683
    Directory:     Track 18, sector 1+ (sector 0 = BAM)
"""

import sys
import struct
import argparse
from pathlib import Path

# ── D64 geometry ─────────────────────────────────────────────────────────────

SECTORS_PER_TRACK = (
    [0]           # 1-indexed; index 0 unused
    + [21]*17     # tracks  1–17
    + [19]*7      # tracks 18–24
    + [18]*6      # tracks 25–30
    + [17]*5      # tracks 31–35
)

TOTAL_TRACKS = 35
DIR_TRACK    = 18
BAM_SECTOR   = 0
DIR_SECTOR   = 1


def sector_offset(track: int, sector: int) -> int:
    off = sum(SECTORS_PER_TRACK[1:track]) + sector
    return off * 256


def make_empty_d64() -> bytearray:
    total = sum(SECTORS_PER_TRACK[1:TOTAL_TRACKS+1]) * 256
    return bytearray(total)


# ── BAM ──────────────────────────────────────────────────────────────────────

def init_bam(img: bytearray, disk_name: str, disk_id: str = "00"):
    """Write BAM sector at track 18, sector 0.  All data sectors marked free."""
    off = sector_offset(DIR_TRACK, BAM_SECTOR)

    # First dir sector link
    img[off + 0] = DIR_TRACK
    img[off + 1] = DIR_SECTOR
    img[off + 2] = 0x41   # DOS version 'A'
    img[off + 3] = 0x00

    # BAM entries: 4 bytes per track (free count + 3 bitmap bytes)
    for t in range(1, TOTAL_TRACKS + 1):
        n = SECTORS_PER_TRACK[t]
        bam_off = off + 4 + (t - 1) * 4
        # Build bitmap: sectors 0..n-1 all free
        bits = (1 << n) - 1
        img[bam_off + 0] = n
        img[bam_off + 1] = bits & 0xFF
        img[bam_off + 2] = (bits >> 8) & 0xFF
        img[bam_off + 3] = (bits >> 16) & 0xFF

    # Mark track 18 sectors 0 and 1 as used (BAM + first dir sector)
    _mark_sector_used(img, DIR_TRACK, BAM_SECTOR)
    _mark_sector_used(img, DIR_TRACK, DIR_SECTOR)

    # Disk name (16 bytes, PETSCII, padded with 0xA0)
    name_bytes = _to_petscii(disk_name, 16)
    img[off + 0x90: off + 0x90 + 16] = name_bytes

    # Shift (0xA0) + disk ID (2 chars) + 0xA0 + "2A"
    img[off + 0xA0] = 0xA0
    id_bytes = _to_petscii(disk_id, 2)
    img[off + 0xA1: off + 0xA3] = id_bytes
    img[off + 0xA3] = 0xA0
    img[off + 0xA4] = 0x32   # '2'
    img[off + 0xA5] = 0x41   # 'A'


def _mark_sector_used(img: bytearray, track: int, sector: int):
    bam_off = sector_offset(DIR_TRACK, BAM_SECTOR) + 4 + (track - 1) * 4
    free = img[bam_off + 0]
    bits = img[bam_off + 1] | (img[bam_off + 2] << 8) | (img[bam_off + 3] << 16)
    if bits & (1 << sector):
        bits &= ~(1 << sector)
        free -= 1
        img[bam_off + 0] = free
        img[bam_off + 1] = bits & 0xFF
        img[bam_off + 2] = (bits >> 8) & 0xFF
        img[bam_off + 3] = (bits >> 16) & 0xFF


def _alloc_sector(img: bytearray) -> tuple[int, int]:
    """Find and allocate the next free sector (skipping track 18)."""
    for t in range(1, TOTAL_TRACKS + 1):
        if t == DIR_TRACK:
            continue
        bam_off = sector_offset(DIR_TRACK, BAM_SECTOR) + 4 + (t - 1) * 4
        bits = img[bam_off + 1] | (img[bam_off + 2] << 8) | (img[bam_off + 3] << 16)
        for s in range(SECTORS_PER_TRACK[t]):
            if bits & (1 << s):
                _mark_sector_used(img, t, s)
                return t, s
    raise RuntimeError("Disk full — no free sectors")


# ── PETSCII helpers ───────────────────────────────────────────────────────────

def _to_petscii(text: str, length: int, pad: int = 0xA0) -> bytearray:
    """Convert ASCII string to PETSCII uppercase, padded to length bytes."""
    out = bytearray(length)
    out[:] = bytes([pad] * length)
    for i, c in enumerate(text[:length]):
        uc = c.upper()
        # ASCII uppercase A–Z = PETSCII uppercase A–Z (same codes 0x41–0x5A)
        out[i] = ord(uc) & 0xFF
    return out


# ── File writer ───────────────────────────────────────────────────────────────

def write_file(img: bytearray, data: bytes) -> tuple[int, int, int]:
    """
    Write file data into the image.
    Returns (first_track, first_sector, block_count).
    """
    chunks = [data[i:i+254] for i in range(0, len(data), 254)]
    block_count = len(chunks)

    sectors = [_alloc_sector(img) for _ in chunks]

    for i, (chunk, (t, s)) in enumerate(zip(chunks, sectors)):
        off = sector_offset(t, s)
        if i + 1 < len(sectors):
            next_t, next_s = sectors[i + 1]
            img[off + 0] = next_t
            img[off + 1] = next_s
            img[off + 2: off + 2 + len(chunk)] = chunk
            # Pad remainder with 0x00
        else:
            img[off + 0] = 0x00
            img[off + 1] = len(chunk) + 1   # last-sector byte count (1-based)
            img[off + 2: off + 2 + len(chunk)] = chunk

    return sectors[0][0], sectors[0][1], block_count


# ── Directory ─────────────────────────────────────────────────────────────────

class Directory:
    def __init__(self, img: bytearray):
        self.img = img
        self.entries: list[dict] = []
        # First dir sector already allocated; chain more as needed
        self._sectors = [(DIR_TRACK, DIR_SECTOR)]
        self._init_dir_sector(DIR_TRACK, DIR_SECTOR, next_t=0, next_s=0xFF)

    def _init_dir_sector(self, t, s, next_t, next_s):
        off = sector_offset(t, s)
        self.img[off + 0] = next_t
        self.img[off + 1] = next_s
        # Zero all 8 directory entries
        for e in range(8):
            self.img[off + 2 + e * 32: off + 2 + (e + 1) * 32] = bytes(32)

    def add_entry(self, name: str, file_type: int, track: int, sector: int,
                  blocks: int):
        slot = len(self.entries)
        if slot >= len(self._sectors) * 8:
            # Need another directory sector
            new_t, new_s = _alloc_sector(self.img)
            prev_t, prev_s = self._sectors[-1]
            prev_off = sector_offset(prev_t, prev_s)
            self.img[prev_off + 0] = new_t
            self.img[prev_off + 1] = new_s
            self._init_dir_sector(new_t, new_s, 0, 0xFF)
            self._sectors.append((new_t, new_s))

        sec_idx = slot // 8
        ent_idx = slot % 8
        dt, ds = self._sectors[sec_idx]
        off = sector_offset(dt, ds) + 2 + ent_idx * 32

        self.img[off + 0]  = file_type | 0x80   # closed PRG
        self.img[off + 1]  = track
        self.img[off + 2]  = sector
        name_bytes = _to_petscii(name, 16)
        self.img[off + 3: off + 19] = name_bytes
        # Bytes 19–28: side-sector / rel info (unused for PRG) — leave zero
        self.img[off + 28] = blocks & 0xFF
        self.img[off + 29] = (blocks >> 8) & 0xFF

        self.entries.append({"name": name, "track": track, "sector": sector})


# ── Main ──────────────────────────────────────────────────────────────────────

def build_d64(prg_files: list[Path], disk_name: str, disk_id: str) -> bytearray:
    img = make_empty_d64()
    init_bam(img, disk_name, disk_id)
    directory = Directory(img)

    for prg_path in prg_files:
        data = prg_path.read_bytes()
        if len(data) < 2:
            print(f"Warning: {prg_path} is too small, skipping", file=sys.stderr)
            continue

        entry_name = prg_path.stem[:16].upper()
        t, s, blocks = write_file(img, data)
        directory.add_entry(entry_name, 0x82, t, s, blocks)   # 0x82 = PRG
        print(f"  Added {entry_name:16s}  {blocks:3d} blocks  "
              f"(track {t:2d}, sector {s:2d})")

    return img


def main():
    ap = argparse.ArgumentParser(description="Create a .d64 disk image from PRG files")
    ap.add_argument("-o", "--output",    required=True, help="Output .d64 file")
    ap.add_argument("-n", "--name",      default="TEST DISK",
                    help="Disk name (max 16 chars)")
    ap.add_argument("-i", "--id",        default="00",
                    help="Disk ID (2 chars, default '00')")
    ap.add_argument("prg_files", nargs="+", help="PRG files to include")
    args = ap.parse_args()

    prg_paths = [Path(p) for p in args.prg_files]
    for p in prg_paths:
        if not p.exists():
            print(f"Error: {p} not found", file=sys.stderr)
            sys.exit(1)

    print(f"Building {args.output}  disk='{args.name}'  id='{args.id}'")
    img = build_d64(prg_paths, args.name, args.id)

    Path(args.output).write_bytes(img)
    print(f"Wrote {len(img)} bytes → {args.output}")


if __name__ == "__main__":
    main()
