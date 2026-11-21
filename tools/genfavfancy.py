#!/usr/bin/env python3
"""Draw res/cards/fancy/FAVORITES.png, Favorites' card in Fancy Pants: a
heart, rendered (tools/raymarch.py) from no images, in matte red plastic with
the soft sheen of the consoles' own: a 2D heart (Inigo Quilez's curve)
extruded and rounded into a pillow, turned as the set's rendered cards are.
It replaces a glossy lacquered heart that shone like nothing else in the set
(2026-11-21).

    tools/genfavfancy.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "FAVORITES.png")
M = rm.rot(4, -24, 0)
MATS = [rm.Mat((196, 32, 44), gloss=0.42, spec=0.55)]


def heart2d(x, y):
    """iq's heart: tip at (0, 0), lobes up to about y = 1.1"""
    x = np.abs(x)
    a = np.sqrt((x - 0.25) ** 2 + (y - 0.75) ** 2) - np.sqrt(2) / 4
    m = np.maximum(x + y, 0) * 0.5
    b = np.sqrt(np.minimum(x ** 2 + (y - 1) ** 2, (x - m) ** 2 + (y - m) ** 2)) * np.sign(x - y)
    return np.where(x + y > 1, a, b)


def scene(p):
    q = rm.local(p, (0, 0, 0), M)
    s = 2.2
    d2 = heart2d(q[..., 0] / s, (q[..., 1] + 1.15) / s) * s
    r = 0.36                      # the pillow's rounding
    h = 0.06                      # half the flat core's thickness
    dx = d2 + r
    dz = np.abs(q[..., 2]) - h
    d = np.sqrt(np.maximum(dx, 0) ** 2 + np.maximum(dz, 0) ** 2) + np.minimum(np.maximum(dx, dz), 0) - r
    return d, np.zeros(d.shape, int)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.0, 0.5, 9.0), look=(0, 0, 0), fov=26)
