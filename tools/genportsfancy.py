#!/usr/bin/env python3
"""Draw res/cards/fancy/PORTS.png, the Ports card in Fancy Pants: an arcade
stick, rendered (tools/shade3d.py), no images - the same object as the Plain
Jane card's mark (tools/genportscard.py), in the round. A steel case, Ports'
accent; a red ball top; six buttons in arcade colors.

    tools/genportsfancy.py
"""
import os

from PIL import Image, ImageDraw

import shade3d as s3

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "PORTS.png")

STEEL = (160, 174, 192)
STEEL_DARK = (118, 128, 144)
PANEL = (52, 56, 66)
CHROME = (205, 210, 216)
BALL = (214, 52, 44)
BUTTONS = [((1000, 862), (220, 58, 58)), ((1150, 850), (240, 186, 40)),
           ((1300, 862), (60, 170, 230)), ((985, 960), (60, 186, 110)),
           ((1135, 948), (232, 110, 40)), ((1285, 960), (156, 92, 210))]
TOP = (0.0, -0.78)              # the top face, seen from above and in front


def main():
    c = s3.Canvas(1600)
    # the case: its front, then its top rounding over onto it
    c.slab(c.rrect_sdf(800, 1150, 650, 150, 70), STEEL_DARK, tilt=(0, 0.18),
           bevel=50, shin=40, spec=0.5)
    c.slab(c.rrect_sdf(800, 905, 650, 175, 90), STEEL, tilt=TOP, bevel=60,
           shin=50, spec=0.7, env=0.5)
    # the play panel set into the top
    c.slab(c.rrect_sdf(800, 908, 600, 135, 60), PANEL, tilt=TOP, bevel=14,
           shin=30, spec=0.25, amb=0.45)
    # buttons: a dark side, then the cap
    for (x, y), col in BUTTONS:
        side = tuple(int(v * 0.55) for v in col)
        c.slab(c.ellipse_sdf(x, y + 20, 64, 31), side, tilt=TOP, bevel=10,
               shin=30, spec=0.3)
        c.slab(c.ellipse_sdf(x, y, 64, 31), col, tilt=TOP, bevel=22,
               shin=70, spec=0.9)
    # the stick: its dust washer, its shaft, its ball
    c.slab(c.ellipse_sdf(520, 905, 96, 44), (30, 32, 38), tilt=TOP, bevel=18,
           shin=40, spec=0.4)
    c.rod(520, 900, 520, 440, 46, CHROME, shin=90, spec=1.1, env=0.6)
    c.sphere(520, 380, 150, BALL, shin=70, spec=1.0, env=0.4)
    im = c.image()
    # a little contact shadow under the ball's shaft on the panel
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([470, 890, 610, 930], fill=(0, 0, 0, 120))
    s3.overlay(im, sh, blur=8)
    s3.finish(im, OUT)


if __name__ == "__main__":
    main()
