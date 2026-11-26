#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: a port PortMaster's own app installed gets its own picture as box art
# (main.c pm_gui_art): added 2026-11-26, when a port installed by the app had
# none on the shelf. A cover named in port.json, else the screenshot, else a
# cover/screenshot file beside it; never a name that leaves the port's folder;
# never over art the player already has.
set -eu
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
R=$T/roms/Ports; mkdir -p "$R/a" "$R/b" "$R/c" "$R/.media"
# pictures from the repository: CI has no Pillow (2.7.9-test3's run failed
# here), and any real image will do
J=res/cards/fancy/source/favorites-heart.jpg P=res/cards/classic/GB.png P2=res/cards/classic/FAVORITES.png
cp "$J" "$R/a/screenshot.jpg"; cp "$P" "$R/b/cover.png"; cp "$P2" "$R/c/screenshot.png"; cp "$P" "$R/.media/Kept.png"
printf '#!/bin/bash\n' > "$R/A Port.sh"; printf '#!/bin/bash\n' > "$R/B Port.sh"; printf '#!/bin/bash\n' > "$R/Kept.sh"
echo '{"items":["A Port.sh","a/"],"attr":{"image":{"screenshot":"screenshot.jpg","covers":[]}}}' > "$R/a/port.json"
echo '{"items":["B Port.sh","b/"],"attr":{"image":{"screenshot":"../../../evil.png"}}}' > "$R/b/port.json"
echo '{"items":["Kept.sh","c/"],"attr":{"image":{"screenshot":"screenshot.png"}}}' > "$R/c/port.json"
cp "$R/.media/Kept.png" "$T/kept.before"
# the old screens: a manifest names a script the old picture cache has art
# for; a port.json names its zip; and what must never go
U=$T/ud/ports; mkdir -p "$U/img" "$U/installed" "$U/home/.config" "$R/d"
cp "$J" "$U/img/oldzip.jpg"; cp "$P" "$U/img/dzip.png"; cp "$P2" "$U/img/unused.png"
printf 'Old Port.sh\noldport/\noldport/game.bin\n' > "$U/installed/oldzip.txt"
printf '#!/bin/bash\n' > "$R/Old Port.sh"; printf '#!/bin/bash\n' > "$R/D Port.sh"
echo '{"name":"dzip.zip","items":["D Port.sh","d/"]}' > "$R/d/port.json"
echo keep > "$U/home/.config/save.cfg"; echo '{}' > "$U/ports.json"
mkdir -p "$T/root" "$T/ud" "$T/card" && cp config/systems.cfg "$T/root/"
TORTOS_ROOT=$T/root TORTOS_ROMS=$T/roms TORTOS_CARD=$T/card TORTOS_USERDATA=$T/ud TORTOS_SHARED=$T/root \
TORTOS_FONT=res/fonts/menu.ttf SDL_VIDEODRIVER=offscreen ./build-native/tortos --shot "$T/x.png" --pm-gui-art > "$T/out" 2>&1 || true
bad=0
[ -s "$R/.media/A Port.png" ] || { echo "FAIL: a port's screenshot as its art"; bad=1; }
[ -s "$R/.media/B Port.png" ] || { echo "FAIL: a name that leaves the folder refused, the file beside it used"; bad=1; }
cmp -s "$R/.media/Kept.png" "$T/kept.before" || { echo "FAIL: the player's own art replaced"; bad=1; }
[ ! -e "$T/evil.png" ] || { echo "FAIL: wrote outside"; bad=1; }
[ -s "$R/.media/Old Port.png" ] || { echo "FAIL: an old install's picture kept as its art"; bad=1; }
[ -s "$R/.media/D Port.png" ] || { echo "FAIL: a port.json's zip name finds its old picture"; bad=1; }
[ ! -e "$U/img" ] || { echo "FAIL: the old picture cache still there"; bad=1; }
[ -s "$U/.img-retired" ] || { echo "FAIL: no mark that it was done"; bad=1; }
[ -s "$U/installed/oldzip.txt" ] && [ -s "$U/home/.config/save.cfg" ] && [ -s "$U/ports.json" ] ||
	{ echo "FAIL: something besides the picture cache removed"; bad=1; }
[ -s "$R/Old Port.sh" ] && [ -s "$R/d/port.json" ] || { echo "FAIL: a port touched"; bad=1; }
grep -q "pictures retired: 3 files" "$T/out" || { echo "FAIL: not logged as 3 files"; bad=1; }
[ $bad -eq 0 ] && echo "ok: art for ports PortMaster's app installed, and the old screens' pictures retired"
exit $bad
