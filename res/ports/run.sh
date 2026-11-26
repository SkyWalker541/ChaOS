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

exec > "$CHAOS_USERDATA/ports/last.log" 2>&1
echo "ChaOS $(cat "$HERE/../VERSION" 2>/dev/null): $PORT"
date

# The Brick's pad for SDL, by position (see mod_ChaOS.txt): with SDL's
# checksum of the name and without, as SDL matches either
# The d-pad is the left stick as well: the Brick has no stick, and a port
# that steers with one (PortMaster's analog_1) can then be played. SDL takes
# a button on half an axis, and one button on two outputs.
M="a:b1,b:b0,x:b3,y:b2,back:b8,start:b9,guide:b10,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b11,rightstick:b12,dpup:b13,dpdown:b14,dpleft:b15,dpright:b16,-leftx:b15,+leftx:b16,-lefty:b13,+lefty:b14,platform:Linux,"
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
# how many there are before the port starts anything (mod_ChaOS.txt)
CHAOS_EV_BASE=$(ls /dev/input/event* 2> /dev/null | wc -l)
export CHAOS_EV_BASE

# ChaOS's settings for PortMaster, put back every time: a PortMaster update
# doesn't know them
if [ -d "$PM" ] && ! cmp -s "$HERE/mod_ChaOS.txt" "$PM/mod_ChaOS.txt"; then
	cp -f "$HERE/mod_ChaOS.txt" "$PM/mod_ChaOS.txt"
fi

# The input devices once the port is going, for the log: the pad, and the
# keyboard gptokeyb makes of it
( sleep 4; echo "inputs, 4 s in:"; grep '^N:' /proc/bus/input/devices ) &

# Ended from outside (ChaOS's power or Select+Start): said so, and what the
# kernel had to say (an out-of-memory kill, a crash)
trap 'echo "run.sh: stopped by ChaOS"; dmesg 2> /dev/null | tail -15; exit 143' TERM HUP INT

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
# python3 for the ports that call it - PortMaster's harbourmaster fetching a
# runtime (Dust Racing 2D), a patcher run on the first start (Lasagna Boy
# Classic's), a script - at the end of PATH, so a port's own Python (Pyxel's,
# Ren'Py's) still comes first; PYTHONHOME set for ChaOS's alone, by the
# wrapper (res/ports/pybin/python3). The Brick has none of its own.
if chaos_python; then
	export CHAOS_PYTHON="$py" PATH="$PATH:$HERE/pybin"
fi
cd "$(dirname "$PORT")" || exit 1
/bin/bash "$PORT"
st=$?
echo "port exited $st"
# a crash or the kernel's out-of-memory killer leaves its mark here
dmesg 2> /dev/null | grep -iE "killed process|out of memory|segfault|unhandled|oom" | tail -10
sync
exit $st
