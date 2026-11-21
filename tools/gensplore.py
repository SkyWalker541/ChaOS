#!/usr/bin/env python3
"""Draw res/pico8/splore.png, the box art for Splore on the PICO-8 shelf.

Splore is PICO-8's own browser of the carts people share, and the shelf
lists it first like a cart, so it gets a cart's label: 128x128 pixels in
PICO-8's sixteen colours, scaled up square (x4) as the shelf shows a label.
Nothing of Lexaloffle's logo or anyone's cart: SPLORE in pixel letters, a
wall of little carts in many colours (what Splore is), a magnifying glass
over them (looking through them). ChaOS ships it
in ChaOS/pico8/; a Splore.png of the player's own in Roms/PICO-8/.media wins.

    tools/gensplore.py
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "pico8", "splore.png")

P = [(0, 0, 0), (29, 43, 83), (126, 37, 83), (0, 135, 81), (171, 82, 54), (95, 87, 79),
     (194, 195, 199), (255, 241, 232), (255, 0, 77), (255, 163, 0), (255, 236, 39),
     (0, 228, 54), (41, 173, 255), (131, 118, 156), (255, 119, 168), (255, 204, 170)]

BIG = {  # 5x7
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
}


def put(im, x, y, c):
    if 0 <= x < 128 and 0 <= y < 128:
        im.putpixel((x, y), P[c])


def rect(im, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(im, x, y, c)


def text(im, s, x, y, font, c, k=1):
    for ch in s:
        g = font[ch]
        for j, row in enumerate(g):
            for i, b in enumerate(row):
                if b == "1":
                    rect(im, x + i * k, y + j * k, x + i * k + k - 1, y + j * k + k - 1, c)
        x += (len(g[0]) + 1) * k


def cart(im, x, y, scene):
    """a little cart: a 22x18 label in a dark shell, a scene on it"""
    rect(im, x, y, x + 23, y + 21, 5)
    rect(im, x + 1, y + 1, x + 22, y + 18, 0)
    scene(im, x + 1, y + 1)
    rect(im, x + 3, y + 20, x + 20, y + 20, 6)


def hills(im, x, y):
    rect(im, x, y, x + 21, y + 17, 12)
    rect(im, x + 15, y + 3, x + 18, y + 6, 10)
    for i in range(22):
        h = 6 + int(3 * abs(((i % 11) - 5.5)) / 5.5)
        rect(im, x + i, y + 17 - h, x + i, y + 17, 3)


def heart(im, x, y):
    rect(im, x, y, x + 21, y + 17, 2)
    for j, row in enumerate(["0110110", "1111111", "1111111", "0111110", "0011100", "0001000"]):
        for i, b in enumerate(row):
            if b == "1":
                rect(im, x + 4 + i * 2, y + 3 + j * 2, x + 5 + i * 2, y + 4 + j * 2, 8)


def ship(im, x, y):
    rect(im, x, y, x + 21, y + 17, 1)
    for sx, sy in ((3, 3), (17, 2), (9, 6), (19, 12), (2, 14)):
        put(im, x + sx, y + sy, 7)
    # a little rocket: nose, body, window, fins, flame
    rect(im, x + 10, y + 3, x + 11, y + 4, 8)
    rect(im, x + 9, y + 5, x + 12, y + 12, 6)
    put(im, x + 10, y + 7, 12); put(im, x + 11, y + 7, 12)
    rect(im, x + 7, y + 10, x + 8, y + 13, 8)
    rect(im, x + 13, y + 10, x + 14, y + 13, 8)
    rect(im, x + 10, y + 13, x + 11, y + 15, 9)


def tree(im, x, y):
    rect(im, x, y, x + 21, y + 17, 15)
    rect(im, x, y + 14, x + 21, y + 17, 4)
    rect(im, x + 7, y + 3, x + 14, y + 10, 11)
    rect(im, x + 10, y + 11, x + 11, y + 13, 4)


def blocks(im, x, y):
    rect(im, x, y, x + 21, y + 17, 0)
    for i, c in enumerate((8, 9, 10, 11, 12, 14)):
        rect(im, x + 2 + (i % 3) * 7, y + 4 + (i // 3) * 6, x + 6 + (i % 3) * 7, y + 8 + (i // 3) * 6, c)


def sea(im, x, y):
    rect(im, x, y, x + 21, y + 17, 12)
    rect(im, x, y + 10, x + 21, y + 17, 1)
    rect(im, x + 6, y + 8, x + 15, y + 9, 4)
    rect(im, x + 10, y + 2, x + 10, y + 7, 7)
    rect(im, x + 11, y + 3, x + 14, y + 6, 7)


def main():
    im = Image.new("RGB", (128, 128), P[1])
    # stars
    for sx, sy in ((6, 4), (40, 8), (90, 5), (118, 14), (20, 30), (110, 34), (4, 60), (123, 70)):
        put(im, sx, sy, 6 if (sx + sy) % 3 else 13)
    # SPLORE, with a pink drop shadow
    w = 6 * 6 * 2 - 2
    x0 = (128 - w) // 2
    text(im, "SPLORE", x0 + 1, 13, BIG, 14, 2)
    text(im, "SPLORE", x0, 12, BIG, 7, 2)
    # a wall of carts
    scenes = [hills, heart, ship, tree, blocks, sea]
    for i, sc in enumerate(scenes):
        cart(im, 22 + (i % 3) * 29, 46 + (i // 3) * 26, sc)
    # the magnifying glass over them, bottom right
    cx, cy, r = 84, 84, 15
    for y in range(128):
        for x in range(128):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if r - 3 <= d <= r:
                put(im, x, y, 13 if d > r - 1.5 else 6)
            elif d < r - 3 and (x - cx) + (y - cy) < -10 and d > r - 8:
                put(im, x, y, 7)           # the lens catching the light
    for t in range(14):
        rect(im, cx + 10 + t, cy + 10 + t, cx + 13 + t, cy + 13 + t, 4)
    im.resize((512, 512), Image.NEAREST).save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
