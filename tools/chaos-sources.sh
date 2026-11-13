#!/bin/sh
# The source of everything in a ChaOS card image, as one tar beside it.
#
#   tools/chaos-sources.sh TortOS-v<base>-brick-sources.tar OUT.tar
#
# The image is TortOS's with ChaOS's launcher, emulator, overlays, boot frames
# and web page in it, so its source is TortOS's sources archive for the same
# release (plastron, the Linux kernel, Buildroot's packages, TortOS), as
# TortOS publishes it, plus ChaOS's (this repository at HEAD) and
# ChaOS-diatom's (dist/brick/ChaOS-diatom-source.tar.gz, the commit the
# shipped diatom was built from), and bash's (dist/brick/bash-source.tar.xz).
set -e
TORTOS_SRC=$1 OUT=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$TORTOS_SRC" ] && [ -n "$OUT" ] || { echo "usage: $0 TortOS-brick-sources.tar OUT.tar" >&2; exit 2; }
V=$(tr -d ' \n' < "$ROOT/CHAOS_VERSION")
BASE=$(tr -d ' \n' < "$ROOT/TORTOS_BASE")
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
S=$W/ChaOS-v$V-brick-sources
mkdir -p "$S"

ln -s "$(cd "$(dirname "$TORTOS_SRC")" && pwd)/$(basename "$TORTOS_SRC")" "$S/TortOS-v$BASE-brick-sources.tar"
git -C "$ROOT" archive --format=tar.gz --prefix=ChaOS/ HEAD > "$S/ChaOS-source.tar.gz"
cp "$ROOT/dist/brick/ChaOS-diatom-source.tar.gz" "$S/"
cp "$ROOT/dist/brick/bash-source.tar.xz" "$S/"
cp "$ROOT/dist/brick/portlibs-source.tar.xz" "$S/"
cp "$ROOT/dist/brick/unsquashfs-source.tar.xz" "$S/"
cat > "$S/README.txt" <<EOF
ChaOS $V for the TrimUI Brick: the source of everything in its card image.

  TortOS-v$BASE-brick-sources.tar
      TortOS $BASE's own sources archive, unchanged, from
      https://github.com/ericreinsmidt/TortOS/releases/tag/v$BASE
      ChaOS's image is TortOS $BASE's with the parts below swapped in, so
      everything else in it (plastron, the Linux kernel, Buildroot's
      packages, the TortOS launcher it started from) is built from this.

  ChaOS-source.tar.gz
      ChaOS, at $(git -C "$ROOT" rev-parse --short HEAD): the launcher
      (tortos.elf), the web page, the overlays, the boot animation, and
      tools/chaos-image.sh, which makes the image. GPL-3.0-or-later.
      https://github.com/SkyWalker541/ChaOS

  ChaOS-diatom-source.tar.gz
      ChaOS-diatom, the emulator (diatom), with rewind and its shaders: PT
      SkyWalker541 (MIT), crt-lottes-fast and lcd3x (public domain).
      MPL-2.0. https://github.com/SkyWalker541/ChaOS-diatom

  bash-source.tar.xz
      GNU bash 5.1.16, unchanged, which ChaOS adds to the root as /bin/bash
      for ports (tools/build-bash.sh builds it). GPL-3.0-or-later.
      https://www.gnu.org/software/bash/

  portlibs-source.tar.xz
      The libraries ChaOS adds to the root's /usr/lib for ports, each
      project's own source (SOURCES.txt says from where, at which commit):
      OpenAL Soft, mpg123 and libsndfile (LGPL-2.1), Ogg, Vorbis, FLAC,
      Opus, opusfile and Speex (BSD-3-Clause), bzip2, Zstandard (BSD),
      libmodplug (public domain), SDL2_mixer, SDL2_gfx and SDL2 with its
      patches (zlib).

  unsquashfs-source.tar.xz
      squashfs-tools 4.6.1's unsquashfs (GPL-2.0-or-later), with zlib, XZ
      Utils' liblzma (0BSD), Zstandard and LZ4 (BSD-2-Clause), static, in
      ChaOS/ports/bin: ports' runtimes are unpacked with it, as the Brick's
      kernel can't mount them (tools/build-unsquashfs.sh builds it).
      tools/build-portlibs.sh builds them.

The licenses are in the image's Licenses/ folder and in each archive.
EOF
tar -chf "$OUT" -C "$W" "ChaOS-v$V-brick-sources"
echo "wrote $OUT"
