# Credits and third-party notices

*Draft for **ChaOS**, the fork. It grows as parts are added; every
part the OS carries or builds on is listed here with its author, its licence and
where it came from. On the card it goes in `Licenses/`, beside the notices the
parts carry themselves.*

---

## Where this OS comes from

**TortOS** by **Eric Reinsmidt** - https://github.com/ericreinsmidt/TortOS

This OS is a fork of TortOS: the launcher, menus, Muse, Over The Hare, box art,
RetroAchievements and the rest of what makes it work on the Brick are Eric's
work. Licence: **GNU General Public License, version 3 or later**. TortOS
releases up to v1.6.0 were MIT. This fork stays under GPL-3.0-or-later, so its
full source is published with every build that is shared.

**Diatom** by **Eric Reinsmidt** - https://github.com/ericreinsmidt/diatom

The libretro frontend games run in. Licence: **Mozilla Public License 2.0**
(MIT up to 2026-10-25). Files this fork changes stay MPL-2.0 and are published.
Diatom carries, under their own licences:
- `libretro.h`, Copyright (C) 2010-2024 The RetroArch team, MIT-style licence
  scoped to that header;
- part of **rcheevos**, Copyright (c) 2018 RetroAchievements.org, MIT.

**plastron** by **Eric Reinsmidt** - https://github.com/ericreinsmidt/plastron

The system libraries TortOS and Diatom link against on the device (SDL2,
SQLite, curl, FFmpeg and others). Their licences are in plastron's package
list, `Licenses/plastron-packages.md` on the card.

---

## Shaders

**PT SkyWalker541** by **SkyWalker541** - https://github.com/SkyWalker541/PT-SkyWalker541

The pixel transparency shader for Game Boy, Game Boy Color and Game Boy
Advance, ported from GLSL to C to run on the Brick's CPU (in ChaOS-diatom,
`shaders/pt/`). Licence:
**MIT**, Copyright (c) 2026 SkyWalker541.

It builds on:
- **"Hash without Sine"** by **David Hoskins** (https://www.shadertoy.com/view/4djSRW),
  the hash behind the backing grain. Licence: **MIT**, Copyright (c) 2014
  David Hoskins; full text below.
- LCD Dot mode is **inspired by Themaister's dot shader**, which is public
  domain. No code is taken from it.

**crt-lottes-fast**: **[CRTS] "Public Domain CRT-Styled Scalar"** by
**Timothy Lottes** (https://www.shadertoy.com/view/MtSfRK), adapted for
RetroArch by **hunterk**, from libretro's glsl-shaders
(https://github.com/libretro/glsl-shaders, `crt/shaders/crt-lottes-fast.glsl`).
The CRT look for the NES, SNES, Genesis, Master System and TurboGrafx-16,
ported from GLSL to C to run on the Brick's CPU (in ChaOS-diatom,
`shaders/crt/`). Licence: **the Unlicense** (public domain); its full text is
in the original's header, kept beside the port.

**lcd3x** by **Gigaherz**, from libretro's glsl-shaders
(`handheld/shaders/lcd3x.glsl`). The handheld-LCD look for the Game Gear and
the Neo Geo Pocket and Pocket Color, ported the same way (ChaOS-diatom,
`shaders/lcd/`). Licence: **public domain**, as its header says.

---

## Boot animation

ChaOS's boot animation is packed for plastron's splash in the format of
plastron's `scripts/make-splash-frames.py --brick` by Eric Reinsmidt
(https://github.com/ericreinsmidt/plastron, GPL-3.0), whose packing
`tools/genchaosmotion.py` repeats. The animation and the logo are ChaOS's own.

## Overlays

**Brick overlays** by **SkyWalker541** - https://github.com/SkyWalker541/TrimUI-Brick-Overlays,
as collected in NextUI Community Overlays (https://github.com/LoveRetro/nextui-community-overlays).

The Game Boy, Game Boy Color and Game Boy Advance overlays made for the Brick's
screen at a whole-number scale, which ChaOS puts in the card's `Overlays/`
folder at first boot. Renamed for the menu, pictures unchanged; the list is in
`res/overlays/SOURCE.md`. Licence: **CC0 1.0** (public domain dedication), in
`res/overlays/LICENSE-SkyWalker541.txt`.

**Brick overlays** by **KrutzOtrem** - https://github.com/KrutzOtrem/Trimui-Brick-Overlays,
as collected in NextUI Community Overlays. His Brick designs made for a
whole-number scale ("native", the Blank version of each) for Game Boy, Game
Boy Color, Game Boy Advance, Game Gear and Neo Geo Pocket Color, named
"KrutzOtrem - <design>" in the menu, pictures unchanged; which file is which
is in `res/overlays/SOURCE.md`. Licence: **MIT**, Copyright (c) 2025
KrutzOtrem, in `res/overlays/LICENSE-KrutzOtrem.txt`.

---

## Emulator cores

Unchanged binaries from the libretro buildbot, as TortOS ships them. Each is
its authors' work.

| Core | Systems | Licence | Source |
|------|---------|---------|--------|
| FCEUmm | NES | GPL-2.0-or-later | https://github.com/libretro/libretro-fceumm |
| Beetle PCE Fast | TurboGrafx-16 and CD | GPL-2.0 | https://github.com/libretro/beetle-pce-fast-libretro |
| Beetle NeoPop | Neo Geo Pocket and Color | GPL-2.0 | https://github.com/libretro/beetle-ngp-libretro |
| mGBA | Game Boy, Color and Advance | MPL-2.0 | https://github.com/libretro/mgba |
| Snes9x 2010 | SNES | **Non-commercial** | https://github.com/libretro/snes9x2010 |
| Genesis Plus GX | Genesis, Master System, Game Gear | **Non-commercial** | https://github.com/libretro/Genesis-Plus-GX |

**Snes9x 2010 and Genesis Plus GX forbid commercial use.** This OS may not be
sold, put behind a paywall, or shipped with a device that is sold.

---

## Art, fonts and data

- **Josefin Sans** by **Santiago Orozco**: the UI and wordmark font. SIL Open
  Font License 1.1; full text in `res/fonts/OFL.txt`.
- **Console photographs** by **Evan Amos** (Vanamo Online Game Museum,
  Wikimedia Commons): the "Fancy Pants" shelf cards. Public domain, released
  by the author. Console designs belong to their makers and appear here to
  identify each platform.
- **Mozilla's root certificate list**, as published by the curl project: used
  to check HTTPS servers. MPL-2.0.
- **ChaOS logo** ("Handheld burst"): original pixel art made for ChaOS, drawn
  pixel by pixel in `tools/genlogo.py` with no third-party images or fonts;
  see `res/logo/SOURCE.md`.

**To replace before release:** TortOS's Muse card (`res/cards/*/MUSE.png`)
shows an iPod photograph whose source TortOS does not record. This fork will
use its own art there instead.

---

## Services

- **RetroAchievements** (https://retroachievements.org): achievements.
- **ScreenScraper** (https://www.screenscraper.fr): box art and game details.
- **libretro thumbnails** (https://thumbnails.libretro.com): box art.
- **MusicBrainz Cover Art Archive** (https://musicbrainz.org): album art.

---

## Licence texts not carried elsewhere

### Hash without Sine (MIT)

```
Copyright (c) 2014 David Hoskins.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### PT SkyWalker541 (MIT)

The full text is in ChaOS-diatom, `shaders/pt/LICENSE`.
