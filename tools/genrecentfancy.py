#!/usr/bin/env python3
"""Draw res/cards/fancy/RECENT.png, Recently Played's card in Fancy Pants: a
twin-bell alarm clock, rendered (tools/raymarch.py) from no images - a case
in Recently Played's cyan (tools/markdef.py's CYAN, as painted metal), chrome
bells, bezel and feet, a printed dial, and hands standing off it that cast
their own shadows. It reads 10:09, as clocks in photographs do.

    tools/genrecentfancy.py
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "RECENT.png")
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")

M = rm.rot(6, -20, 0)
DIAL_R = 0.98
DIAL_Z = 0.30


def dial_picture():
    n = 1200
    im = Image.new("RGB", (n, n), (244, 242, 236))
    d = ImageDraw.Draw(im)
    c = n / 2
    R = n / 2
    ink = (34, 36, 44)
    for i in range(60):
        a = math.radians(i * 6)
        big = i % 5 == 0
        r0, r1 = R * (0.80 if big else 0.87), R * 0.93
        d.line([(c + r0 * math.sin(a), c - r0 * math.cos(a)), (c + r1 * math.sin(a), c - r1 * math.cos(a))],
               fill=ink, width=16 if big else 6)
    f = ImageFont.truetype(FONT, 120)
    for k, s in ((0, "12"), (3, "3"), (6, "6"), (9, "9")):
        a = math.radians(k * 30)
        r = R * 0.64
        d.text((c + r * math.sin(a), c - r * math.cos(a)), s, font=f, fill=ink, anchor="mm")
    # a gentle shading toward the rim, where the bezel shades the dial
    yy, xx = np.mgrid[0:n, 0:n]
    rr = np.hypot(xx - c, yy - c) / R
    a = np.asarray(im, np.float64) * (1 - 0.18 * np.clip((rr - 0.78) / 0.22, 0, 1) ** 2)[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1))


DIAL = rm.Tex(dial_picture(), -DIAL_R, DIAL_R, -DIAL_R, DIAL_R, base=(244, 242, 236))


def frame(p):
    return rm.local(p, (0, -0.15, 0), M)


CASE = (40, 176, 214)
MATS = [rm.Mat(CASE, gloss=0.72, spec=1.0),                                 # 0 case
        rm.Mat(lambda p: DIAL(frame(p)), gloss=0.2, spec=0.2),             # 1 dial
        rm.Mat((214, 218, 224), gloss=0.92, metal=True),                   # 2 chrome
        rm.Mat((28, 30, 36), gloss=0.5, spec=0.5),                         # 3 hands
        rm.Mat((206, 40, 34), gloss=0.6, spec=0.6)]                        # 4 second hand


def hand(q, angle, length, width, z):
    a = math.radians(angle)
    c, s = math.cos(a), math.sin(a)
    # into the hand's own frame: along +y is toward the tip
    x = q[:, 0] * c - q[:, 1] * s
    y = q[:, 0] * s + q[:, 1] * c
    h = np.stack([x, y - length * 0.4, q[:, 2] - z], -1)
    return rm.box(h, (width, length * 0.6, 0.012), 0.008)


def scene(p):
    q = frame(p)
    case = rm.cyl(q, 1.22, 0.36, 0.22, axis=2)
    # the dial set into the front
    dial = rm.cyl(q - [0, 0, DIAL_Z - 0.02], DIAL_R, 0.02, 0.0, axis=2)
    case = np.maximum(case, -rm.cyl(q - [0, 0, 0.36], DIAL_R, 0.08, 0.0, axis=2))
    bezel = rm.torus(q - [0, 0, 0.33], DIAL_R + 0.02, 0.06)
    chrome = [bezel]
    for s in (-1, 1):
        # bells: domes on their stalks, their flat mouths turned down and out
        bc = np.array([s * 0.92, 1.18, -0.05])
        ax = np.array([s * 0.62, 0.78, 0.0]); ax /= np.linalg.norm(ax)
        qb = q - bc
        dome = np.maximum(rm.sphere(qb, 0.58), -(qb @ ax) - 0.05)
        chrome.append(dome)
        chrome.append(rm.sphere(qb - ax * 0.6, 0.07))
        chrome.append(rm.capsule(q, (s * 0.45, 1.05, -0.05), (s * 0.82, 1.05, -0.05), 0.04))
        # feet
        chrome.append(rm.capsule(q, (s * 0.72, -0.9, 0), (s * 1.02, -1.42, 0), 0.07))
        chrome.append(rm.sphere(q - [s * 1.04, -1.47, 0], 0.11))
    # the hammer's post and knob
    chrome.append(rm.capsule(q, (0, 1.15, -0.05), (0, 1.55, -0.05), 0.035))
    chrome.append(rm.sphere(q - [0, 1.6, -0.05], 0.08))
    chrome = np.min(np.stack(chrome, -1), -1)
    hands = np.minimum(hand(q, -60, 0.58, 0.045, DIAL_Z + 0.02),     # hour, near 10
                       hand(q, 54, 0.80, 0.032, DIAL_Z + 0.045))     # minute, at 9
    hands = np.minimum(hands, rm.cyl(q - [0, 0, DIAL_Z + 0.05], 0.07, 0.03, 0.01, axis=2))
    second = hand(q, 170, 0.86, 0.010, DIAL_Z + 0.07)
    d = np.stack([case, dial, chrome, hands, second], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.4, 0.8, 9.0), look=(0, 0.05, 0), fov=28)
