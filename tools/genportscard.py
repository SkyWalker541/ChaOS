#!/usr/bin/env python3
"""Draw the Ports shelf's Plain Jane card, res/cards/classic/PORTS.png.

Ports is not a console, so like Favorites and Muse there is no photograph to
cut and the cards are generated - the classic one on tools/cardstyle.py, the drawn set's own
stock, rule and tracked name.

THE MARK is the Fancy Pants card's computer (tools/genportsphoto.py, a
beige early-90s all-in-one, its CRT over a base unit) drawn flat from the
front, so the two themes show the same machine: the bezel with a prompt and
cursor on its screen and its round power button; the base unit with its
three knobs, label plate, lights, floppy slot and eject button. Ports come from
the PC. No strokes, as the deck has none. (An arcade stick until 2.7.4.)

THE ACCENT, A0AEC0, a cool steel grey - the color of a PC case, which is
where ports come from, and the one family left on a shelf of twelve colors
and two cyans (Favorites and Recently Played, tools/markdef.py's CYAN). The
same value is the Ports row's accent in config/systems.cfg; change one,
change the other.

    tools/genportscard.py
"""
import os
import sys


from PIL import ImageDraw  # noqa: F401  (cardstyle draws with it)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "PORTS.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")

STEEL = (0xA0, 0xAE, 0xC0)
SS = 4


def mark(d, cx, cy, fill, cut, scale=1.0):
    """The Fancy Pants card's computer in flat shapes, from the front: about
    300x318 at scale 1, ink centred on (cx, cy). The monitor's bezel, its
    screen knocked out with a prompt and cursor on it and its round power
    button in the corner; under it, the base unit, a little narrower, with
    the three knobs along its top, and its blank label plate, two lights, the
    floppy slot and its eject button knocked out. No strokes, as the deck has
    none."""
    k = SS * scale
    top = cy - 159 * k
    # the monitor's bezel
    mw, mh = 300 * k, 222 * k
    mx0 = cx - mw / 2
    d.rounded_rectangle([mx0, top, mx0 + mw, top + mh], radius=12 * k, fill=fill)
    sx0, sy0, sx1, sy1 = mx0 + 30 * k, top + 26 * k, mx0 + mw - 30 * k, top + mh - 44 * k
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=16 * k, fill=cut)
    px, py = sx0 + 28 * k, sy0 + 30 * k
    t = 12 * k
    d.polygon([(px, py), (px + 28 * k, py + 24 * k), (px, py + 48 * k),
               (px, py + 48 * k - t * 1.4), (px + 28 * k - t * 1.1, py + 24 * k), (px, py + t * 1.4)], fill=fill)
    d.rectangle([px + 44 * k, py + 35 * k, px + 86 * k, py + 48 * k], fill=fill)
    r = 10 * k
    bx, by = mx0 + 34 * k, top + mh - 21 * k
    d.ellipse([bx - r, by - r, bx + r, by + r], fill=cut)
    # the base unit, inset under it, a gap between
    uw, uh = 262 * k, 88 * k
    u0, uy = cx - uw / 2, top + mh + 8 * k
    d.rounded_rectangle([u0, uy, u0 + uw, uy + uh], radius=6 * k, fill=fill)
    # three knobs along its top, at the right
    for n in range(3):
        r = 7 * k
        kx = u0 + uw - (78 - n * 20) * k
        d.ellipse([kx - r, uy + 10 * k - r, kx + r, uy + 10 * k + r], fill=cut)
    # the label plate, two lights, the floppy slot and its eject button
    d.rounded_rectangle([u0 + 16 * k, uy + 30 * k, u0 + 74 * k, uy + 52 * k], radius=10 * k, fill=cut)
    for ly in (34, 48):
        d.rectangle([u0 + 100 * k, uy + ly * k, u0 + 108 * k, uy + (ly + 4) * k], fill=cut)
    # the floppy drive's panel: its slot, its light, its eject button
    d.rounded_rectangle([u0 + 140 * k, uy + 22 * k, u0 + 248 * k, uy + 66 * k], radius=4 * k, fill=cut)
    d.rounded_rectangle([u0 + 144 * k, uy + 26 * k, u0 + 244 * k, uy + 62 * k], radius=3 * k, fill=fill)
    d.rounded_rectangle([u0 + 152 * k, uy + 34 * k, u0 + 236 * k, uy + 41 * k], radius=2 * k, fill=cut)
    d.rectangle([u0 + 160 * k, uy + 50 * k, u0 + 167 * k, uy + 53 * k], fill=cut)
    d.rounded_rectangle([u0 + 208 * k, uy + 49 * k, u0 + 228 * k, uy + 57 * k], radius=4 * k, fill=cut)
    # the line where the drive's panel sits
    d.rectangle([u0 + 8 * k, uy + 70 * k, u0 + uw - 8 * k, uy + 72 * k], fill=cut)


def classic():
    im = cardstyle.card(STEEL, "PORTS",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 0.88 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


if __name__ == "__main__":
    classic()
