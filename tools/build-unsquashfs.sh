#!/bin/sh
# ChaOS: unsquashfs for ports' runtimes, as dist/brick/unsquashfs is built:
# squashfs-tools 4.6.1 (GPL-2.0-or-later) with zlib, xz, zstd and lz4, static,
# for aarch64. The sources it was built from are dist/brick/unsquashfs-source.tar.xz.
#
#   tools/build-unsquashfs.sh SRC OUT     (needs aarch64-linux-gnu-gcc)
#
# SRC holds squashfs-tools/ and lz4/ (git checkouts of 4.6.1 and v1.10.0) and
# zlib-1.3.1.tar.gz, xz-5.6.3.tar.gz and zstd-1.5.6.tar.gz.
#
# PortMaster's runtimes (Godot, .NET, Mono, Java...) are squashfs images a
# port's script mounts. The Brick's kernel has neither loop devices nor zlib
# squashfs, so ChaOS's `mount` (res/ports/bin/mount) unpacks one instead, once,
# with this - the way PortMaster itself does on muOS.
set -e
SRC=$(cd "${1:?usage: $0 SRC OUT}" && pwd)
OUT=${2:?usage: $0 SRC OUT}
case $OUT in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
CC=aarch64-linux-gnu-gcc
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
P=$W/prefix
mkdir -p "$P/include" "$P/lib"

tar -xzf "$SRC/zlib-1.3.1.tar.gz" -C "$W"
(cd "$W/zlib-1.3.1" && CC=$CC CFLAGS=-O2 ./configure --static --prefix="$P" > /dev/null && make -j"$(nproc)" install > /dev/null)

tar -xzf "$SRC/xz-5.6.3.tar.gz" -C "$W"
(cd "$W/xz-5.6.3" && ./configure --host=aarch64-linux-gnu CC=$CC --prefix="$P" --disable-shared \
	--disable-xz --disable-xzdec --disable-lzmadec --disable-lzmainfo --disable-scripts \
	--disable-doc --disable-nls > /dev/null && make -j"$(nproc)" install > /dev/null)

tar -xzf "$SRC/zstd-1.5.6.tar.gz" -C "$W"
(cd "$W/zstd-1.5.6/lib" && make -j"$(nproc)" CC=$CC libzstd.a > /dev/null &&
	cp libzstd.a "$P/lib" && cp zstd.h zstd_errors.h zdict.h "$P/include")

cp -r "$SRC/lz4" "$W/lz4"
(cd "$W/lz4/lib" && make -j"$(nproc)" CC=$CC liblz4.a > /dev/null &&
	cp liblz4.a "$P/lib" && cp lz4.h lz4hc.h "$P/include")

cp -r "$SRC/squashfs-tools" "$W/sq"
cd "$W/sq/squashfs-tools"
make -j"$(nproc)" unsquashfs CC=$CC \
	GZIP_SUPPORT=1 XZ_SUPPORT=1 ZSTD_SUPPORT=1 LZ4_SUPPORT=1 LZO_SUPPORT=0 \
	XATTR_SUPPORT=0 \
	EXTRA_CFLAGS="-I$P/include" EXTRA_LDFLAGS="-static -L$P/lib" > /dev/null
aarch64-linux-gnu-strip -o "$OUT" unsquashfs
echo "wrote $OUT"
