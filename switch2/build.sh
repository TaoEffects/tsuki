#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-switch2"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++"
AS="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc"
ELF2NRO="$DEVKITPRO/tools/bin/elf2nro"
NACPTOOL="$DEVKITPRO/tools/bin/nacptool"

mkdir -p "$BUILD_DIR/obj"

CXXFLAGS=(
    -O2 -g0
    -std=c++20
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
    -fPIE -mtp=soft -mcpu=cortex-a78c
    -D__SWITCH__ -D__SWITCH2__ -D_GNU_SOURCE
    -I"$ROOT_DIR/src"
    -I"$ROOT_DIR/third_party"
    -I"$DEVKITPRO/libounce/include"
    -I"$DEVKITPRO/portlibs/switch2/include"
)

LDFLAGS=(
    -specs="$DEVKITPRO/libounce/switch.specs"
    -fPIE -mcpu=cortex-a78c
    -L"$DEVKITPRO/portlibs/switch2/lib"
    -L"$DEVKITPRO/libounce/lib"
)

LIBS=(
    -lEGL -lGLESv2 -louncevk-gl -lvulkan
    -lnx -lsysbase -lstdc++ -lc -lm -lgcc
)

echo "[AS]  src/assets_fonts.S"
(cd "$ROOT_DIR" && "$AS" -c src/assets_fonts.S -o "$BUILD_DIR/obj/assets_fonts.o")
OBJS=("$BUILD_DIR/obj/assets_fonts.o")

SOURCES=(
    src/main.cpp
    src/platform_switch2.cpp
    src/audio.cpp
    src/gfx.cpp
    src/nro_scanner.cpp
    src/ftp_server.cpp
    src/file_manager.cpp
    src/app.cpp
)

for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/obj/$(basename "$src" .cpp).o"
    echo "[CXX] $src"
    "$CXX" "${CXXFLAGS[@]}" -c "$ROOT_DIR/$src" -o "$obj"
    OBJS+=("$obj")
done

echo "[LD]  tsuki.elf"
"$CXX" "${LDFLAGS[@]}" "${OBJS[@]}" "${LIBS[@]}" -o "$BUILD_DIR/tsuki.elf"

echo "[NACP] tsuki.nacp"
"$NACPTOOL" --create "Tsuki" "TaoEffects" "1.0.0" "$BUILD_DIR/tsuki.nacp"

ICON_ARG=()
if [ -f "$ROOT_DIR/switch2/icon.jpg" ]; then
    ICON_ARG=(--icon="$ROOT_DIR/switch2/icon.jpg")
fi

echo "[NRO] tsuki.nro (flags=0, NO --alignedheader)"
"$ELF2NRO" "$BUILD_DIR/tsuki.elf" "$BUILD_DIR/tsuki.nro" \
    --nacp="$BUILD_DIR/tsuki.nacp" \
    "${ICON_ARG[@]}"

python3 - "$BUILD_DIR/tsuki.nro" <<'PY'
import struct, sys, hashlib
p = sys.argv[1]
data = open(p, "rb").read()
magic = data[0x10:0x14]
flags = struct.unpack_from("<I", data, 0x1c)[0]
sha = hashlib.sha256(data).hexdigest()
print(f"[VERIFY] {p}: size={len(data)} magic={magic} flags={flags} sha256={sha}")
assert magic == b"NRO0", f"Bad NRO magic: {magic}"
assert flags == 0, f"Unexpected NRO flags={flags} (must be 0!)"
PY
