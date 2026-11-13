"""The Plain Jane set's card, for the cards that are generated rather than
drawn: Favorites, Muse, Recently Played and Ports.

The console cards (res/cards/classic) are drawn by hand, and four things set
them apart from a flat slab - all measured off them, 2026-11-13:

  - the stock is a gradient, (35,38,50) at the top to (25,28,38) at the foot,
    not one flat (28,31,42);
  - a hairline of light runs along the top edge, three pixels in;
  - the name is tracked out, about 6px between letters at 54px;
  - the mark is centred a little higher, at y=285, and no taller than about
    320px.

So the base is not drawn here: it is the NES card itself with its mark and
name painted out (the stock under them is the same in every column, so each
row is filled from the column beside the mark) and its rule recolored, edges,
hairline and gradient kept exactly. Generated and drawn cards then differ only
where they are meant to: the mark, the color and the name.
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")
BASE = os.path.join(ROOT, "res", "cards", "classic", "NES.png")

W, H = 640, 820
LABEL = (239, 239, 244)
MARK_CY = 285               # where the console marks sit, by their centres
TRACK = 6.0                 # px between letters, at 54px
SS = 4

NES_RULE = (196, 68, 58)    # the NES card's rule, painted over
RULE_ROWS = range(585, 601)


def base(accent):
    """The NES card, emptied, its rule in `accent`."""
    im = Image.open(BASE).convert("RGBA")
    px = im.load()
    # the stock beside the mark and under the name, row by row
    for y in list(range(40, 580)) + list(range(605, 800)):
        c = px[40, y]
        for x in range(40, W - 40):
            px[x, y] = c
    # the rule, each row's blend of the old color kept for the new
    for y in RULE_ROWS:
        stock = px[20, y - 6] if y < 593 else px[20, y + 6]
        for x in range(W):
            p = px[x, y]
            if p[3] == 0:
                continue
            t = (p[0] - stock[0]) / float(NES_RULE[0] - stock[0])
            t = max(0.0, min(1.0, t))
            px[x, y] = tuple(round(stock[i] + (accent[i] - stock[i]) * t)
                             for i in range(3)) + (p[3],)
    return im


def label(im, text, size=54):
    """The name under the rule, tracked as the drawn cards' are."""
    d = ImageDraw.Draw(im)
    while True:
        f = ImageFont.truetype(FONT, size)
        widths = [f.getlength(ch) for ch in text]
        total = sum(widths) + TRACK * (len(text) - 1)
        if total <= W - 70 or size <= 30:
            break
        size -= 2
    x = (W - total) / 2.0
    for ch, w in zip(text, widths):
        d.text((x, 726), ch, font=f, fill=LABEL + (255,), anchor="ls")
        x += w + TRACK
    return im


def card(accent, name, draw_mark):
    """A whole card: draw_mark(d, cx, cy, k, fill, cut) draws the mark into a
    supersampled layer, centred on (cx, cy), at k pixels to the card's one;
    `cut` is transparent, so a knock-out shows the stock's gradient through."""
    layer = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    draw_mark(d, (W // 2) * SS, MARK_CY * SS, SS, accent + (255,), (0, 0, 0, 0))
    layer = layer.resize((W, H), Image.LANCZOS)
    im = base(accent)
    im.alpha_composite(layer)
    return label(im, name)
