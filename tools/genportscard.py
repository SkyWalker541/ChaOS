#!/usr/bin/env python3
"""Draw the Ports shelf's two cards: res/cards/classic/PORTS.png and
res/cards/fancy/PORTS.png.

Ports is not a console, so like Favorites and Muse there is no photograph to
cut and the cards are generated - the classic one on tools/cardstyle.py, the drawn set's own
stock, rule and tracked name.

THE MARK is an arcade stick reduced to flat shapes - a ball on a shaft over a
base with two buttons - the generic picture of "a game", which is what a port
is: any game at all, brought over. No strokes, as the deck has none.

THE ACCENT, A0AEC0, a cool steel grey - the color of a PC case, which is
where ports come from, and the one family left on a shelf of twelve colors
and two cyans (Favorites and Recently Played, tools/markdef.py's CYAN). The
same value is the Ports row's accent in config/systems.cfg; change one,
change the other.

    tools/genportscard.py
"""
import os

import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "PORTS.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")

STEEL = (0xA0, 0xAE, 0xC0)
SS = 4


def mark(d, cx, cy, fill, cut, scale=1.0):
    """The stick, about 300x363 at scale 1: its ink is centred 16px above
    (cx, cy), which the classic card allows for."""
    k = SS * scale
    # the base: a wide slab
    bw, bh = 300 * k, 120 * k
    bx0, by0 = cx - bw / 2, cy + 45 * k
    d.rounded_rectangle([bx0, by0, bx0 + bw, by0 + bh], radius=28 * k, fill=fill)
    # two buttons knocked out of it, right of the stick
    for dx in (55, 112):
        r = 19 * k
        bcx, bcy = cx + dx * k, by0 + bh / 2
        d.ellipse([bcx - r, bcy - r, bcx + r, bcy + r], fill=cut)
    # the shaft, rising from the base's left half
    sx = cx - 70 * k
    sw = 26 * k
    d.rectangle([sx - sw / 2, cy - 110 * k, sx + sw / 2, by0 + 8 * k], fill=fill)
    # the collar where it meets the base
    cw, ch = 96 * k, 26 * k
    d.rounded_rectangle([sx - cw / 2, by0 - ch + 6 * k, sx + cw / 2, by0 + 6 * k],
                        radius=12 * k, fill=fill)
    # the ball
    r = 58 * k
    by = cy - 140 * k
    d.ellipse([sx - r, by - r, sx + r, by + r], fill=fill)


def classic():
    im = cardstyle.card(STEEL, "PORTS",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy + 16 * k, fill, cut, 0.88 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


def fancy():
    """The same mark alone, for the photographic set: trimmed, longest side
    96% of a 384 frame, centered (res/cards/fancy/SOURCE.md). The buttons are
    knocked through to nothing, as the photographs' backgrounds are."""
    n = 1600
    big = Image.new("RGBA", (n * SS, n * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    mark(d, n * SS // 2, n * SS // 2, STEEL + (255,), (0, 0, 0, 0), scale=3.0)
    box = big.getchannel("A").getbbox()
    big = big.crop(box)
    k = 369.0 / max(big.size)
    sw, sh = round(big.width * k), round(big.height * k)
    big = big.resize((sw, sh), Image.LANCZOS)
    out = Image.new("RGBA", (384, 384), (0, 0, 0, 0))
    out.paste(big, ((384 - sw) // 2, (384 - sh) // 2), big)
    out.save(FANCY)
    print(FANCY, out.size, "ink", out.getchannel("A").getbbox())


if __name__ == "__main__":
    classic()
    fancy()
