#!/usr/bin/env python3
"""Cut res/cards/fancy/RECENT.png, the Recently Played card in Fancy Pants,
from res/cards/fancy/source/recent-watch.jpg: a steel watch with ChaOS on its
dial, made by SkyWalker541 for ChaOS (generated with Gemini).

The source is a JPEG with a checkerboard painted where transparency would be,
so the watch is cut by its shape, not its pixels: the case's circle (centre
704.5, 386.5, radius 340, measured where the case meets the board) and the
crown's rounded rectangle, both drawn at 4x and brought down for a smooth
edge. Then it is turned as the rendered cards are (24 degrees about its
upright, 4 about its level, in perspective), trimmed to its alpha, its
longest side 96% of 384 and centred (res/cards/fancy/SOURCE.md).

    tools/genrecentwatch.py
"""
import os

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "fancy", "source", "recent-watch.jpg")
OUT = os.path.join(ROOT, "res", "cards", "fancy", "RECENT.png")

CASE = (704.5, 386.5, 340.0)
CROWN = (1036, 338, 1083, 430, 8)
SS = 4


def cut(src):
    h, w = src.shape[:2]
    cx, cy, r = CASE
    m = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(m)
    d.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], fill=255)
    x0, y0, x1, y1, rr = CROWN
    d.rounded_rectangle([x0 * SS, y0 * SS, x1 * SS, y1 * SS], radius=rr * SS, fill=255)
    return np.dstack([src, np.array(m.resize((w, h), Image.LANCZOS))])


def turn(img, yaw=-24, pitch=4, f=2200):
    h, w = img.shape[:2]
    c = np.array([w / 2, h / 2])
    y, p = np.radians(yaw), np.radians(pitch)
    ry = np.array([[np.cos(y), 0, np.sin(y)], [0, 1, 0], [-np.sin(y), 0, np.cos(y)]])
    rx = np.array([[1, 0, 0], [0, np.cos(p), -np.sin(p)], [0, np.sin(p), np.cos(p)]])
    rm = rx @ ry
    pts = np.array([[0, 0], [w, 0], [w, h], [0, h]], np.float32)
    out = []
    for x, yv in pts:
        v = rm @ np.array([x - c[0], yv - c[1], 0.0])
        s = f / (f + v[2])
        out.append([c[0] + v[0] * s, c[1] + v[1] * s])
    m = cv2.getPerspectiveTransform(pts, np.array(out, np.float32))
    # premultiplied, so the edge takes no dark fringe from the cleared pixels
    pm = img.astype(np.float32)
    pm[:, :, :3] *= pm[:, :, 3:4] / 255
    t = cv2.warpPerspective(pm, m, (w, h), flags=cv2.INTER_LANCZOS4,
                            borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0, 0))
    a = np.clip(t[:, :, 3], 0, 255)
    t[:, :, :3] = np.where(a[..., None] > 0, t[:, :, :3] * 255 / np.maximum(a[..., None], 1e-3), 0)
    return np.clip(t, 0, 255).astype(np.uint8)


def frame(t):
    ys, xs = np.where(t[:, :, 3] > 8)
    t = t[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    h, w = t.shape[:2]
    k = 369.0 / max(h, w)
    t = cv2.resize(t, (round(w * k), round(h * k)), interpolation=cv2.INTER_AREA)
    out = np.zeros((384, 384, 4), np.uint8)
    oy, ox = (384 - t.shape[0]) // 2, (384 - t.shape[1]) // 2
    out[oy:oy + t.shape[0], ox:ox + t.shape[1]] = t
    return out


if __name__ == "__main__":
    cv2.imwrite(OUT, frame(turn(cut(cv2.imread(SRC)))))
    print(OUT)
