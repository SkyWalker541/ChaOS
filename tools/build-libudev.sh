#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: libudev.so.1 for ports, as dist/brick/libudev.so.1 is built:
# libudev-zero 1.0.3 (ISC, dist/brick/libudev-LICENSE.txt), a libudev that
# needs no udev daemon, for aarch64. The Brick has no udev, and PortMaster's
# Weston (weston_pkg: libinput) won't load without the library: Switchfin,
# The Brainvore and Pixel Wheels stopped there (2026-11-26). Installed in
# ChaOS/ports/lib, the end of a port's library path (res/ports/run.sh). The
# sources: dist/brick/libudev-source.tar.xz.
#
#   tools/build-libudev.sh SYSROOT OUT     (needs aarch64-linux-gnu-gcc, git)
set -e
SYSROOT=$(cd "${1:?usage: $0 SYSROOT OUT}" && pwd)
OUT=${2:?usage: $0 SYSROOT OUT}
case "$OUT" in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
git clone -q --depth 1 --branch 1.0.3 https://github.com/illiliti/libudev-zero "$W/libudev-zero"
mkdir -p "$OUT"
tar -C "$W" --exclude=.git -cJf "$OUT/libudev-source.tar.xz" libudev-zero
make -s -C "$W/libudev-zero" CC="aarch64-linux-gnu-gcc --sysroot=$SYSROOT" CFLAGS="-O2 -fPIC" libudev.so.1
aarch64-linux-gnu-strip --strip-unneeded "$W/libudev-zero/libudev.so.1"
cp "$W/libudev-zero/libudev.so.1" "$OUT/libudev.so.1"
cp "$W/libudev-zero/LICENSE" "$OUT/libudev-LICENSE.txt"
ls -la "$OUT/libudev.so.1"
