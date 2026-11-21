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
    """The monitor in flat shapes, about 300x300 at scale 1, ink centred on
    (cx, cy): the IBM 5151's box (no stand: it sat on the PC), its screen
    knocked out of the upper bezel with a prompt and cursor on it, its two
    knobs knocked out of the chin at the right, and its two feet."""
    k = SS * scale
    bw, bh = 310 * k, 262 * k
    x0, y0 = cx - bw / 2, cy - 150 * k
    d.rounded_rectangle([x0, y0, x0 + bw, y0 + bh], radius=22 * k, fill=fill)
    # the screen, knocked out, a deep chin under it
    sx0, sy0, sx1, sy1 = x0 + 34 * k, y0 + 30 * k, x0 + bw - 34 * k, y0 + bh - 70 * k
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=26 * k, fill=cut)
    # a prompt: a chevron, then a cursor block
    px, py = sx0 + 34 * k, sy0 + 40 * k
    t = 13 * k
    d.polygon([(px, py), (px + 30 * k, py + 26 * k), (px, py + 52 * k),
               (px, py + 52 * k - t * 1.4), (px + 30 * k - t * 1.1, py + 26 * k), (px, py + t * 1.4)], fill=fill)
    d.rectangle([px + 48 * k, py + 38 * k, px + 92 * k, py + 52 * k], fill=fill)
    # brightness and contrast, two knobs at the chin's right
    for dx in (78, 38):
        r = 13 * k
        kx, ky = x0 + bw - dx * k, y0 + bh - 35 * k
        d.ellipse([kx - r, ky - r, kx + r, ky + r], fill=cut)
    # the feet
    for fx in (0.16, 0.84):
        d.rounded_rectangle([x0 + bw * fx - 34 * k, y0 + bh - 4 * k, x0 + bw * fx + 34 * k, y0 + bh + 16 * k],
                            radius=6 * k, fill=fill)


def classic():
    im = cardstyle.card(STEEL, "PORTS",
                        lambda d, cx, cy, k, fill, cut: mark(d, cx, cy, fill, cut, 0.88 * k / SS))
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


# ---- fancy ----------------------------------------------------------------
#
# The IBM 5151, the Personal Computer Display (1981), as photographs of it
# show it: the monochrome monitor that sat on top of the IBM PC, showing the
# Monochrome Display Adapter's text in green P39 phosphor - where a DOS prompt
# in green belongs. A wide box in IBM's pale warm grey, its front edges
# deeply rounded; a broad frame round a recessed panel a shade darker, the
# tube set into its left and, at its right, a strip with the nameplate and
# two black knobs (brightness, contrast), each with its white mark; a field of
# small vent holes across the top behind the front; small dark feet. The
# nameplate is blank: the shelf names no maker. Lit as AMOS lights (raymarch).

M = rm.rot(6, -30, 0)
PEARL = (219, 212, 192)      # the case, IBM's warm putty, a little aged
PANEL = (194, 189, 172)      # the recessed panel, a shade greyer
SW, SH = 1.24, 0.96          # the tube's face, half sizes
SX, SY = -0.30, 0.02         # its middle, left of the case's
FZ = 0.78                    # the case's front


def screen_picture():
    """A real MS-DOS session, not filler: into the games folder, a bare
    listing (DIR /B, MS-DOS 5 on) of three games that are DOS games and
    PortMaster ports alike, and the prompt waiting. In a monospace face drawn
    small and blown up square, as a CRT's text mode is."""
    n = 500                                   # drawn at a half, then x2
    w, h = n, int(n * SH / SW)
    im = Image.new("RGB", (w, h), (12, 20, 14))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(MONO, 23)
    g = (120, 255, 140)
    lines = ["C:\\>CD GAMES", "C:\\GAMES>DIR /B", "DOOM", "KEEN", "QUAKE", "C:\\GAMES>_"]
    for k, t in enumerate(lines):
        d.text((38, 34 + k * 28), t, font=f, fill=g)
    im = im.convert("L").point(lambda v: 255 if v > 90 else 0)   # hard pixels, no grey edges
    im = Image.merge("RGB", [im.point(lambda v: 12 + v * (c - 12) // 255) for c in g])
    im = im.resize((w * 2, h * 2), Image.NEAREST)
    a = np.asarray(im, np.float64)
    a[::4] *= 0.78                            # scanlines
    hh, ww = a.shape[:2]
    yy, xx = np.mgrid[0:hh, 0:ww]
    rr = np.hypot((xx - ww / 2) / (ww / 2), (yy - hh / 2) / (hh / 2))
    a *= (1.08 - 0.35 * rr ** 2)[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


SCREEN = rm.Tex(screen_picture(), SX - SW, SX + SW, SY - SH, SY + SH, base=(14, 22, 16))


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


KX = 1.30                    # the knobs' and nameplate's column
KY = (-0.40, -0.80)          # contrast above brightness


def knob_face(q):
    """black, knurled, with a white mark at one o'clock on its face"""
    out = np.broadcast_to((np.array([22, 22, 24], np.float64) / 255.0) ** 2.2, q.shape).copy()
    for ky in KY:
        dx, dy = q[:, 0] - KX, q[:, 1] - ky
        mark = (np.abs(dx * 0.5 - dy * 0.866) < 0.018) & (dx * 0.866 + dy * 0.5 > 0.02) & \
               (dx * 0.866 + dy * 0.5 < 0.085) & (q[:, 2] > FZ - 0.12)
        out[mark] = (np.array([236, 234, 226]) / 255.0) ** 2.2
    return out


MATS = [rm.Mat(PEARL, gloss=0.26, spec=0.35),                                # 0 case
        rm.Mat(lambda p: SCREEN(frame(p)), gloss=0.9, spec=0.5, emit=0.9),   # 1 the tube's glass
        rm.Mat((44, 46, 44), gloss=0.25, spec=0.3),                          # 2 the dark round the tube
        rm.Mat(lambda p: knob_face(frame(p)), gloss=0.4, spec=0.45),         # 3 knobs
        rm.Mat(PANEL, gloss=0.24, spec=0.3),                                 # 4 the recessed panel
        rm.Mat((186, 186, 182), gloss=0.55, spec=0.6, metal=True),           # 5 nameplate, brushed
        rm.Mat((36, 36, 38), gloss=0.2, spec=0.2)]                           # 6 feet


def scene(p):
    q = frame(p)
    # the case: wide and deep, its front edges rounded
    case = rm.box(q - [0, 0, -0.75], (2.05, 1.42, 1.53), 0.2)
    # the seam where the front bezel meets the shell, round the sides and top
    seam = np.maximum(np.abs(q[..., 2] - (FZ - 0.62)) - 0.012, -rm.box(q - [0, 0, -0.75], (2.03, 1.40, 2.0), 0.18))
    case = np.maximum(case, -seam)
    # the frame's opening, its walls sloping in, and the panel at its back
    hole = tapered_hole(q, np.array([0.0, -0.03, 0]), (1.80, 1.16), (1.70, 1.07), FZ + 0.1, FZ - 0.17, 0.2)
    case = np.maximum(case, -hole)
    panel = rm.box(q - [0.0, -0.03, FZ - 0.2], (1.72, 1.09, 0.04), 0.18)
    # the tube's opening in the panel, sloping down to the glass
    tw = tapered_hole(q, np.array([SX, SY, 0]), (SW + 0.12, SH + 0.12), (SW + 0.02, SH + 0.02),
                      FZ - 0.14, FZ - 0.36, 0.2)
    panel = np.maximum(panel, -tw)
    case = np.maximum(case, -tw)
    # a groove between the tube's side and the strip at the right
    groove = np.maximum(np.abs(q[..., 0] - 1.0) - 0.012, np.abs(q[..., 1] + 0.03) - 1.02)
    panel = np.maximum(panel, -np.maximum(groove, np.abs(q[..., 2] - (FZ - 0.16)) - 0.03))
    step = rm.box(q - [SX, SY, FZ - 0.36], (SW + 0.04, SH + 0.04, 0.02), 0.2)
    bulge = 0.05 * (1 - (((q[..., 0] - SX) / SW) ** 2 + ((q[..., 1] - SY) / SH) ** 2) * 0.5)
    tube = rm.box(q - [SX, SY, FZ - 0.37], (SW, SH, 0.03), 0.2) - bulge
    # the strip: nameplate high, two knobs low, each in a shallow collar
    plate = rm.box(q - [KX, 0.68, FZ - 0.155], (0.13, 0.09, 0.01), 0.008)
    knobs = np.full(q.shape[:-1], 1e9)
    for ky in KY:
        kq = q - [KX, ky, FZ - 0.1]
        ang = np.arctan2(kq[..., 1], kq[..., 0])
        kr = 0.105 + 0.006 * (np.cos(ang * 18) > 0)            # knurled rim
        k = np.maximum(np.hypot(kq[..., 0], kq[..., 1]) - kr, np.abs(kq[..., 2]) - 0.07) - 0.01
        knobs = np.minimum(knobs, k)
    # vents: a field of small holes across the top, just behind the front
    gx = ((q[..., 0] + 4) % 0.09) - 0.045
    gz = ((q[..., 2] + 4) % 0.09) - 0.045
    holes = np.maximum(np.hypot(gx, gz) - 0.02,
                       np.maximum(np.abs(q[..., 0]) - 1.45, np.abs(q[..., 2] - (FZ - 0.55)) - 0.3))
    holes = np.maximum(holes, np.abs(q[..., 1] - 1.42) - 0.035)
    case = np.maximum(case, -holes)
    # small dark feet under the corners
    feet = np.full(q.shape[:-1], 1e9)
    for fx in (-1.65, 1.65):
        for fz in (0.4, -1.95):
            feet = np.minimum(feet, rm.cyl(q - [fx, -1.46, fz], 0.13, 0.05, 0.02, axis=1))
    d = np.stack([case, tube, step, knobs, panel, plate, feet], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    classic()
    rm.render(scene, FANCY, cam=(0.0, 2.6, 9.6), look=(0, -0.15, -0.5), fov=29)
