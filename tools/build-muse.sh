#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: Muse's player (src/muse/), as dist/brick/muse is built. Until 2.7.9
# the card carried TortOS's own build; ChaOS builds it now, so a change to it
# reaches players (it lets go of the sound device when nothing plays, for
# ports). Linked against the FFmpeg and ALSA the Brick's root already has:
# FFmpeg 6.1.1 (libavcodec 60.31.102, libavformat 60.16.100, libavutil
# 58.29.100, libavfilter 9.12.100: the root's exact numbers), whose headers
# are generated here from its own source; nothing of FFmpeg is shipped.
#
#   tools/build-muse.sh <work dir> <ChaOS root.sqfs> <ALSA sysroot> [OUT]
#
# Needs aarch64-linux-gnu-gcc, git, unsquashfs.
set -eu
W=${1:?work dir}; ROOT=${2:?root.sqfs}; SYSROOT=${3:?sysroot with alsa headers}
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${4:-$REPO/dist/brick/muse}
FFV=n6.1.1
mkdir -p "$W" && cd "$W"
unsquashfs -f -d "$W/devroot" "$ROOT" 'lib/lib*' 'lib/ld-*' 'usr/lib/lib*' > /dev/null
# the root's numbers, checked: a different FFmpeg would be a different ABI
for l in libavcodec.so.60.31.102 libavformat.so.60.16.100 libavutil.so.58.29.100 libavfilter.so.9.12.100; do
	[ -e "$W/devroot/usr/lib/$l" ] || { echo "the root's FFmpeg isn't 6.1.1 ($l missing)" >&2; exit 1; }
done
[ -d ffmpeg ] || git clone -q --depth 1 --branch "$FFV" https://github.com/FFmpeg/FFmpeg ffmpeg
rm -rf ffb ffdev && mkdir ffb
(cd ffb && ../ffmpeg/configure --enable-cross-compile --arch=aarch64 --target-os=linux \
	--cross-prefix=aarch64-linux-gnu- --prefix="$W/ffdev" --disable-programs --disable-doc \
	--disable-asm --disable-everything --enable-shared --disable-static > /dev/null && make -s install-headers)
R=$W/devroot
aarch64-linux-gnu-gcc -O2 -mcpu=cortex-a53 -Wall -Wextra -Wno-unused-parameter \
	-Wno-missing-field-initializers -std=gnu11 -D_GNU_SOURCE -I"$SYSROOT/usr/include" \
	-I"$W/ffdev/include" -o "$W/muse" "$REPO"/src/muse/*.c -L"$R/usr/lib" -L"$R/lib" \
	-Wl,-rpath-link,"$R/usr/lib:$R/lib" -l:libavformat.so.60 -l:libavcodec.so.60 \
	-l:libavfilter.so.9 -l:libswresample.so.4 -l:libavutil.so.58 -l:libasound.so.2 -lpthread
aarch64-linux-gnu-strip "$W/muse"
strings "$W/muse" | grep -q '^READY	proto=' || { echo "muse: no READY line" >&2; exit 1; }
cp "$W/muse" "$OUT"
ls -la "$OUT"
