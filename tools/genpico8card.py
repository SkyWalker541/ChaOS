#!/usr/bin/env python3
"""Draw the PICO-8 shelf's Plain Jane card, res/cards/classic/PICO8.png.

PICO-8 is a fantasy console with no hardware to photograph, so like Ports its
cards are generated: this one on tools/cardstyle.py, the drawn set's own
stock, rule and tracked name; the Fancy Pants one as an object
(tools/genpico8fancy.py).

THE MARK is a cartridge reduced to flat shapes - a slab with its top corner
cut, a label knocked out of it with a pixel heart on it, grip ridges under -
because a PICO-8 game is a cart, and a cart of pixels. Nothing of PICO-8's
own logo. No strokes, as the deck has none.

THE ACCENT, FF77A8, the pink of PICO-8's sixteen-color palette. The same
value is the PICO-8 row's accent in config/systems.cfg; change one, change
the other.

    tools/genpico8card.py
"""
import os
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "PICO8.png")

PINK = (0xFF, 0x77, 0xA8)
SS = 4

# the pixels on the label, a heart of 7x6
GRID = [
    ".##.##.",
    "#######",
    "#######",
    ".#####.",
    "..###..",
    "...#...",
]


def mark(d, cx, cy, fill, cut, scale=1.0):
    """The cart, about 260x320 at scale 1, centred on (cx, cy)."""
    k = SS * scale
    w, h = 260 * k, 320 * k
    x0, y0 = cx - w / 2, cy - h / 2
    x1, y1 = x0 + w, y0 + h
    notch = 52 * k
    # the slab, its top right corner cut
    d.polygon([(x0, y0), (x1 - notch, y0), (x1, y0 + notch), (x1, y1), (x0, y1)], fill=fill)
    # the label
    lx0, ly0 = x0 + 34 * k, y0 + 70 * k
    lx1, ly1 = x1 - 34 * k, y1 - 70 * k
    # the grip ridges under it, knocked out
    for i in range(4):
        rx = cx + (i - 1.5) * 34 * k
        d.rectangle([rx - 6 * k, y1 - 50 * k, rx + 6 * k, y1 - 18 * k], fill=cut)
    d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=10 * k, fill=cut)
    # its pixels, square, centred on the label
    cols, rows = len(GRID[0]), len(GRID)
    px = min((lx1 - lx0) * 0.78 / cols, (ly1 - ly0) * 0.78 / rows)
    gx = (lx0 + lx1) / 2 - cols * px / 2
    gy = (ly0 + ly1) / 2 - rows * px / 2
    for r, line in enumerate(GRID):
        for c, ch in enumerate(line):
            if ch == "#":
                d.rectangle([gx + c * px, gy + r * px, gx + (c + 1) * px - 1, gy + (r + 1) * px - 1],
                            fill=fill)


def classic():
    im = cardstyle.card(PINK, "PICO-8",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 0.95 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


if __name__ == "__main__":
    classic()
