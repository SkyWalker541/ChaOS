# Where this art came from

Photographs of the console hardware, background removed to transparent alpha
and renamed to the tags in `systems.cfg`. 384x384.

Replaced 2026-09-11 with the SAME photographer's shots of the consoles ALONE.
The previous set included controllers on some machines and not others, which
made the silhouettes disagree down a shelf. These are trimmed to their own
alpha, scaled so the longest side is 96% of the frame, and centred - the old
set ranged from 89% to 98% and read as uneven for that reason rather than any
other. The high-resolution originals they were cut from are kept outside the
repository; `GENESIS.png` is the file that arrived as `MD.png`.

  Author:  Evan Amos
  Source:  Vanamo Online Game Museum / Wikimedia Commons
           https://commons.wikimedia.org/wiki/User:Evan-Amos
  License: Public Domain, released by the author

Public domain carries no attribution requirement. This file records where the
images came from so nobody has to work it out again, not because anything is
owed. Console trade dress belongs to the respective rights holders and appears
here to identify the platform, as it does in the source photographs.

Every image is US-region hardware, which is what the shelf names them after:
NES.png is an NES and not a Famicom, SNES.png a SNES, GENESIS.png a Genesis
Model 2, PCE.png a TurboGrafx-16. They were imported under Japanese short
codes (FC, SFC, MD) that describe the code, not the photograph.

FAVORITES.png is not one of these photographs, and nothing above applies to
it. Favorites is a shelf rather than a console, so there is no hardware to
photograph and it carries a heart instead. Since ChaOS 2.7.4 it is rendered
(`tools/genfavfancy.py`) from no images, in matte red plastic to match the
set's finish, where it was a glossy generated heart. It carries no
attribution requirement, and no rights are claimed in it.

## MUSE.png

Not a console, and not one of the photographs above. A green iPod nano (4th
generation) at the same 3/4 angle as the set, supplied by Eric on 2026-09-18
for Muse's card; where the photograph came from is not recorded here. The
original is kept outside the repository in `cards_backup_photos/muse.png`, and
`tools/genmusecard.py` cuts this card from it: trimmed to its alpha, longest
side 96% of 384 and centred, in the 384 canvas because it is taller than wide.

## RECENT.png

Not a console: Recently Played's card, a steel watch with ChaOS on its dial.
The dial is SkyWalker541's, made for ChaOS (generated with Gemini) and given
to the project on 2026-11-14; its source is `source/recent-watch.jpg`, kept
here. `tools/genrecentwatch.py` lays its dial and inner ring on a modelled
steel case and crown and renders it (`tools/raymarch.py`), so it turns as the
other rendered cards do, with the case's side showing, framed as above. It
replaces the rendered alarm clock (`tools/genrecentfancy.py`), which is kept. Since 2.7.4 its case is satin rather than polished,
to sit with the set's matte plastics.

## PS.png

The PlayStation (SCPH-1001, the US model), supplied by SkyWalker541 on
2026-11-14, downloaded from vhv.rs (an aggregator of PNGs:
https://www.vhv.rs/viewpic/hxJhoJw_playstation-1-png-transparent-png/), its
transparency flattened there onto a white and grey checkerboard. It is the photograph from Evan Amos's set on Wikimedia
Commons, released by him into the public domain, as the consoles above are.
The checkerboard was taken off by filling from the edges over near-white,
neutral pixels only - the console's own grey is darker - then trimmed and
scaled as above, longest side 96% of 384.

## PORTS.png

Not a console: Ports' card, a beige early-90s all-in-one computer, its CRT
over a base unit with a floppy drive, supplied by SkyWalker541 on 2026-11-21
for this card
(`source/ports-pc.png`, kept here). `tools/genportsphoto.py` cuts it from its
white backdrop, cleans and white-balances it toward the set and frames it as
above. It has no maker's name on it. It replaced a rendered arcade stick
(`tools/genportsfancy.py`, kept).
