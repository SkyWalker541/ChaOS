#!/bin/sh
# ChaOS Pad Test, a port for testing the pad (tools/padtest/chaos-padtest.c):
# a zip to unpack into the card's Roms/Ports, as a port's zip is. Not part of
# ChaOS's image; for testing a device, the Brick Pro first.
#
#   tools/build-padtest.sh SYSROOT OUT.zip
set -e
SYSROOT=$(cd "${1:?usage: $0 SYSROOT OUT.zip}" && pwd)
OUT=${2:?usage: $0 SYSROOT OUT.zip}
case "$OUT" in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/chaospadtest"
aarch64-linux-gnu-gcc --sysroot="$SYSROOT" -O2 -Wall -Wextra -I"$SYSROOT/usr/include/SDL2" -D_REENTRANT \
	-o "$W/chaospadtest/chaos-padtest" "$ROOT/tools/padtest/chaos-padtest.c" \
	-L"$SYSROOT/usr/lib" -Wl,-rpath-link,"$SYSROOT/usr/lib" -lSDL2 -lSDL2_ttf -lm
aarch64-linux-gnu-strip "$W/chaospadtest/chaos-padtest"
cp "$ROOT/tools/padtest/padtest.gptk" "$W/chaospadtest/"
cp "$ROOT/res/fonts/menu.ttf" "$W/chaospadtest/font.ttf"
cp "$ROOT/res/fonts/OFL.txt" "$W/chaospadtest/font-OFL.txt"
cp "$ROOT/tools/padtest/ChaOS Pad Test.sh" "$W/"
chmod +x "$W/ChaOS Pad Test.sh" "$W/chaospadtest/chaos-padtest"
rm -f "$OUT"
(cd "$W" && zip -qr "$OUT" "ChaOS Pad Test.sh" chaospadtest)
echo "wrote $OUT"
