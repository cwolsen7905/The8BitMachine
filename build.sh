#!/usr/bin/env bash
set -e

# ---------------------------------------------------------------------------
# Prerequisites (macOS):
#   brew install cmake sdl2 cc65
# ---------------------------------------------------------------------------

BUILD_TYPE=${1:-Debug}
BUILD_DIR="build"

# ---------------------------------------------------------------------------
# Step 1 — Assemble ROMs  (requires cc65: brew install cc65)
# ---------------------------------------------------------------------------
echo "==> Assembling ROMs..."

if command -v ca65 >/dev/null 2>&1 && command -v ld65 >/dev/null 2>&1; then
    mkdir -p "${BUILD_DIR}/roms"

    for ASM_SRC in roms/*.s; do
        BASE="$(basename "${ASM_SRC}" .s)"
        OBJ="${BUILD_DIR}/roms/${BASE}.o"
        CFG="roms/${BASE}.cfg"
        RAW="${BUILD_DIR}/roms/${BASE}.raw"
        PRG="roms/${BASE}.prg"

        if [ ! -f "${CFG}" ]; then
            echo "    [skip] ${ASM_SRC} — no matching ${CFG}"
            continue
        fi

        echo "    ${ASM_SRC} → ${PRG}"
        ca65 -o "${OBJ}" "${ASM_SRC}"
        ld65 -C "${CFG}" -o "${RAW}" "${OBJ}"

        # Prepend 2-byte little-endian load address ($0200) to make a .prg
        { printf '\x00\x02'; cat "${RAW}"; } > "${PRG}"
    done
else
    echo "    [skip] ca65/ld65 not found — install with: brew install cc65"
    echo "           ROM files in roms/*.prg will not be rebuilt."
fi

# ---------------------------------------------------------------------------
# Step 2 — Build the emulator
# ---------------------------------------------------------------------------
echo ""
echo "==> Configuring (${BUILD_TYPE})..."
cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "==> Building..."
cmake --build "${BUILD_DIR}" --parallel "$(sysctl -n hw.logicalcpu 2>/dev/null || nproc)"

echo ""
echo "==> Done."
echo ""
echo "    Run emulator:   ./${BUILD_DIR}/the-8-bit-machine"
echo "    Load test ROM:  File → Load ROM → roms/test.prg"
