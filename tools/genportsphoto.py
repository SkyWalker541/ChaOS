#!/usr/bin/env python3
"""Cut res/cards/fancy/PORTS.png, Ports' card in Fancy Pants, from
res/cards/fancy/source/ports-pc.jpg: a warm grey early-90s all-in-one, its
CRT over a base unit, on a white studio backdrop (res/cards/fancy/SOURCE.md).

Its plastic is nearly the backdrop's own white, so neither colour nor
brightness tells them apart; OpenCV's GrabCut does, started from the
machine's outline traced by hand (OUTLINE): sure inside it, well in from it;
sure backdrop, well out from it; and left to decide in the band between,
which is where the edges and the soft floor shadow are. The largest piece is
kept, filled, its outline smoothed, pulled in a pixel so no backdrop rims it,
and feathered a pixel. The plastic is cleaned of the picture's speckle (a 3x3
median) and sharpened lightly; its colours are kept as they are, the real
machine's. Then trimmed, longest side 96% of 384, centred, as the set is.

    tools/genportsphoto.py
"""
import os

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "fancy", "source", "ports-pc.jpg")
OUT = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")

# the machine's outline in the source picture (1307x800), traced by hand from
# contrast-stretched close-ups: the bezel, the housing stepping down behind
# it, the base unit's rounded corner and foot, its underside's line
OUTLINE = [(300, 70), (860, 45), (905, 48), (920, 60), (920, 136), (1069, 177), (1062, 640),
           (1052, 675), (1045, 695), (1025, 702), (780, 765), (560, 752), (350, 740), (335, 730),
           (335, 588), (285, 580), (288, 90)]


def main():
    src = Image.open(SRC).convert("RGB")
    w, h = src.size
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).polygon(OUTLINE, fill=1)
    m = np.asarray(m) > 0
    mask = np.full((h, w), cv2.GC_PR_BGD, np.uint8)
    mask[m] = cv2.GC_PR_FGD
    mask[ndimage.binary_erosion(m, iterations=12)] = cv2.GC_FGD
    mask[~ndimage.binary_dilation(m, iterations=9)] = cv2.GC_BGD
    img = cv2.cvtColor(np.asarray(src), cv2.COLOR_RGB2BGR)
    cv2.grabCut(img, mask, None, np.zeros((1, 65)), np.zeros((1, 65)), 6, cv2.GC_INIT_WITH_MASK)
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
    card.save(OUT)
    print(OUT, card.size, "ink", card.getchannel("A").getbbox())


if __name__ == "__main__":
    main()
