# Brick Pro: integration test plan

For the first days with a Brick Pro in hand, on Pre-Release. Each step says
what to do, what should happen, and which log line proves it. Logs come from
ChaOS Transfer's **Download logs** (or `.userdata/brick/logs/chaos.log` and
`ports/logs/` on the card). **Logging > Button Presses** (About ChaOS) adds a
raw `[in] code=... val=...` line for every key the launcher reads.

The Brick Pro's controls, and the name each has in ChaOS:

| On the Pro | Kernel (brick-pad) | ChaOS and Diatom | A port (SDL, CHAOS_PAD) |
|---|---|---|---|
| D-pad | BTN_DPAD_* | Up/Down/Left/Right | dpup/dpdown/dpleft/dpright |
| A B X Y | SOUTH EAST NORTH WEST | A B X Y | by Controls Layout |
| L1 R1, L2 R2 | TL TR, TL2 TR2 | L1 R1, L2 R2 | shoulders, triggers |
| Select, Start, Menu | SELECT START MODE (0048 patch) | Select, Start, Menu | back, start, guide |
| M1, M2 | THUMBL, THUMBR (0048) | **F1, F2** | currently leftstick/rightstick (see 4.3) |
| L3, R3 | TRIGGER_HAPPY1, 2 (0046) | L3, R3 | **not mapped yet** (see 4.3) |
| Left stick | ABS_X, ABS_Y (0 to 4095) | menus' d-pad; Calibrate Sticks | leftx, lefty (CHAOS_STICKS) |
| Right stick | ABS_RX, ABS_RY | Calibrate Sticks | rightx, righty |
| Vol +/-, Power | KEY_VOLUMEUP/DOWN, KEY_POWER | Vol, Bright (with Menu), Power | not the port's |

## 1. Boot and the screen

1. Install Pre-Release with ChaOS Installer. Power on.
   - The boot logo shows and the shelf appears: Pro Detect chose the Pro's
     device tree and panel (a white screen means it didn't).
   - chaos.log: `input: two sticks (Brick Pro)`.
2. **MENU > System Settings > Screen Test.**
   - Page 1: the panel's size, depth and refresh. chaos.log: `hwtest: screen: ...`.
   - Red, Green, Blue, White, Black: no dead or stuck pixels, even colour.
   - Gradients: smooth, no banding steps or colour cast.
   - Grid and edges: the white border on every edge, the red corners whole,
     the green cross in the centre. Nothing cut off or shifted: the screen is
     the right size and the right way round.
   - Checkerboard: no shimmer or moiré (pixel alignment).
   - Moving bar: smooth, no tearing. chaos.log:
     `hwtest: screen: moving bar at N frames a second` (about 60).
3. Brightness (Menu + Vol): every step, lowest still visible.
4. A game in Diatom (Display Mode Integer, then Aspect): centred, sharp,
   no tearing. Diatom's log: `brick: fb ...` the panel's size.
5. A port: the port's log `pvrfb: 1024x768 ...` and, with the Pad Test (4),
   `padtest: window WxH`.

## 2. Every button in ChaOS's menus

1. **MENU > System Settings > Calibrate Sticks**: follow it through. Then
   check `.userdata/brick/sticks.cfg` was written.
2. **MENU > System Settings > Button Test**: press every button once,
   including L3, R3, M1 (shows F1), M2 (F2), Vol +/-, Menu + Vol (Bright),
   and Power. Each lights while held and turns green. Move both sticks to
   every edge. Hold Select + Start to leave.
   - chaos.log: `hwtest: <button>, first press` for each, then
     `hwtest: 24 of 24 buttons seen` (anything missing is named),
     and each stick axis's range against its calibration.
3. In ordinary menus: the left stick moves as the d-pad, with no drift at rest;
   F1 or F2 alone goes Home; L2/R2 jump by letter on a shelf.

## 3. Games (Diatom)

For each console you play (at least NES, SNES, GBA, Genesis, PlayStation):

1. A, B, X, Y, L1, R1, L2, R2, Select, Start do what they do on a Brick.
2. F1 (M1) rewinds when Rewind is on; F2 (M2) fast forwards.
3. Menu opens the in-game menu; Menu + Select saves and quits.
4. Left stick as d-pad (in-game menu, **Left Stick as D-Pad**): on, the stick
   moves; off, it doesn't.
5. PlayStation: a game that uses analog (Ape Escape, Gran Turismo 2's
   steering): both sticks, and L3/R3, work as a DualShock's.
6. A free controller test ROM (the 240p Test Suite's controller test, for SNES,
   Genesis and GBA) shows each button by the console's own name.

## 4. Ports and PortMaster

1. **PortMaster's app**: A chooses, B goes back (it's always Nintendo).
   The stick moves the cursor, if PortMaster takes it. Update PortMaster from
   its own menu; install a port.
2. **ChaOS Pad Test** (`tools/build-padtest.sh` makes the zip; unpack it into
   `Roms/Ports`, then Rescan Folder on the Ports shelf). Run it, press every
   button and move both sticks, then quit with Select + Start. Its log
   (`ports/logs/ChaOS Pad Test.log`):
   - `padtest: joystick 0 "Brick Gamepad" ... N buttons, 4 axes`: the Pro
     should say 4 axes (a Brick says 0).
   - `padtest: controller <name> down` for each mapped button, with the
     names Controls Layout says (Xbox: the Brick's B is `a`; Nintendo:
     its A is `a`).
   - `padtest: raw bN down` for each key: the number each reaches SDL as.
   - `padtest: key <name>`: what gptokeyb sends (padtest.gptk maps each
     button to a key named after it).
   - On quitting, the summary: controller buttons never pressed, and each
     axis's range. leftx/lefty/rightx/righty should reach near -32768 and
     32767 and rest near 0.
   - Do it twice, with Controls Layout on Xbox, then Nintendo.
3. **What we expect to fix from it**: today CHAOS_PAD maps SDL's
   `leftstick`/`rightstick` to b11/b12, which are the F1/F2 keys (M1/M2 on
   the Pro). The Pro's L3 and R3 come after the d-pad, probably as b17 and
   b18, and aren't mapped. The Pad Test's raw numbers say exactly; then
   CHAOS_STICKS (src/portsticks.h) maps them as `leftstick`/`rightstick`
   on a Pro.
4. A real port that uses sticks (a twin-stick shooter, or a 3D port): both
   sticks, the right way round, full reach, no drift. Its log:
   `input: sticks for the port: leftx:aN,...`.
5. The power menu in a port: shows every time; Resume brings sound back.
6. Doukutsu-rs: `pvrfb: tried again with GLES 2 profile: it worked`.

## 5. The rest

- Power off from the shelf, a game, a port: the logo stays until it's off.
- Muse: music and books, the lock (F1 + F2), Bluetooth.
- Wi-Fi, ChaOS Transfer, Download logs, an update over Wi-Fi.
- Battery: the percentage moves sensibly; charging shows.

## Reporting

Send the logs from ChaOS Transfer after each section, saying which section.
Anything a test names as never seen, or a picture that looks wrong, is the
first thing to fix.
