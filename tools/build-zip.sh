#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: zip for ports, as dist/brick/zip is built: Info-ZIP Zip 3.0 (the
# Info-ZIP licence, dist/brick/zip-LICENSE.txt) with Ubuntu's patches
# (3.0-16ubuntu1), for aarch64. BusyBox has unzip but no zip, and the
# patchers of PortMaster's GameMaker ports zip the game back up at the end
# (Lasagna Boy Classic stopped there: "Missing zip!", 2026-11-26). Installed
# in ports/pybin, the end of a port's PATH (res/ports/run.sh). The sources:
# dist/brick/zip-source.tar.xz.
#
#   tools/build-zip.sh SYSROOT OUT     (needs aarch64-linux-gnu-gcc, network)
set -e
SYSROOT=$(cd "${1:?usage: $0 SYSROOT OUT}" && pwd)
OUT=${2:?usage: $0 SYSROOT OUT}
case $OUT in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
A=http://archive.ubuntu.com/ubuntu/pool/main/z/zip
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cd "$W"
curl -sfO "$A/zip_3.0.orig.tar.gz"
curl -sfO "$A/zip_3.0-16ubuntu1.debian.tar.xz"
mkdir -p "$OUT"
tar -cJf "$OUT/zip-source.tar.xz" zip_3.0.orig.tar.gz zip_3.0-16ubuntu1.debian.tar.xz
tar -xzf zip_3.0.orig.tar.gz
cd zip30
tar -xJf ../zip_3.0-16ubuntu1.debian.tar.xz
grep -v '^#' debian/patches/series | while read -r p; do [ -n "$p" ] && patch -s -p1 < "debian/patches/$p"; done
sh unix/configure "aarch64-linux-gnu-gcc --sysroot=$SYSROOT" "-O2 -Wall -I. -DUNIX -D_FILE_OFFSET_BITS=64" > /dev/null
eval make -s -f unix/Makefile zips $(cat flags)
aarch64-linux-gnu-strip zip
cp zip "$OUT/zip"
cp LICENSE "$OUT/zip-LICENSE.txt"
ls -la "$OUT/zip"
