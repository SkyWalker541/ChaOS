#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ChaOS: runs one port (src/ports.h). The launcher starts this in a process
# group of its own and ends the group on power or Select+Start held.
#
#   run.sh <the port's script>
#
# CHAOS_USERDATA comes from the launcher. What the port prints goes to
# <userdata>/ports/last.log, the one place to look when a port won't start.

PORT="$1"
HERE=$(cd "$(dirname "$0")" && pwd)
PM=/roms/ports/PortMaster

# pkill and pgrep, which PortMaster's funcs.txt uses and BusyBox lacks
export PATH="$HERE/bin:$PATH"
export HOME="$CHAOS_USERDATA/ports/home"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CONFIG_HOME="$HOME/.config"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME"

# At most 8 MB of it written: a port that prints an error in a loop wrote
# 231 MB to the card while it ran (Doukutsu-rs, ALSA errors after its sound
# died, 2.7.9-test3), card writes the game waited on and a log Download logs
# then choked on. What comes after is read and dropped, so the port is never
# blocked or cut off by a full pipe. Flushed line by line until then, so a
# crash still leaves its last words.
fifo=/tmp/chaos-port-log.$$
rm -f "$fifo"
if mkfifo "$fifo" 2> /dev/null; then
	awk -v max=8388608 '{
		if (n <= max) { n += length($0) + 1; print; fflush() }
		else if (!said) { print "[ChaOS: the log stops here, at 8 MB; the port goes on]"; fflush(); said = 1 }
	}' < "$fifo" > "$CHAOS_USERDATA/ports/last.log" 2>&1 &
	exec > "$fifo" 2>&1
	rm -f "$fifo"
else
	exec > "$CHAOS_USERDATA/ports/last.log" 2>&1
fi
# ChaOS's own version is in CHAOS; VERSION is the one the updater compares
# and can lag (it said 2.7.8 on a 2.7.9-test5 card)
echo "ChaOS $(cat "$HERE/../CHAOS" 2>/dev/null || cat "$HERE/../VERSION" 2>/dev/null): $PORT"
date

# The Brick's pad for SDL, by position (see mod_ChaOS.txt): with SDL's
# checksum of the name and without, as SDL matches either
# Positional, as PortMaster and NextUI lay a pad out (their Xbox layout):
# nothing remapped. The d-pad was the left stick as well until 2.7.9-test5;
# ChaOS forces no mapping of its own on ports now, so a port gets the d-pad
# as a d-pad, as PortMaster expects on a device with no stick (Aquilus's
# "defaults aren't great", 2026-11-26).
# The Brick Pro's sticks are SDL's sticks too: CHAOS_STICKS, set by ChaOS from
# Calibrate Sticks ("leftx:a0,lefty:a1,..." - src/portsticks.h), empty on a
# Brick
M="a:b1,b:b0,x:b3,y:b2,back:b8,start:b9,guide:b10,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b11,rightstick:b12,dpup:b13,dpdown:b14,dpleft:b15,dpright:b16,${CHAOS_STICKS}platform:Linux,"
CHAOS_PAD="19001eec427269636b2047616d657000,Brick Gamepad,$M
19000000427269636b2047616d657000,Brick Gamepad,$M"
export CHAOS_PAD
export SDL_GAMECONTROLLERCONFIG="$CHAOS_PAD"

# SDL for ports: the Brick's own with one change, so that it finds a
# keyboard with no udev, and one made after the game starts - which is how a
# port's buttons arrive, as keys on gptokeyb's virtual keyboard
# (res/ports/sdl2, tools/build-portlibs.sh). Ahead of the system's; a port
# that brings an SDL of its own puts that ahead of both.
# and after it, the libraries fetched from ChaOS's pack for ports that need
# what the Brick hasn't got (src/libpack.h), kept in .userdata
export LD_LIBRARY_PATH="/usr/lib/chaos-ports:$CHAOS_USERDATA/ports/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# and last, for a port that runs on Weston only, libudev-zero (lib/,
# tools/build-libudev.sh), which Weston's libinput needs. Not for every port:
# an SDL that finds a libudev uses it to find the pad and the keyboard
# gptokeyb makes, and libudev-zero's guesses aren't the Brick's own scan, so
# with it on every port's path ports lost their buttons (Undertale's title
# screen, PortMaster's app) in 2.7.9-test3 (2026-11-26)
if [ -f "$PORT" ] && grep -q weston "$PORT" 2> /dev/null; then
	export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$HERE/lib"
fi
# Weston's seat daemon (PortMaster's weston_pkg) ties its seat to a virtual
# terminal unless told not to; the Brick has none (no /dev/tty0), so it never
# made Weston's session active and refused it every input device: Switchfin,
# The Brainvore and Pixel Wheels stopped on "failed to create compositor
# backend" (2026-11-26)
export SEATD_VTBOUND=0
# .NET (PortMaster's dotnet runtime) stops at once without ICU, which the
# Brick hasn't got: GameMaker ports' patchers run UndertaleModCli on it, and
# Lasagna Boy Classic's stopped on "Couldn't find a valid ICU package"
# (2026-11-26). Without ICU it runs with invariant culture, as Microsoft says.
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
# The sound hardware itself as a port's default device, not the shared mixer
# (res/ports/asound-ports.conf); ALSA's own configuration first, so only the
# default changes. Only when the hardware is free: while anything still holds
# the mixer (Muse's player keeps its device), the hardware would answer busy
# and the port would have no sound at all, so it keeps the mixer then.
snd=$(cat /proc/asound/card0/pcm0p/sub0/status 2> /dev/null | head -1)
if [ -f /usr/share/alsa/alsa.conf ] && [ -f "$HERE/asound-ports.conf" ] && [ "$snd" = closed ]; then
	export ALSA_CONFIG_PATH="/usr/share/alsa/alsa.conf:$HERE/asound-ports.conf"
	echo "ChaOS: sound straight to the hardware"
else
	echo "ChaOS: sound through the shared mixer (the hardware: ${snd:-unknown})"
fi
# how many there are before the port starts anything (mod_ChaOS.txt)
CHAOS_EV_BASE=$(ls /dev/input/event* 2> /dev/null | wc -l)
export CHAOS_EV_BASE

# ChaOS's settings for PortMaster, put back every time: a PortMaster update
# doesn't know them. libgl_ChaOS.txt too: gl4es without GBM (it says why)
for f in mod_ChaOS.txt libgl_ChaOS.txt; do
	if [ -d "$PM" ] && [ -f "$HERE/$f" ] && ! cmp -s "$HERE/$f" "$PM/$f"; then
		cp -f "$HERE/$f" "$PM/$f"
	fi
done

# The input devices once the port is going, for the log: the pad, and the
# keyboard gptokeyb makes of it
( sleep 4; echo "inputs, 4 s in:"; grep '^N:' /proc/bus/input/devices ) &

# Ended from outside (ChaOS's power or Select+Start): said so, and what the
# kernel had to say (an out-of-memory kill, a crash)
trap 'echo "run.sh: stopped by ChaOS"; dmesg 2> /dev/null | tail -15; exit 143' TERM HUP INT
# The CPU's clock as it was before the port (src/main.c port_cpu_begin), put
# back as this ends, however it ends: the launcher does too, but if it is the
# one that died, the clock would stay raised until it came back. Idempotent.
cpu_back() {
	[ -f /tmp/chaos-port-cpu ] || return 0
	read -r g mn mx < /tmp/chaos-port-cpu || return 0
	c=/sys/devices/system/cpu/cpu0/cpufreq
	case "$mn$mx" in *[!0-9]*|'') return 0 ;; esac
	echo "$mn" > $c/scaling_min_freq 2> /dev/null
	echo "$mx" > $c/scaling_max_freq 2> /dev/null
	[ "$(cat $c/scaling_governor 2> /dev/null)" = "$g" ] || echo "$g" > $c/scaling_governor 2> /dev/null
}
trap cpu_back EXIT

# ChaOS's Python (tools/build-python.sh), shipped as one squashfs and
# unpacked once into userdata (the kernel can't mount it, and the card has no
# symlinks: python3 is a copy), again only when the image changes. False if
# there is none.
img="$HERE/python/chaos-python.squashfs"
py="$CHAOS_USERDATA/ports/python"
chaos_python() {
	stamp="$(wc -c < "$img" 2> /dev/null | tr -d ' ')"
	if [ -n "$stamp" ] && [ "$(cat "$py/.chaos-stamp" 2> /dev/null)" != "$stamp" ]; then
		echo "unpacking Python ($stamp bytes)"
		rm -rf "$py.new"
		"$HERE/bin/unsquashfs" -no-progress -ignore-errors -f -d "$py.new" "$img" > /dev/null 2>&1
		if [ -x "$py.new/bin/python3.11" ]; then
			[ -x "$py.new/bin/python3" ] || cp -f "$py.new/bin/python3.11" "$py.new/bin/python3"
			echo "$stamp" > "$py.new/.chaos-stamp"
			rm -rf "$py" && mv "$py.new" "$py"
		else
			echo "Python didn't unpack"
			rm -rf "$py.new"
		fi
	fi
	[ -x "$py/bin/python3" ]
}

if [ "$PORT" = --portmaster ]; then
	# ChaOS: PortMaster's own app, on ChaOS's Python. Then PortMaster's own
	# launcher, with ChaOS's hooks (chaos/launch_hooks.txt).
	chaos_python || { echo "no Python for PortMaster"; exit 1; }
	# the SDL2 ports get, under the name pySDL2 looks for first (no symlinks
	# on the card: a copy, renewed when the system's changes)
	if [ -f /usr/lib/chaos-ports/libSDL2-2.0.so.0 ]; then
		mkdir -p "$py/lib/sdl"
		cmp -s /usr/lib/chaos-ports/libSDL2-2.0.so.0 "$py/lib/sdl/libSDL2.so" ||
			cp -f /usr/lib/chaos-ports/libSDL2-2.0.so.0 "$py/lib/sdl/libSDL2.so"
	fi
	# PYTHONHOME: found by name, not by where the binary thinks it runs from
	export PATH="$py/bin:$PATH" PYTHONHOME="$py"
	mkdir -p "$PM/chaos"
	cmp -s "$HERE/chaos/launch_hooks.txt" "$PM/chaos/launch_hooks.txt" ||
		cp -f "$HERE/chaos/launch_hooks.txt" "$PM/chaos/launch_hooks.txt"
	# ChaOS's theme for it (res/ports/chaos_theme), kept up to date in
	# PortMaster/themes/, and chosen once, the first time: a theme picked in
	# PortMaster's own options afterwards is left alone
	if [ -d "$HERE/chaos_theme" ] && ! cmp -s "$HERE/chaos_theme/theme.json" "$PM/themes/chaos_theme/theme.json"; then
		rm -rf "$PM/themes/chaos_theme" && mkdir -p "$PM/themes" &&
			cp -rf "$HERE/chaos_theme" "$PM/themes/chaos_theme"
	fi
	python3 - "$PM/config/config.json" << 'PYEOF'
import json, os, sys
p = sys.argv[1]
try:
    d = json.load(open(p))
    if not isinstance(d, dict): d = {}
except Exception:
    d = {}
if "theme" not in d:
    d["theme"] = "chaos_theme"
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + ".tmp", "w") as f: json.dump(d, f, indent=4)
    os.replace(p + ".tmp", p)
    print("PortMaster's theme: ChaOS")
PYEOF
	export controlfolder="$PM" PM_CUR_TTY=/dev/null
	cd "$PM" || exit 1
	/bin/bash -c 'source "$controlfolder/utils/pm_launch.txt" chaos'
	st=$?
	echo "PortMaster exited $st"
	sync
	exit $st
fi
# python3 and zip for the ports that call them - PortMaster's harbourmaster fetching a
# runtime (Dust Racing 2D), a patcher run on the first start (Lasagna Boy
# Classic's), a script - at the end of PATH, so a port's own Python (Pyxel's,
# Ren'Py's) still comes first; PYTHONHOME set for ChaOS's alone, by the
# wrapper (res/ports/pybin/python3). The Brick has neither of its own; zip is
# there whether Python is or not.
chaos_python && export CHAOS_PYTHON="$py"
export PATH="$PATH:$HERE/pybin"
# A port whose script puts /usr/lib on its library path ahead of what this
# exported (Undertale: LD_LIBRARY_PATH="/usr/lib:$GAMEDIR/lib:...") loads the
# system's SDL, which can't see the Brick's buttons with no udev - ChaOS's
# own (/usr/lib/chaos-ports) can - and the port took no button (Aquilus,
# 2.7.9-test3/4). NextUI patches such scripts; ChaOS leaves them as they
# are and preloads its SDL instead, which then stands for the system's.
# Only then, and only if the port carries no SDL of its own, and not for a
# Weston port, whose SDL is the runtime's.
if [ -f /usr/lib/chaos-ports/libSDL2-2.0.so.0 ] &&
   grep -Eq 'LD_LIBRARY_PATH=["'"'"']?[^#]*/usr/lib([:"'"'"'$]|$)' "$PORT" 2> /dev/null &&
   ! grep -q weston "$PORT" 2> /dev/null; then
	gd=$(sed -n 's|^[[:space:]]*\(export[[:space:]]\{1,\}\)\{0,1\}GAMEDIR=.*/ports/\([^/"'"'"' ]*\).*|\2|p' "$PORT" | head -1)
	if [ -z "$gd" ] || ! find "/roms/ports/$gd" -maxdepth 3 -name 'libSDL2-2.0.so*' 2> /dev/null | grep -q .; then
		export LD_PRELOAD="/usr/lib/chaos-ports/libSDL2-2.0.so.0${LD_PRELOAD:+:$LD_PRELOAD}"
		echo "ChaOS: its script puts /usr/lib first; ChaOS's SDL preloaded"
	fi
fi
# A port's saves in the card's common Saves folder (Saves/Ports/<its
# folder>/<folder>), as every other game's are, so the Saves that Syncthing
# shares carries them too: each folder a port keeps saves or settings in is
# bound from there over the one in the port's own folder while it runs. The
# first time, what the port had is copied there (and left where it was,
# under the bind, so nothing is lost); after, Saves' copy is the one, as
# synced from another device. Unbound as this ends (chaos_unbind), and
# again at the next start if this was cut off.
card=${CHAOS_USERDATA%/.userdata/*}
binds=/tmp/chaos-port-binds
chaos_unbind() {
	[ -f "$binds" ] || return 0
	while IFS= read -r m; do
		[ -n "$m" ] && /bin/umount "$m" 2> /dev/null
	done < "$binds"
	rm -f "$binds"
}
chaos_unbind
gd=$(sed -n 's|^[[:space:]]*\(export[[:space:]]\{1,\}\)\{0,1\}GAMEDIR=.*/ports/\([^/"'"'"' ]*\).*|\2|p' "$PORT" | head -1)
if [ -n "$gd" ] && [ -n "$card" ] && [ -d "/roms/ports/$gd" ] && [ -d "$card/Saves" ]; then
	for sub in saves save savedata savegames conf config cfg userdata user; do
		src="/roms/ports/$gd/$sub"
		dst="$card/Saves/Ports/$gd/$sub"
		[ -d "$src" ] || continue
		if [ ! -d "$dst" ]; then
			mkdir -p "$dst.new" && cp -a "$src/." "$dst.new/" && mv "$dst.new" "$dst" ||
				{ rm -rf "$dst.new"; echo "ChaOS: couldn't copy $sub to Saves"; continue; }
			echo "ChaOS: $gd/$sub copied to Saves/Ports"
		fi
		/bin/mount -o bind "$dst" "$src" && echo "$src" >> "$binds" &&
			echo "ChaOS: $gd/$sub kept in Saves/Ports"
	done
fi
trap 'cpu_back; chaos_unbind' EXIT
cd "$(dirname "$PORT")" || exit 1
/bin/bash "$PORT"
st=$?
echo "port exited $st"
# a crash or the kernel's out-of-memory killer leaves its mark here
dmesg 2> /dev/null | grep -iE "killed process|out of memory|segfault|unhandled|oom" | tail -10
sync
exit $st
