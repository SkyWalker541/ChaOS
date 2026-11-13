#!/usr/bin/env python3
"""Draw the Recently Played shelf's two cards: res/cards/classic/RECENT.png
and res/cards/fancy/RECENT.png (the mark alone, as that set's cards are: trimmed,
longest side 96% of a 384 frame, centred, the hands knocked through to
nothing). In the sets' folders, where the launcher looks for a card; it sat
beside them until 2026-11-13, found by nothing, so the shelf drew its
generated card in every set.

Same standing arrangement as tools/genfavcard.py: not a console, so it is
generated, in TortOS's cyan (tools/markdef.py's CYAN), on the same card stock
and at the same measurements as the set - 640x820, a 12px accent rule at y=587,
the label under it in (239,239,244), 54px, baseline 727.

The mark is a clock, knocked out of a solid disc rather than outlined, for the
same reason as the bookmark on the Favorites card: the deck has no strokes.
Two hands, punched back to the card stock.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markdef import CYAN  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "classic", "RECENT.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "RECENT.png")
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")

W, H = 640, 820
STOCK = (28, 31, 42)
LABEL = (239, 239, 244)
RULE_Y, RULE_H = 587, 12
RADIUS = 28
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
    fancy()
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    d.rounded_rectangle([0, 0, W * SS - 1, H * SS - 1], radius=RADIUS * SS,
                        fill=STOCK + (255,))
    d.rectangle([0, RULE_Y * SS, W * SS - 1, (RULE_Y + RULE_H) * SS - 1],
                fill=CYAN + (255,))

    # The clock sits above the rule, centred on the panel, as the bookmark does.
    clock(d, (W // 2) * SS, 300 * SS, 130 * SS, CYAN + (255,), STOCK + (255,))

    im = im.resize((W, H), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    try:
        f = ImageFont.truetype(FONT, 54)
    except OSError:
        f = ImageFont.load_default()
    label = "RECENTLY PLAYED"
    # Same size as the set when it fits its width, smaller only if it must.
    size = 54
    while size > 30 and f.getlength(label) > W - 80:
        size -= 2
        f = ImageFont.truetype(FONT, size)
    d.text((W // 2, 727), label, font=f, fill=LABEL + (255,), anchor="ms")

    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
