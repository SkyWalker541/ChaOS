#!/bin/sh
# ChaOS: the port library pack (src/libpack.h) - the libraries ports need
# that a Brick under ChaOS hasn't got, each a file on its own beside
# libpack.json, which says what each is, its MD5 and size. ChaOS fetches
# one when a port it is installing or starting needs it. Published as the
# assets of the "portlibs" release on ChaOS's GitHub.
#
#   tools/build-libpack.sh SYSROOT SRC OUT
#
# SRC holds the sources (dist/brick/libpack-source.tar.xz unpacked: each
# project's own release, as Ubuntu's archive keeps it, SOURCES.txt says
# which); OUT gets the libraries, libpack.json and LICENSES/. Which ones:
# what tools/ports-survey.py found the most ports lacking, 2026-11-13.
#
#   libpulse-simple.so.0  ChaOS's: PulseAudio's simple API, refusing every
#                         connection (res/ports/libpack/pulse-simple.c)
#   libtheoradec.so.1     libtheora 1.1.1 (LOVE's video)
#   libpixman-1.so.0      pixman 0.42.2
#   libgmp.so.10          GMP 6.3.0
#   libcairo.so.2         cairo 1.18.0 (image, PNG, FreeType, fontconfig)
#   libfontconfig.so.1    fontconfig 2.15.0
#   libFLAC.so.8          FLAC 1.3.3 (the version Debian's ports were built with)
#   libevdev.so.2         libevdev 1.13.1
#   libgomp.so.1          GCC 13.3.0's OpenMP runtime (the cross compiler's own)
#
# Needs aarch64-linux-gnu-gcc and -g++, cmake, meson, ninja, gperf and autotools.
set -e
SYSROOT=$(cd "${1:?usage: $0 SYSROOT SRC OUT}" && pwd)
SRC=$(cd "${2:?usage: $0 SYSROOT SRC OUT}" && pwd)
OUT=${3:?usage: $0 SYSROOT SRC OUT}
case "$OUT" in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
ST=$W/stage
mkdir -p "$ST/usr/lib" "$ST/usr/include" "$OUT"
CC="aarch64-linux-gnu-gcc --sysroot=$SYSROOT"
CXX="aarch64-linux-gnu-g++ --sysroot=$SYSROOT"
CF="-O2 -I$ST/usr/include"
LF="-L$ST/usr/lib -Wl,-rpath-link,$ST/usr/lib"
export PKG_CONFIG_SYSROOT_DIR= PKG_CONFIG_LIBDIR=$ST/usr/lib/pkgconfig:$SYSROOT/usr/lib/pkgconfig
cat > "$W/tc.cmake" <<EOT
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_SYSROOT $SYSROOT)
set(CMAKE_FIND_ROOT_PATH $ST/usr $SYSROOT/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOT
cat > "$W/cross.ini" <<EOT
[binaries]
c = ['aarch64-linux-gnu-gcc', '--sysroot=$SYSROOT']
cpp = ['aarch64-linux-gnu-g++', '--sysroot=$SYSROOT']
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkg-config = 'pkg-config'
[built-in options]
c_args = ['-I$ST/usr/include']
c_link_args = ['-L$ST/usr/lib', '-Wl,-rpath-link,$ST/usr/lib']
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'cortex-a53'
endian = 'little'
EOT
x() { tar -xf "$SRC/$1" -C "$W"; }
log() { echo "built $1"; }
run() { "$@" > "$W/last.log" 2>&1 || { tail -40 "$W/last.log" >&2; exit 1; }; }
cm() {
	s=$1; shift
	run cmake -S "$s" -B "$s/b" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$W/tc.cmake" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib "$@"
	run ninja -C "$s/b"
	DESTDIR=$ST run ninja -C "$s/b" install
}
ms() {
	s=$1; shift
	run meson setup "$s/b" "$s" --cross-file "$W/cross.ini" --prefix=/usr --libdir=lib \
		--buildtype=release -Ddefault_library=shared "$@"
	run ninja -C "$s/b"
	DESTDIR=$ST run ninja -C "$s/b" install
}
ac() {
	s=$1; shift
	( cd "$s" && run ./configure --host=aarch64-linux-gnu --prefix=/usr CC="$CC" CXX="$CXX" \
		CFLAGS="$CF" CXXFLAGS="$CF" LDFLAGS="$LF" --disable-static "$@" &&
	  run make -j"$(nproc)" && run make DESTDIR="$ST" install )
}

# For building against only - the Brick's root has them all: zlib in the
# sysroot, and these
x libpng1.6_1.6.57.orig.tar.gz
cm "$W/libpng-1.6.57" -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_STATIC=OFF
x freetype_2.13.3+dfsg.orig.tar.xz
cm "$W/freetype-2.13.3" -DBUILD_SHARED_LIBS=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON \
	-DFT_DISABLE_BZIP2=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON
x expat_2.7.4.orig.tar.gz
cm "$W/libexpat-R_2_7_4/expat" -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF \
	-DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF -DEXPAT_SHARED_LIBS=ON
tar -xJf "$ROOT/dist/brick/portlibs-source.tar.xz" -C "$W" portlibs-source/ogg
cm "$W/portlibs-source/ogg" -DBUILD_SHARED_LIBS=ON -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF
echo "built what they're built against"
mark=$W/mark; touch "$mark"; sleep 1   # what is built after this ships

# PulseAudio's simple API, refusing
$CC -O2 -fPIC -shared -Wl,-soname,libpulse-simple.so.0 -o "$ST/usr/lib/libpulse-simple.so.0" \
	"$ROOT/res/ports/libpack/pulse-simple.c"
log libpulse-simple
x libtheora_1.1.1+dfsg.1.orig.tar.gz
# 1.1.1's config.guess predates aarch64
cp /usr/share/misc/config.guess /usr/share/misc/config.sub "$W/libtheora-1.1.1/" 2>/dev/null || true
# the library alone: Debian's copy leaves out documents its docs build wants
( cd "$W/libtheora-1.1.1" && run ./configure --host=aarch64-linux-gnu --prefix=/usr CC="$CC" \
	CFLAGS="$CF" LDFLAGS="$LF" --disable-static --disable-examples --disable-oggtest \
	--disable-vorbistest --disable-sdltest --disable-encode --with-ogg="$ST/usr" --disable-asm &&
  run make -C lib -j"$(nproc)" && run make -C lib DESTDIR="$ST" install &&
  run make -C include DESTDIR="$ST" install )
log libtheora
x pixman_0.42.2.orig.tar.gz
ms "$W/pixman-0.42.2" -Dtests=disabled -Dgtk=disabled -Dlibpng=disabled -Da64-neon=disabled
log pixman
x gmp_6.3.0+dfsg.orig.tar.xz
# Debian's copy leaves out the manual; its folder only has to make nothing
mkdir -p "$W/gmp-6.3.0+dfsg/doc"
[ -f "$W/gmp-6.3.0+dfsg/doc/Makefile.in" ] || printf 'all install install-data install-exec uninstall clean distclean mostlyclean maintainer-clean check installcheck installdirs info dvi pdf ps html install-strip tags ctags distdir:\n\t@:\n' \
	> "$W/gmp-6.3.0+dfsg/doc/Makefile.in"
ac "$W/gmp-6.3.0+dfsg" --enable-cxx=no
log gmp
x fontconfig_2.15.0.orig.tar.xz
ms "$W/fontconfig-2.15.0" -Ddoc=disabled -Dtests=disabled -Dtools=disabled -Dcache-build=disabled \
	-Dnls=disabled -Dbaseconfig-dir=/etc/fonts -Dconfig-dir=/etc/fonts/conf.d \
	-Ddefault-fonts-dirs=/usr/share/fonts
log fontconfig
x cairo_1.18.0.orig.tar.xz
ms "$W/cairo-1.18.0" -Dtests=disabled -Dxlib=disabled -Dxcb=disabled -Dquartz=disabled -Dglib=disabled \
	-Dspectre=disabled -Dsymbol-lookup=disabled -Dgtk_doc=false -Dpng=enabled -Dfreetype=enabled \
	-Dfontconfig=enabled -Dzlib=enabled -Dgtk2-utils=disabled -Dtee=disabled -Ddwrite=disabled
log cairo
x flac_1.3.3.orig.tar.xz
ac "$W/flac-1.3.3" --disable-cpplibs --disable-xmms-plugin --disable-doxygen-docs --disable-examples \
	--disable-ogg --disable-programs 2>/dev/null || ac "$W/flac-1.3.3" --disable-cpplibs \
	--disable-xmms-plugin --disable-doxygen-docs --disable-ogg
log FLAC
x libevdev_1.13.1+dfsg.orig.tar.xz
ms "$W/libevdev-1.13.1" -Dtests=disabled -Ddocumentation=disabled
log libevdev
cp -L "$(aarch64-linux-gnu-gcc -print-file-name=libgomp.so.1)" "$ST/usr/lib/libgomp.so.1"
touch "$ST/usr/lib/libgomp.so.1"
log libgomp

# Each library under the name ports ask for, stripped, and the list
python3 - "$ST/usr/lib" "$mark" "$OUT" <<'EOF'
import hashlib, json, os, shutil, subprocess, sys
lib, mark, out = sys.argv[1:]
licence = {
	'libpulse-simple.so.0': 'MIT (ChaOS)', 'libtheoradec.so.1': 'BSD-3-Clause',
	'libpixman-1.so.0': 'MIT', 'libgmp.so.10': 'LGPL-3.0-or-later OR GPL-2.0-or-later',
	'libcairo.so.2': 'LGPL-2.1-only OR MPL-1.1', 'libfontconfig.so.1': 'MIT',
	'libFLAC.so.8': 'BSD-3-Clause', 'libevdev.so.2': 'MIT',
	'libgomp.so.1': 'GPL-3.0-or-later WITH GCC-exception-3.1',
}
since = os.path.getmtime(mark)
libs = {}
for name in sorted(licence):
	src = os.path.join(lib, name)
	if not os.path.exists(src):
		sys.exit('no %s' % name)
	dst = os.path.join(out, name)
	shutil.copyfile(os.path.realpath(src), dst)
	subprocess.run(['aarch64-linux-gnu-strip', '--strip-unneeded', dst], check=True)
	data = open(dst, 'rb').read()
	libs[name] = {'file': name, 'md5': hashlib.md5(data).hexdigest(), 'size': len(data),
	              'licence': licence[name]}
json.dump({'version': 1, 'libs': libs}, open(os.path.join(out, 'libpack.json'), 'w'), indent=1)
print('wrote %d libraries' % len(libs))
EOF
