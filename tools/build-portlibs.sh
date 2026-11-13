#!/bin/sh
# ChaOS: the libraries ports expect a system to have and the Brick's hasn't
# (src/ports.h): OpenAL Soft, Ogg, Vorbis, FLAC, Opus, opusfile, mpg123 and
# SDL2_mixer, cross-built for aarch64 against the Brick's own root
# (SYSROOT, with ALSA and SDL2 in it), as dist/brick/portlibs.tar.xz is.
# FluidSynth (LGPL-2.1) and SDL2_net (zlib) for Doom's engines, too.
# tools/chaos-image.sh puts them in the root's /usr/lib.
#
#   tools/build-portlibs.sh SYSROOT OUT.tar.xz
#
# Needs aarch64-linux-gnu-gcc and -g++, cmake, ninja and autotools. Sources come from
# dist/brick/portlibs-source.tar.xz (each project's own, unchanged but for
# Vorbis 1.3.7's library names, below); every license is in it.
set -e
SYSROOT=$(cd "${1:?usage: $0 SYSROOT OUT.tar.xz}" && pwd)
OUT=${2:?usage: $0 SYSROOT OUT.tar.xz}
case "$OUT" in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
tar -xJf "$ROOT/dist/brick/portlibs-source.tar.xz" -C "$W"
S=$W/portlibs-source ST=$W/stage TC=$W/tc.cmake
cat > "$TC" <<EOT
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
export PKG_CONFIG_SYSROOT_DIR= PKG_CONFIG_LIBDIR=$ST/usr/lib/pkgconfig:$SYSROOT/usr/lib/pkgconfig
cm() {
	src=$1; shift
	b=$W/b-$(echo "$src" | tr / _)
	cmake -S "$S/$src" -B "$b" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON "$@" > /dev/null
	ninja -C "$b" > "$b.log" 2>&1 || { tail -30 "$b.log" >&2; exit 1; }
	DESTDIR=$ST ninja -C "$b" install > /dev/null
	echo "built $src"
}
# Vorbis 1.3.7's CMake names each library by its whole version
# (libvorbisfile.so.3.3.8) where its autotools and every system use the
# major one (libvorbisfile.so.3), which is what ports ask for
sed -i 's/PROPERTIES SOVERSION ${VORBIS_VERSION_INFO})/PROPERTIES VERSION ${VORBIS_VERSION_INFO} SOVERSION 0)/;
        s/PROPERTIES SOVERSION ${VORBISENC_VERSION_INFO})/PROPERTIES VERSION ${VORBISENC_VERSION_INFO} SOVERSION 2)/;
        s/PROPERTIES SOVERSION ${VORBISFILE_VERSION_INFO})/PROPERTIES VERSION ${VORBISFILE_VERSION_INFO} SOVERSION 3)/' \
	"$S/vorbis/lib/CMakeLists.txt"
cm ogg -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF
cm vorbis
cm flac -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF \
	-DINSTALL_MANPAGES=OFF -DWITH_OGG=ON -DBUILD_CXXLIBS=OFF
cm opus -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF
cm opusfile -DOP_DISABLE_HTTP=ON -DOP_DISABLE_DOCS=ON -DOP_DISABLE_EXAMPLES=ON
cm mpg123/ports/cmake -DBUILD_PROGRAMS=OFF -DBUILD_LIBOUT123=OFF
cm openal-soft -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
	-DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_BACKEND_ALSA=ON \
	-DALSOFT_REQUIRE_ALSA=ON -DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_PIPEWIRE=OFF \
	-DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_OSS=OFF \
	-DALSOFT_BACKEND_PORTAUDIO=OFF -DALSOFT_BACKEND_SDL2=OFF -DALSOFT_EMBED_HRTF_DATA=ON \
	-DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_UPDATE_BUILD_VERSION=OFF
# bzip2, by its own makefile for the shared library, under the name ports
# built on Debian ask for (libbz2.so.1.0)
( cd "$S/bzip2" && make -s -f Makefile-libbz2_so CC="aarch64-linux-gnu-gcc --sysroot=$SYSROOT" > /dev/null 2>&1 )
cp -a "$S/bzip2/libbz2.so.1.0.8" "$ST/usr/lib/"
ln -sf libbz2.so.1.0.8 "$ST/usr/lib/libbz2.so.1.0"
ln -sf libbz2.so.1.0 "$ST/usr/lib/libbz2.so.1"
echo "built bzip2"
cm zstd/build/cmake -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_STATIC=OFF
# its own sinf otherwise clashes with the one glibc declares
cm libmodplug -DCMAKE_CXX_FLAGS=-DHAVE_SINF
cm libsndfile -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_EXTERNAL_LIBS=ON \
	-DENABLE_MPEG=OFF -DINSTALL_MANPAGES=OFF -DENABLE_CPACK=OFF
( cd "$S/speex" && ./autogen.sh > /dev/null 2>&1 && mkdir -p "$W/b-speex" && cd "$W/b-speex" &&
  "$S/speex/configure" --host=aarch64-linux-gnu --prefix=/usr CC="aarch64-linux-gnu-gcc --sysroot=$SYSROOT" \
	--disable-static --disable-binaries --enable-neon > /dev/null && make -s > /dev/null &&
  make -s DESTDIR="$ST" install > /dev/null )
echo "built speex"
# SDL2_gfx: four files, built as its own build would name the library
aarch64-linux-gnu-gcc --sysroot="$SYSROOT" -O2 -fPIC -shared -I"$SYSROOT/usr/include/SDL2" -D_REENTRANT \
	-Wl,-soname,libSDL2_gfx-1.0.so.0 -o "$ST/usr/lib/libSDL2_gfx-1.0.so.0.0.2" \
	"$S"/SDL2_gfx/SDL2_framerate.c "$S"/SDL2_gfx/SDL2_gfxPrimitives.c "$S"/SDL2_gfx/SDL2_imageFilter.c \
	"$S"/SDL2_gfx/SDL2_rotozoom.c -L"$SYSROOT/usr/lib" -lSDL2 -lm
ln -sf libSDL2_gfx-1.0.so.0.0.2 "$ST/usr/lib/libSDL2_gfx-1.0.so.0"
echo "built SDL2_gfx"
# FluidSynth, for the MIDI music of Doom's engines (Crispy Doom asks for
# libfluidsynth.so.3): its C++11 OS layer rather than GLib, which the Brick
# hasn't, no OpenMP (libgomp), and output left to the port (SDL_mixer)
cm fluidsynth -Dosal=cpp11 -Denable-alsa=OFF -Denable-aufile=OFF -Denable-dbus=OFF -Denable-ipv6=OFF \
	-Denable-jack=OFF -Denable-ladspa=OFF -Denable-libinstpatch=OFF -Denable-libsndfile=ON \
	-Denable-midishare=OFF -Denable-network=OFF -Denable-oss=OFF -Denable-sdl3=OFF \
	-Denable-pulseaudio=OFF -Denable-pipewire=OFF -Denable-readline=OFF -Denable-openmp=OFF \
	-Denable-systemd=OFF -Denable-native-dls=ON -DCMAKE_INSTALL_LIBDIR=lib \
	-DGCEM_INCLUDE_DIR="$S/fluidsynth/gcem/include" -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$ST/usr/lib"
# SDL2_net, which Crispy Doom links for its multiplayer
cm SDL_net -DSDL2NET_SAMPLES=OFF -DSDL2NET_INSTALL=ON
cm SDL_mixer -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_VENDORED=OFF -DSDL2MIXER_DEPS_SHARED=OFF \
	-DSDL2MIXER_OPUS=ON -DSDL2MIXER_VORBIS=VORBISFILE -DSDL2MIXER_FLAC=ON \
	-DSDL2MIXER_FLAC_LIBFLAC=OFF -DSDL2MIXER_FLAC_DRFLAC=ON -DSDL2MIXER_MP3=ON \
	-DSDL2MIXER_MP3_MPG123=ON -DSDL2MIXER_MP3_DRMP3=OFF -DSDL2MIXER_MOD=OFF \
	-DSDL2MIXER_MIDI=ON -DSDL2MIXER_MIDI_TIMIDITY=ON -DSDL2MIXER_MIDI_FLUIDSYNTH=OFF \
	-DSDL2MIXER_WAVPACK=OFF -DSDL2MIXER_GME=OFF
# SDL2 for ports: the Brick's own (plastron's: 2.32.10 with its PowerVR
# driver, patches 0001 and 0002 in the sources) and three more: 0003, so that a
# port's keyboard is found with no udev, and 0004, so that the Brick's pad,
# all buttons and no axes, is a joystick, and 0005, so that one button can be
# two things in a mapping, and 0006, so that its PowerVR driver says what it
# did, in a port's log, and 0007, so that its screen is XRGB, as a
# desktop's is (res/ports/sdl2). In a folder of its
# own, ahead of /usr/lib on a port's LD_LIBRARY_PATH (res/ports/run.sh); the
# launcher and the emulator keep plastron's.
cd "$S/SDL2"
for p in "$S"/SDL2-patches/*; do patch -p1 -s < "$p"; done
mkdir -p "$W/b-sdl" && cd "$W/b-sdl"
PKG_CONFIG_SYSROOT_DIR=$SYSROOT PKG_CONFIG_LIBDIR=$SYSROOT/usr/lib/pkgconfig "$S/SDL2/configure" \
	--host=aarch64-linux-gnu --prefix=/usr --with-sysroot="$SYSROOT" \
	CC="aarch64-linux-gnu-gcc --sysroot=$SYSROOT" CFLAGS=-O2 \
	--enable-video-pvrfb --enable-video-opengles --disable-video-opengl --disable-video-x11 \
	--disable-video-wayland --disable-video-kmsdrm --disable-video-directfb --disable-video-vulkan \
	--disable-video-rpi --disable-video-vivante --enable-alsa --disable-pulseaudio --disable-pipewire \
	--disable-jack --disable-sndio --disable-esd --disable-arts --disable-nas --disable-dbus \
	--disable-ibus --disable-fcitx --disable-libudev --disable-hidapi --disable-rpath \
	--disable-static > /dev/null
make -j"$(nproc)" > /dev/null
echo "built SDL2 for ports"
cd "$W"

# The libraries and their names alone: no headers, CMake files or programs
mkdir -p "$W/out/usr/lib/chaos-ports"
cp -a "$ST"/usr/lib/lib*.so.* "$W/out/usr/lib/"
cp -a "$W"/b-sdl/build/.libs/libSDL2-2.0.so.0* "$W/out/usr/lib/chaos-ports/"
aarch64-linux-gnu-strip --strip-unneeded "$W"/out/usr/lib/*.so.*.* "$W"/out/usr/lib/chaos-ports/*.so.*.* 2>/dev/null || true
tar -C "$W/out" --owner=0 --group=0 --sort=name -cJf "$OUT" usr
echo "wrote $OUT"
