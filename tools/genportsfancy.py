#!/usr/bin/env python3
"""Draw res/cards/fancy/PORTS.png, the Ports card in Fancy Pants: an arcade
stick, rendered (tools/raymarch.py) from no images - the Plain Jane card's
mark (tools/genportscard.py) as an object: a steel case with a black play
panel, a red ball-top stick, six buttons in arcade colours.

    tools/genportsfancy.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")

M = rm.rot(0, -24, 0)          # turned a little, as the consoles are
BTN = [(0.35, -0.22, (200, 40, 40)), (0.80, -0.30, (236, 180, 30)), (1.25, -0.22, (40, 140, 210)),
       (0.35, 0.22, (40, 160, 90)), (0.80, 0.14, (226, 100, 30)), (1.25, 0.22, (130, 80, 190))]
MATS = [rm.Mat((150, 158, 170), gloss=0.45, spec=0.6),            # 0 case
        rm.Mat((22, 23, 27), gloss=0.55, spec=0.5),               # 1 panel
        rm.Mat((205, 208, 214), gloss=0.9, metal=True),           # 2 shaft
        rm.Mat((196, 28, 24), gloss=0.82, spec=1.2),              # 3 ball
        rm.Mat((18, 18, 20), gloss=0.3),                          # 4 washer
        ] + [rm.Mat(c, gloss=0.75, spec=1.0) for _, _, c in BTN]  # 5.. buttons


def scene(p):
    q = rm.local(p, (0, -0.25, 0), M)
    case = rm.box(q - [0, -0.1, 0], (1.75, 0.32, 0.95), 0.16)
    panel = rm.box(q - [0, 0.235, 0.0], (1.62, 0.02, 0.82), 0.015)
    shaft = rm.cyl(q - [-0.85, 0.65, 0], 0.07, 0.45)
    ball = rm.sphere(q - [-0.85, 1.22, 0], 0.36)
    washer = rm.cyl(q - [-0.85, 0.27, 0], 0.26, 0.018, 0.012)
    ds = [case, panel, shaft, ball, washer]
    for x, z, _ in BTN:
        ds.append(rm.cyl(q - [x, 0.30, z], 0.17, 0.07, 0.045))
    d = np.stack(ds, -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.0, 3.4, 8.0), look=(0, 0.15, 0), fov=27)
