#!/usr/bin/env python3
"""Draw the Recently Played shelf's two cards: res/cards/classic/RECENT.png
(and, with fancy() only, a flat Fancy Pants card; the one in use is rendered
by tools/genrecentfancy.py). In the sets' folders, where the launcher looks for a card; it sat
beside them until 2026-11-13, found by nothing, so the shelf drew its
generated card in every set.

Same standing arrangement as tools/genfavcard.py: not a console, so it is
generated, in TortOS's cyan (tools/markdef.py's CYAN), on tools/cardstyle.py -
the drawn set's own stock, rule and tracked name.

The mark is a clock, knocked out of a solid disc rather than outlined, for the
same reason as the bookmark on the Favorites card: the deck has no strokes.
Two hands, punched back to the card stock.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markdef import CYAN  # noqa: E402
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "classic", "RECENT.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "RECENT.png")

SS = 4                      # supersample, so the edges are not ragged


def hand(cx, cy, angle_deg, length, width):
    """A straight hand from the centre, as a thin quad, at angle (0 = up)."""
    a = math.radians(angle_deg)
    dx, dy = math.sin(a), -math.cos(a)
    px, py = -dy, dx
    tx, ty = cx + dx * length, cy + dy * length
    w = width / 2.0
    return [(cx + px * w, cy + py * w), (tx + px * w, ty + py * w),
            (tx - px * w, ty - py * w), (cx - px * w, cy - py * w)]


def clock(d, cx, cy, r, fill, cut):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
    k = r / 130.0
    d.polygon(hand(cx, cy, 0, 92 * k, 26 * k), fill=cut)     # minute
    d.polygon(hand(cx, cy, 90, 66 * k, 26 * k), fill=cut)    # hour
    d.ellipse([cx - 14 * k, cy - 14 * k, cx + 14 * k, cy + 14 * k], fill=cut)


def fancy():
    n = 384 * SS
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    clock(d, n // 2, n // 2, (369 * SS) // 2, CYAN + (255,), (0, 0, 0, 0))
    im = im.resize((384, 384), Image.LANCZOS)
    im.save(FANCY)
    print(FANCY, im.size, "ink", im.getchannel("A").getbbox())


def main():
    # A disc 290px across: the round console marks' size (SNES's buttons are
    # 321 by 313), centred where they are
    im = cardstyle.card(tuple(CYAN), "RECENTLY PLAYED",
                        lambda d, cx, cy, k, fill, cut: clock(d, cx, cy, 145 * k, fill, cut))
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
