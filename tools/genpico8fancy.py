#!/usr/bin/env python3
"""Draw res/cards/fancy/PICO8.png, the PICO-8 card in Fancy Pants: a game
cartridge, rendered (tools/raymarch.py) from no images - the Plain Jane
card's mark (tools/genpico8card.py) as an object: a slab of pink plastic with
its top corner cut, a dark label set into it with a pixel heart standing off
it, and grip ridges under the label. Nothing of PICO-8's own logo.

    tools/genpico8fancy.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "PICO8.png")

M = rm.rot(4, -24, 0)          # turned a little, as the consoles are
HEART = [
    ".##.##.",
    "#######",
    "#######",
    ".#####.",
    "..###..",
    "...#...",
]
PX = 0.17                      # one pixel of the heart
MATS = [rm.Mat((255, 119, 168), gloss=0.55, spec=0.8),     # 0 the shell, PICO-8's pink
        rm.Mat((26, 28, 40), gloss=0.35, spec=0.4),        # 1 the label
        rm.Mat((255, 119, 168), gloss=0.7, spec=1.0),      # 2 the heart
        ]
CELLS = [(c, r) for r, line in enumerate(HEART) for c, ch in enumerate(line) if ch == "#"]


def scene(p):
    q = rm.local(p, (0, 0, 0), M)
    shell = rm.box(q, (1.25, 1.55, 0.2), 0.07)
    # the top right corner cut off at 45 degrees
    cut = (q[..., 0] + q[..., 1] - 2.25) / np.sqrt(2)
    shell = np.maximum(shell, cut)
    # the label: a dark panel set into the front, a little proud of it
    lab = rm.box(q - [0, 0.12, 0.2], (0.95, 0.95, 0.025), 0.02)
    # the grip ridges under it
    ridges = np.full(q.shape[:-1], 1e9)
    for i in range(4):
        x = (i - 1.5) * 0.34
        ridges = np.minimum(ridges, rm.box(q - [x, -1.28, 0.2], (0.06, 0.17, 0.03), 0.025))
    shell = np.minimum(shell, ridges)
    # the heart, each pixel a little block standing off the label
    heart = np.full(q.shape[:-1], 1e9)
    w, h = len(HEART[0]), len(HEART)
    for c, r in CELLS:
        x = (c - (w - 1) / 2) * PX
        y = 0.12 - (r - (h - 1) / 2) * PX
        heart = np.minimum(heart, rm.box(q - [x, y, 0.245], (PX / 2 - 0.01, PX / 2 - 0.01, 0.025), 0.01))
    d = np.stack([shell, lab, heart], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.0, 0.5, 9.0), look=(0, 0, 0), fov=26)
