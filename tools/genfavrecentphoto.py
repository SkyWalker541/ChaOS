#!/usr/bin/env python3
"""Cut Favorites' and Recently Played's cards in Fancy Pants from photographs
supplied by SkyWalker541 (res/cards/fancy/SOURCE.md):

  FAVORITES.png  from source/favorites-heart.jpg, a red heart, matte
  RECENT.png     from source/recent-clock.jpg, a mid-century wall clock

Each on a light studio backdrop. A rough outline first, each by what tells it
from its backdrop: the heart by its red, which its grey shadow and the caption
tag under it don't have; the clock by any step away from the white, which its
cream case takes. Its largest piece, filled, seeds GrabCut in a narrow band;
the result is smoothed, pulled in a pixel and feathered one, the colours
cleaned of speckle and lightly sharpened, and the card trimmed, longest side
96% of 384, centred, as the set is (tools/genportsphoto.py, genmusephoto.py).

    tools/genfavrecentphoto.py [outdir]
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "fancy", "source")
OUT = os.path.join(ROOT, "res", "cards", "fancy")


def rough_heart(a):
    return (a[..., 0] - a[..., 1]) > 30


def rough_clock(a):
    return (255 - a.min(-1)) > 12


CARDS = [
    ("FAVORITES.png", "favorites-heart.jpg", rough_heart),
    ("RECENT.png", "recent-clock.jpg", rough_clock),
]


def largest(m):
    lab, n = ndimage.label(m)
    return ndimage.binary_fill_holes(lab == 1 + np.argmax(ndimage.sum(m, lab, range(1, n + 1))))


def cut(src_path, rough_fn, out_path):
    src = Image.open(src_path).convert("RGB")
    a = np.asarray(src).astype(int)
    rough = largest(rough_fn(a))

    mask = np.full(rough.shape, cv2.GC_PR_BGD, np.uint8)
    mask[rough] = cv2.GC_PR_FGD
    mask[ndimage.binary_erosion(rough, iterations=8)] = cv2.GC_FGD
    mask[~ndimage.binary_dilation(rough, iterations=6)] = cv2.GC_BGD
    img = cv2.cvtColor(np.asarray(src), cv2.COLOR_RGB2BGR)
    cv2.grabCut(img, mask, None, np.zeros((1, 65)), np.zeros((1, 65)), 5, cv2.GC_INIT_WITH_MASK)
    obj = largest((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD))
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
    print(out_path, "ink", card.getchannel("A").getbbox())


def main(outdir):
    for name, src, fn in CARDS:
        cut(os.path.join(SRC, src), fn, os.path.join(outdir, name))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else OUT)
