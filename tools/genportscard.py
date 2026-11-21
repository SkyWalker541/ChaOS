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
    """A mid-90s monitor in flat shapes, about 290x320 at scale 1, ink centred
    on (cx, cy): the bezel with its screen knocked out and a prompt and cursor
    on it, a row of buttons and the power button knocked out of the chin, and
    the tilt-swivel foot under it."""
    k = SS * scale
    bw, bh = 290 * k, 262 * k
    x0, y0 = cx - bw / 2, cy - 160 * k
    d.rounded_rectangle([x0, y0, x0 + bw, y0 + bh], radius=26 * k, fill=fill)
    sx0, sy0, sx1, sy1 = x0 + 26 * k, y0 + 24 * k, x0 + bw - 26 * k, y0 + bh - 58 * k
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=22 * k, fill=cut)
    px, py = sx0 + 32 * k, sy0 + 38 * k
    t = 13 * k
    d.polygon([(px, py), (px + 30 * k, py + 26 * k), (px, py + 52 * k),
               (px, py + 52 * k - t * 1.4), (px + 30 * k - t * 1.1, py + 26 * k), (px, py + t * 1.4)], fill=fill)
    d.rectangle([px + 48 * k, py + 38 * k, px + 92 * k, py + 52 * k], fill=fill)
    # the chin: four small buttons, then the power button
    by = y0 + bh - 30 * k
    for n in range(4):
        bx = x0 + bw - (150 - n * 24) * k
        d.rounded_rectangle([bx - 8 * k, by - 5 * k, bx + 8 * k, by + 5 * k], radius=4 * k, fill=cut)
    r = 11 * k
    d.ellipse([x0 + bw - 46 * k - r, by - r, x0 + bw - 46 * k + r, by + r], fill=cut)
    # the neck and the round tilt-swivel foot
    ny = y0 + bh
    d.rectangle([cx - 50 * k, ny - 2 * k, cx + 50 * k, ny + 26 * k], fill=fill)
    d.ellipse([cx - 120 * k, ny + 18 * k, cx + 120 * k, ny + 58 * k], fill=fill)


def classic():
    im = cardstyle.card(STEEL, "PORTS",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 0.88 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


# ---- fancy ----------------------------------------------------------------
#
# A mid-90s 15" SVGA monitor, after the NEC MultiSync 3FGx (1993) and the
# beige monitors like it on every desk then: a squarish bezel in warm light
# grey, the tube's face (slightly curved, 4:3) in a bevelled opening, a deeper
# chin with a row of small buttons and the power button with its green light
# at the right, the long tapering back that held the tube's neck, vent slots
# across it, and a round tilt-swivel foot. Its screen is VGA text mode, light
# grey on black: a real MS-DOS session. No maker's name.
#
# Shot as a museum archive shoots hardware (Evan Amos's Vanamo Online Game
# Museum): even studio light, sharp all over, neutral colour, no props.

M = rm.rot(5, -40, 0)
CASE = (205, 201, 189)       # 90s "putty": warm light grey
SW, SH = 1.30, 0.975         # the tube's face, half sizes (4:3)
SX, SY = 0.0, 0.18           # its middle
FZ = 0.62                    # the bezel's front


def screen_picture():
    """A real MS-DOS session, not filler: into the games folder, a bare
    listing (DIR /B, MS-DOS 5 on) of three games that are DOS games and
    PortMaster ports alike, and the prompt waiting. In a monospace face drawn
    small and blown up square, as a CRT's text mode is."""
    n = 500                                   # drawn at a half, then x2
    w, h = n, int(n * SH / SW)
    im = Image.new("RGB", (w, h), (9, 10, 12))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(MONO, 23)
    g = (196, 198, 200)      # VGA text mode's light grey
    lines = ["C:\\>CD GAMES", "C:\\GAMES>DIR /B", "DOOM", "KEEN", "QUAKE", "C:\\GAMES>_"]
    for k, t in enumerate(lines):
        d.text((38, 34 + k * 28), t, font=f, fill=g)
    im = im.convert("L").point(lambda v: 255 if v > 90 else 0)   # hard pixels, no grey edges
    im = Image.merge("RGB", [im.point(lambda v: 9 + v * (c - 9) // 255) for c in g])
    im = im.resize((w * 2, h * 2), Image.NEAREST)
    a = np.asarray(im, np.float64)
    a[::4] *= 0.9                             # a colour tube's faint lines
    hh, ww = a.shape[:2]
    yy, xx = np.mgrid[0:hh, 0:ww]
    rr = np.hypot((xx - ww / 2) / (ww / 2), (yy - hh / 2) / (hh / 2))
    a *= (1.08 - 0.35 * rr ** 2)[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


SCREEN = rm.Tex(screen_picture(), SX - SW, SX + SW, SY - SH, SY + SH, base=(9, 10, 12))


def frame(p):
    return rm.local(p, (0, 0, 0), M)


def tapered_hole(q, c, front, back, z_front, z_back, r):
    """A rounded box whose sides slope: half sizes `front` at z_front, `back`
    at z_back - the funnel a moulded bezel steps down through"""
    qb = q - c
    t = np.clip((z_front - q[..., 2]) / (z_front - z_back), 0, 1)
    hb = np.stack([front[0] + (back[0] - front[0]) * t - r, front[1] + (back[1] - front[1]) * t - r,
                   np.full_like(t, (z_front - z_back) / 2)], -1)
    qb = np.stack([qb[..., 0], qb[..., 1], q[..., 2] - (z_front + z_back) / 2], -1)
    db = np.abs(qb) - hb
    db[..., 2] = np.abs(qb[..., 2]) - (z_front - z_back) / 2
    return np.linalg.norm(np.maximum(db, 0), axis=-1) + np.minimum(db.max(-1), 0) - r


MATS = [rm.Mat(CASE, gloss=0.28, spec=0.35),                                 # 0 case
        rm.Mat(lambda p: SCREEN(frame(p)), gloss=0.9, spec=0.55, emit=0.9),  # 1 the tube's glass
        rm.Mat((52, 53, 55), gloss=0.25, spec=0.3),                          # 2 the dark border in the bezel
        rm.Mat((184, 180, 168), gloss=0.35, spec=0.4),                       # 3 buttons, a shade darker
        rm.Mat((90, 230, 110), gloss=0.5, spec=0.4, emit=1.0)]               # 4 power light


def scene(p):
    q = frame(p)
    # the bezel: a squarish slab, its edges softly rounded
    bez = rm.box(q - [0, 0, FZ - 0.28], (1.62, 1.5, 0.28), 0.16)
    # the back: the tube's housing, narrowing hard toward the neck
    back = tapered_hole(q, np.array([0, 0.12, 0]), (1.5, 1.3), (0.75, 0.68), FZ - 0.4, -2.4, 0.22)
    case = rm.smin(bez, back, 0.08)
    # vent slots across the back's top
    sx = ((q[..., 0] + 4) % 0.16) - 0.08
    vents = np.maximum(np.abs(sx) - 0.025, np.abs(q[..., 2] + 1.3) - 0.55)
    vents = np.maximum(vents, np.abs(q[..., 0]) - 0.9)
    top = 0.12 + 1.3 + (0.68 - 1.3) * np.clip((FZ - 0.4 - q[..., 2]) / (FZ - 0.4 + 2.4), 0, 1)
    vents = np.maximum(vents, np.abs(q[..., 1] - top) - 0.04)
    case = np.maximum(case, -vents)
    # the screen's opening, its walls bevelled in, a dark border, the glass
    hole = tapered_hole(q, np.array([SX, SY, 0]), (SW + 0.12, SH + 0.12), (SW + 0.02, SH + 0.02),
                        FZ + 0.05, FZ - 0.16, 0.18)
    case = np.maximum(case, -hole)
    border = rm.box(q - [SX, SY, FZ - 0.18], (SW + 0.05, SH + 0.05, 0.02), 0.17)
    bulge = 0.07 * (1 - (((q[..., 0] - SX) / SW) ** 2 + ((q[..., 1] - SY) / SH) ** 2) * 0.5)
    tube = rm.box(q - [SX, SY, FZ - 0.2], (SW, SH, 0.03), 0.16) - bulge
    # the chin: four small buttons and the power button, its light above it
    by = -1.18
    btn = np.full(q.shape[:-1], 1e9)
    for n in range(4):
        btn = np.minimum(btn, rm.box(q - [0.35 + n * 0.17, by, FZ], (0.055, 0.03, 0.02), 0.015))
    btn = np.minimum(btn, rm.cyl(q - [1.24, by, FZ], 0.075, 0.025, 0.012, axis=2))
    led = rm.sphere(q - [1.24, by + 0.16, FZ - 0.005], 0.025)
    # the neck and the round tilt-swivel foot
    neck = rm.box(q - [0, -1.62, -0.4], (0.45, 0.16, 0.45), 0.08)
    foot = rm.cyl(q - [0, -1.82, -0.4], 1.02, 0.07, 0.05, axis=1)
    case = np.minimum(case, np.minimum(neck, foot))
    d = np.stack([case, tube, border, btn, led], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    classic()
    rm.render(scene, FANCY, cam=(0.0, 2.4, 9.6), look=(0, -0.25, -0.4), fov=29)
