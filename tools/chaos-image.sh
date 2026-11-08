#!/bin/sh
# A ChaOS card image for the TrimUI Brick, made from a TortOS release image.
#
#   tools/chaos-image.sh TortOS-v2.1.1-brick.img.xz OUT.img.xz [DIATOM]
#
# Until ChaOS has a plastron build of its own, this takes TortOS's released
# image and changes only the copy of TortOS it carries for the card
# (/usr/share/tortos/card in the root squashfs, copied onto the card at first
# boot by /etc/init.d/tortos): ChaOS's tortos.elf and diatom, the overlays it
# ships (TortOS/overlays, put in Overlays/ at first start), the boot
# animation (plastron's splash frames), ChaOS Transfer's
# web page (TortOS/res/web), its credits in
# Licenses/, and its version (CHAOS_VERSION) as TortOS/VERSION and TortOS/CHAOS.
# And /etc/init.d/tortos learns the version, and that a card whose TortOS is
# not ChaOS (no TortOS/CHAOS) gets ChaOS's copy whatever its version says, so
# an update from TortOS to ChaOS takes. The boot partition, the kernel and
# every other file are the release's, byte for byte.
#
# Needs xz, unsquashfs and mksquashfs, and root (to keep the files' owners).
# DIATOM defaults to ../chaos-diatom/build/brick/diatom; tortos.elf is
# build/tortos.elf (make -f mk/cross.mk DEVICE=brick), or TORTOS_ELF. The
# release (.github/workflows/release.yml) uses dist/brick's, with
# DIATOM_NOTICE naming ChaOS-diatom's THIRD-PARTY.md, and ROOT_OUT, where the
# new root squashfs is copied for updates over Wi-Fi.
set -e
IN=$1 OUT=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIATOM=${3:-$ROOT/../chaos-diatom/build/brick/diatom}
[ -n "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 TortOS-brick.img.xz OUT.img.xz [diatom]" >&2; exit 2; }
ELF=${TORTOS_ELF:-$ROOT/build/tortos.elf}
NOTICE=${DIATOM_NOTICE:-$(dirname "$DIATOM")/../../THIRD-PARTY.md}
[ -f "$ELF" ] && [ -f "$DIATOM" ] || { echo "build tortos.elf and diatom first" >&2; exit 1; }

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
xz -dc "$IN" > "$W/card.img"

# The root partition, by its GPT name: start and length in 512-byte sectors.
set -- $(python3 - "$W/card.img" <<'EOF'
import struct, sys
f = open(sys.argv[1], 'rb'); f.seek(512); h = f.read(92)
lba, num, size = struct.unpack('<QII', h[72:88]); f.seek(lba * 512)
for i in range(num):
    e = f.read(size)
    if e[56:128].decode('utf-16le').rstrip('\0') == 'plastron-root':
        a, b = struct.unpack('<QQ', e[32:48]); print(a, b - a + 1)
EOF
)
START=$1 COUNT=$2
[ -n "$COUNT" ] || { echo "no plastron-root partition in $IN" >&2; exit 1; }

dd if="$W/card.img" of="$W/root.sqfs" bs=512 skip="$START" count="$COUNT" status=none
unsquashfs -q -d "$W/root" "$W/root.sqfs" > /dev/null
C=$W/root/usr/share/tortos/card
[ -f "$C/TortOS/tortos.elf" ] || { echo "no TortOS in this image's root" >&2; exit 1; }
INIT=$W/root/etc/init.d/tortos

install -m 755 "$ELF" "$C/TortOS/tortos.elf"
install -m 755 "$DIATOM" "$C/TortOS/diatom"
rm -rf "$C/TortOS/overlays"
cp -R "$ROOT/res/overlays" "$C/TortOS/overlays"
find "$C/TortOS/overlays" -type d -exec chmod 755 {} +
find "$C/TortOS/overlays" -type f -exec chmod 644 {} +
# ChaOS Transfer's page: the launcher serves it off the card, so it is the
# card's copy that has to be ChaOS's (mk/payload.sh does the same)
rm -f "$C/TortOS/res/web/"*.html "$C/TortOS/res/web/"*.svg "$C/TortOS/res/web/"*.js "$C/TortOS/res/web/"*.css
install -m 644 "$ROOT/res/web/"* "$C/TortOS/res/web/"
install -m 644 "$ROOT/res/web/index.html" "$C/TortOS/res/web/wire.html"
# The boot: ChaOS's logo flying together (tools/genchaosmotion.py), in
# place of TortOS's flyby, for plastron's splash
install -m 644 "$ROOT/res/boot/chaos-splash-brick.bin" "$W/root/usr/share/splash/frames.bin"
# The games partition is called ChaOS on a computer: made so on a new card's
# first boot, and a TortOS card's TORTOS renamed on its first ChaOS boot,
# before it is mounted. ChaOS Installer knows both names.
D=$W/root/etc/init.d/datapart
grep -q 'mkfs.exfat -c 32K -L TORTOS' "$D" || { echo "datapart has changed: check the label" >&2; exit 1; }
sed -i 's/mkfs.exfat -c 32K -L TORTOS/mkfs.exfat -c 32K -L ChaOS/; s/formatted as exFAT, label TORTOS/formatted as exFAT, label ChaOS/' "$D"
sed -i '/^\t\trepair \$games$/a\
		# ChaOS: a TortOS card named ChaOS from now on\
		exfatlabel $games 2>/dev/null | grep -q "TORTOS$" \&\&\
			exfatlabel $games ChaOS > /dev/null 2>\&1 \&\&\
			echo "datapart: $games renamed ChaOS" > /dev/kmsg' "$D"
grep -q 'exfatlabel $games ChaOS' "$D" || { echo "datapart: the rename did not go in" >&2; exit 1; }
# What a computer calls the Brick on the USB cable
sed -i 's|echo TortOS > \$g/strings/0x409/manufacturer|echo ChaOS > $g/strings/0x409/manufacturer|' "$W/root/etc/init.d/usbgadget"
install -m 644 "$ROOT/CREDITS.md" "$C/Licenses/ChaOS.md"
V=$(tr -d ' \n' < "$ROOT/CHAOS_VERSION")
echo "$V" > "$C/TortOS/VERSION"
echo "$V" > "$C/TortOS/CHAOS"
chmod 644 "$C/TortOS/VERSION" "$C/TortOS/CHAOS"
IFS=. read -r va vb vc < "$ROOT/CHAOS_VERSION"
INIT=$W/root/etc/init.d/tortos
grep -q '^SEED_V=' "$INIT" || { echo "no SEED_V in the image's init.d/tortos" >&2; exit 1; }
sed -i "s/^SEED_V=.*/SEED_V=$((va * 1000000 + vb * 1000 + ${vc:-0}))/" "$INIT"
grep -q '^newer_seed() {$' "$INIT" || { echo "no newer_seed in the image's init.d/tortos" >&2; exit 1; }
sed -i '/^newer_seed() {$/a\
	# ChaOS: a card whose TortOS is not ChaOS gets ChaOS, whatever its version\
	[ -f "$CARD/TortOS/CHAOS" ] || return 0' "$INIT"
[ -f "$NOTICE" ] && install -m 644 "$NOTICE" "$C/Licenses/ChaOS-diatom.md"

# ChaOS's own folder on the card is ChaOS/, where TortOS's was TortOS/: the
# image's copy, the launch script's and the Bluetooth player's paths, and
# plastron's init.d/tortos, which copies it onto the card. A TortOS card has
# no ChaOS/launch.sh, so its first ChaOS boot copies ChaOS on; then the old
# TortOS/ (TortOS's own files only, by its own design) goes, keeping where
# the shelf was and what was playing. The launcher looks in /mnt/SDCARD/ChaOS
# (src/device/brick.c), ChaOS Installer in either.
mv "$C/TortOS" "$C/ChaOS"
sed -i 's|/mnt/SDCARD/TortOS|/mnt/SDCARD/ChaOS|' "$C/ChaOS/launch.sh" "$W/root/etc/init.d/bluetooth"
grep -q '^TORTOS_DIR=/mnt/SDCARD/ChaOS$' "$C/ChaOS/launch.sh" || { echo "launch.sh has changed: check its folder" >&2; exit 1; }
grep -q 'SDCARD/ChaOS/btplayer' "$W/root/etc/init.d/bluetooth" || { echo "bluetooth has changed: check its path" >&2; exit 1; }
sed -i 's|\$CARD/TortOS|$CARD/ChaOS|g; s|\$SEED/TortOS|$SEED/ChaOS|g; s|= TortOS \]|= ChaOS ]|' "$INIT"
grep -q 'TortOS copied onto the card' "$INIT" || { echo "init.d/tortos has changed: check the seed" >&2; exit 1; }
cat > "$W/move.sh" <<'MOVE'
	# ChaOS: TortOS's folder from before, once ChaOS is on: where the shelf
	# was and what was playing kept, the rest, TortOS's own files, gone
	if [ -d "$CARD/TortOS" ]; then
		for f in .last .playing; do
			[ -f "$CARD/TortOS/$f" ] && [ ! -f "$CARD/ChaOS/$f" ] && cp "$CARD/TortOS/$f" "$CARD/ChaOS/"
		done
		rm -rf "$CARD/TortOS" && echo "tortos: TortOS's old folder removed" > /dev/kmsg
	fi
MOVE
sed -i "/echo \"tortos: TortOS copied onto the card\" > \/dev\/kmsg/r $W/move.sh" "$INIT"
sed -i 's/tortos: TortOS copied onto the card/tortos: ChaOS copied onto the card/' "$INIT"
grep -q "TortOS's old folder removed" "$INIT" || { echo "init.d/tortos: the move did not go in" >&2; exit 1; }
# Updates over Wi-Fi (src/update.h): which TortOS release this card's boot
# partition is, so the launcher offers only updates for the same one, and
# launch.sh's last line rebooting instead of powering off when the launcher
# leaves the flag for an update to install at the next boot
tr -d ' \n' < "$ROOT/TORTOS_BASE" > "$C/ChaOS/BASE"
chmod 644 "$C/ChaOS/BASE"
L=$C/ChaOS/launch.sh
[ "$(tail -n 1 "$L")" = "poweroff" ] || { echo "launch.sh has changed: check how it ends" >&2; exit 1; }
sed -i '$d' "$L"
cat >> "$L" <<'EOF2'
# ChaOS: an update installs at the next boot (src/update.h): boot again rather
# than power off
if [ -f /tmp/chaos_reboot ]; then reboot; else poweroff; fi
EOF2
chown -R 0:0 "$C"

mksquashfs "$W/root" "$W/new.sqfs" -comp lz4 -Xhc -b 131072 -noappend -no-xattrs -quiet > /dev/null
[ "$(stat -c%s "$W/new.sqfs")" -le $((COUNT * 512)) ] || { echo "the new root does not fit" >&2; exit 1; }
# The root alone too, for updates over Wi-Fi: what plastron's boot writes
# over the root partition when it finds it on the card (src/update.h)
if [ -n "$ROOT_OUT" ]; then cp "$W/new.sqfs" "$ROOT_OUT"; fi
dd if=/dev/zero of="$W/card.img" bs=512 seek="$START" count="$COUNT" conv=notrunc status=none
dd if="$W/new.sqfs" of="$W/card.img" bs=512 seek="$START" conv=notrunc status=none
xz -T0 -6 -c "$W/card.img" > "$OUT"
echo "wrote $OUT"
