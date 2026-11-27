#!/bin/sh
# ChaOS: launch.sh's Bluetooth reconnect loop as a script of its own,
# ChaOS/bt-reconnect.sh, so the launcher can start it too.
#
# What broke: the loop (bt_reconnect, TortOS's launch.sh) is what tells the
# system a headset is connected (/tmp/tortos_btsink, which the sound follows)
# and reconnects a remembered one, and only launch.sh's bt_on started it, at
# boot, when Bluetooth was left on. With Bluetooth off at boot - every new
# card - turning it on in the menu started Bluetooth but never the loop: a
# headset paired and connected there said "Connected" and the sound stayed on
# the speaker until a reboot (found 2026-11-27).
#
# Not a second loop: the functions are cut out of launch.sh itself at build
# time (bt_a2dp_ready, bt_reconnect), and launch.sh's bt_on runs this script
# in their place (tools/chaos-image.sh), so boot and the menu run one copy,
# and only one at a time: a second start leaves the first running. The
# launcher stops it when Bluetooth goes off (src/bt.c bt_loop_stop).
#
#   tools/chaos-btloop.sh LAUNCH_SH OUT
set -e
L=$1
OUT=$2
fail() { echo "chaos-btloop: $*" >&2; exit 1; }

for f in bt_a2dp_ready bt_reconnect; do
	[ "$(grep -c "^$f() {\$" "$L")" = 1 ] || fail "launch.sh has changed: no one $f()"
done
grep -q '^TORTOS_DIR=' "$L" || fail "launch.sh has changed: no TORTOS_DIR"

{
	cat <<'HEAD'
#!/bin/sh
# ChaOS: Bluetooth's reconnect loop, launch.sh's own bt_reconnect, on its own
# so the launcher can start it when Bluetooth is turned on from the menu.
# Written at build time by tools/chaos-btloop.sh; edit launch.sh, not this.
# One at a time: a start while one runs does nothing.
HEAD
	grep '^TORTOS_DIR=' "$L" | head -1
	cat <<'GUARD'
PIDFILE=${CHAOS_BTLOOP_PID:-/tmp/chaos-btloop.pid}
if [ -r "$PIDFILE" ]; then
	read -r old < "$PIDFILE" || old=
	case "$old" in
	''|*[!0-9]*) ;;
	*) grep -q bt-reconnect "/proc/$old/cmdline" 2> /dev/null && exit 0 ;;
	esac
fi
echo $$ > "$PIDFILE"
# from the launcher, just after the stack came up: both profiles registered
# with BlueZ before any connect, as launch.sh's bt_on waits before its loop
[ "$1" = settle ] && sleep 3
# bt_pcm_name, as launch.sh has it
[ -f "$TORTOS_DIR/bt-alsa.sh" ] && . "$TORTOS_DIR/bt-alsa.sh"

GUARD
	sed -n '/^bt_a2dp_ready() {$/,/^}$/p' "$L"
	echo
	sed -n '/^bt_reconnect() {$/,/^}$/p' "$L"
	echo
	echo 'bt_reconnect'
} > "$OUT"
chmod 755 "$OUT"
sh -n "$OUT" || fail "the script it wrote doesn't parse"
