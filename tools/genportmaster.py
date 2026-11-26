#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw res/ports/portmaster.png, the box art for the Ports shelf's PortMaster
card: PortMaster's own boat and PM letters (its splash's, MIT, from
PortMaster-GUI: res/ports/art/), on ChaOS's night panel, with its name in
ChaOS's menu font. As tools/gensplore.py draws Splore's.

    python3 tools/genportmaster.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "res", "ports", "art")
OUT = os.path.join(ROOT, "res", "ports", "portmaster.png")
S = 512

card = Image.new("RGBA", (S, S), (0x17, 0x15, 0x1C, 255))
# a sea, under the boat
d = ImageDraw.Draw(card)
d.rectangle([0, 300, S, S], fill=(0x1B, 0x2A, 0x3D, 255))

boat = Image.open(os.path.join(ART, "pm-boat.png")).convert("RGBA")
bw = 440
boat = boat.resize((bw, round(boat.height * bw / boat.width)), Image.LANCZOS)
card.alpha_composite(boat, ((S - bw) // 2, 330 - boat.height))

logo = Image.open(os.path.join(ART, "pm-logo.png")).convert("RGBA")
logo = logo.crop(logo.getbbox())
lw = 250
logo = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
card.alpha_composite(logo, ((S - lw) // 2, 250))

font = ImageFont.truetype(os.path.join(ROOT, "res", "fonts", "menu.ttf"), 52)
text = "PortMaster"
w = d.textlength(text, font=font)
d = ImageDraw.Draw(card)
d.text(((S - w) / 2, 440), text, font=font, fill=(0xF4, 0xEA, 0xD5, 255))

card.convert("RGB").save(OUT, optimize=True)
print("wrote", os.path.relpath(OUT, ROOT))
