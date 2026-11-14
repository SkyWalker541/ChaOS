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
(Over The Hare and Down To The Wire are ChaOS Transfer and ChaOS Wire here)
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

**Scanlines**: **Resolution-Independent Scanlines**, based on **"Scanlines
Sine Absolute Value"** by **RiskyJumps**, from libretro's glsl-shaders
(`scanlines/shaders/res-independent-scanlines.glsl`). The lighter CRT
choice, ported the same way (ChaOS-diatom, `shaders/scan/`). Licence:
**public domain**, as its header says.

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
- **Libraries for ports**, added to the image's `/usr/lib` because ports expect any system to have them: **OpenAL Soft** 1.24.3 (LGPL-2.1-or-later), **mpg123** (LGPL-2.1), **libsndfile** 1.2.2 (LGPL-2.1-or-later), **Ogg**, **Vorbis**, **FLAC**, **Opus**, **opusfile** and **Speex** (BSD-3-Clause, Xiph.Org), **bzip2** 1.0.8 (bzip2 licence), **Zstandard** 1.5.7 (BSD), **libmodplug** (public domain), **FluidSynth** 2.5.7 (LGPL-2.1-or-later, with **GCEM**, Apache-2.0, built in), **SDL2_mixer** 2.8.1, **SDL2_net** 2.2.0 and **SDL2_gfx** (zlib), and, for ports alone in `/usr/lib/chaos-ports`, **SDL2** 2.32.10 with plastron's PowerVR driver and ChaOS's two changes (`res/ports/sdl2`). Built by `tools/build-portlibs.sh` from `dist/brick/portlibs-source.tar.xz`, which is in each release's sources archive and says where each came from; their licences are in `Licenses/ports/` on the card.

- **The port library pack** (`dist/libpack/`, the "portlibs" release, `tools/build-libpack.sh`): libraries a port may need that the Brick hasn't got, fetched by ChaOS only for a port that needs one, into `.userdata/brick/ports/lib`: **libtheora** 1.1.1 (BSD-3-Clause), **pixman** 0.42.2 (MIT), **GMP** 6.3.0 (LGPL-3.0-or-later or GPL-2.0-or-later), **cairo** 1.18.0 (LGPL-2.1 or MPL-1.1), **fontconfig** 2.15.0 (MIT), **FLAC** 1.3.3 (BSD-3-Clause), **libevdev** 1.13.1 (MIT), GCC 13.3.0's **libgomp** (GPL-3.0-or-later with the GCC Runtime Library Exception), and ChaOS's own stand-in for PulseAudio's **libpulse-simple** (MIT). `dist/libpack/SOURCES.txt` says where each source came from; the sources go with the release.
- **GNU bash** 5.1.16 (https://www.gnu.org/software/bash/, GPL-3.0-or-later): `/bin/bash` in the image, unchanged, for ports. Its licence is `Licenses/bash.txt` on the card and its source is in each release's sources archive (`dist/brick/bash-source.tar.xz`, built by `tools/build-bash.sh`).
- **squashfs-tools** 4.6.1's **unsquashfs** (https://github.com/plougher/squashfs-tools, GPL-2.0-or-later), built static with **zlib** 1.3.1 (zlib licence), **xz** 5.6.3 (0BSD and public domain), **Zstandard** 1.5.6 (BSD) and **LZ4** 1.10.0 (BSD-2-Clause): unpacks ports' runtimes on the card (`TortOS/ports/bin/unsquashfs`), unchanged. Its licence is `Licenses/squashfs-tools.txt` on the card and its source is in each release's sources archive (`dist/brick/unsquashfs-source.tar.xz`, built by `tools/build-unsquashfs.sh`).
- **The Favorites, Muse, Recently Played, Ports and PICO-8 cards** are ChaOS's own, with no third-party images: in Plain Jane on the drawn set's own card (`tools/cardstyle.py`, `tools/genpico8card.py`), in Fancy Pants rendered in 3D as objects (`tools/raymarch.py`; `tools/genmusefancy.py`, `tools/genportsfancy.py`, `tools/genrecentfancy.py`, `tools/genpico8fancy.py`). The PICO-8 cards are a generic cartridge with a pixel heart, nothing of PICO-8's own logo; their pink is a color of its palette. Muse's Fancy Pants card replaces TortOS's iPod photograph, whose source was never recorded.
- **ChaOS logo** ("Handheld burst"): original pixel art made for ChaOS, drawn
  pixel by pixel in `tools/genlogo.py` with no third-party images or fonts;
  see `res/logo/SOURCE.md`.

---

## The website

The site at https://skywalker541.github.io/ChaOS/ (`web/`) is ChaOS's own:
its pictures are ChaOS's screens, captured from the launcher, and its opening
is the boot animation's own squares and paths (`tools/genwebboot.py`, from
`tools/genchaosmotion.py`). It uses **Josefin Sans** by Santiago Orozco (SIL
Open Font License 1.1, `web/OFL.txt`). The games on its screens are invented
names, and the port screenshot shown is from PortMaster's catalog.

---

## Services

- **libretro-database**'s cheat files (https://github.com/libretro/libretro-database,
  `cht/`, CC BY-SA 4.0, by libretro and its contributors): not in the image. The
  Brick fetches a game's file when the player asks (Get Cheats); the website's
  lists of those files (`cheats/`, made by `tools/cheat-index.py`) are shared
  under the same licence, with a LICENSE.txt saying so. ChaOS-diatom applies
  RetroArch's memory cheats as RetroArch's cheat manager does, written from
  its behaviour rather than its code.
- **RetroAchievements** (https://retroachievements.org): achievements.
- **libretro thumbnails** (https://thumbnails.libretro.com): box art.
- **MusicBrainz Cover Art Archive** (https://musicbrainz.org): album art.
- **itch.io** (https://itch.io): box art for homebrew, from its public tag feeds; each cover is its developer's. ChaOS is not affiliated with itch.io.
- **NextUI-Itchio-Pak** by Christophe Vanlancker (https://github.com/carroarmato0/NextUI-Itchio-Pak, MIT): how to read itch.io's tag feeds on a handheld, which ChaOS's own itch.io code (src/itch.c) follows.
- **PortMaster** (https://portmaster.games, https://github.com/PortsMaster/PortMaster-GUI, MIT): ports, and the files they run with. Not in the image: the Brick downloads PortMaster's own release only when you choose **Get PortMaster**. Each port's screenshot in ChaOS's PortMaster browser comes from PortMaster's catalog and is its porter's or developer's. ChaOS's settings for it (`res/ports/mod_ChaOS.txt`, MIT) follow its other systems' `mod_*.txt`.
- **minui-portmaster** by ben16w (https://github.com/ben16w/minui-portmaster, MIT), which NextUI's PortMaster pak now points to: the reference for running PortMaster on a TrimUI Brick.
- **Syncthing** (https://syncthing.net, https://github.com/syncthing/syncthing, MPL-2.0): keeps folders the same across the player's devices. Not in the image: the Brick downloads Syncthing's own arm64 release, the version and its SHA-256 pinned in `src/syncthing.h`, only when you choose **Get Syncthing**, and runs it unchanged. ChaOS talks to it through its REST API; no Syncthing code is in ChaOS.
- **PICO-8** by Lexaloffle Games (https://www.lexaloffle.com/pico-8.php), proprietary: not included and never downloaded by ChaOS. ChaOS runs the player's own purchased copy (its Raspberry Pi build) from the card's `PICO-8` folder. Running it through a `wget` that hands Splore's downloads to curl (`res/pico8/bin/wget`) is **spruceUI**'s approach (https://github.com/spruceUI/spruceOS, `Emu/PICO8`); ChaOS's script is its own, written for the two calls PICO-8 makes.
- **RomM** support speaks [RomM](https://github.com/rommapp/romm)'s own API (AGPL-3.0 server, not included), as its handheld client [Grout](https://github.com/rommapp/grout) (MIT) uses it; no code from either is in ChaOS.

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
