#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: the Python PortMaster's app runs on, built here rather than taken
# from another firmware. CPython for aarch64, linked against the libraries
# ChaOS's own root already has (OpenSSL 3, SQLite, zlib, libffi 8, glibc), so
# only the interpreter and its standard library are new: about 36 MB, 8.5 MB
# as an xz squashfs (chaos-python-<ver>-aarch64.squashfs), mounted like
# PortMaster's other runtimes. Relocatable: Python finds its lib/ beside bin/.
#
#   tools/build-python.sh <work dir> <ChaOS root.sqfs> <SDL2 sysroot>
#
# Needs: aarch64-linux-gnu-gcc, git, autoconf/automake/libtool, unsquashfs,
# mksquashfs, and the host's libssl/zlib/sqlite3/bzip2 headers (they are the
# same on both machines but for OpenSSL's, which are 64-bit on both).
set -eu
W=${1:?work dir}; ROOT=${2:?root.sqfs}; SYSROOT=${3:?SDL2 sysroot}
PORTLIBS=$(cd "$(dirname "$0")/.." && pwd)/dist/brick/portlibs-source.tar.xz
PYV=3.11.18; FFIV=3.4.6; XZV=5.6.4
mkdir -p "$W" && cd "$W"
D=$W/dev; mkdir -p "$D/include" "$D/lib"

# the device's libraries to link against, and its runtime to test under qemu
unsquashfs -f -d "$W/devroot" "$ROOT" 'lib/lib*' 'lib/ld-*' 'usr/lib/lib*' >/dev/null
cp -a "$W"/devroot/usr/lib/libssl.so* "$W"/devroot/usr/lib/libcrypto.so* "$W"/devroot/usr/lib/libz.so* \
      "$W"/devroot/usr/lib/libsqlite3.so* "$W"/devroot/usr/lib/libffi.so* "$W"/devroot/usr/lib/libbz2.so* "$D/lib/"
[ -e "$D/lib/libbz2.so" ] || ln -sf "$(cd "$D/lib" && ls libbz2.so.1.* | head -1)" "$D/lib/libbz2.so"

# headers: the host's, but libffi's are per-architecture, so built for aarch64
cp -r /usr/include/openssl "$D/include/"
cp /usr/include/x86_64-linux-gnu/openssl/*.h "$D/include/openssl/"
cp /usr/include/zlib.h /usr/include/zconf.h /usr/include/sqlite3.h /usr/include/sqlite3ext.h /usr/include/bzlib.h "$D/include/"
[ -d libffi ] || git clone -q --depth 1 --branch "v$FFIV" https://github.com/libffi/libffi
(cd libffi && [ -x configure ] || ./autogen.sh >/dev/null)
rm -rf ffib && mkdir ffib && (cd ffib && ../libffi/configure -q --host=aarch64-linux-gnu --prefix="$D/ffi" --disable-docs && make -s -j8 && make -s install)
cp "$D"/ffi/include/*.h "$D/include/"

# liblzma, which ChaOS's root hasn't: built in, statically, for the _lzma
# module PortMaster unpacks its fonts with ("lzma module is not available")
[ -d xz ] || git clone -q --depth 1 --branch "v$XZV" https://github.com/tukaani-project/xz
rm -rf xz/cb
cmake -S xz -B xz/cb -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
	-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$D/xz" -DXZ_TOOL_XZ=OFF \
	-DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF \
	-DXZ_DOC=OFF -DXZ_NLS=OFF > /dev/null
cmake --build xz/cb -j8 > /dev/null && cmake --install xz/cb > /dev/null
cp -r "$D"/xz/include/* "$D/include/" && cp "$D/xz/lib/liblzma.a" "$D/lib/"

# a host Python of the same version builds the target one
[ -d cpython ] || git clone -q --depth 1 --branch "v$PYV" https://github.com/python/cpython
if [ ! -x hostinst/bin/python3.11 ]; then
	rm -rf hostpy && mkdir hostpy && (cd hostpy && ../cpython/configure -q --prefix="$W/hostinst" && make -s -j16 && make -s install)
fi

rm -rf cross stage && mkdir cross
(cd cross && CONFIG_SITE=/dev/null ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no ac_cv_buggy_getaddrinfo=no \
	../cpython/configure -q --host=aarch64-linux-gnu --build=x86_64-linux-gnu \
	--with-build-python="$W/hostinst/bin/python3.11" --prefix=/opt/chaos-python \
	--with-openssl="$D" --without-ensurepip --disable-test-modules \
	CC=aarch64-linux-gnu-gcc CPPFLAGS="-I$D/include" LDFLAGS="-L$D/lib -Wl,-rpath-link,$D/lib" &&
	make -s -j16 && make -s install DESTDIR="$W/stage")

# only what a program needs: no tests, IDLE, Tk, 2to3, static library, headers
S=$W/stage/opt/chaos-python; L=$S/lib/python3.11
rm -rf "$L/test" "$L"/*/test "$L"/*/tests "$L/idlelib" "$L/tkinter" "$L/turtledemo" "$L/ensurepip" \
       "$L/lib2to3" "$L/pydoc_data" "$L"/config-3.11-* "$S/include" "$S/share" "$S/lib/pkgconfig" \
       "$S"/lib/libpython3.11.a "$S"/bin/idle* "$S"/bin/2to3* "$S"/bin/pydoc* "$S"/bin/*-config
find "$S" -name '*.opt-[12].pyc' -delete
aarch64-linux-gnu-strip --strip-unneeded "$S/bin/python3.11" "$L"/lib-dynload/*.so
cp cpython/LICENSE "$S/LICENSE.Python.txt"

# SDL2_gfx as pySDL2 binds it. The root's (tools/build-portlibs.sh) is a
# source whose functions are named GFX_pixelColor..., and pySDL2 asks for
# pixelColor: PortMaster's app stopped at "Could not find function
# 'pixelColor'" (2026-11-26, the first try on a Brick). The same source,
# built with the plain names, in lib/sdl, which run.sh puts first on
# PYSDL2_DLL_PATH; the rest of SDL is the root's.
G=$W/gfx; rm -rf "$G" && mkdir -p "$G" "$S/lib/sdl"
tar -xJf "$PORTLIBS" -C "$G" portlibs-source/SDL2_gfx
GS=$G/portlibs-source/SDL2_gfx
GD=$(grep -ohE '\bGFX_[a-z][A-Za-z0-9_]*' "$GS"/*.h "$GS"/*.c | sort -u | sed 's/^GFX_\(.*\)$/-DGFX_\1=\1/' | tr '\n' ' ')
# shellcheck disable=SC2086
aarch64-linux-gnu-gcc --sysroot="$SYSROOT" -O2 -fPIC -shared $GD -I"$SYSROOT/usr/include/SDL2" -D_REENTRANT \
	-Wl,-soname,libSDL2_gfx.so -o "$S/lib/sdl/libSDL2_gfx.so" "$GS/SDL2_framerate.c" \
	"$GS/SDL2_gfxPrimitives.c" "$GS/SDL2_imageFilter.c" "$GS/SDL2_rotozoom.c" -L"$SYSROOT/usr/lib" -lSDL2 -lm
aarch64-linux-gnu-strip --strip-unneeded "$S/lib/sdl/libSDL2_gfx.so"
cp "$GS/LICENSE" "$S/LICENSE.SDL2_gfx.txt"

# it runs, on ChaOS's own libraries, with what PortMaster imports
qemu-aarch64 -L "$W/devroot" -E LD_LIBRARY_PATH="$W/devroot/lib:$W/devroot/usr/lib" "$S/bin/python3.11" -c '
import sys, ssl, ctypes, sqlite3, zlib, lzma, bz2, json, socket, hashlib, zipfile, tarfile, urllib.request
ctypes.CDLL("libSDL2-2.0.so.0"); ctypes.CDLL("libSDL2_ttf-2.0.so.0"); ctypes.CDLL("libSDL2_image-2.0.so.0")
g = ctypes.CDLL(sys.argv[1] + "/lib/sdl/libSDL2_gfx.so"); g.pixelColor; g.rotozoomSurface
print("ok: python", ssl.OPENSSL_VERSION)' "$S"

OUT=$W/chaos-python-$PYV-aarch64.squashfs
mksquashfs "$S" "$OUT" -comp xz -noappend -all-root -quiet >/dev/null
ls -la "$OUT"
