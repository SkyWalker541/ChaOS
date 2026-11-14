#!/usr/bin/env python3
"""Draw the PlayStation shelf's Plain Jane card, res/cards/classic/PS.png.

On tools/cardstyle.py, the drawn set's own stock, rule and tracked name, like
the PICO-8 and Ports cards.

THE MARK is a compact disc reduced to flat shapes - the disc, its clamping
ring knocked out around the hole, and a band of the data side catching the
light - because a PlayStation game is a disc, as a NES game is a cartridge.
Nothing of Sony's logo or the controller's symbols. No strokes, as the deck
has none.

THE ACCENT, 8C93A8, the cool grey of the console's own case, as the SNES card
takes the SNES's purple. The same value is the PlayStation row's accent in
config/systems.cfg; change one, change the other.

    tools/genpscard.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "PS.png")

GREY = (0x8C, 0x93, 0xA8)
PALE = (0xC9, 0xCD, 0xDA)
SS = 4


def mark(d, cx, cy, fill, cut, scale=1.0):
    """The disc, about 300 across at scale 1, centred on (cx, cy)."""
    k = SS * scale
    r = 150 * k

    def disc(rad, f):
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=f)

    disc(r, fill)
    # the light across the data side: a wedge of the pale grey, from the
    # upper left, between the rim and the clamping ring
    a0, a1 = 200, 250
    pts = [(cx + 62 * k * math.cos(math.radians(a)), cy + 62 * k * math.sin(math.radians(a)))
           for a in range(a0, a1 + 1)]
    pts += [(cx + (r - 14 * k) * math.cos(math.radians(a)), cy + (r - 14 * k) * math.sin(math.radians(a)))
            for a in range(a1, a0 - 1, -1)]
    d.polygon(pts, fill=PALE + (255,))
    # the clamping ring, knocked out, then the plastic hub inside it, then the hole
    disc(58 * k, cut)
    disc(46 * k, fill)
    disc(20 * k, cut)


def classic():
    im = cardstyle.card(GREY, "PLAYSTATION",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 1.0 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


if __name__ == "__main__":
    classic()
