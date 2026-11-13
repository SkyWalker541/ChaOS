#!/usr/bin/env python3
"""Draw Muse's two cards: res/cards/classic/MUSE.png and res/cards/fancy/MUSE.png.

Muse is not a console, so like Favorites it has no photograph in the classic
set to trace and no hand-drawn card to copy - which is why it is generated, the
way tools/genfavcard.py generates the Favorites card.

THE ACCENT is the iPod's own anodised green, sampled from the photograph the
fancy card is cut from: the median of its body pixels, 2026-09-18, is #9CD345.
Clear of the three greens already on the shelf - Game Boy's olive 7E9B47,
NGPC's emerald 00AA4F and Game Gear's teal 00B589. The same value is
MUSE_ACCENT in src/main.c; change one, change the other.

CLASSIC is built on tools/cardstyle.py, the drawn set's own stock, rule and
tracked name (ChaOS, 2026-11-13; it was a flat slab to the set's measurements
before). The mark is an iPod reduced to three flat
shapes - body, screen, click wheel - with the screen and the wheel KNOCKED OUT
back to the card stock rather than outlined, because the deck has no strokes
anywhere and a stroked mark thins to nothing at the 62% a side card is drawn at.

FANCY follows res/cards/fancy/SOURCE.md: trimmed to its own alpha, longest side
96% of a 384 frame, centered - and KEPT IN THE 384 CANVAS, because it is taller
than it is wide. src/cards.h says why that is load-bearing: cropped, contain-fit
would grow it through the system name at y=618. Game Boy Color's ink is 369px
tall in the same canvas, and so is this.

    tools/genmusecard.py [photo]     default: cards_backup_photos/muse.png
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "MUSE.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "MUSE.png")
PHOTO = os.path.join(ROOT, "cards_backup_photos", "muse.png")

GREEN = (0x9C, 0xD3, 0x45)



def mark(d, cx, cy, k, fill, cut, scale=0.94):
    """An iPod in three flat shapes: the body, then the screen and the click
    wheel punched through it, then the wheel's center button left standing.
    At 0.94, 320px tall: the height of the tallest console marks."""
    k = k * scale
    bw, bh = 220 * k, 340 * k
    x0, y0 = cx - bw / 2, cy - bh / 2
    d.rounded_rectangle([x0, y0, x0 + bw, y0 + bh], radius=34 * k, fill=fill)

    sw, sh = 172 * k, 124 * k
    sx, sy = cx - sw / 2, y0 + 26 * k
    d.rounded_rectangle([sx, sy, sx + sw, sy + sh], radius=10 * k, fill=cut)

    wr = 70 * k                                # the wheel
    wy = y0 + bh - 26 * k - wr
    d.ellipse([cx - wr, wy - wr, cx + wr, wy + wr], fill=cut)
    br = 26 * k                                # its center button
    d.ellipse([cx - br, wy - br, cx + br, wy + br], fill=fill)


def classic():
    im = cardstyle.card(GREEN, "MUSE", mark)
    im.save(CLASSIC)
    print(CLASSIC, os.path.getsize(CLASSIC), "bytes")


def fancy(photo):
    src = Image.open(photo).convert("RGBA")
    box = src.getchannel("A").getbbox()
    if box:
        src = src.crop(box)
    k = 369.0 / max(src.size)                  # 96% of 384
    sw, sh = round(src.width * k), round(src.height * k)
    src = src.resize((sw, sh), Image.LANCZOS)
    out = Image.new("RGBA", (384, 384), (0, 0, 0, 0))
    out.paste(src, ((384 - sw) // 2, (384 - sh) // 2), src)
    out.save(FANCY)
    print(FANCY, out.size, "ink", out.getchannel("A").getbbox())


def main():
    classic()
    photo = sys.argv[1] if len(sys.argv) > 1 else PHOTO
    if os.path.exists(photo):
        fancy(photo)
    else:
        print("no photograph at", photo, "- the fancy card was not redrawn")


if __name__ == "__main__":
    main()
