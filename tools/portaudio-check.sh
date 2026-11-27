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
[ -n "$B" ] || { echo "usage: tools/portaudio-check.sh BUILD_DIR" >&2; exit 2; }
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
# and linked with ALSA, as Doukutsu-rs is: its ALSA among the program's own
aarch64-linux-gnu-gcc -std=gnu11 -O1 -Wall -Wextra -Werror -o "$B/portaudio-check-linked" \
	"$ROOT/tools/portaudio-check.c" -ldl -Wl,--no-as-needed "$F/lib/libasound.so.2"
aarch64-linux-gnu-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -shared -fPIC -o "$B/alsafix-source.so" \
	"$ROOT/tools/alsafix/chaos-alsafix.c" -ldl
cp "$ROOT/dist/brick/libchaos-alsafix.so" "$B/alsafix-shipped.so"
tar xJf "$ROOT/dist/brick/portlibs.tar.xz" -C "$B" usr/lib/libopenal.so.1 usr/lib/libopenal.so.1.24.3 \
	usr/lib/chaos-ports/libSDL2-2.0.so.0 usr/lib/chaos-ports/libSDL2-2.0.so.0.3200.10
cp "$F/lib/libasound.so.2" "$B/lib/"
# Only the fixture's configuration: alsa.conf's hooks would load this
# machine's own (/etc/asound.conf, /etc/alsa/conf.d - pipewire's, on a
# desktop - and ~/.asoundrc), after everything else, and they'd win. They
# point into the build directory instead, /etc/asound.conf at the Brick's
# for step 2 and at nothing for step 1; HOME and XDG_CONFIG_HOME too.
for d in /var/lib/alsa/conf.d /usr/etc/alsa/conf.d /etc/alsa/conf.d; do
	grep -q "\"$d\"" "$F/share/alsa.conf" ||
		fail "the Brick's alsa.conf no longer loads $d as it did: update this check"
done
grep -q '"/etc/asound.conf|||/usr/etc/asound.conf"' "$F/share/alsa.conf" ||
	fail "the Brick's alsa.conf no longer loads /etc/asound.conf as it did: update this check"
hooks() {
	sed -e "s#\"/var/lib/alsa/conf.d\"#\"$B/none\"#" -e "s#\"/usr/etc/alsa/conf.d\"#\"$B/none\"#" \
	    -e "s#\"/etc/alsa/conf.d\"#\"$B/none\"#" \
	    -e "s#\"/etc/asound.conf|||/usr/etc/asound.conf\"#\"$1\"#" "$F/share/alsa.conf"
}
hooks "$B/none" > "$B/alsa-null.conf"
hooks "$F/etc/asound.conf" > "$B/alsa.conf"
mkdir -p "$B/home"
# the Brick's mixer takes 48 kHz; ports ask for 44.1, so plug converts
printf 'pcm.!default { type plug; slave { pcm "null"; rate 48000; format S16_LE; channels 2 } }\n' > "$B/null.conf"
printf '[general]\nfrequency = 44100\n' > "$B/alsoft.conf"

run() {
	qemu-aarch64 -L "$LIBC" -E HOME="$B/home" -E XDG_CONFIG_HOME="$B/home" "$@"
}

for h in source shipped; do
	for way in alsa openal sdl; do
		prog=$B/portaudio-check
		[ $way = alsa ] && prog=$B/portaudio-check-linked
		out=$(run -E LD_PRELOAD="$B/alsafix-$h.so" \
			-E LD_LIBRARY_PATH="$B/lib:$B/usr/lib:$B/usr/lib/chaos-ports" \
			-E ALSA_CONFIG_DIR="$F/share" -E ALSA_CONFIG_PATH="$B/alsa-null.conf:$B/null.conf" \
			-E ALSOFT_CONF="$B/alsoft.conf" -E ALSOFT_LOGLEVEL=0 -E SDL_AUDIODRIVER=alsa \
			"$prog" $way 2>&1) ||
			fail "with the $h helper, $way: $(echo "$out" | grep -v '^\[ALSOFT\]' | tail -3)"
	done
	echo "ok: ALSA linked, OpenAL Soft and SDL all play with the $h ALSA helper"
done

# 2. Where a port's sound really goes, as res/ports/sound.sh (run.sh's) sets
# it up: with no headset, with one, and with a sink no headset is behind -
# against what the port's log says. Run in BusyBox's shell and tools, as on
# the Brick, when this machine has BusyBox (CI installs it).
SH=sh
mkdir -p "$B/bb"
if command -v busybox > /dev/null 2>&1; then
	SH="busybox sh"
	for t in sed grep printf cat; do ln -sf "$(command -v busybox)" "$B/bb/$t"; done
fi
# the system's alsa.conf as sound.sh reads it - /etc/asound.conf named as on
# the Brick - with only this machine's own conf.d kept out
hooks '/etc/asound.conf|||/usr/etc/asound.conf' > "$B/alsa-brick.conf"
mac=A8_F5_E1_4A_93_71
printf 'pcm.bt_%s {\n    type bluealsa\n    device "%s"\n    profile "a2dp"\n}\n' \
	"$mac" "$(echo $mac | tr _ :)" > "$B/asoundrc-headset"
: > "$B/asoundrc"     # the launcher's .asoundrc, as it is with no headset paired

# sound.sh's choice, then the default device ALSA resolves with it
port_sound() {     # SINK_NAME ASOUNDRC [STUBS] -> "said:got"
	rm -f "$B/chaos-ports-alsa.conf" "$B/chaos-ports-headset.conf"
	out=$(PATH="$B/bb:${3:+$3:}$PATH" ALSA_CONFIG_PATH="$B/alsa.conf:$2" $SH -c \
		'. "$0" && chaos_port_sound "$1" "$2" "$3" "$4" && echo "PATH=$ALSA_CONFIG_PATH"' \
		"$ROOT/res/ports/sound.sh" "$1" "$2" "$B/alsa-brick.conf" "$B") || fail "sound.sh failed: $out"
	said=$(echo "$out" | sed -n 's/^ChaOS: sound through //p')
	path=$(echo "$out" | sed -n 's/^PATH=//p')
	# the device's /etc/asound.conf is the fixture's here (qemu-user can't
	# redirect alsa.conf's access() to it)
	[ -f "$B/chaos-ports-alsa.conf" ] &&
		sed -i "s#\"/etc/asound.conf|||/usr/etc/asound.conf\"#\"$F/etc/asound.conf\"#" "$B/chaos-ports-alsa.conf"
	got=$(run -E LD_LIBRARY_PATH="$B/lib" -E ALSA_CONFIG_DIR="$F/share" \
		-E ALSA_CONFIG_PATH="$path" "$B/portaudio-check" default 2>&1) ||
		fail "ALSA's configuration didn't load: $got"
	echo "$said:$got"
}

[ "$(port_sound "" "$B/asoundrc")" = "the shared mixer:mixer" ] ||
	fail "no headset: $(port_sound "" "$B/asoundrc") (the log's word, then where it plays)"
echo "ok: with no headset, a port plays through the shared mixer, as its log says"

r=$(port_sound "bt_$mac" "$B/asoundrc-headset")
[ "$r" = "the Bluetooth headset:bt_$mac" ] || fail "a headset: $r"
grep -q "$mac\|$(echo $mac | tr _ :)" "$ROOT/res/ports/sound.sh" && fail "a headset's address in sound.sh"
echo "ok: with a headset, a port plays through it, as its log says"

# bluealsa's list, as on the Brick: the headset's A2DP stream there, or not
# yet (connected, no stream: the mixer, not a device that opens nothing)
mkdir -p "$B/a2dp" "$B/noa2dp"
printf '#!/bin/sh\necho "hci0: %s [Headset], trusted audio-headset"\necho "  A2DP (SBC): S16_LE 2 channels 48000 Hz"\n' \
	"$(echo $mac | tr _ :)" > "$B/a2dp/bluealsa-aplay"
printf '#!/bin/sh\necho "hci0: %s [Headset], trusted audio-headset"\n' \
	"$(echo $mac | tr _ :)" > "$B/noa2dp/bluealsa-aplay"
chmod 755 "$B/a2dp/bluealsa-aplay" "$B/noa2dp/bluealsa-aplay"
r=$(port_sound "bt_$mac" "$B/asoundrc-headset" "$B/a2dp")
[ "$r" = "the Bluetooth headset:bt_$mac" ] || fail "a headset with its stream: $r"
r=$(port_sound "bt_$mac" "$B/asoundrc-headset" "$B/noa2dp")
[ "$r" = "the shared mixer:mixer" ] || fail "a headset with no stream yet: $r"
echo "ok: only a headset whose stream is there; one without keeps the mixer"

# a sink whose PCM the launcher's .asoundrc doesn't have (unpaired since), and
# one that isn't a PCM's name at all: the mixer, not a device that can't open
r=$(port_sound "bt_$mac" "$B/asoundrc")
[ "$r" = "the shared mixer:mixer" ] || fail "a sink with no PCM: $r"
r=$(port_sound "bt_x\"; touch $B/owned #" "$B/asoundrc-headset")
[ "$r" = "the shared mixer:mixer" ] && [ ! -e "$B/owned" ] || fail "a sink that isn't a name: $r"
echo "ok: a headset that can't be opened, or a name that isn't one, keeps the mixer"

# and run.sh asks sound.sh, and sets nothing of ALSA's itself
grep -q '^	chaos_port_sound "$CHAOS_PORT_SINK" ' "$ROOT/res/ports/run.sh" || fail "run.sh doesn't ask sound.sh with the launcher's choice"
grep -Eq '^[^#]*ALSA_CONFIG_PATH=' "$ROOT/res/ports/run.sh" &&
	fail "run.sh sets ALSA_CONFIG_PATH itself: this check doesn't read what that gives a port"
grep -q 'res/ports/sound.sh' "$ROOT/tools/chaos-image.sh" || fail "the card image doesn't carry sound.sh"
# a Weston port, or one with its own libasound, is never sent to a headset
grep -q 'grep -q weston "$PORT"' "$ROOT/res/ports/run.sh" && grep -q "libasound.so\*" "$ROOT/res/ports/run.sh" ||
	fail "run.sh sends Weston ports, or ports with their own libasound, to a headset"
echo "ok: run.sh's sound is sound.sh's"
