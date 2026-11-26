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
# Needs xz, unsquashfs and mksquashfs, mtools, and root (to keep the files'
# owners).
# DIATOM defaults to ../chaos-diatom/build/brick/diatom; tortos.elf is
# build/tortos.elf (make -f mk/cross.mk DEVICE=brick), or TORTOS_ELF. The
# release (.github/workflows/release.yml) uses dist/brick's, with
# DIATOM_NOTICE naming ChaOS-diatom's THIRD-PARTY.md, ROOT_OUT, where the
# new root squashfs is copied for updates over Wi-Fi, and APP_OUT, where ChaOS's
# own files go for the small ones.
set -e
IN=$1 OUT=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIATOM=${3:-$ROOT/../chaos-diatom/build/brick/diatom}
[ -n "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 TortOS-brick.img.xz OUT.img.xz [diatom]" >&2; exit 2; }
ELF=${TORTOS_ELF:-$ROOT/build/tortos.elf}
# The updater init runs for an update (TortOS 2.2.0's, /usr/sbin/tortos-update
# on the root), built from tools/tortos-update/ to take ChaOS's images:
# TortOS's own takes only TortOS-v<version>-<device>.img.xz and would refuse
# every ChaOS release. make -f mk/cross.mk build/tortos-update DEVICE=brick.
UPDATER=${TORTOS_UPDATE:-$ROOT/build/tortos-update}
NOTICE=${DIATOM_NOTICE:-$(dirname "$DIATOM")/../../THIRD-PARTY.md}
[ -f "$ELF" ] && [ -f "$DIATOM" ] || { echo "build tortos.elf and diatom first" >&2; exit 1; }
[ -f "$UPDATER" ] || { echo "build tortos-update first (mk/cross.mk)" >&2; exit 1; }
# ChaOS: bash, for ports (src/ports.h): static, aarch64 (tools/build-bash.sh)
BASH_BIN=${CHAOS_BASH:-$ROOT/dist/brick/bash}
file "$BASH_BIN" | grep -q 'ARM aarch64.*statically linked' ||
	{ echo "$BASH_BIN isn't a static aarch64 bash (tools/build-bash.sh)" >&2; exit 1; }
# ChaOS's, for the Brick: static (it runs once the root is gone), aarch64,
# with the request it reads and ChaOS's image names. The launcher has the
# names too, so they alone don't tell the two apart.
grep -q 'ChaOS-v%s-%s.img.xz' "$UPDATER" && grep -q '/games/.update/request' "$UPDATER" &&
	file "$UPDATER" | grep -q 'ARM aarch64.*statically linked' ||
	{ echo "$UPDATER isn't ChaOS's updater for the Brick" >&2; exit 1; }

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
# ChaOS's updater in place of TortOS's, where init's ::restart: line runs it
[ -f "$W/root/usr/sbin/tortos-update" ] || { echo "no updater in this image's root: is it TortOS 2.2.0 or later?" >&2; exit 1; }
grep -q '::restart:/usr/sbin/tortos-update' "$W/root/etc/inittab" || { echo "inittab has changed: check the updater" >&2; exit 1; }
install -m 755 "$UPDATER" "$W/root/usr/sbin/tortos-update"
install -m 755 "$DIATOM" "$C/TortOS/diatom"

# ChaOS's plastron (github.com/SkyWalker541/plastron, Eric Reinsmidt's with
# ChaOS's changes for the Brick Pro: dist/brick/plastron/README.md): its boot
# loader, which tells the Brick Pro from the Brick and sets up the Pro's DRAM;
# its boot script, which loads the Pro's device tree on a Pro; that tree; and
# the button driver, which reads the Pro's L3, R3 and sticks; and the Pro's
# own kernel, ImagePro.lz4, TortOS's with the Pro's panel added, which only a
# Pro boots. TortOS's kernel, its brick.dtb and every other module stay as they
# are: a Brick boots what it did. The boot partition is FAT, written with mtools; the boot loader goes
# 128 KiB into the card, ahead of the partitions.
# Only where PLASTRON_REV says this release carries them (from 2.6.0)
if [ -f "$ROOT/PLASTRON_REV" ]; then
	P=$ROOT/dist/brick/plastron
	for f in boot.scr brickpro.dtb u-boot-sunxi-with-spl.bin brick-pad.ko ImagePro.lz4; do
		[ -f "$P/$f" ] || { echo "no $f in dist/brick/plastron" >&2; exit 1; }
	done
	[ -f "$W/root/usr/lib/brick/brick-pad.ko" ] || { echo "no brick-pad.ko in the root: check plastron" >&2; exit 1; }
	install -m 644 "$P/brick-pad.ko" "$W/root/usr/lib/brick/brick-pad.ko"
	set -- $(python3 - "$W/card.img" <<-'EOF'
	import struct, sys
	f = open(sys.argv[1], 'rb'); f.seek(512); h = f.read(92)
	lba, num, size = struct.unpack('<QII', h[72:88]); f.seek(lba * 512)
	for i in range(num):
	    e = f.read(size)
	    if e[56:128].decode('utf-16le').rstrip('\0') == 'boot':
	        a, b = struct.unpack('<QQ', e[32:48]); print(a)
	EOF
	)
	BOOT_AT=$(($1 * 512))
	[ "$BOOT_AT" -gt 0 ] || { echo "no boot partition in $IN" >&2; exit 1; }
	mdir -i "$W/card.img@@$BOOT_AT" ::/brick.dtb > /dev/null || { echo "no brick.dtb on the boot partition" >&2; exit 1; }
	mcopy -o -i "$W/card.img@@$BOOT_AT" "$P/boot.scr" "$P/brickpro.dtb" "$P/ImagePro.lz4" ::/
	[ "$(stat -c%s "$P/u-boot-sunxi-with-spl.bin")" -le $(( (8192 - 256) * 512 )) ] || { echo "the boot loader is too big" >&2; exit 1; }
	dd if="$P/u-boot-sunxi-with-spl.bin" of="$W/card.img" bs=1024 seek=128 conv=notrunc status=none
fi
# ChaOS: PlayStation, on PCSX-ReARMed (tools/build-pcsx.sh): ChaOS's own core,
# beside TortOS's
[ -d "$C/TortOS/cores" ] || { echo "no cores folder in this image: check the PlayStation core" >&2; exit 1; }
[ ! -e "$C/TortOS/cores/pcsx_rearmed_libretro.so" ] || { echo "TortOS ships PCSX-ReARMed now: check which to use" >&2; exit 1; }
install -m 755 "$ROOT/dist/brick/pcsx_rearmed_libretro.so" "$C/TortOS/cores/pcsx_rearmed_libretro.so"
# ChaOS: ports (src/ports.h). PortMaster's standard places, so its ports and
# its own app work unchanged: bash at /bin/bash (the root has only BusyBox's
# sh), /roms for the card's Roms (the card is case-blind, so /roms/ports is
# Roms/Ports), and OS_NAME, which PortMaster reads as the system's name and
# so finds mod_ChaOS.txt by. On the card, ChaOS/ports: what runs a port and
# ChaOS's settings for PortMaster.
[ ! -e "$W/root/bin/bash" ] || { echo "the root has a bash already: check ports" >&2; exit 1; }
install -m 755 "$BASH_BIN" "$W/root/bin/bash"
# The libraries ports expect any system to have, which the Brick's doesn't:
# OpenAL, Ogg, Vorbis, FLAC, Opus, mpg123, SDL2_mixer, and SDL2 for ports in
# /usr/lib/chaos-ports (tools/build-portlibs.sh)
for f in $(tar -tJf "$ROOT/dist/brick/portlibs.tar.xz" | grep '^usr/lib/lib'); do
	[ ! -e "$W/root/$f" ] || { echo "the root has $f already: check the port libraries" >&2; exit 1; }
done
[ ! -e "$W/root/usr/lib/chaos-ports" ] || { echo "the root has /usr/lib/chaos-ports already" >&2; exit 1; }
tar -xJf "$ROOT/dist/brick/portlibs.tar.xz" -C "$W/root" --no-same-owner
[ ! -e "$W/root/roms" ] || { echo "the root has a /roms already: check ports" >&2; exit 1; }
ln -s /mnt/SDCARD/Roms "$W/root/roms"
# OpenGL ES 1 by the name ports built elsewhere ask for (libGLESv1_CM);
# the Brick's PowerVR driver calls it libGLES_CM
[ -e "$W/root/usr/lib/libGLES_CM.so.1" ] || { echo "no libGLES_CM in the root: check ports" >&2; exit 1; }
for n in libGLESv1_CM.so.1 libGLESv1_CM.so; do
	[ ! -e "$W/root/usr/lib/$n" ] || { echo "the root has $n already: check ports" >&2; exit 1; }
	ln -s libGLES_CM.so.1 "$W/root/usr/lib/$n"
done
OSR=$W/root/etc/os-release
[ -L "$OSR" ] && OSR=$W/root/$(readlink "$OSR" | sed 's|^/||; s|^\.\./||')
grep -q '^NAME=' "$OSR" || { echo "no os-release to name ChaOS in" >&2; exit 1; }
grep -q '^OS_NAME=' "$OSR" || echo 'OS_NAME=ChaOS' >> "$OSR"
mkdir -p "$C/TortOS/ports"
install -m 755 "$ROOT/res/ports/run.sh" "$C/TortOS/ports/run.sh"
install -m 644 "$ROOT/res/ports/mod_ChaOS.txt" "$C/TortOS/ports/mod_ChaOS.txt"
install -m 644 "$ROOT/res/ports/asound-ports.conf" "$C/TortOS/ports/asound-ports.conf"
mkdir -p "$C/TortOS/ports/bin"
install -m 755 "$ROOT/res/ports/bin/pkill" "$ROOT/res/ports/bin/pgrep" "$ROOT/res/ports/bin/mount" "$C/TortOS/ports/bin/"
# unsquashfs, for runtimes the kernel can't mount (res/ports/bin/mount,
# tools/build-unsquashfs.sh)
install -m 755 "$ROOT/dist/brick/unsquashfs" "$C/TortOS/ports/bin/unsquashfs"
# PortMaster's own app: ChaOS's hooks for it, and the Python it runs on
# (tools/build-python.sh), unpacked by run.sh --portmaster on first use
# python3 for ports, at the end of their PATH (res/ports/pybin/python3)
mkdir -p "$C/TortOS/ports/pybin"
install -m 755 "$ROOT/res/ports/pybin/python3" "$C/TortOS/ports/pybin/python3"
# libudev-zero, for Weston's libinput: the Brick has no udev (tools/build-libudev.sh)
mkdir -p "$C/TortOS/ports/lib"
install -m 755 "$ROOT/dist/brick/libudev.so.1" "$C/TortOS/ports/lib/libudev.so.1"
install -m 644 "$ROOT/dist/brick/libudev-LICENSE.txt" "$C/TortOS/ports/lib/libudev-LICENSE.txt"
# and zip, which BusyBox hasn't (tools/build-zip.sh)
install -m 755 "$ROOT/dist/brick/zip" "$C/TortOS/ports/pybin/zip"
install -m 644 "$ROOT/dist/brick/zip-LICENSE.txt" "$C/TortOS/ports/pybin/zip-LICENSE.txt"
mkdir -p "$C/TortOS/ports/chaos" "$C/TortOS/ports/python"
install -m 644 "$ROOT/res/ports/chaos/launch_hooks.txt" "$C/TortOS/ports/chaos/launch_hooks.txt"
# and its look: PortMaster's theme in ChaOS's colors (res/ports/chaos_theme)
mkdir -p "$C/TortOS/ports/chaos_theme"
install -m 644 "$ROOT"/res/ports/chaos_theme/* "$C/TortOS/ports/chaos_theme/"
# the Ports shelf's PortMaster card (tools/genportmaster.py)
install -m 644 "$ROOT/res/ports/portmaster.png" "$C/TortOS/ports/portmaster.png"
install -m 644 "$ROOT/dist/brick/chaos-python.squashfs" "$C/TortOS/ports/python/chaos-python.squashfs"
# ChaOS: PICO-8 (src/pico8.h), the player's own copy: what runs it, the
# curl-backed wget its Splore downloads with, and the read-me the launcher
# puts in the card's PICO-8 folder. ChaOS carries none of PICO-8 itself.
mkdir -p "$C/TortOS/pico8/bin"
install -m 755 "$ROOT/res/pico8/run.sh" "$C/TortOS/pico8/run.sh"
install -m 755 "$ROOT/res/pico8/bin/wget" "$C/TortOS/pico8/bin/wget"
install -m 644 "$ROOT/res/pico8/README.txt" "$C/TortOS/pico8/README.txt"
install -m 644 "$ROOT/res/pico8/splore.png" "$C/TortOS/pico8/splore.png"   # Splore's label (tools/gensplore.py)
# and the overlay over it, when its console has one (res/pico8/overlay.c)
mkdir -p "$C/TortOS/pico8/lib"
install -m 755 "$ROOT/dist/brick/pico8-overlay.so" "$C/TortOS/pico8/lib/overlay.so"
[ -x "$W/root/usr/bin/curl" ] || { echo "no curl in the root: PICO-8's Splore needs it" >&2; exit 1; }
# The shelf: ChaOS's systems.cfg, with the Ports row, and the cards for the
# shelves that aren't consoles - Favorites, Muse, Recently Played, Ports - in
# both of TortOS's sets, as ChaOS draws them (tools/cardstyle.py,
# tools/gen*fancy.py); ChaOS's own two sets are drawn by the launcher)
[ -d "$C/TortOS/cards/classic" ] && [ -d "$C/TortOS/cards/fancy" ] || { echo "no cards in the image: check ports" >&2; exit 1; }
install -m 644 "$ROOT/config/systems.cfg" "$C/TortOS/systems.cfg"
# ChaOS Pixel Burst's two pixel fonts, beside the menu font, and their
# licenses (res/fonts/PIXEL-FONTS.md); mk/payload.sh does the same
install -m 644 "$ROOT/res/fonts/pixel.ttf" "$ROOT/res/fonts/pixel-head.ttf" "$C/TortOS/"
install -m 644 "$ROOT/res/fonts/OFL-pixel-pixelify.txt" "$ROOT/res/fonts/OFL-pixel-tiny5.txt" "$C/Licenses/"
install -m 644 "$ROOT/res/fonts/PIXEL-FONTS.md" "$C/Licenses/Pixel-fonts.md"
for set in classic fancy; do
	for card in FAVORITES MUSE PORTS RECENT PICO8 PS; do
		install -m 644 "$ROOT/res/cards/$set/$card.png" "$C/TortOS/cards/$set/$card.png"
	done
done
# One ROM folder per console, with the .media folder box art goes in, as
# mk/payload.sh makes them: TortOS's image has only TortOS's consoles, so
# without this a fresh card had no PlayStation folder (seen on 2.7.3-test2).
# Read from systems.cfg (awk, not sed: folder names contain spaces).
awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$3); print $3}' "$ROOT/config/systems.cfg" |
while IFS= read -r folder; do
	mkdir -p "$C/Roms/$folder/.media"
done
# bash's license beside the others; its source is in the sources archive
# (tools/chaos-sources.sh)
tar -xJOf "$ROOT/dist/brick/bash-source.tar.xz" bash-5.1.16/COPYING > "$C/Licenses/bash.txt"
chmod 644 "$C/Licenses/bash.txt"
tar -xJOf "$ROOT/dist/brick/unsquashfs-source.tar.xz" unsquashfs-source/squashfs-tools/COPYING > "$C/Licenses/squashfs-tools.txt"
chmod 644 "$C/Licenses/squashfs-tools.txt"
# PCSX-ReARMed's (GPL-2.0), named ChaOS- so that a small update brings it with
# the core; its source is in the sources archive
tar -xJOf "$ROOT/dist/brick/pcsx_rearmed-source.tar.xz" pcsx_rearmed-7b0e8f8f/COPYING > "$C/Licenses/ChaOS-PCSX-ReARMed.md"
chmod 644 "$C/Licenses/ChaOS-PCSX-ReARMed.md"
# and the port libraries' licenses, each project's own text
mkdir -p "$C/Licenses/ports"
for p in "ogg COPYING" "vorbis COPYING" "flac COPYING.Xiph" "flac COPYING.GPL" "opus COPYING" \
         "opusfile COPYING" "mpg123 COPYING" "openal-soft COPYING" "SDL_mixer LICENSE.txt" \
         "SDL2 LICENSE.txt" "bzip2 LICENSE" "zstd LICENSE" "libmodplug COPYING" \
         "libsndfile COPYING" "speex COPYING" "SDL2_gfx LICENSE" "fluidsynth LICENSE" \
         "SDL_net LICENSE.txt"; do
	set -- $p
	tar -xJOf "$ROOT/dist/brick/portlibs-source.tar.xz" "portlibs-source/$1/$2" > "$C/Licenses/ports/$1-${2%.txt}.txt"
done
# GCEM, inside FluidSynth
tar -xJOf "$ROOT/dist/brick/portlibs-source.tar.xz" "portlibs-source/fluidsynth/gcem/LICENSE" > "$C/Licenses/ports/fluidsynth-gcem-LICENSE.txt"
# PortMaster's Python (tools/build-python.sh): CPython's license
install -m 644 "$ROOT/res/ports/python/LICENSE.Python.txt" "$C/Licenses/ports/python-LICENSE.txt"
chmod 644 "$C/Licenses/ports/"*
rm -rf "$C/TortOS/overlays"
cp -R "$ROOT/res/overlays" "$C/TortOS/overlays"
find "$C/TortOS/overlays" -type d -exec chmod 755 {} +
find "$C/TortOS/overlays" -type f -exec chmod 644 {} +
# ChaOS Transfer's page: the launcher serves it off the card, so it is the
# card's copy that has to be ChaOS's (mk/payload.sh does the same)
rm -f "$C/TortOS/res/web/"*.html "$C/TortOS/res/web/"*.svg "$C/TortOS/res/web/"*.js "$C/TortOS/res/web/"*.css
install -m 644 "$ROOT/res/web/"* "$C/TortOS/res/web/"
# Over the cable the page wears the cable's name, ChaOS Wire (sys_menu.h)
sed 's/ChaOS Transfer/ChaOS Wire/g' "$ROOT/res/web/index.html" > "$C/TortOS/res/web/wire.html"
chmod 644 "$C/TortOS/res/web/wire.html"
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
# A test build (2.6.0-test1, the testing channel: src/update.h) seeds as just
# before the release it leads to: init.d/tortos reads TortOS/VERSION as three
# numbers at every boot, and a version it can't read would have it copy ChaOS
# onto the card at every boot. So 2.6.0-test1 is 2.5.999 there, and in
# SEED_V; TortOS/CHAOS keeps the real name.
NUM=${V%%-*}
IFS=. read -r va vb vc <<EOF
$NUM
EOF
SEED=$((va * 1000000 + vb * 1000 + ${vc:-0}))
SV=$NUM
if [ "$NUM" != "$V" ]; then
	case "$V" in *-test|*-test[0-9]*) ;; *) echo "CHAOS_VERSION $V: a test build is N.N.N-test" >&2; exit 1 ;; esac
	SEED=$((SEED - 1))
	SV=$((SEED / 1000000)).$((SEED / 1000 % 1000)).$((SEED % 1000))
fi
echo "$SV" > "$C/TortOS/VERSION"
echo "$V" > "$C/TortOS/CHAOS"
chmod 644 "$C/TortOS/VERSION" "$C/TortOS/CHAOS"
INIT=$W/root/etc/init.d/tortos
grep -q '^SEED_V=' "$INIT" || { echo "no SEED_V in the image's init.d/tortos" >&2; exit 1; }
sed -i "s/^SEED_V=.*/SEED_V=$SEED/" "$INIT"
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
# ChaOS's own boot files make it a base of its own: TORTOS_BASE and
# PLASTRON_REV, "2.2.0+p1", so a card on TortOS's own boot files is offered
# the whole image (System Update, over Wi-Fi) to get them
if [ -f "$ROOT/PLASTRON_REV" ]; then
	printf '%s+%s' "$(tr -d ' \n' < "$ROOT/TORTOS_BASE")" "$(tr -d ' \n' < "$ROOT/PLASTRON_REV")" > "$C/ChaOS/BASE"
else
	tr -d ' \n' < "$ROOT/TORTOS_BASE" > "$C/ChaOS/BASE"
fi
chmod 644 "$C/ChaOS/BASE"
L=$C/ChaOS/launch.sh
# ChaOS's logs are chaos.log (and .1 to .9), as a player sees them on the card
# and in Download logs; a card's older tortos.log files take the new name on
# its first boot with this, so they still rotate out
grep -q '^LOG=$LOGS_PATH/tortos.log$' "$L" || { echo "launch.sh has changed: check its log" >&2; exit 1; }
sed -i 's|^LOG=$LOGS_PATH/tortos.log$|for f in "$LOGS_PATH"/tortos.log*; do [ -f "$f" ] \&\& mv -f "$f" "$LOGS_PATH/chaos.log${f#"$LOGS_PATH"/tortos.log}"; done\nLOG=$LOGS_PATH/chaos.log|; s|this one and tortos.log.1 to .9|this one and chaos.log.1 to .9|' "$L"
grep -q '^LOG=$LOGS_PATH/chaos.log$' "$L" || { echo "launch.sh: the log did not take its name" >&2; exit 1; }
# Diatom in the card's zone from its start (the launcher keeps it in
# .userdata/<device>/zone, src/main.c zone_set): launch.sh starts it before
# the launcher, and its log lines were in UTC, hours off the launcher's
grep -q '^	"$TORTOS_DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" \\$' "$L" || { echo "launch.sh has changed: check how it starts Diatom" >&2; exit 1; }
sed -i 's|^	"$TORTOS_DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" \\$|	z=""; [ -r "$USERDATA_PATH/zone" ] \&\& read -r z < "$USERDATA_PATH/zone"\n	${z:+env TZ="$z"} "$TORTOS_DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" \\|' "$L"
grep -q 'env TZ="$z"} "$TORTOS_DIR/diatom"' "$L" || { echo "launch.sh: Diatom's zone did not go in" >&2; exit 1; }
[ "$(tail -n 1 "$L")" = "poweroff" ] || { echo "launch.sh has changed: check how it ends" >&2; exit 1; }
sed -i '$d' "$L"
cat >> "$L" <<'EOF2'
# ChaOS: an update installs at the next boot (src/update.h): boot again rather
# than power off
if [ -f /tmp/chaos_reboot ]; then reboot; else poweroff; fi
EOF2
# Updates over Wi-Fi come in two sizes (src/update.h). Most releases change
# only ChaOS's own files on the card - the launcher, the emulator, overlays,
# the web page - and send just those (APP_OUT: ChaOS/ without the cores, which
# are TortOS's and change only with it, and ChaOS's notices in Licenses/). One
# that changes the system around them (the kernel's modules, init scripts,
# the boot animation) sends the whole root. Which is which: SYSTEM, a hash of
# the root with ChaOS's copy left out and the version it seeds blanked, in
# ChaOS/SYSTEM on the card and in ChaOS-ota.txt; the same on both, and only
# the files are needed.
SYS_ID=$(cd "$W/root" && find . -path ./usr/share/tortos/card -prune -o \( -type f -o -type l \) -print \
	| LC_ALL=C sort | while read -r f; do
		if [ -L "$f" ]; then printf '%s -> %s\n' "$f" "$(readlink "$f")"
		elif [ "$f" = ./etc/init.d/tortos ]; then printf '%s ' "$f"; sed 's/^SEED_V=.*/SEED_V=/' "$f" | sha256sum
		else printf '%s ' "$f"; sha256sum < "$f"; fi
	done | sha256sum | cut -c1-16)
echo "$SYS_ID" > "$C/ChaOS/SYSTEM"
chmod 644 "$C/ChaOS/SYSTEM"
if [ -n "$APP_OUT" ]; then
	# TortOS's cores stay out; ChaOS's PlayStation core goes in, so a small
	# update brings it
	EXCL=$(cd "$C" && for f in ChaOS/cores/*; do
		[ "$f" = ChaOS/cores/pcsx_rearmed_libretro.so ] || printf -- '--exclude=%s ' "$f"; done)
	# shellcheck disable=SC2086
	tar -C "$C" --owner=0 --group=0 --sort=name $EXCL -cf - ChaOS \
		$(cd "$C" && ls Licenses/ChaOS*.md 2>/dev/null) | xz -T0 -9 > "$APP_OUT"
fi
echo "system $SYS_ID"
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
