#!/bin/sh
# ChaOS: make check-btloop. Bluetooth's reconnect loop as the card has it,
# ChaOS/bt-reconnect.sh, written from launch.sh as tools/chaos-image.sh does
# (tools/chaos-btloop.sh says what broke): that it is cut out whole, that
# bt_on's call can be swapped for it, that it runs, and that a second start
# leaves the first alone.
#
#   tools/btloop-check.sh BUILD_DIR
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$1
[ -n "$B" ] || { echo "usage: tools/btloop-check.sh BUILD_DIR" >&2; exit 2; }
fail() { echo "btloop-check: $*" >&2; exit 1; }
rm -rf "$B"
mkdir -p "$B/bin" "$B/ChaOS"

# launch.sh as the image has it: ChaOS's folder (chaos-image.sh's first sed)
sed 's|/mnt/SDCARD/TortOS|/mnt/SDCARD/ChaOS|' "$ROOT/sd/brick/launch.sh" > "$B/launch.sh"
sh "$ROOT/tools/chaos-btloop.sh" "$B/launch.sh" "$B/bt-reconnect.sh"
grep -q '^TORTOS_DIR=/mnt/SDCARD/ChaOS$' "$B/bt-reconnect.sh" || fail "the script doesn't know ChaOS's folder"
grep -q 'tortos_btsink' "$B/bt-reconnect.sh" || fail "the loop's body didn't come with it"
[ "$(tail -n 1 "$B/bt-reconnect.sh")" = bt_reconnect ] || fail "the script doesn't run the loop"
[ "$(grep -c '^	bt_reconnect$' "$B/launch.sh")" = 1 ] || fail "bt_on no longer calls bt_reconnect once, as chaos-image.sh swaps"
echo "ok: the loop is launch.sh's own, cut out whole"

# It runs: with no adapter (hciconfig says nothing) the loop ends at once
printf '#!/bin/sh\nexit 0\n' > "$B/bin/hciconfig"
chmod 755 "$B/bin/hciconfig"
PATH="$B/bin:$PATH" CHAOS_BTLOOP_PID="$B/pid" timeout 10 sh "$B/bt-reconnect.sh" || fail "it didn't run"
[ -s "$B/pid" ] || fail "it left no pid for the launcher to stop it by"
echo "ok: it runs, and says its pid"

# A second start while one runs does nothing: a stand-in loop, by the name
# the launcher and the script look for
cp "$(command -v sleep)" "$B/bin/bt-reconnect"
"$B/bin/bt-reconnect" 30 &
held=$!
echo "$held" > "$B/pid"
PATH="$B/bin:$PATH" CHAOS_BTLOOP_PID="$B/pid" timeout 10 sh "$B/bt-reconnect.sh" || fail "the second start failed"
[ "$(cat "$B/pid")" = "$held" ] || { kill $held; fail "a second start replaced the running loop"; }
kill $held
wait $held 2> /dev/null || true
echo "ok: one loop at a time"
