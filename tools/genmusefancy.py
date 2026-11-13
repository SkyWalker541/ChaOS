#!/usr/bin/env python3
"""Draw res/cards/fancy/MUSE.png, Muse's card in Fancy Pants: a pocket music
player, rendered (tools/shade3d.py), no images. Its own design rather than any
one maker's: an anodised body in Muse's green (9CD345, src/main.c's
MUSE_ACCENT), a screen showing a song playing, and a round control pad with
skip, play and a list.

It replaces a photograph whose source TortOS never recorded (CREDITS.md).

    tools/genmusefancy.py
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw

import shade3d as s3

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "MUSE.png")

GREEN = (0x9C, 0xD3, 0x45)
GREEN_SIDE = (92, 128, 40)
PAD = (238, 240, 242)
SCREEN = (20, 22, 28)
CX, CY = 800, 800


def main():
    c = s3.Canvas(1600)
    # the body: its edge first, a little right and down, then the face
    c.slab(c.rrect_sdf(CX + 34, CY + 26, 300, 560, 80), GREEN_SIDE, bevel=40,
           shin=40, spec=0.4)
    body = c.slab(c.rrect_sdf(CX, CY, 300, 560, 80), GREEN, bevel=46,
                  shin=55, spec=0.8, env=0.5)
    # the screen, a glass panel set into it
    scr = c.slab(c.rrect_sdf(CX, CY - 270, 240, 200, 22), SCREEN, bevel=8,
                 shin=120, spec=0.9, amb=0.9, env=0.1)
    # the pad, domed, and its centre button
    pad = c.slab(c.ellipse_sdf(CX, CY + 250, 200, 200), PAD, bevel=110,
                 shin=40, spec=0.5, env=0.4)
    c.slab(c.ellipse_sdf(CX, CY + 250, 70, 70), GREEN, bevel=40, shin=70,
           spec=0.9)
    im = c.image()
    d = ImageDraw.Draw(im)

    # what the screen shows: a cover, a title, an artist, the time going by
    x0, y0 = CX - 210, CY - 440
    cov = Image.new("RGBA", (150, 150))
    g = np.zeros((150, 150, 4), np.uint8)
    t = np.linspace(0, 1, 150)
    for i, (a, b) in enumerate(zip((58, 215, 255), (194, 71, 143))):
        g[..., i] = (a + (b - a) * (t[None, :] * 0.6 + t[:, None] * 0.4)).astype(np.uint8)
    g[..., 3] = 255
    cov = Image.fromarray(g)
    m = Image.new("L", (150, 150), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, 149, 149], radius=10, fill=255)
    im.paste(cov, (x0, y0 + 20), m)
    d.rounded_rectangle([x0 + 175, y0 + 45, x0 + 400, y0 + 65], radius=10, fill=(235, 236, 240, 255))
    d.rounded_rectangle([x0 + 175, y0 + 85, x0 + 340, y0 + 101], radius=8, fill=(130, 134, 146, 255))
    d.rounded_rectangle([x0 + 175, y0 + 120, x0 + 300, y0 + 134], radius=7, fill=(90, 94, 104, 255))
    d.rounded_rectangle([x0, y0 + 220, x0 + 420, y0 + 232], radius=6, fill=(70, 74, 84, 255))
    d.rounded_rectangle([x0, y0 + 220, x0 + 250, y0 + 232], radius=6, fill=GREEN + (255,))
    d.ellipse([x0 + 238, y0 + 214, x0 + 262, y0 + 238], fill=(245, 245, 245, 255))
    d.rounded_rectangle([x0, y0 + 270, x0 + 60, y0 + 282], radius=6, fill=(90, 94, 104, 255))
    d.rounded_rectangle([x0 + 360, y0 + 270, x0 + 420, y0 + 282], radius=6, fill=(90, 94, 104, 255))

    # the pad's marks: list at the top, skips at the sides, play/pause below
    ink = (120, 126, 138, 255)
    px, py = CX, CY + 250
    for k in (-1, 0, 1):
        d.rounded_rectangle([px - 34, py - 150 + k * 16 - 4, px + 34, py - 150 + k * 16 + 4], radius=4, fill=ink)
    for s in (-1, 1):
        bx = px + s * 140
        for off in (0, 26):
            tri = [(bx + s * (off - 6), py), (bx + s * (off - 30), py - 20), (bx + s * (off - 30), py + 20)]
            d.polygon(tri, fill=ink)
        d.rectangle([bx + s * 18 - 4, py - 20, bx + s * 18 + 4, py + 20], fill=ink)
    d.polygon([(px - 40, py + 128), (px - 40, py + 172), (px - 6, py + 150)], fill=ink)
    d.rectangle([px + 10, py + 130, px + 20, py + 170], fill=ink)
    d.rectangle([px + 30, py + 130, px + 40, py + 170], fill=ink)

    # glass on the screen: a soft diagonal sheen
    sheen = Image.new("RGBA", im.size, (255, 255, 255, 0))
    sd = ImageDraw.Draw(sheen)
    sd.polygon([(CX - 240, CY - 470), (CX - 20, CY - 470), (CX - 260, CY - 110), (CX - 240, CY - 110)],
               fill=(255, 255, 255, 34))
    s3.overlay(im, sheen, mask_arr=scr, blur=6)
    s3.finish(im, OUT)


if __name__ == "__main__":
    main()
