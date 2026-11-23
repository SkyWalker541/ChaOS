#!/usr/bin/env python3
"""Draw the Recently Played shelf's Plain Jane card, res/cards/classic/RECENT.png
(and, with fancy() only, a flat Fancy Pants card; the one in use since 2.7.7
is a photograph, cut by tools/genfavrecentphoto.py). In the sets' folders, where the launcher looks for a card; it sat
beside them until 2026-11-13, found by nothing, so the shelf drew its
generated card in every set.

Same standing arrangement as tools/genfavcard.py: not a console, so it is
generated, in TortOS's cyan (tools/markdef.py's CYAN), on tools/cardstyle.py -
the drawn set's own stock, rule and tracked name.

The mark is the Fancy Pants card's wall clock drawn flat: its rounded-square
case (a superellipse) solid, its face knocked out to the stock, twelve hour
marks, and the hands at the photograph's 10:10 with the second hand toward 7,
standing in the face - knocked out rather than outlined, as the deck has no
strokes; hands and marks bold enough to read at a side card's 62%. A round
clock with two hands punched through it until 2.7.6.
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


def superellipse(cx, cy, a, b, n, steps=360):
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s_ = math.cos(t), math.sin(t)
        pts.append((cx + a * math.copysign(abs(c) ** (2 / n), c),
                    cy + b * math.copysign(abs(s_) ** (2 / n), s_)))
    return pts


def bar(cx, cy, ang, length, w, back=0.0):
    """A hand or a mark: from `back` behind (cx, cy) to `length` along `ang`,
    clockwise from 12."""
    a = math.radians(ang - 90)
    ux, uy = math.cos(a), math.sin(a)
    px, py = -uy * w / 2, ux * w / 2
    x0, y0 = cx - ux * back, cy - uy * back
    x1, y1 = cx + ux * length, cy + uy * length
    return [(x0 + px, y0 + py), (x1 + px, y1 + py), (x1 - px, y1 - py), (x0 - px, y0 - py)]


def clock(d, cx, cy, k, fill, cut):
    """The wall clock, 300px across at k = 1."""
    A, F, n = 150 * k, 120 * k, 3.6
    d.polygon(superellipse(cx, cy, A, A * 0.96, 4.2), fill=fill)       # the case
    d.polygon(superellipse(cx, cy, F, F * 0.96, n), fill=cut)          # the face
    for h in range(12):                                                # the hours
        long_ = h % 3 == 0
        a = math.radians(h * 30 - 90)
        c, s_ = math.cos(a), math.sin(a)
        edge = F / ((abs(c) ** n + abs(s_ / 0.96) ** n) ** (1 / n))
        mx, my = cx + c * (edge - 10 * k), cy + s_ * (edge - 10 * k)
        d.polygon(bar(mx, my, h * 30 + 180, (26 if long_ else 15) * k,
                      (13 if long_ else 9) * k), fill=fill)
    d.polygon(bar(cx, cy, 305, 64 * k, 19 * k, 12 * k), fill=fill)     # hour, past 10
    d.polygon(bar(cx, cy, 60, 94 * k, 14 * k, 12 * k), fill=fill)      # minute, at 2
    d.polygon(bar(cx, cy, 204, 96 * k, 5.5 * k, 22 * k), fill=fill)    # seconds, toward 7
    r = 15 * k
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def fancy():
    n = 384 * SS
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    clock(d, n // 2, n // 2, 369 * SS / 300, CYAN + (255,), (0, 0, 0, 0))
    im = im.resize((384, 384), Image.LANCZOS)
    im.save(FANCY)
    print(FANCY, im.size, "ink", im.getchannel("A").getbbox())


def main():
    # 285px across at 0.95, as the heart and the round console marks are
    im = cardstyle.card(tuple(CYAN), "RECENTLY PLAYED",
                        lambda d, cx, cy, k, fill, cut: clock(d, cx, cy, 0.95 * k, fill, cut))
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
