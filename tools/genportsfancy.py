#!/usr/bin/env python3
"""Draw res/cards/fancy/PORTS.png, the Ports card in Fancy Pants: an arcade
stick, rendered (tools/raymarch.py) from no images, as a modern fight stick
is built rather than as a toy: a slim brushed aluminium case with a bevelled edge,
a glossy black acrylic top held by six screws, a red ball-top lever on a
chrome shaft through a dust washer, eight 30 mm-style buttons in two curved
rows of four, red behind and white in front - each a black rim round a
dished plunger - and two small buttons on the back edge. No maker's marks.

    tools/genportsfancy.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")

M = rm.rot(0, -24, 0)              # turned a little, as the consoles are
W, D = 2.05, 1.05                  # the case's half width and half depth
TOP = 0.0                          # the acrylic's top face
STICK = (-1.12, 0.06)              # the lever, in x and z
# two rows of four, the upper row's ends lower: the curve of an arcade panel
ROWS = [[(0.02, -0.12), (0.42, -0.30), (0.84, -0.30), (1.26, -0.24)],
        [(0.10, 0.30), (0.50, 0.12), (0.92, 0.12), (1.34, 0.18)]]
BTN = [b for row in ROWS for b in row]
SCREWS = [(-1.88, -0.86), (0.0, -0.86), (1.88, -0.86), (-1.88, 0.86), (0.0, 0.86), (1.88, 0.86)]
SMALL = [(1.30, -0.80), (1.62, -0.80)]

RED = (178, 22, 28)
# The case is anodised aluminium: satin, so lit as a fine pale finish with a
# sharp highlight on its bevel, where a mirror metal would show the dark floor
MATS = [rm.Mat((150, 153, 160), gloss=0.55, spec=1.1),         # 0 case, anodised aluminium
        rm.Mat((10, 10, 12), gloss=0.94, spec=1.3),            # 1 acrylic top
        rm.Mat((214, 216, 220), gloss=0.93, metal=True),       # 2 chrome: shaft, screws
        rm.Mat(RED, gloss=0.86, spec=1.3),                     # 3 ball
        rm.Mat((14, 14, 16), gloss=0.45, spec=0.5),            # 4 washer, rims
        rm.Mat(RED, gloss=0.72, spec=1.0),                     # 5 red plungers
        rm.Mat((232, 230, 224), gloss=0.72, spec=1.0),         # 6 white plungers
        ]


def scene(p):
    q = rm.local(p, (0, -0.1, 0), M)
    # the case: slim, its top edge bevelled
    case = rm.box(q - [0, -0.24, 0], (W, 0.22, D), 0.07)
    bevel = (np.abs(q[..., 0]) + q[..., 1] - (W + 0.02)) / np.sqrt(2)
    case = np.maximum(case, bevel)
    bevel = (np.abs(q[..., 2]) + q[..., 1] - (D + 0.02)) / np.sqrt(2)
    case = np.maximum(case, bevel)
    # the acrylic, a sheet on top, a little in from the edge
    acr = rm.box(q - [0, TOP - 0.02, 0], (W - 0.12, 0.02, D - 0.12), 0.012)

    chrome = rm.cyl(q - [STICK[0], 0.36, STICK[1]], 0.042, 0.36, 0.0, axis=1)
    for x, z in SCREWS:
        chrome = np.minimum(chrome, rm.cyl(q - [x, TOP + 0.004, z], 0.045, 0.008, 0.006, axis=1))
    ball = rm.sphere(q - [STICK[0], 1.0, STICK[1]], 0.29)
    washer = rm.cyl(q - [STICK[0], TOP + 0.01, STICK[1]], 0.27, 0.008, 0.006, axis=1)
    # the lever's black collar just above the washer
    washer = np.minimum(washer, rm.cyl(q - [STICK[0], 0.08, STICK[1]], 0.07, 0.06, 0.02, axis=1))

    rims = washer
    red = np.full(q.shape[:-1], 1e9)
    blk = np.full(q.shape[:-1], 1e9)
    for i, (x, z) in enumerate(BTN + SMALL):
        small = i >= len(BTN)
        R = 0.09 if small else 0.165
        c = q - [x, TOP, z]
        # the rim: a low ring standing on the acrylic
        rim = np.maximum(rm.cyl(c - [0, 0.02, 0], R, 0.02, 0.012, axis=1),
                         -rm.cyl(c - [0, 0.03, 0], R * 0.80, 0.04, 0.0, axis=1))
        rims = np.minimum(rims, rim)
        # the plunger, its top dished
        pl = rm.cyl(c - [0, 0.03, 0], R * 0.78, 0.03, 0.01, axis=1)
        pl = np.maximum(pl, -rm.sphere(c - [0, 0.06 + R * 2.4 - 0.012, 0], R * 2.4))
        if small:
            rims = np.minimum(rims, pl)
        elif i >= 4:
            blk = np.minimum(blk, pl)
        else:
            red = np.minimum(red, pl)
    d = np.stack([case, acr, chrome, ball, rims, red, blk], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.0, 4.2, 7.6), look=(0.0, 0.05, 0), fov=28)
