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
python3 - "$R" <<'PY'
import sys
from PIL import Image
r = sys.argv[1]
Image.new('RGB', (64, 48), (200, 80, 40)).save(r + '/a/screenshot.jpg')
Image.new('RGB', (64, 48), (40, 80, 200)).save(r + '/b/cover.png')
Image.new('RGB', (64, 48), (40, 200, 80)).save(r + '/c/screenshot.png')
Image.new('RGB', (64, 48), (1, 2, 3)).save(r + '/.media/Kept.png')
PY
printf '#!/bin/bash\n' > "$R/A Port.sh"; printf '#!/bin/bash\n' > "$R/B Port.sh"; printf '#!/bin/bash\n' > "$R/Kept.sh"
echo '{"items":["A Port.sh","a/"],"attr":{"image":{"screenshot":"screenshot.jpg","covers":[]}}}' > "$R/a/port.json"
echo '{"items":["B Port.sh","b/"],"attr":{"image":{"screenshot":"../../../evil.png"}}}' > "$R/b/port.json"
echo '{"items":["Kept.sh","c/"],"attr":{"image":{"screenshot":"screenshot.png"}}}' > "$R/c/port.json"
cp "$R/.media/Kept.png" "$T/kept.before"
mkdir -p "$T/root" "$T/ud" "$T/card" && cp config/systems.cfg "$T/root/"
TORTOS_ROOT=$T/root TORTOS_ROMS=$T/roms TORTOS_CARD=$T/card TORTOS_USERDATA=$T/ud TORTOS_SHARED=$T/root \
TORTOS_FONT=res/fonts/menu.ttf SDL_VIDEODRIVER=offscreen ./build-native/tortos --shot "$T/x.png" --pm-gui-art > "$T/out" 2>&1 || true
bad=0
[ -s "$R/.media/A Port.png" ] || { echo "FAIL: a port's screenshot as its art"; bad=1; }
[ -s "$R/.media/B Port.png" ] || { echo "FAIL: a name that leaves the folder refused, the file beside it used"; bad=1; }
cmp -s "$R/.media/Kept.png" "$T/kept.before" || { echo "FAIL: the player's own art replaced"; bad=1; }
[ ! -e "$T/evil.png" ] || { echo "FAIL: wrote outside"; bad=1; }
[ $bad -eq 0 ] && echo "ok: art for ports PortMaster's app installed"
exit $bad
