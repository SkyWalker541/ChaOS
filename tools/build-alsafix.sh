#!/bin/sh
# ChaOS: dist/brick/libchaos-alsafix.so, the ALSA helper preloaded for ports
# (tools/alsafix/chaos-alsafix.c says what and why). Needs only
# aarch64-linux-gnu-gcc: it links no ALSA, and calls the port's own.
#
#   tools/build-alsafix.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -shared -fPIC -o "$ROOT/dist/brick/libchaos-alsafix.so" \
	"$ROOT/tools/alsafix/chaos-alsafix.c" -ldl
aarch64-linux-gnu-strip --strip-unneeded "$ROOT/dist/brick/libchaos-alsafix.so"
echo "built dist/brick/libchaos-alsafix.so"
