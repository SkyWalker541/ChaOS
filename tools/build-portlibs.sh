#!/bin/sh
# ChaOS: the libraries ports expect a system to have and the Brick's hasn't
# (src/ports.h): OpenAL Soft, Ogg, Vorbis, FLAC, Opus, opusfile, mpg123 and
# SDL2_mixer, cross-built for aarch64 against the Brick's own root
# (SYSROOT, with ALSA and SDL2 in it), as dist/brick/portlibs.tar.xz is.
# tools/chaos-image.sh puts them in the root's /usr/lib.
#
#   tools/build-portlibs.sh SYSROOT OUT.tar.xz
#
# Needs aarch64-linux-gnu-gcc and -g++, cmake and ninja. Sources come from
# dist/brick/portlibs-source.tar.xz (each project's own, unchanged but for
# Vorbis 1.3.7's library names, below); every license is in it.
set -e
SYSROOT=$(cd "${1:?usage: $0 SYSROOT OUT.tar.xz}" && pwd)
OUT=${2:?usage: $0 SYSROOT OUT.tar.xz}
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
	cmake -S "$S/$src" -B "$W/b-$(basename "$src")" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON "$@" > /dev/null
	ninja -C "$W/b-$(basename "$src")" > /dev/null
	DESTDIR=$ST ninja -C "$W/b-$(basename "$src")" install > /dev/null
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
cm SDL_mixer -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_VENDORED=OFF -DSDL2MIXER_DEPS_SHARED=OFF \
	-DSDL2MIXER_OPUS=ON -DSDL2MIXER_VORBIS=VORBISFILE -DSDL2MIXER_FLAC=ON \
	-DSDL2MIXER_FLAC_LIBFLAC=OFF -DSDL2MIXER_FLAC_DRFLAC=ON -DSDL2MIXER_MP3=ON \
	-DSDL2MIXER_MP3_MPG123=ON -DSDL2MIXER_MP3_DRMP3=OFF -DSDL2MIXER_MOD=OFF \
	-DSDL2MIXER_MIDI=ON -DSDL2MIXER_MIDI_TIMIDITY=ON -DSDL2MIXER_MIDI_FLUIDSYNTH=OFF \
	-DSDL2MIXER_WAVPACK=OFF -DSDL2MIXER_GME=OFF
# The libraries and their names alone: no headers, CMake files or programs
mkdir -p "$W/out/usr/lib"
cp -a "$ST"/usr/lib/lib*.so.* "$W/out/usr/lib/"
aarch64-linux-gnu-strip --strip-unneeded "$W"/out/usr/lib/*.so.*.* 2>/dev/null || true
tar -C "$W/out" --owner=0 --group=0 --sort=name -cJf "$OUT" usr
echo "wrote $OUT"
