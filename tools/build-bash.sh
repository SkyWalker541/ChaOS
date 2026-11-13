#!/bin/sh
# ChaOS: bash for ports (src/ports.h), as dist/brick/bash is built: GNU bash
# 5.1.16 (dist/brick/bash-source.tar.xz, GPL-3.0-or-later), static, so it
# needs nothing from the Brick's root, for aarch64.
#
#   tools/build-bash.sh OUT        (needs aarch64-linux-gnu-gcc)
#
# PortMaster's ports are bash scripts (#!/bin/bash) and the Brick's root has
# only BusyBox's sh; tools/chaos-image.sh installs the result as /bin/bash.
set -e
OUT=${1:?usage: $0 OUT}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
tar -xJf "$ROOT/dist/brick/bash-source.tar.xz" -C "$W"
cd "$W/bash-5.1.16"
./configure --host=aarch64-linux-gnu --build=x86_64-linux-gnu CC=aarch64-linux-gnu-gcc \
	--enable-static-link --without-bash-malloc --disable-nls --without-curses > /dev/null
make -j"$(nproc)" > /dev/null
aarch64-linux-gnu-strip -o "$OUT" bash
echo "wrote $OUT"
