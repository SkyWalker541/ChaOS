#!/usr/bin/env python3
"""Draw res/cards/fancy/MUSE.png, Muse's card in Fancy Pants: a pocket music
player, rendered (tools/raymarch.py) from no images. Its own design, not any
one maker's: an anodised body in Muse's green (9CD345, MUSE_ACCENT in
src/main.c), a lit screen showing a song playing, and a round control pad
marked with a list, the two skips and play/pause.

It replaces a photograph whose source TortOS never recorded (CREDITS.md).

    tools/genmusefancy.py
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "MUSE.png")
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")

GREEN = (0x9C, 0xD3, 0x45)
M = rm.rot(4, -22, -9)               # turned and leant, as the consoles are
SCR = (-0.74, 0.74, 0.30, 1.42)      # the screen's face, local x0 x1 y0 y1
PAD_Y, PAD_R = -0.78, 0.66


def screen_picture():
    w, h = 900, 680
    im = Image.new("RGB", (w, h), (14, 15, 19))
    d = ImageDraw.Draw(im)
    # the cover: a dusk sky over hills, cyan to magenta
    cov = Image.new("RGB", (300, 300))
    g = np.zeros((300, 300, 3), np.float64)
    yy = np.linspace(0, 1, 300)[:, None]
    for i, (a, b) in enumerate(zip((58, 200, 255), (214, 70, 140))):
        g[..., i] = a + (b - a) * yy
    cov = Image.fromarray(g.astype(np.uint8))
    cd = ImageDraw.Draw(cov)
    cd.ellipse([170, 70, 250, 150], fill=(255, 226, 150))
    cd.polygon([(0, 230), (90, 170), (170, 220), (240, 160), (300, 200), (300, 300), (0, 300)], fill=(40, 30, 70))
    cd.polygon([(0, 260), (120, 215), (300, 250), (300, 300), (0, 300)], fill=(24, 18, 44))
    m = Image.new("L", (300, 300), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, 299, 299], radius=18, fill=255)
    im.paste(cov, (60, 70), m)
    f1 = ImageFont.truetype(FONT, 56)
    f2 = ImageFont.truetype(FONT, 40)
    f3 = ImageFont.truetype(FONT, 30)
    d.text((400, 120), "Night Drive", font=f1, fill=(240, 240, 244))
    d.text((400, 200), "The Pixels", font=f2, fill=(150, 154, 166))
    d.text((400, 254), "Afterglow", font=f3, fill=(110, 114, 126))
    # the time going by
    d.rounded_rectangle([60, 450, 840, 466], radius=8, fill=(60, 63, 72))
    d.rounded_rectangle([60, 450, 520, 466], radius=8, fill=GREEN)
    d.ellipse([502, 440, 538, 476], fill=(250, 250, 250))
    d.text((60, 500), "2:14", font=f3, fill=(130, 134, 146))
    d.text((840, 500), "3:52", font=f3, fill=(130, 134, 146), anchor="ra")
    # a status line
    d.text((60, 18), "Muse", font=f3, fill=(130, 134, 146))
    d.rounded_rectangle([770, 22, 830, 46], radius=5, outline=(130, 134, 146), width=3)
    d.rectangle([776, 28, 812, 40], fill=GREEN)
    return im.filter(ImageFilter.GaussianBlur(0.6))


def pad_picture():
    n = 1000
    im = Image.new("RGB", (n, n), (226, 228, 230))
    d = ImageDraw.Draw(im)
    ink = (120, 126, 138)
    c = n // 2
    # list, top
    for k in (-1, 0, 1):
        d.rounded_rectangle([c - 70, 150 + k * 34 - 9, c + 70, 150 + k * 34 + 9], radius=9, fill=ink)
    # skips, left and right
    for s in (-1, 1):
        bx = c + s * 345
        for off in (0, 52):
            tip = bx + s * (off + 26)
            d.polygon([(tip, c), (tip - s * 52, c - 42), (tip - s * 52, c + 42)], fill=ink)
        d.rectangle([bx + s * 84 - 8, c - 42, bx + s * 84 + 8, c + 42], fill=ink)
    # play / pause, bottom
    d.polygon([(c - 80, 805), (c - 80, 895), (c - 12, 850)], fill=ink)
    d.rectangle([c + 16, 808, c + 38, 892], fill=ink)
    d.rectangle([c + 58, 808, c + 80, 892], fill=ink)
    return im.filter(ImageFilter.GaussianBlur(1.2))


SCREEN = rm.Tex(screen_picture(), *SCR)
PADTEX = rm.Tex(pad_picture(), -PAD_R, PAD_R, PAD_Y - PAD_R, PAD_Y + PAD_R, base=(226, 228, 230))


def frame(p):
    return rm.local(p, (0, 0.05, 0), M)


MATS = [rm.Mat(GREEN, gloss=0.42, spec=0.7),                                   # 0 body
        rm.Mat(lambda p: SCREEN(frame(p)), gloss=0.95, spec=1.0, emit=0.85),   # 1 screen glass
        rm.Mat(lambda p: PADTEX(frame(p)), gloss=0.5, spec=0.6),               # 2 pad
        rm.Mat(GREEN, gloss=0.6, spec=0.8),                                    # 3 centre button
        rm.Mat((12, 13, 16), gloss=0.6, spec=0.5)]                             # 4 screen bezel


def scene(p):
    q = frame(p)
    body = rm.box(q, (0.98, 1.85, 0.17), 0.15)
    bezel = rm.box(q - [0, 0.86, 0.165], (0.84, 0.62, 0.012), 0.06)
    screen = rm.box(q - [0, 0.86, 0.172], (0.76, 0.57, 0.012), 0.03)
    pad = rm.cyl(q - [0, PAD_Y, 0.17], PAD_R, 0.03, 0.025, axis=2)
    btn = rm.cyl(q - [0, PAD_Y, 0.20], 0.22, 0.03, 0.025, axis=2)
    d = np.stack([body, screen, pad, btn, bezel], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.6, 0.9, 9.5), look=(0, 0.05, 0), fov=26)
