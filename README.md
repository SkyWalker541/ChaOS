<p align="center">
  <img src="res/logo/lockup.png" alt="ChaOS" height="96">
</p>

<p align="center">
  <b>Custom firmware for the TrimUI Brick.</b><br>
  Eleven classic consoles with a shader made for each, PC games through PortMaster, your own RomM library, and your music, all from the handheld.<br>
  Built on <a href="https://github.com/ericreinsmidt/TortOS">TortOS</a> by Eric Reinsmidt.
</p>

<p align="center">
  <a href="#install">Install</a> &nbsp;·&nbsp;
  <a href="#playing-games">Games</a> &nbsp;·&nbsp;
  <a href="#the-look">The look</a> &nbsp;·&nbsp;
  <a href="#in-a-game">In a game</a> &nbsp;·&nbsp;
  <a href="#ports">Ports</a> &nbsp;·&nbsp;
  <a href="#romm">RomM</a> &nbsp;·&nbsp;
  <a href="#updates">Updates</a> &nbsp;·&nbsp;
  <a href="#controls">Controls</a> &nbsp;·&nbsp;
  <a href="#faq">FAQ</a>
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
| **Games start instantly** | The emulator is already running, so a game is up the moment you press A. Quit or press power and it picks up from that moment next time. |
| **A shader for every console** | A CRT for the TV consoles, PT SkyWalker541 for the Game Boys, an LCD for the other handhelds, at Integer, Aspect or Stretch scale. |
| **Ports, from the Brick** | Browse PortMaster's catalog, see each game's screenshot, and install it. Its runtime and any library it needs come with it. |
| **Your RomM library** | Download games from your own RomM server, sync their saves, and fetch BIOS files. |
| **Everything on the handheld** | Wi-Fi, box art, RetroAchievements, Bluetooth headphones, updates, and sending games from your phone. A computer is needed once, to set up the card. |
| **Music and audiobooks** | Muse, on SELECT from anywhere, plays on through a game. |
| **Your Brick stays yours** | ChaOS runs from the SD card and changes nothing on the Brick itself. Take the card out and it boots its own system. |

## Install

You need a TrimUI Brick or Brick Hammer, a microSD card and, just this once, a computer.

**The easy way:** [ChaOS Installer](https://github.com/SkyWalker541/ChaOS-Installer/releases/latest), for macOS, Windows and Linux. It downloads the latest ChaOS and puts it on the card, or updates a card that has ChaOS or TortOS on it already.

<details>
<summary><b>By hand, with a disk-image writer</b></summary>

1. Download `ChaOS-v<version>-brick.img.xz` from [Releases](https://github.com/SkyWalker541/ChaOS/releases/latest).
2. Write it to the card with a disk-image writer such as Raspberry Pi Imager or balenaEtcher. **This erases the card.**
3. Put the card in the Brick and turn it on. The first boot takes a little longer while it makes the rest of the card into space for games, music and saves.

</details>

**First steps on the Brick:**

1. Set your time zone under **MENU > Date & Time**.
2. Join Wi-Fi under **MENU > Wi-Fi**.
3. Add games (see [Playing games](#playing-games)).

**Updating** is from the Brick itself, under **MENU > Updates** (see [Updates](#updates)), or over the USB cable with **ChaOS Wire**, or with ChaOS Installer's **Update**. Your games, music, saves and settings always stay. A TortOS card becomes a ChaOS card the same way.

**Removing:** take the card out, and the Brick boots its own system again. Everything ChaOS keeps is on the card, Wi-Fi passwords and Bluetooth pairings included, so reformat it before you give it away.

## Playing games

### Systems

| Console | Folder | File types | Core |
|---|---|---|---|
| NES | `Roms/NES` | `.nes` `.fds` `.unf` `.unif` `.zip` | FCEUmm |
| SNES | `Roms/SNES` | `.sfc` `.smc` `.zip` | Snes9x 2010 |
| Genesis | `Roms/Genesis` | `.md` `.gen` `.bin` `.smd` `.zip` | Genesis Plus GX |
| Master System | `Roms/Master System` | `.sms` `.zip` | Genesis Plus GX |
| TurboGrafx-16 and CD | `Roms/TurboGrafx-16` | `.pce` `.sgx` `.cue` `.ccd` `.chd` `.toc` `.m3u` `.zip` | Beetle PCE Fast |
| Game Boy | `Roms/Game Boy` | `.gb` `.dmg` `.zip` | mGBA |
| Game Boy Color | `Roms/Game Boy Color` | `.gbc` `.cgb` `.zip` | mGBA |
| Game Boy Advance | `Roms/Game Boy Advance` | `.gba` `.agb` `.zip` | mGBA |
| Game Gear | `Roms/Game Gear` | `.gg` `.zip` | Genesis Plus GX |
| Neo Geo Pocket | `Roms/Neo Geo Pocket` | `.ngp` `.ngc` `.ngpc` `.npc` `.zip` | Beetle NeoPop |
| Neo Geo Pocket Color | `Roms/Neo Geo Pocket Color` | `.ngp` `.ngc` `.ngpc` `.npc` `.zip` | Beetle NeoPop |
| Ports | `Roms/Ports` | installed from the Brick | [PortMaster](#ports) |

**BIOS files** go loose in `Bios/`. Only TurboGrafx-CD games need one, `syscard3.pce`. Game Boy Advance runs without its BIOS; add `gba_bios.bin` for the original boot animation. RomM can fetch them for you.

### Adding games

- **From a computer:** put the card in it (it shows up as **ChaOS**) and copy games into their console's folder in `Roms/`.
- **Over Wi-Fi, with ChaOS Transfer:** open **MENU > ChaOS Transfer**, open the address it shows in a browser on your phone or computer, and type the PIN (new every time). Drag games into their console's folder, albums into `Music` or books into `Audiobooks`. They're on the shelf when you leave the screen.
- **Over the USB cable, with ChaOS Wire** (Mac, Linux or Windows 11): plug the Brick into the computer, open **MENU > ChaOS Wire**, then `http://10.55.0.2` in a browser on that computer. Its page can also update ChaOS from the computer.
- **From your own RomM server:** see [RomM](#romm).

### Shelves and browsing

- **Three ways to browse** (a row, a column or a cube) and four looks: Plain Jane, Fancy Pants, and ChaOS's own **Pixel Cards** and **Pixel Names**, where each console is its name in the logo's pixel letters.
- **Game List View** shows a console's games as a scrolling list instead of cards (**MENU > Appearance**).
- **Favorites:** press **Y** on a game. **Recently Played** keeps your last twelve games across every console. Both are shelves at the front of the systems row, and either can be hidden.
- **Letter scrubbing:** **L2** and **R2** jump to the previous or next letter on any game list, with the letter shown big. Holding the d-pad shows it too.
- **Sorting, Play Time** and **favorites** per console, from the console's menu.

### Box art

**MENU > Box Art** fetches every missing cover over Wi-Fi. For one game, press **X** on it and choose **Get Box Art**: from libretro's collection, from **itch.io** for homebrew, or from one of your own screenshots. From your phone, ChaOS Transfer's **Cover** button takes a photo, a picture's link, an itch.io page or a Google Images result.

### Saves and achievements

- **Resume:** quitting or pressing power saves the game, and it continues from that moment next time. Six save slots as well.
- **Battery saves** are `.srm` files in `Saves/`, named after the game. Save states are in `.userdata/shared/.tortos/`, one folder per console.
- **RetroAchievements:** sign in under **MENU > RetroAchievements**. Achievements unlocked offline are sent later.

## The look

### Shaders

Each console's menu (MENU on its shelf) and the in-game menu have a **Shader** row, offering only the shaders made for that console:

| Consoles | Shader | By |
|---|---|---|
| Game Boy, Game Boy Color, Game Boy Advance | PT SkyWalker541, or lcd3x | SkyWalker541; Gigaherz |
| NES, SNES, Genesis, Master System, TurboGrafx-16 | crt-lottes-fast, or the lighter Scanlines | Timothy Lottes, adapted by hunterk; RiskyJumps |
| Game Gear, Neo Geo Pocket, Neo Geo Pocket Color | lcd3x | Gigaherz |

- **Left and right** turn it on and off; **A** opens its settings. **Reset to Recommended** puts back the shader's own.
- **Style:** the CRT consoles choose between crt-lottes-fast and Scanlines (about a tenth of the work). The Game Boys choose between PT SkyWalker541 and lcd3x.
- **Speed**, in the in-game Shader menu, says whether the game keeps full speed with the shader as set: **Full Speed**, **Near the Limit** or **Too Slow**, with the time it takes.

<details>
<summary><b>More about the shader settings</b></summary>

- PT's **Pixel Effect** (its grid) follows the game's pixels, so it's drawn only in Display Mode **Integer**. For a grid in Aspect or Stretch, use the lcd3x Style.
- The CRT starts flat: curvature works, but costs the CPU about twice as much.
- **Shader Time** is the shader's share of each frame, **Frame Time** the whole frame's work against the time a frame has. If Frame Time is high but Shader Time small, the console's emulator is the slow part. Right after a change the figures are an estimate, marked **(est.)**.
- Settings are saved per console.

</details>

### Display Mode

Per console: **Integer** (whole-number scaling, every pixel the same size), **Aspect** (as large as the game's shape allows) or **Stretch** (the whole screen). Every shader works in all three.

### Overlays

The **Overlay** row offers **None** or any PNG in that console's folder in `Overlays/`. ChaOS includes overlays for the Game Boys (by [SkyWalker541](https://github.com/SkyWalker541/TrimUI-Brick-Overlays) and [KrutzOtrem](https://github.com/KrutzOtrem/Trimui-Brick-Overlays)), Game Gear and Neo Geo Pocket Color (by KrutzOtrem).

- Every console has a folder, so you can add your own: **1024×768** PNGs with a transparent window for the game. The [NextUI Community Overlays](https://github.com/LoveRetro/nextui-community-overlays) collect many more.
- The included ones are framed for **Integer**, and are put back after an update if missing. Your own are never touched.

### Appearance

**MENU > Appearance** has the UI Theme, UI Direction, Transition Speed, Game List View and **Menu Color** on one page, and switches for which shelves show: **Show Favorites**, **Show Recently Played** and **Show Muse** (SELECT opens Muse either way).

- **Every menu says what its rows do**, in a line at the bottom.
- **The clock and battery** are in every menu's heading. **MENU > Date & Time** sets 12- or 24-hour time and how the battery shows.
- **Night Mode** (**MENU > Night Mode**) adds eight volume steps below the quietest, for playing in bed.

## In a game

- **MENU** opens the in-game menu: save and load, Display Mode, shader, overlay, controls, screenshots, achievements, reset and quit. The paused game shows each shader setting and overlay as you scroll, and **holding Y** hides the menu to see the whole picture.
- **Fast forward:** **MENU + R1** (2×, 3×, 4×).
- **Rewind:** turn it on in a console's menu, then **hold MENU + L1** to step back through the last minutes. It costs a little speed, so it's off until you want it.
- **Screenshots:** **MENU + X** saves one; **Screenshots** in the in-game menu shows them full screen. They're PNGs in `Screenshots/<console>/<game>/`.
- **Button mapping:** a console's **Controls** shows what every button does and lets you change it, per console or per game, with **turbo**. The Game Boys, NES, Master System, Game Gear, TurboGrafx-16 and Neo Geo Pockets start with X and Y as turbo A and B.
- **Games come first:** nothing of ChaOS's own runs while you play. A download in progress pauses and picks up afterwards. Muse keeps playing.
- **The battery to the end:** a game is saved and the Brick turns off when the battery is truly empty, about half an hour later than its gauge's 2%.

## Ports

Ports are PC games rebuilt for handhelds, packaged by [PortMaster](https://portmaster.games): Cave Story, Doom, OpenTyrian, Spelunky Classic and over a thousand more, most of them free.

**Getting started**

1. **MENU > Get PortMaster** downloads PortMaster over Wi-Fi. Nothing about ports is on the Brick until you choose this.
2. A **Ports** shelf appears. Its first card, **PortMaster**, opens the browser.

**The browser**

- **May Run on ChaOS** lists every port whose needs the Brick can meet, matching PortMaster's own filters: it leaves out ports that need a second analog stick (the d-pad stands in for one stick), 2 GB of memory, OpenGL, or another processor. **Show All** lists everything, with those marked. Also **Genres**, **Installed**, **Updates**, and **Ready to Run Only**, which hides ports that need files from a game you own.
- Each port shows its **screenshot**, genre, size, who ported it, what it needs, and its description (**Y** reads it all). **L2 / R2** jump by letter.
- The catalog comes from PortMaster and refreshes daily; **Refresh List** fetches it now.

**Installing**

- **A** installs or updates a port. The download is checked against PortMaster's checksum, its **runtime** (Godot, .NET, Java, Pyxel and so on) is fetched and unpacked with it, and any **library** it needs that the Brick hasn't got comes from ChaOS's library pack. Its screenshot becomes its box art.
- A port that needs files from a game you own says which and where, after it installs.
- **Updates** unpack over the old version, so a port's saves and the game files you copied in stay.

**Playing**

- **Select + Start** quits a port.
- **Power** freezes it and asks: **Resume**, back to the game exactly as it was, **Close Port**, or **Power Off**. Pressing power again resumes too.
- A port that stops on a library ChaOS doesn't have says which, instead of dropping back to the shelf in silence.

**Removing**

- **X** on a port's page removes it, either **Keeping Saves and Settings** (its own files go, what it saved stays for when you reinstall) or **Removing Everything**.
- **Uninstall PortMaster**, at the bottom of PortMaster's page, works the same way for every port at once, and keeps PortMaster's settings for next time if you like.
- PortMaster's own updates (**Update PortMaster**) keep its runtimes and settings.

Each port's last run is logged in `.userdata/brick/ports/logs/`, the latest in `.userdata/brick/ports/last.log`.

## RomM

[RomM](https://romm.app) is a game library you run on your own computer or NAS. **MENU > RomM** connects the Brick to it over Wi-Fi.

- **Sign In:** set **Server** to its address (like `nas.local:8080`), then **Sign In**. On RomM 5 the Brick shows a short code to approve in RomM's web page; on RomM 4, make a pairing code in RomM (your profile, Client API Tokens) and type it in. Only RomM's device token is kept, never a password.
- **Games:** your library by console, with covers. **A** downloads a game onto its shelf, with RomM's cover as its box art. A game in several files (a CD game) comes as a folder.
- **Sync Saves:** RomM's own save sync, for the games downloaded from RomM. A save the server replaces is kept first in `Saves/.romm-backup`, and when both changed yours is sent up beside the server's, so nothing is lost. It runs only when you choose it.
- **BIOS:** the BIOS files RomM keeps for ChaOS's consoles, into `Bios/`.

## Music and audiobooks

**Muse**, on **SELECT** from anywhere, plays music and audiobooks, and keeps playing through a game.

- Music goes in `Music/`, a folder per artist with one per album: `Music/Radiohead/The Bends/01 Planet Telex.mp3`.
- Each book goes in its own folder in `Audiobooks/`, as one M4B or a folder of MP3s. A book remembers where you stopped.
- MP3, M4A, M4B, MP4, AAC, FLAC, Ogg, Opus and WAV all play. **MENU > Show** on Muse's shelf switches between music and books.
- **Bluetooth headphones:** **MENU > Bluetooth**, **Y** to search and **A** to pair. They reconnect by themselves, the volume buttons reach them, and their play, pause and skip buttons control Muse.

## Updates

**MENU > Updates** keeps ChaOS up to date over Wi-Fi. Your games, music, saves and settings stay.

- When the Brick joins Wi-Fi it checks once for a newer ChaOS and says so at the top of the screen. It never downloads or installs anything by itself.
- **What's New** shows what a release changes.
- **Update** downloads only what changed when the system underneath is the same, a few MB. A release that changes the system (like 2.0.0, which adds what ports need) downloads the whole system and writes it during a restart.
- **Full Install** is always offered, and also repairs a card whose ChaOS files have gone wrong.
- It needs the charger or a quarter of a charge. If the power goes out while installing, nothing is lost: the Brick finishes on the next start-up, or update with [ChaOS Installer](https://github.com/SkyWalker541/ChaOS-Installer/releases/latest).

<details>
<summary><b>What the Updates screen's messages mean</b></summary>

| Message | What it means | What to do |
|---|---|---|
| needs Wi-Fi | The Brick isn't online. | Join a network under **MENU > Wi-Fi**. |
| Wi-Fi is off | Wi-Fi is turned off. | Turn it on under **MENU > Wi-Fi**, then **Try Again**. |
| Wi-Fi isn't connected | No network, or no internet address yet. | Connect, wait a moment, then **Try Again**. |
| charge to 25% | The battery is too low to install safely. | Charge to 25%, or plug in the charger. |
| This one needs ChaOS Installer | The new version changes parts of the card the Brick can't update by itself. | Update with ChaOS Installer on a computer. |
| Couldn't reach GitHub | No internet on this network, or GitHub is down. | Check the network, then **Try Again**. |
| Couldn't start the check | The check couldn't start at all. | Restart the Brick. If it keeps happening, send the logs. |
| Update info unreadable | The release's update information is damaged or missing. | Try again later. |
| Not enough space on the card | An update needs about 200 MB free. | Free up space, then **Try Again**. |
| Couldn't start the download | The download couldn't start. | **Try Again**, or restart the Brick. |
| The download stopped | The connection dropped partway. | **Try Again** on a steadier connection. |
| The download went missing | The partial download disappeared from the card. | **Try Again**, and leave `.dev` alone while it downloads. |
| Checksum didn't match | The download arrived damaged and was deleted. Nothing changed. | **Try Again**, or another network. |
| The download wouldn't unpack | Usually a full or failing card. It was deleted. | Free up space and **Try Again**. |
| Couldn't check the download | The check couldn't run. | **Try Again**. If it keeps happening, send the logs. |
| The download is gone | The finished download was removed before installing. | Download it again. |
| The download isn't a system | The file on the card isn't a ChaOS update. Nothing changed. | **Remove Download**, then download again. |
| Too big for this card | The new system doesn't fit the system partition. Nothing changed. | Update with ChaOS Installer. |
| No boot partition found | The card's boot partition is missing. Nothing changed. | Update with ChaOS Installer. |
| Couldn't open the boot files | The boot partition wouldn't open. Nothing changed. | Restart and try again, or use ChaOS Installer. |
| Couldn't write to the card | The card couldn't be written to. Nothing changed. | Restart and try again, or check the card on a computer. |
| Couldn't copy the update | A small update stopped partway. The download stays on the card. | **Install and Restart** again. |
| Needs ChaOS Installer | This card's start-up files can't install updates by themselves. | Update with ChaOS Installer once. |

If **About ChaOS** still shows the old version after a restart, the install didn't finish: start the Brick again, and if it still doesn't take, use ChaOS Installer. Every step is in the logs, on lines starting `update:`.

</details>

## Controls

| Button | What it does |
|---|---|
| **A / B** | Choose and back |
| **MENU** | Settings for whatever you're looking at; in a game, the in-game menu |
| **SELECT** | Muse, from anywhere |
| **X / Y** | On the shelf, a game's details and favorite; in a game, turbo A and B |
| **L2 / R2** | Jump by letter |
| **MENU + R1** | Fast forward |
| **MENU + L1** (hold) | Rewind, where it's on |
| **MENU + X** | Screenshot |
| **POWER** | Save and turn off (in a port, the Resume / Close Port / Power Off panel) |

**In menus:** **A** chooses or opens a row marked **›**, **B** goes back one screen, **left / right** change a setting (hold to keep changing it), and **MENU** closes every menu at once. **MENU > Controls** shows every button on every screen. For every menu in detail, see TortOS's [guide](docs/guide.md).

## FAQ

<details>
<summary><b>My games don't show up.</b></summary>

Check the folder name against the [table](#systems), spelled exactly, and that the file type is listed for that console. A console with nothing in its folder is hidden. Games sent over ChaOS Transfer appear when you leave its screen; games copied any other way while the Brick is on appear after **Rescan Folder** in that console's menu, or after a restart.

</details>

<details>
<summary><b>A port won't start, or shows a black screen.</b></summary>

Check that it's listed under **May Run on ChaOS**: ports outside it need something the Brick doesn't have. If ChaOS names a missing library, it may be in a later library pack. Otherwise send the port's log from `.userdata/brick/ports/logs/`, and the port's own `log.txt` in its folder if it has one.

</details>

<details>
<summary><b>Will an update or reinstall lose my saves?</b></summary>

No. Updates over Wi-Fi, ChaOS Wire and ChaOS Installer's **Update** all keep your games, music, saves, settings, ports and port saves. Only writing the image by hand erases the card.

</details>

<details>
<summary><b>Something went wrong. How do I send the logs?</b></summary>

Open **MENU > ChaOS Transfer**, open its address in a browser, and click **Download logs** at the bottom of the first page. You get one `.tar.gz` with the last ten boots' logs and a short summary of the device, with Wi-Fi, headset and account names masked. The logs are on the card in `.userdata/brick/logs/`.

</details>

## Credits and license

ChaOS is **TortOS by Eric Reinsmidt** ([TortOS](https://github.com/ericreinsmidt/TortOS), on [plastron](https://github.com/ericreinsmidt/plastron)), with ChaOS's changes by SkyWalker541. Games run in [ChaOS-diatom](https://github.com/SkyWalker541/ChaOS-diatom), a fork of Eric Reinsmidt's [diatom](https://github.com/ericreinsmidt/diatom) that adds the shaders. Ports come from [PortMaster](https://portmaster.games). Who made what, and where everything came from, is in [CREDITS.md](CREDITS.md).

ChaOS is free software under the **GNU General Public License, version 3 or any later version** ([LICENSE](LICENSE)), as TortOS is: anyone may change it, share it and sell it, and whoever gets it, changed or not, gets its source and the same rights. TortOS is Copyright (c) 2026 Eric Reinsmidt; ChaOS's changes are Copyright (c) 2026 SkyWalker541.

- ChaOS-diatom is under the Mozilla Public License 2.0.
- PT SkyWalker541 is MIT; crt-lottes-fast, lcd3x and Scanlines are public domain.
- SkyWalker541's overlays are CC0; KrutzOtrem's are MIT.
- The libraries ChaOS adds for ports keep their own licenses, in `Licenses/` on the card.
- The cores keep their own licenses, two of them non-commercial.

Full notices are in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) and [CREDITS.md](CREDITS.md), and every release carries the source of everything in its image.

**For developers:** ChaOS is C and SDL2. [docs/building.md](docs/building.md) is TortOS's build guide. A ChaOS release image is TortOS's release image with ChaOS's launcher, emulator, overlays, web page, boot animation and port support swapped in, by `tools/chaos-image.sh` ([dist/README.md](dist/README.md)).
