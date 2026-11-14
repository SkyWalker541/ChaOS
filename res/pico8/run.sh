#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ChaOS: runs PICO-8 (src/pico8.h), the player's own copy, from the card's
# PICO-8 folder. The launcher starts this in a process group of its own, as it
# does a port, and ends the group on power or Select+Start held.
#
#   run.sh <cart>        play a cart
#   run.sh -splore       Splore, PICO-8's browser of shared carts
#
# CHAOS_USERDATA and CHAOS_CARD come from the launcher. What PICO-8 prints goes
# to <userdata>/pico-8/last.log.

HERE=$(cd "$(dirname "$0")" && pwd)
CARD=${CHAOS_CARD:-/mnt/SDCARD}
ENGINE="$CARD/PICO-8"
CARTS="$CARD/Roms/PICO-8"

# PICO-8 keeps everything under $HOME/.lexaloffle/pico-8: its config.txt,
# the carts' saves (cdata), Splore's downloads and favourites. In the
# userdata, so an update leaves it all where it was.
export HOME="$CHAOS_USERDATA/pico-8"
P8="$HOME/.lexaloffle/pico-8"
mkdir -p "$P8" "$CARD/Screenshots/PICO-8"

exec > "$HOME/last.log" 2>&1
echo "ChaOS $(cat "$HERE/../VERSION" 2>/dev/null): PICO-8 $*"
date

# config.txt, which PICO-8 reads at the start and writes back at the end,
# with ChaOS's lines in it whatever else it says: wget for downloads (the
# curl one beside this), the carts folder as PICO-8's own, and screenshots
# (F6/F7 in PICO-8) beside ChaOS's
CFG="$P8/config.txt"
setcfg() {
	if [ -f "$CFG" ] && grep -q "^$1 " "$CFG"; then
		sed -i "s|^$1 .*|$1 $2|" "$CFG"
	else
		echo "$1 $2" >> "$CFG"
	fi
}
setcfg use_wget 1
setcfg root_path "$CARTS/"
setcfg desktop_path "\"$CARD/Screenshots/PICO-8\""
setcfg windowed 0

export PATH="$HERE/bin:$PATH"
export CHAOS_CACERT="$HERE/../cacert.pem"

# The Brick's pad, by position as ChaOS's ports have it (res/ports/run.sh):
# b1 B (bottom), b0 A (right), b3 Y (left), b2 X (top). PICO-8 takes SDL's a
# and y as its O and b and x as its X, so B is O and A is X, as on spruceUI;
# Y is X and X is O again, and L1 is X and R1 is O, spruceUI's "racing"
# layout. SDL takes two buttons on one output. Start and MENU are PICO-8's
# pause menu.
M="a:b1,b:b0,x:b3,y:b2,x:b4,y:b5,start:b9,start:b10,back:b8,dpup:b13,dpdown:b14,dpleft:b15,dpright:b16,platform:Linux,"
export SDL_GAMECONTROLLERCONFIG="19001eec427269636b2047616d657000,Brick Gamepad,$M
19000000427269636b2047616d657000,Brick Gamepad,$M"

# The SDL ports use: the Brick's own, finding the pad with no udev
export LD_LIBRARY_PATH="/usr/lib/chaos-ports${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

trap 'echo "run.sh: stopped by ChaOS"; exit 143' TERM HUP INT

cd "$ENGINE" || exit 1
if [ "$1" = "-splore" ]; then
	./pico8_64 -splore -width 1024 -height 768 -root_path "$CARTS/"
else
	./pico8_64 -width 1024 -height 768 -run "$1"
fi
st=$?
echo "pico8 exited $st"
dmesg 2> /dev/null | grep -iE "killed process|out of memory|segfault|unhandled|oom" | tail -5
sync
exit $st
