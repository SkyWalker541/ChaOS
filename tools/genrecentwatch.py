#!/usr/bin/env python3
"""Draw res/cards/fancy/RECENT.png, Recently Played's card in Fancy Pants: a
steel watch with ChaOS on its dial, rendered (tools/raymarch.py) so it turns
as the set's other rendered cards do, with its case's side showing.

The face is SkyWalker541's: res/cards/fancy/source/recent-watch.jpg, a watch
with ChaOS on its dial made for ChaOS (generated with Gemini). Its dial and
the polished ring around it - everything inside 298 px of the centre at
(704.5, 386.5) - are laid on the front of a modelled case as a picture. The
case, its rounded edge and the crown are modelled in brushed steel of the
picture's own grey, so the side shows when the watch is turned.

    tools/genrecentwatch.py
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raymarch as rm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "fancy", "source", "recent-watch.jpg")
OUT = os.path.join(ROOT, "res", "cards", "fancy", "RECENT.png")

CX, CY, FACE_PX, CASE_PX = 704.5, 386.5, 298, 340
M = rm.rot(4, -26, 0)                 # turned as the set's rendered cards are
CASE_R = 1.30                          # the case, in scene units
FACE_R = CASE_R * FACE_PX / CASE_PX    # the picture's dial and inner ring
DEPTH = 0.26                           # half the case's thickness
FRONT = DEPTH - 0.02                   # where the face sits, just inside the rim


def face_picture():
    im = Image.open(SRC).convert("RGB")
    return im.crop((round(CX - FACE_PX), round(CY - FACE_PX),
                    round(CX + FACE_PX), round(CY + FACE_PX))).resize((1200, 1200), Image.LANCZOS)


FACE = rm.Tex(face_picture(), -FACE_R, FACE_R, -FACE_R, FACE_R, base=(150, 149, 145))


def frame(p):
    return rm.local(p, (0, 0, 0), M)


# The picture's polished silver: a bright metal, glossy enough to carry the
# studio's softbox and dark floor as the picture's case carries its bands, and
# lifted a little (emit) so its side reads silver, not the floor's grey
STEEL = (236, 237, 239)
MATS = [rm.Mat(STEEL, gloss=0.8, metal=True, emit=0.18),                     # 0 case
        rm.Mat(lambda p: FACE(frame(p)), gloss=0.55, spec=0.3, emit=0.95),  # 1 face, under glass
        rm.Mat((240, 241, 243), gloss=0.9, metal=True, emit=0.25)]           # 2 polished rim and crown


def scene(p):
    q = frame(p)
    # the case: a short rounded drum, its front cut away for the face
    case = rm.cyl(q, CASE_R, DEPTH, 0.12, axis=2)
    case = np.maximum(case, -rm.cyl(q - [0, 0, DEPTH], FACE_R, 0.05, 0.0, axis=2))
    face = rm.cyl(q - [0, 0, FRONT - 0.02], FACE_R + 0.004, 0.02, 0.0, axis=2)
    # a polished lip round the face, and the crown at three o'clock
    rim = rm.torus(q - [0, 0, DEPTH - 0.01], FACE_R + 0.035, 0.035)
    crown = rm.cyl(q - [CASE_R + 0.10, 0, 0], 0.15, 0.08, 0.03, axis=0)
    # its grip: shallow grooves round the crown
    ang = np.arctan2(q[..., 2], q[..., 1])
    crown = crown + 0.008 * (np.sin(ang * 24) > 0)
    stem = rm.cyl(q - [CASE_R + 0.01, 0, 0], 0.06, 0.06, 0.0, axis=0)
    shiny = np.minimum(np.minimum(rim, crown), stem)
    d = np.stack([case, face, shiny], -1)
    return d.min(-1), d.argmin(-1)


scene.mats = MATS

if __name__ == "__main__":
    rm.render(scene, OUT, cam=(0.3, 0.5, 9.0), look=(0, 0, 0), fov=26)
