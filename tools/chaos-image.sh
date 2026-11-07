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
# build/tortos.elf (make -f mk/cross.mk DEVICE=brick).
set -e
IN=$1 OUT=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIATOM=${3:-$ROOT/../chaos-diatom/build/brick/diatom}
[ -n "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 TortOS-brick.img.xz OUT.img.xz [diatom]" >&2; exit 2; }
[ -f "$ROOT/build/tortos.elf" ] && [ -f "$DIATOM" ] || { echo "build tortos.elf and diatom first" >&2; exit 1; }

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

install -m 755 "$ROOT/build/tortos.elf" "$C/TortOS/tortos.elf"
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
[ -f "$(dirname "$DIATOM")/../../THIRD-PARTY.md" ] &&
	install -m 644 "$(dirname "$DIATOM")/../../THIRD-PARTY.md" "$C/Licenses/ChaOS-diatom.md"
chown -R 0:0 "$C"

mksquashfs "$W/root" "$W/new.sqfs" -comp lz4 -Xhc -b 131072 -noappend -no-xattrs -quiet > /dev/null
[ "$(stat -c%s "$W/new.sqfs")" -le $((COUNT * 512)) ] || { echo "the new root does not fit" >&2; exit 1; }
dd if=/dev/zero of="$W/card.img" bs=512 seek="$START" count="$COUNT" conv=notrunc status=none
dd if="$W/new.sqfs" of="$W/card.img" bs=512 seek="$START" conv=notrunc status=none
xz -T0 -6 -c "$W/card.img" > "$OUT"
echo "wrote $OUT"
