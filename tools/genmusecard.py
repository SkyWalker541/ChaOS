#!/usr/bin/env python3
"""Draw Muse's Plain Jane card, res/cards/classic/MUSE.png.

Since ChaOS 2.7.6 the Fancy Pants card is a photograph of a first-generation
iPod, cut by tools/genmusephoto.py; this file's fancy() is the older path,
kept for the green iPod nano photograph it was cut from, and main() no longer
runs it.

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
before). The mark is the Fancy Pants card's
first-generation iPod drawn flat - its body, its blank screen, the button ring
round the scroll wheel with "menu", back, forward and play/pause in it, and
the centre button - in the photograph's own proportions. The screen and the
ring are KNOCKED OUT back to the card stock rather than outlined, because the
deck has no strokes anywhere and a stroked mark thins to nothing at the 62% a
side card is drawn at. (Three shapes, body, screen and wheel, until 2.7.5.)

FANCY follows res/cards/fancy/SOURCE.md: trimmed to its own alpha, longest side
96% of a 384 frame, centered - and KEPT IN THE 384 CANVAS, because it is taller
than it is wide. src/cards.h says why that is load-bearing: cropped, contain-fit
would grow it through the system name at y=618. Game Boy Color's ink is 369px
tall in the same canvas, and so is this.

    tools/genmusecard.py [photo]     default: cards_backup_photos/muse.png
"""
import os
import sys

from PIL import Image, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASSIC = os.path.join(ROOT, "res", "cards", "classic", "MUSE.png")
FANCY = os.path.join(ROOT, "res", "cards", "fancy", "MUSE.png")
PHOTO = os.path.join(ROOT, "cards_backup_photos", "muse.png")

GREEN = (0x9C, 0xD3, 0x45)



def mark(d, cx, cy, k, fill, cut, scale=0.94):
    """A first-generation iPod, flat: the body; the screen knocked out and
    blank, as the Fancy Pants card's is; the button
    ring knocked out round the scroll wheel, its four buttons' marks standing
    in it; the centre button parted from the wheel by a knocked-out gap."""
    k = k * scale
    bw, bh = 208 * k, 340 * k
    x0, y0 = cx - bw / 2, cy - bh / 2
    d.rounded_rectangle([x0, y0, x0 + bw, y0 + bh], radius=28 * k, fill=fill)

    # the screen, landscape, and its menu
    sw, sh = 168 * k, 128 * k
    sx, sy = cx - sw / 2, y0 + 22 * k
    d.rounded_rectangle([sx, sy, sx + sw, sy + sh], radius=8 * k, fill=cut)

    # the button ring, the wheel, the centre button, in the photograph's
    # proportions: measured across the ring's outer edge (339 px there), the
    # wheel is 77% of it, the centre button 27%, "menu" 15% wide, back and
    # forward 6% each, play/pause 8%
    R = 80 * k
    D = 2 * R                                  # the ring's outer width
    r1, r2 = 0.775 * R, 0.274 * R
    wy = y0 + bh - 22 * k - R
    d.ellipse([cx - R, wy - R, cx + R, wy + R], fill=cut)
    d.ellipse([cx - r1, wy - r1, cx + r1, wy + r1], fill=fill)
    g = r2 + 0.06 * R
    d.ellipse([cx - g, wy - g, cx + g, wy + g], fill=cut)
    d.ellipse([cx - r2, wy - r2, cx + r2, wy + r2], fill=fill)

    rm = (R + r1) / 2                          # the labels sit mid-ring
    # "menu", sized to its measured width
    size = 40
    f = ImageFont.truetype(cardstyle.FONT, size)
    l, t0, r_, b0 = d.textbbox((0, 0), "menu", font=f)
    f = ImageFont.truetype(cardstyle.FONT, max(1, round(size * 0.150 * D / (r_ - l))))
    # the photograph's label is a heavier face than the card's; a stroke of
    # its own colour gives it that weight
    d.text((cx, wy - rm), "menu", font=f, fill=fill, anchor="mm",
           stroke_width=max(1, round(0.0025 * D)), stroke_fill=fill)

    # back and forward: a bar and two triangles, 6.3% wide, 3.2% tall
    bw_, bh_ = 0.063 * D, 0.032 * D
    bar = 0.012 * D
    tri = (bw_ - bar) / 2
    for sgn in (-1, 1):                        # -1 back on the left, 1 forward on the right
        bx0 = cx + sgn * rm - bw_ / 2
        hy = bh_ / 2
        if sgn < 0:                            # |<<
            d.rectangle([bx0, wy - hy, bx0 + bar, wy + hy], fill=fill)
            for i in range(2):
                tx = bx0 + bar + i * tri
                d.polygon([(tx + tri, wy - hy), (tx + tri, wy + hy), (tx, wy)], fill=fill)
        else:                                  # >>|
            for i in range(2):
                tx = bx0 + i * tri
                d.polygon([(tx, wy - hy), (tx, wy + hy), (tx + tri, wy)], fill=fill)
            d.rectangle([bx0 + 2 * tri, wy - hy, bx0 + bw_, wy + hy], fill=fill)

    # play/pause: a triangle and two bars, 8.4% wide, 3.7% tall
    pw, ph = 0.084 * D, 0.037 * D
    px0, py = cx - pw / 2, wy + rm
    hy = ph / 2
    tw, gap, bwid = 0.42 * pw, 0.12 * pw, 0.15 * pw
    d.polygon([(px0, py - hy), (px0, py + hy), (px0 + tw, py)], fill=fill)
    b1 = px0 + tw + gap
    d.rectangle([b1, py - hy, b1 + bwid, py + hy], fill=fill)
    b2 = b1 + bwid + gap
    d.rectangle([b2, py - hy, b2 + bwid, py + hy], fill=fill)


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


if __name__ == "__main__":
    main()
