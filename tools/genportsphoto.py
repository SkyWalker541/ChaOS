#!/usr/bin/env python3
"""Cut res/cards/fancy/PORTS.png, Ports' card in Fancy Pants, from
res/cards/fancy/source/ports-pc.png: a beige early-90s all-in-one, its CRT
over a base unit, on a white studio backdrop (res/cards/fancy/SOURCE.md).

The object is told from the backdrop by colour, not brightness: the beige
plastic is warm (red over blue by more than 22) and the vents are dark, while
the backdrop and its soft floor shadow are near neutral; the screen, enclosed
by the bezel, is filled in. Its outline is smoothed,
pulled in a pixel so no backdrop rims it, and feathered a pixel. The plastic
is cleaned of the picture's speckle (a 3x3 median), a fifth of the yellow
cast comes out of the beige, as a white-balanced studio shot shows it, and
it's sharpened lightly, to sit with Evan Amos's photographs. Then trimmed,
longest side 96% of 384, centred, as the set is.

    tools/genportsphoto.py
"""
import os

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "fancy", "source", "ports-pc.png")
OUT = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")


def main():
    src = Image.open(SRC).convert("RGB")
    a = np.asarray(src).astype(np.float64)
    obj = ((a[..., 0] - a[..., 2]) > 22) | (a.mean(-1) < 100)
    obj = ndimage.binary_opening(obj, iterations=2)
    lab, n = ndimage.label(obj)
    obj = lab == 1 + np.argmax(ndimage.sum(obj, lab, range(1, n + 1)))
    obj = ndimage.binary_fill_holes(ndimage.binary_closing(obj, iterations=3))
    smooth = ndimage.binary_erosion(ndimage.gaussian_filter(obj.astype(float), 2.0) > 0.5, iterations=1)
    alpha = ndimage.gaussian_filter(smooth.astype(float), 0.8)

    c = np.asarray(src.filter(ImageFilter.MedianFilter(3))).astype(np.float64)
    lum = c.mean(-1, keepdims=True)
    c = lum + (c - lum) * 0.8
    c[..., 2] *= 1.03
    c[..., 0] *= 0.99
    c = np.clip((c - 128) * 1.06 + 128 + 4, 0, 255)
    out = Image.fromarray(c.astype(np.uint8)).filter(ImageFilter.UnsharpMask(radius=1.2, percent=55, threshold=2))
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
