#!/usr/bin/env python3
"""Draw the Ports shelf's two cards: res/cards/classic/PORTS.png and
res/cards/fancy/PORTS.png.

Ports is not a console, so like Favorites and Muse there is no photograph to
cut and the cards are generated - the classic one on tools/cardstyle.py, the drawn set's own
stock, rule and tracked name.

THE MARK is a retro PC monitor (ChaOS 2.7.4, for the arcade stick): ports
come from the PC, and its screen is a command line. Classic is flat shapes,
the case with its screen knocked out, a prompt and cursor on it, a drive slot
in the chin, the neck and the foot, no strokes, as the deck has none. Fancy
is the same monitor rendered (tools/raymarch.py) in matte beige plastic, and
its screen is a real MS-DOS session (CD GAMES, DIR /B, three games that are
DOS games and PortMaster ports alike), not filler. tools/genportsfancy.py,
the arcade stick, is kept.

THE ACCENT, A0AEC0, a cool steel grey - the color of a PC case, which is
where ports come from, and the one family left on a shelf of twelve colors
and two cyans (Favorites and Recently Played, tools/markdef.py's CYAN). The
same value is the Ports row's accent in config/systems.cfg; change one,
change the other.

    tools/genportscard.py
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "PORTS.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"   # drawn into the picture only

STEEL = (0xA0, 0xAE, 0xC0)
SS = 4


def mark(d, cx, cy, fill, cut, scale=1.0):
    """A monitor in flat shapes, about 300x330 at scale 1, ink centred on
    (cx, cy): the case with its screen knocked out, a prompt and a cursor on
    the screen, a drive slot in the chin, the neck and the foot."""
    k = SS * scale
    bw, bh = 300 * k, 236 * k
    x0, y0 = cx - bw / 2, cy - 165 * k
    d.rounded_rectangle([x0, y0, x0 + bw, y0 + bh], radius=30 * k, fill=fill)
    # the screen, knocked out
    sx0, sy0, sx1, sy1 = x0 + 30 * k, y0 + 28 * k, x0 + bw - 30 * k, y0 + bh - 62 * k
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=18 * k, fill=cut)
    # a prompt: a chevron, then a cursor block
    px, py = sx0 + 34 * k, sy0 + 40 * k
    t = 13 * k
    d.polygon([(px, py), (px + 30 * k, py + 26 * k), (px, py + 52 * k),
               (px, py + 52 * k - t * 1.4), (px + 30 * k - t * 1.1, py + 26 * k), (px, py + t * 1.4)], fill=fill)
    d.rectangle([px + 48 * k, py + 38 * k, px + 92 * k, py + 52 * k], fill=fill)
    # a drive slot in the chin, knocked out
    d.rounded_rectangle([x0 + bw - 132 * k, y0 + bh - 38 * k, x0 + bw - 40 * k, y0 + bh - 26 * k],
                        radius=6 * k, fill=cut)
    # the neck and the foot
    ny = y0 + bh
    d.polygon([(cx - 52 * k, ny - 2 * k), (cx + 52 * k, ny - 2 * k), (cx + 36 * k, ny + 44 * k),
               (cx - 36 * k, ny + 44 * k)], fill=fill)
    d.rounded_rectangle([cx - 118 * k, ny + 40 * k, cx + 118 * k, ny + 70 * k], radius=14 * k, fill=fill)


def classic():
    im = cardstyle.card(STEEL, "PORTS",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 0.88 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


# ---- fancy ----------------------------------------------------------------

M = rm.rot(4, -26, 0)
BEIGE = (212, 204, 182)
SW, SH = 1.46, 1.08          # the tube's face, half sizes


def screen_picture():
    """A real MS-DOS session, not filler: into the games folder, a bare
    listing (DIR /B, MS-DOS 5 on) of three games that are DOS games and
    PortMaster ports alike, and the prompt waiting. In a monospace face drawn
    small and blown up square, as a CRT's text mode is."""
    n = 250                                   # drawn at a quarter, then x4
    w, h = n, int(n * SH / SW)
    im = Image.new("RGB", (w, h), (12, 20, 14))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(MONO, 15)
    g = (120, 255, 140)
    lines = ["C:\\>CD GAMES", "C:\\GAMES>DIR /B", "DOOM", "KEEN", "QUAKE", "C:\\GAMES>_"]
    for k, t in enumerate(lines):
        d.text((16, 14 + k * 17), t, font=f, fill=g)
    im = im.convert("L").point(lambda v: 255 if v > 90 else 0)   # hard pixels, no grey edges
    im = Image.merge("RGB", [im.point(lambda v: 12 + v * (c - 12) // 255) for c in g])
    im = im.resize((w * 4, h * 4), Image.NEAREST)
    a = np.asarray(im, np.float64)
    a[::4] *= 0.78                            # scanlines
    hh, ww = a.shape[:2]
    yy, xx = np.mgrid[0:hh, 0:ww]
    rr = np.hypot((xx - ww / 2) / (ww / 2), (yy - hh / 2) / (hh / 2))
    a *= (1.08 - 0.35 * rr ** 2)[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


SCREEN = rm.Tex(screen_picture(), -SW, SW, -SH + 0.42, SH + 0.42, base=(14, 22, 16))


def frame(p):
    return rm.local(p, (0, 0, 0), M)


MATS = [rm.Mat(BEIGE, gloss=0.38, spec=0.5),                                 # 0 case
        rm.Mat(lambda p: SCREEN(frame(p)), gloss=0.82, spec=0.6, emit=0.9),  # 1 the tube's glass
        rm.Mat((34, 34, 38), gloss=0.3, spec=0.3),                           # 2 drive slot, vents
        rm.Mat((90, 220, 110), gloss=0.4, spec=0.4, emit=1.0)]               # 3 power light


def scene(p):
    q = frame(p)
    # the front: a deep rounded box; the back tapers like a tube's
    front = rm.box(q - [0, 0.2, 0.25], (1.85, 1.62, 0.42), 0.16)
    t = np.clip((0.0 - q[..., 2]) / 1.4, 0, 1)
    qb = q - [0, 0.28, -0.75]
    hb = np.stack([1.85 - 0.55 * t - 0.22, 1.5 - 0.45 * t - 0.22, np.full_like(t, 0.62 - 0.22)], -1)
    db = np.abs(qb) - hb
    back = (np.linalg.norm(np.maximum(db, 0), axis=-1) + np.minimum(db.max(-1), 0)) - 0.22
    case = rm.smin(front, back, 0.12)
    # the bezel's opening, and the tube's face a little inside, bulging
    win = rm.box(q - [0, 0.42, 0.68], (SW + 0.04, SH + 0.04, 0.12), 0.1)
    case = np.maximum(case, -win)
    bulge = 0.06 * (1 - ((q[..., 0] / SW) ** 2 + ((q[..., 1] - 0.42) / SH) ** 2) * 0.5)
    tube = rm.box(q - [0, 0.42, 0.56], (SW + 0.02, SH + 0.02, 0.03), 0.08) - bulge
    # the chin: a drive slot and a power light
    slot = rm.box(q - [0.85, -1.12, 0.67], (0.46, 0.035, 0.02), 0.01)
    led = rm.sphere(q - [-1.38, -1.12, 0.66], 0.055)
    # neck and foot
    neck = rm.box(q - [0, -1.62, -0.25], (0.55, 0.22, 0.5), 0.08)
    foot = rm.box(q - [0, -1.9, -0.2], (1.25, 0.09, 0.95), 0.08)
    case = np.minimum(case, np.minimum(neck, foot))
    d = np.stack([case, tube, slot, led], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    classic()
    rm.render(scene, FANCY, cam=(0.0, 1.2, 9.5), look=(0, -0.05, 0), fov=28)
