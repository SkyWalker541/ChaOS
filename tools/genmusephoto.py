#!/usr/bin/env python3
"""Cut res/cards/fancy/MUSE.png, Muse's card in Fancy Pants, from
res/cards/fancy/source/muse-ipod.jpg: a first-generation iPod (2001) on a
white studio backdrop, at the set's 3/4 angle (res/cards/fancy/SOURCE.md).

ITS SCREEN IS BLANK. The photograph shows a menu; the card shows the LCD as
it is with nothing on it (SkyWalker541, 2026-11-22). blank_screen() fits a
smooth surface to the screen's own empty pixels, finds what is darker than
it - the menu, its highlighted row, the icons, the rule - and fills that with
the surface and grain of the photograph's own size, kept clear of the dark
rim the bezel casts on the screen's left and bottom edges, which stays.

The backdrop is pure white and the iPod's edge a grey, so a cut at 10 levels
below white finds the whole device in one piece; GrabCut then settles the
edge in a narrow band around it, sure inside well in from it, sure backdrop
well out. The largest piece is kept, filled, its outline smoothed, pulled in
a pixel so no backdrop rims it, and feathered a pixel. The colours are the
photograph's, cleaned of its speckle and sharpened lightly, as Ports' are.
Then trimmed, longest side 96% of 384, centred, in the 384 canvas, as the
set is.

    tools/genmusephoto.py [out]
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "fancy", "source", "muse-ipod.jpg")
OUT = os.path.join(ROOT, "res", "cards", "fancy", "MUSE.png")


# The LCD's edges in the photograph (1200x896): lines fitted to where the
# white bezel meets the screen, each within 0.6 px of the edge it follows.
def _top(x): return 0.13533 * x + 84.07
def _bot(x): return 0.15636 * x + 307.91
def _lef(y): return 0.019025 * y + 430.50
def _rig(y): return -0.013783 * y + 725.70


def blank_screen(src):
    a = np.asarray(src).astype(float)
    h, w = a.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w].astype(float)

    def inside(il, ir, it, ib):
        return ((xx > _lef(yy) + il) & (xx < _rig(yy) - ir) &
                (yy > _top(xx) + it) & (yy < _bot(xx) - ib))

    # clear of the left and bottom rim, close to the light top and right
    zone = inside(11, 2, 3, 10)
    fitz = inside(11, 4, 6, 10)
    u, v = (xx - 580) / 150.0, (yy - 280) / 130.0

    def basis(m):
        U, V = u[m], v[m]
        return np.stack([np.ones_like(U), U, V, U * U, U * V, V * V,
                         U ** 3, V ** 3, U * U * V, U * V * V], 1)

    # the empty screen's brightness, refitted without what it leaves out
    lum = a.mean(-1)
    sel = fitz & (lum > np.percentile(lum[fitz], 35))
    for _ in range(4):
        cl = np.linalg.lstsq(basis(sel), lum[sel], rcond=None)[0]
        fit = np.zeros_like(lum)
        fit[fitz] = basis(fitz) @ cl
        sd = (lum - fit)[sel].std()
        sel = fitz & (lum - fit > -2.0 * sd)

    surf = np.zeros_like(a)
    bz = basis(zone)
    for c in range(3):
        surf[..., c][zone] = bz @ np.linalg.lstsq(basis(sel), a[..., c][sel], rcond=None)[0]

    # what is on it: darker than the surface past its grain, grown by its edge
    dark = zone & ((surf.mean(-1) - lum) > 3.0 * sd)
    content = ndimage.binary_closing(ndimage.binary_dilation(dark, iterations=3) & zone,
                                     iterations=2) & zone
    rng = np.random.default_rng(1)
    grain = ndimage.gaussian_filter(rng.normal(0, 1, (h, w)), 0.7)
    grain *= sd / grain.std()
    wgt = ndimage.gaussian_filter(content.astype(float), 1.0)
    wgt[~zone] = 0
    out = a * (1 - wgt[..., None]) + (surf + grain[..., None]) * wgt[..., None]
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def main(out_path):
    src = blank_screen(Image.open(SRC).convert("RGB"))
    a = np.asarray(src).astype(int)
    rough = (255 - a.min(-1)) > 10
    lab, n = ndimage.label(rough)
    rough = ndimage.binary_fill_holes(lab == 1 + np.argmax(ndimage.sum(rough, lab, range(1, n + 1))))

    mask = np.full(rough.shape, cv2.GC_PR_BGD, np.uint8)
    mask[rough] = cv2.GC_PR_FGD
    mask[ndimage.binary_erosion(rough, iterations=8)] = cv2.GC_FGD
    mask[~ndimage.binary_dilation(rough, iterations=6)] = cv2.GC_BGD
    img = cv2.cvtColor(np.asarray(src), cv2.COLOR_RGB2BGR)
    cv2.grabCut(img, mask, None, np.zeros((1, 65)), np.zeros((1, 65)), 5, cv2.GC_INIT_WITH_MASK)
    obj = (mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD)
    lab, n = ndimage.label(obj)
    obj = ndimage.binary_fill_holes(lab == 1 + np.argmax(ndimage.sum(obj, lab, range(1, n + 1))))
    smooth = ndimage.binary_erosion(ndimage.gaussian_filter(obj.astype(float), 2.0) > 0.5, iterations=1)
    alpha = ndimage.gaussian_filter(smooth.astype(float), 0.8)

    out = src.filter(ImageFilter.MedianFilter(3)).filter(
        ImageFilter.UnsharpMask(radius=1.2, percent=55, threshold=2))
    out.putalpha(Image.fromarray((alpha * 255).astype(np.uint8)))
    out = out.crop(out.getchannel("A").getbbox())
    k = 369.0 / max(out.size)
    out = out.resize((round(out.width * k), round(out.height * k)), Image.LANCZOS)
    card = Image.new("RGBA", (384, 384), (0, 0, 0, 0))
    card.paste(out, ((384 - out.width) // 2, (384 - out.height) // 2), out)
    card.save(out_path)
    print(out_path, card.size, "ink", card.getchannel("A").getbbox())


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else OUT)
