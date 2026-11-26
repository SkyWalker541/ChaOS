#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: the Python PortMaster's app runs on, built here rather than taken
# from another firmware. CPython for aarch64, linked against the libraries
# ChaOS's own root already has (OpenSSL 3, SQLite, zlib, libffi 8, glibc), so
# only the interpreter and its standard library are new: about 36 MB, 8.5 MB
# as an xz squashfs (chaos-python-<ver>-aarch64.squashfs), mounted like
# PortMaster's other runtimes. Relocatable: Python finds its lib/ beside bin/.
#
#   tools/build-python.sh <work dir> <ChaOS root.sqfs>
#
# Needs: aarch64-linux-gnu-gcc, git, autoconf/automake/libtool, unsquashfs,
# mksquashfs, and the host's libssl/zlib/sqlite3/bzip2 headers (they are the
# same on both machines but for OpenSSL's, which are 64-bit on both).
set -eu
W=${1:?work dir}; ROOT=${2:?root.sqfs}
PYV=3.11.18; FFIV=3.4.6
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

# it runs, on ChaOS's own libraries, with what PortMaster imports
qemu-aarch64 -L "$W/devroot" -E LD_LIBRARY_PATH="$W/devroot/lib:$W/devroot/usr/lib" "$S/bin/python3.11" -c '
import ssl, ctypes, sqlite3, zlib, json, socket, hashlib, zipfile, tarfile, urllib.request
ctypes.CDLL("libSDL2-2.0.so.0"); ctypes.CDLL("libSDL2_ttf-2.0.so.0"); ctypes.CDLL("libSDL2_image-2.0.so.0")
print("ok: python", ssl.OPENSSL_VERSION)'

OUT=$W/chaos-python-$PYV-aarch64.squashfs
mksquashfs "$S" "$OUT" -comp xz -noappend -all-root -quiet >/dev/null
ls -la "$OUT"
