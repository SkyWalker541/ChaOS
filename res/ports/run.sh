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
M="a:b1,b:b0,x:b3,y:b2,back:b8,start:b9,guide:b10,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b11,rightstick:b12,dpup:b13,dpdown:b14,dpleft:b15,dpright:b16,platform:Linux,"
CHAOS_PAD="19001eec427269636b2047616d657000,Brick Gamepad,$M
19000000427269636b2047616d657000,Brick Gamepad,$M"
export CHAOS_PAD
export SDL_GAMECONTROLLERCONFIG="$CHAOS_PAD"

# SDL for ports: the Brick's own with one change, so that it finds a
# keyboard with no udev, and one made after the game starts - which is how a
# port's buttons arrive, as keys on gptokeyb's virtual keyboard
# (res/ports/sdl2, tools/build-portlibs.sh). Ahead of the system's; a port
# that brings an SDL of its own puts that ahead of both.
export LD_LIBRARY_PATH="/usr/lib/chaos-ports${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
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

cd "$(dirname "$PORT")" || exit 1
/bin/bash "$PORT"
st=$?
echo "port exited $st"
sync
exit $st
