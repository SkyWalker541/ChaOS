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


# Polished stainless steel: nearly a mirror, so what it shows is what it
# reflects. The set's studio (dim ceiling, dark floor) makes such a mirror
# grey, so this card has its own, as watches are photographed: a bright
# white sweep overhead and behind, the key softbox, and black flags standing
# beside the watch and a dark line on the horizon, which polished steel
# gives back as the crisp dark bands that tell it from grey paint.
STEEL = (238, 240, 243)


def watch_studio(d):
    x, y, z = d[..., 0], d[..., 1], d[..., 2]
    env = np.where(y > 0, 0.95 + 0.6 * y, 0.55 + 0.35 * (1 + y))
    # the horizon, a dark line all the way round
    env = env * (1 - 0.85 * np.exp(-((y + 0.02) / 0.07) ** 2))
    # black flags: tall, either side, a little behind
    env = np.where((np.abs(x - 0.62) < 0.13) & (y > -0.35) & (y < 0.75), env * 0.06, env)
    env = np.where((np.abs(x + 0.78) < 0.10) & (y > -0.25) & (y < 0.6), env * 0.08, env)
    # the key softbox and a strip light, very bright
    sb = np.clip((np.sum(d * rm.KEY, -1) - 0.86) / 0.05, 0, 1) * 4.0
    strip = np.clip((np.sum(d * rm.FILL, -1) - 0.93) / 0.03, 0, 1) * 2.2
    return (env + sb + strip)[..., None] * np.array([0.99, 1.0, 1.03])


rm.studio = watch_studio


MATS = [rm.Mat(STEEL, gloss=0.96, metal=True),                               # 0 case
        rm.Mat(lambda p: FACE(frame(p)), gloss=0.55, spec=0.3, emit=0.95),  # 1 face, under glass
        rm.Mat((242, 243, 245), gloss=0.98, metal=True)]                     # 2 polished rim and crown


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
