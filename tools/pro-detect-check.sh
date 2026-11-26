#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: Pro Detect is in every Pre-Release, from 2.7.9-test1 on (the
# maintainer's rule, CLAUDE.md). A -test version without PLASTRON_REV or one
# of the boot files builds a card that boots a Brick and leaves a Brick Pro's
# screen white, and chaos-image.sh would leave them out without a word. Public
# releases (no -test) carry them only once the maintainer moves them.
ROOT=${1:-.}
V=$(tr -d ' \n' < "$ROOT/CHAOS_VERSION")
P=$ROOT/dist/brick/plastron
bad=0
fail() { echo "pro detect: $*" >&2; bad=1; }

case "$V" in
*-test*) ;;
*) echo "ok: $V is a Public Release; Pro Detect isn't required"; exit 0 ;;
esac

[ -s "$ROOT/PLASTRON_REV" ] || fail "$V is a Pre-Release without PLASTRON_REV"
for f in boot.scr brickpro.dtb u-boot-sunxi-with-spl.bin brick-pad.ko ImagePro.lz4 \
         source/0011-chaos-brick-pro-detect.patch source/boot.cmd; do
	[ -s "$P/$f" ] || fail "no $f in dist/brick/plastron"
done
# the boot loader leaves PRO! or BRIC; the boot script reads it and picks the
# Pro's tree and kernel
# (the SPL writes the word with instructions, so it isn't in the binary as
# text; the script reading it from 0x47f00000 is what shows it is used)
if [ -s "$P/boot.scr" ]; then
	grep -qai '47f00000' "$P/boot.scr" || fail "boot.scr doesn't read Pro Detect's mark"
	grep -qa 'brickpro.dtb' "$P/boot.scr" || fail "boot.scr doesn't load the Pro's tree"
	grep -qa 'ImagePro' "$P/boot.scr" || fail "boot.scr doesn't load the Pro's kernel"
fi
# the Pro's M1/M2 as the Brick's F1/F2, and its sticks and L3/R3
[ -s "$P/source/0046-ChaOS-brick-pad-reads-the-Brick-Pro-s-L3-R3-and-sticks.patch" ] || fail "no stick patch"
[ -s "$P/source/0048-ChaOS-brick-pad-the-Brick-Pro-s-keys-as-the-Brick-s.patch" ] || fail "no Pro keys patch"
grep -q 'dist/brick/plastron' "$ROOT/tools/chaos-image.sh" || fail "chaos-image.sh no longer writes the Pro's files"

[ $bad -eq 0 ] || { echo "pro detect: FAILED" >&2; exit 1; }
echo "ok: Pro Detect is in $V"
