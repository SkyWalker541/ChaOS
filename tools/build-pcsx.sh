#!/bin/sh
# Build PCSX-ReARMed, the PlayStation core, for the Brick: the file
# dist/brick/pcsx_rearmed_libretro.so, and its source beside it,
# dist/brick/pcsx_rearmed-source.tar.xz, which the sources archive carries
# (tools/chaos-sources.sh). GPL-2.0.
#
# libretro/pcsx_rearmed at the commit below, with nothing changed, built the
# way its Makefile.libretro builds for an Allwinner Cortex-A53 (platform=h5):
# the ARM64 dynamic recompiler (ari64) and the NEON GPU, the build the other
# Brick firmwares run. Against the sysroot tortos.elf is built against, so it
# asks the Brick's glibc for nothing tortos.elf doesn't (2.38 at most).
#
# Unlike TortOS's cores, which come from scutes, this one is ChaOS's: it is
# in ChaOS's small updates too (tools/chaos-image.sh), so it reaches a card
# without a full install. A save state records the core's version, so a new
# build costs players their PlayStation states: change it for a fix, not for
# the sake of it.
#
# Usage: tools/build-pcsx.sh SYSROOT
set -e
COMMIT=7b0e8f8fadfbce3d170d195b9d9eecaabfa08c8e
SHA256=ae66942c83793294319eafb0f75b83560c800a3a92f9752510536ecf9831b3b1
SYSROOT=${1:?usage: $0 SYSROOT}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

git clone -q https://github.com/libretro/pcsx_rearmed.git "$W/pcsx"
git -C "$W/pcsx" checkout -q "$COMMIT"
make -C "$W/pcsx" -f Makefile.libretro platform=h5 -j"$(nproc)" \
	CC="aarch64-linux-gnu-gcc --sysroot=$SYSROOT" \
	CXX="aarch64-linux-gnu-g++ --sysroot=$SYSROOT" AR=aarch64-linux-gnu-ar > "$W/build.log"
aarch64-linux-gnu-strip --strip-unneeded "$W/pcsx/pcsx_rearmed_libretro.so"
GOT=$(sha256sum "$W/pcsx/pcsx_rearmed_libretro.so" | cut -d' ' -f1)
[ "$GOT" = "$SHA256" ] || echo "note: built $GOT, pinned $SHA256 (another compiler?)" >&2
cp "$W/pcsx/pcsx_rearmed_libretro.so" "$ROOT/dist/brick/"
git -C "$W/pcsx" archive --format=tar --prefix=pcsx_rearmed-${COMMIT%${COMMIT#????????}}/ HEAD \
	| xz -T0 -9 > "$ROOT/dist/brick/pcsx_rearmed-source.tar.xz"
echo "pcsx_rearmed_libretro.so $GOT"
