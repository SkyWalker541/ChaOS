<p align="center">
  <img src="res/logo/lockup.png" alt="ChaOS" height="96">
</p>

<p align="center">
  <b>Custom firmware for the TrimUI Brick.</b><br>
  Eleven classic consoles with shader and overlay support, PC games through PortMaster, your own RomM library, and your music, all from the handheld.<br>
  Built on <a href="https://github.com/ericreinsmidt/TortOS">TortOS</a> by Eric Reinsmidt.
</p>


<p align="center">
  <a href="https://skywalker541.github.io/ChaOS/"><b>Website and guide</b></a> &nbsp;·&nbsp;
  <a href="https://github.com/SkyWalker541/ChaOS-Installer/releases/latest">ChaOS Installer</a> &nbsp;·&nbsp;
  <a href="https://github.com/SkyWalker541/ChaOS/releases/latest">Latest release</a> &nbsp;·&nbsp;
  <a href="https://github.com/SkyWalker541/ChaOS/issues">Report a problem</a>
</p>

> [!WARNING]
> **ChaOS is still in testing and development.** Things may change from one release to the next, and some
> may not work as they should yet. Keep a copy of your saves, and please
> [report what you find](https://github.com/SkyWalker541/ChaOS/issues), with the logs if you can.

> [!IMPORTANT]
> **ChaOS is for the TrimUI Brick (and Brick Hammer) only at this time.**

<div align="center">
<table>
  <tr>
    <td align="center"><img src="res/readme/chaos-pixel-names.png" width="380" alt="The ChaOS Pixel Names theme"><br><sub>ChaOS Pixel Names: each console in the logo's pixel letters</sub></td>
    <td align="center"><img src="res/readme/chaos-pixel-cards.png" width="380" alt="The ChaOS Pixel Cards theme"><br><sub>ChaOS Pixel Cards</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/chaos-menu.png" width="380" alt="The ChaOS menu with Night Mode, the clock and the battery"><br><sub>Night Mode, and the clock and battery in every menu</sub></td>
    <td align="center"><img src="res/readme/chaos-appearance.png" width="380" alt="The Appearance page"><br><sub>The look, on one page, in your color</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/chaos-shader.png" width="380" alt="The NES's shader settings"><br><sub>Each console's shader and its settings</sub></td>
    <td align="center"><img src="res/readme/chaos-transfer.png" width="380" alt="ChaOS Transfer in a browser"><br><sub>ChaOS Transfer and ChaOS Wire, from any browser</sub></td>
  </tr>
</table>
</div>
## At a glance

| | |
|---|---|
| **Games start instantly** | The emulator is already running, so a game is up the moment you press A, and it picks up where you stopped. |
| **Shaders and overlays** | CRT, LCD and PT SkyWalker541 looks you can tune, frames around the picture, and a check that says if the game keeps its speed. |
| **Ports, from the Brick** | Browse PortMaster's catalog and install ports, with their runtimes and libraries. |
| **Your RomM library** | Download games from your own RomM server, sync saves, fetch BIOS files. |
| **Music and audiobooks** | Muse plays from anywhere, with Bluetooth headphones and a pocket lock screen. |
| **No computer needed** | Wi-Fi, box art, achievements, updates and sending games from your phone, all from the handheld. |
| **Your Brick stays yours** | ChaOS runs from the SD card. Take the card out and the Brick boots its own system. |

## Install

You need a TrimUI Brick or Brick Hammer, a microSD card and, just this once, a computer.

1. Run [ChaOS Installer](https://github.com/SkyWalker541/ChaOS-Installer/releases/latest) (macOS, Windows, Linux). It puts the latest ChaOS on the card, or updates a card that already has ChaOS or TortOS, keeping everything on it. Or write `ChaOS-v<version>-brick.img.xz` from [Releases](https://github.com/SkyWalker541/ChaOS/releases/latest) with a disk-image writer (this erases the card).
2. Put the card in the Brick and turn it on.
3. Set the time zone (**MENU > Date & Time**), join Wi-Fi (**MENU > Wi-Fi**), and add games with **MENU > ChaOS Transfer**.

Updates come over Wi-Fi from **MENU > Updates**, and keep your games, saves and settings. To go back to the Brick's own system, take the card out.

**[The website](https://skywalker541.github.io/ChaOS/)** walks through setup with the Brick's own screens, and covers everything ChaOS does:

| | |
|---|---|
| [Get started](https://skywalker541.github.io/ChaOS/#start) | Install, first boot, Wi-Fi, adding games, box art |
| [Buttons](https://skywalker541.github.io/ChaOS/#buttons) | Every shortcut: anywhere, on the shelves, in a game, in a port, in Muse |
| [Shaders](https://skywalker541.github.io/ChaOS/#shaders) and the [speed check](https://skywalker541.github.io/ChaOS/#speed) | Every style and setting, and how to get a good speed reading |
| [Shelves and themes](https://skywalker541.github.io/ChaOS/#shelves) | Pixel Names, Pixel Cards, Cubic, Game List View, letter scrubbing |
| [Ports](https://skywalker541.github.io/ChaOS/#ports) | PortMaster on the Brick: installing, playing, removing |
| [RomM](https://skywalker541.github.io/ChaOS/#romm) | Pairing, downloading games, save sync, BIOS |
| [Muse](https://skywalker541.github.io/ChaOS/#muse) | Music, audiobooks, Bluetooth, the lock screen |
| [Updates](https://skywalker541.github.io/ChaOS/#updates) | How updates work, and what each message means |
| [FAQ](https://skywalker541.github.io/ChaOS/#faq) | Missing games, saves, ports, logs |

The [guide](docs/guide.md) is the long version: every menu row and setting, and the reasons behind them.

## Quick buttons

| | |
|---|---|
| **MENU** | Settings for what you're looking at; in a game, the in-game menu |
| **SELECT** | Muse, the music and audiobook player |
| **X / Y** on a game | Its info / add to Favorites |
| **L2 / R2** | Jump by letter |
| **MENU + R1** · **MENU + L1** · **MENU + X** | Fast forward · rewind (hold) · screenshot |
| **SELECT + START** in a port | Quit it |
| **POWER** | Save and turn off (in a port: Resume, Close Port or Power Off) |

## Credits and license

ChaOS is a fork of **[TortOS](https://github.com/ericreinsmidt/TortOS) by Eric Reinsmidt**, on his [plastron](https://github.com/ericreinsmidt/plastron), with ChaOS's changes by SkyWalker541. Games run in [ChaOS-diatom](https://github.com/SkyWalker541/ChaOS-diatom), a fork of Eric Reinsmidt's [Diatom](https://github.com/ericreinsmidt/diatom). Ports come from [PortMaster](https://portmaster.games). Everyone whose work is in ChaOS, and where everything came from, is in [CREDITS.md](CREDITS.md) and on the [website](https://skywalker541.github.io/ChaOS/#credits).

ChaOS is free software under the **GNU General Public License, version 3 or any later version** ([LICENSE](LICENSE)), as TortOS is: anyone may study, change and share it, and whoever gets it, changed or not, gets its source and the same rights. TortOS is Copyright (c) 2026 Eric Reinsmidt; ChaOS's changes are Copyright (c) 2026 SkyWalker541.

- ChaOS-diatom is under the Mozilla Public License 2.0.
- PT SkyWalker541 is MIT; crt-lottes-fast, lcd3x and Scanlines are public domain.
- SkyWalker541's overlays are CC0; KrutzOtrem's are MIT.
- The libraries ChaOS adds for ports keep their own licenses, in `Licenses/` on the card.
- The cores keep their own licenses. Snes9x 2010 and Genesis Plus GX forbid commercial use, so a ChaOS image may not be sold or shipped with a device that is sold.

Full notices are in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) and [CREDITS.md](CREDITS.md), and every release carries the source of everything in its image.

**For developers:** ChaOS is C and SDL2. [docs/building.md](docs/building.md) is TortOS's build guide, which ChaOS follows. A ChaOS release image is TortOS's release image with ChaOS's launcher, emulator, overlays, web page, boot animation and port support swapped in, by `tools/chaos-image.sh` ([dist/README.md](dist/README.md)). The website is `web/`, published by `.github/workflows/pages.yml`.
