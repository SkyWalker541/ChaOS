#!/bin/sh
# ChaOS: make check-portaudio. Ports' sound, with the Brick's own ALSA and the
# libraries ports use, under qemu-user (tools/portaudio-check.c says why).
#
# 1. Each way a port plays - ALSA linked (Doukutsu-rs), OpenAL Soft (LÖVE),
#    SDL (SDL_mixer and most ports) - with ChaOS's ALSA helper preloaded,
#    both the one built from source and the one that ships
#    (dist/brick/libchaos-alsafix.so), to ALSA's null device through plug's
#    rate converter, as the Brick's mixer converts.
# 2. What a port's default device really is, with the Brick's alsa.conf and
#    /etc/asound.conf and the ALSA_CONFIG_PATH the launcher gives it
#    (sd/brick/launch.sh), against what run.sh's log says it plays through.
#    run.sh said "straight to the hardware" for a year of releases that played
#    through the mixer: ALSA reads /etc/asound.conf from alsa.conf's hooks,
#    after every file named in ALSA_CONFIG_PATH (2026-11-26).
#
#   tools/portaudio-check.sh BUILD_DIR
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$1
F=$ROOT/tools/fixtures/brick-alsa
LIBC=/usr/aarch64-linux-gnu
fail() { echo "portaudio-check: $*" >&2; exit 1; }

for t in aarch64-linux-gnu-gcc qemu-aarch64; do
	command -v $t > /dev/null 2>&1 ||
		fail "needs $t (apt install gcc-aarch64-linux-gnu qemu-user): no check is skipped"
done
[ -x "$LIBC/lib/ld-linux-aarch64.so.1" ] || fail "needs arm64 glibc in $LIBC (gcc-aarch64-linux-gnu)"

rm -rf "$B"
mkdir -p "$B/lib"
aarch64-linux-gnu-gcc -std=gnu11 -O1 -Wall -Wextra -Werror -o "$B/portaudio-check" "$ROOT/tools/portaudio-check.c" -ldl
aarch64-linux-gnu-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -shared -fPIC -o "$B/alsafix-source.so" \
	"$ROOT/tools/alsafix/chaos-alsafix.c" -ldl
cp "$ROOT/dist/brick/libchaos-alsafix.so" "$B/alsafix-shipped.so"
tar xJf "$ROOT/dist/brick/portlibs.tar.xz" -C "$B" usr/lib/libopenal.so.1 usr/lib/libopenal.so.1.24.3 \
	usr/lib/chaos-ports/libSDL2-2.0.so.0 usr/lib/chaos-ports/libSDL2-2.0.so.0.3200.10
cp "$F/lib/libasound.so.2" "$B/lib/"
# the Brick's mixer takes 48 kHz; ports ask for 44.1, so plug converts
printf 'pcm.!default { type plug; slave { pcm "null"; rate 48000; format S16_LE; channels 2 } }\n' > "$B/null.conf"
printf '[general]\nfrequency = 44100\n' > "$B/alsoft.conf"

run() {
	qemu-aarch64 -L "$LIBC" "$@"
}

for h in source shipped; do
	for way in alsa openal sdl; do
		out=$(run -E LD_PRELOAD="$B/alsafix-$h.so" \
			-E LD_LIBRARY_PATH="$B/lib:$B/usr/lib:$B/usr/lib/chaos-ports" \
			-E ALSA_CONFIG_DIR="$F/share" -E ALSA_CONFIG_PATH="$F/share/alsa.conf:$B/null.conf" \
			-E ALSOFT_CONF="$B/alsoft.conf" -E ALSOFT_LOGLEVEL=0 -E SDL_AUDIODRIVER=alsa \
			"$B/portaudio-check" $way 2>&1) ||
			fail "with the $h helper, $way: $(echo "$out" | grep -v '^\[ALSOFT\]' | tail -3)"
	done
	echo "ok: ALSA linked, OpenAL Soft and SDL all play with the $h ALSA helper"
done

# 2. alsa.conf's hooks name /etc/asound.conf; qemu-user can't redirect the
# access() they make, so they're pointed at the fixture's
sed "s#\"/etc/asound.conf|||/usr/etc/asound.conf\"#\"$F/etc/asound.conf\"#" "$F/share/alsa.conf" > "$B/alsa.conf"
grep -q "$F/etc/asound.conf" "$B/alsa.conf" || fail "the Brick's alsa.conf no longer loads /etc/asound.conf as it did"
: > "$B/asoundrc"     # the launcher's .asoundrc, as it is with no headset
got=$(run -E LD_LIBRARY_PATH="$B/lib" -E ALSA_CONFIG_DIR="$F/share" \
	-E ALSA_CONFIG_PATH="$B/alsa.conf:$B/asoundrc" "$B/portaudio-check" default 2>&1) ||
	fail "ALSA's configuration didn't load: $got"
said=$(sed -n 's/^[[:space:]]*echo "ChaOS: sound through \(.*\)"$/\1/p' "$ROOT/res/ports/run.sh")
grep -Eq '^[^#]*ALSA_CONFIG_PATH=' "$ROOT/res/ports/run.sh" &&
	fail "run.sh sets ALSA_CONFIG_PATH: this check doesn't read what that gives a port"
case "$said:$got" in
"the shared mixer:mixer") ;;
*) fail "run.sh's log says the port plays through '${said:-nothing}', but its default device plays to '$got'" ;;
esac
echo "ok: a port plays through the shared mixer, as its log says"
