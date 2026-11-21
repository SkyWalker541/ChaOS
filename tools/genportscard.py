#!/usr/bin/env python3
"""Draw the Ports shelf's Plain Jane card, res/cards/classic/PORTS.png.

Ports is not a console, so like Favorites and Muse there is no photograph to
cut and the cards are generated - the classic one on tools/cardstyle.py, the drawn set's own
stock, rule and tracked name.

THE MARK is the Fancy Pants card's PC (tools/genportsphoto.py, a mid-90s
beige monitor on its desktop case) drawn flat from the front, so the two
themes show the same machine: the monitor with a prompt and cursor on its
screen, its button, lights and four knobs; the swivel and foot plate; the
case with its buttons, floppy slot, vent grille and CD drive. Ports come from
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
    """The Fancy Pants card's PC in flat shapes, from the front: about 300x326
    at scale 1, ink centred on (cx, cy). The monitor, its screen knocked out
    with a prompt and cursor on it, and in its chin the button, the two
    lights and four knobs; its swivel and foot plate; and the desktop case
    under it with its two buttons, the floppy slot, the vent grille and the
    CD drive over a blank bay, all knocked out. No strokes, as the deck has
    none."""
    k = SS * scale
    top = cy - 163 * k
    # the monitor
    mw, mh = 236 * k, 196 * k
    mx0 = cx - mw / 2
    d.rounded_rectangle([mx0, top, mx0 + mw, top + mh], radius=16 * k, fill=fill)
    sx0, sy0, sx1, sy1 = mx0 + 22 * k, top + 20 * k, mx0 + mw - 22 * k, top + mh - 44 * k
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=12 * k, fill=cut)
    px, py = sx0 + 24 * k, sy0 + 28 * k
    t = 11 * k
    d.polygon([(px, py), (px + 26 * k, py + 22 * k), (px, py + 44 * k),
               (px, py + 44 * k - t * 1.4), (px + 26 * k - t * 1.1, py + 22 * k), (px, py + t * 1.4)], fill=fill)
    d.rectangle([px + 40 * k, py + 32 * k, px + 78 * k, py + 44 * k], fill=fill)
    cyl = top + mh - 22 * k
    d.rounded_rectangle([mx0 + 20 * k, cyl - 8 * k, mx0 + 44 * k, cyl + 8 * k], radius=3 * k, fill=cut)
    for dx in (60, 76):
        r = 3 * k
        d.ellipse([mx0 + dx * k - r, cyl - r, mx0 + dx * k + r, cyl + r], fill=cut)
    for n in range(4):
        r = 8 * k
        kx = mx0 + mw - (98 - n * 24) * k
        d.ellipse([kx - r, cyl - r, kx + r, cyl + r], fill=cut)
    # the swivel and the foot plate
    y = top + mh
    d.rectangle([cx - 46 * k, y - 2 * k, cx + 46 * k, y + 12 * k], fill=fill)
    d.rounded_rectangle([cx - 84 * k, y + 10 * k, cx + 84 * k, y + 24 * k], radius=5 * k, fill=fill)
    # the desktop case, a gap under the plate
    cy0 = y + 30 * k
    cw, ch = 300 * k, 96 * k
    c0 = cx - cw / 2
    d.rounded_rectangle([c0, cy0, c0 + cw, cy0 + ch], radius=8 * k, fill=fill)
    # its two square buttons and the floppy slot
    for bx in (16, 38):
        d.rounded_rectangle([c0 + bx * k, cy0 + 16 * k, c0 + (bx + 15) * k, cy0 + 31 * k], radius=2 * k, fill=cut)
    d.rounded_rectangle([c0 + 64 * k, cy0 + 18 * k, c0 + 136 * k, cy0 + 30 * k], radius=2 * k, fill=cut)
    # the vent grille
    for n in range(14):
        vx = c0 + (16 + n * 9) * k
        d.rectangle([vx, cy0 + 46 * k, vx + 4 * k, cy0 + 80 * k], fill=cut)
    # the CD drive over a blank bay
    d.rounded_rectangle([c0 + 160 * k, cy0 + 14 * k, c0 + 284 * k, cy0 + 44 * k], radius=2 * k, fill=cut)
    d.rounded_rectangle([c0 + 160 * k, cy0 + 50 * k, c0 + 284 * k, cy0 + 82 * k], radius=2 * k, fill=cut)
    d.rectangle([c0 + 170 * k, cy0 + 26 * k, c0 + 210 * k, cy0 + 30 * k], fill=fill)


def classic():
    im = cardstyle.card(STEEL, "PORTS",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 0.88 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


if __name__ == "__main__":
    classic()
