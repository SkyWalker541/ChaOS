#!/usr/bin/env python3
"""Draw res/cards/classic/FAVORITES.png, the card for the Favorites shelf.

The other nine cards are drawn by hand and nothing generates them - gencards.py
was deleted because it could not reproduce any of the nine. This one is
generated because it is not a console: it belongs to TortOS, its accent is
TortOS's cyan rather than a machine's, and that cyan already has exactly one
definition in tools/markdef.py. A hand-drawn copy would be a fourth place for
it to drift.

On tools/cardstyle.py (ChaOS, 2026-11-13): the drawn set's own stock, rule
and tracked name, where it was a flat slab matched by measurement before.
One mark: markdef.heart(), solid, in the same cyan as the rule - the Fancy
Pants card's heart (a photograph since 2.7.7) drawn flat, front on. A
bookmark ribbon with the heart knocked out of it until 2.7.6; three ascending
stars before 2026-08-30, then three hearts for about an hour.

KNOCKED OUT RATHER THAN OUTLINED, and that is the whole reason it fits. The
other nine cards are flat shapes - no strokes anywhere in the deck - and this
is the only card that is not a console, so it has to join that set by
construction rather than by luck. A stroked version was drawn and compared
side by side; it also thins out badly at the 62% a side card is scaled to,
where a solid silhouette does not.

The heart is markdef.heart(), the same curve src/main.c draws beside a
favorited game's name. A card and a shelf disagreeing about what "favorite"
looks like is the kind of thing nobody notices and everybody feels.
"""
import os
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markdef import CYAN, heart  # noqa: E402
import cardstyle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "classic", "FAVORITES.png")



def mark(d, cx, cy, k, fill, cut, scale=0.95):
    """The heart, 300px across at 0.95, centred: the size the round marks are."""
    k = k * scale
    pts = heart(240)
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    s = 300 * k / (max(xs) - min(xs))
    mx, my = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    d.polygon([(cx + (x - mx) * s, cy + (y - my) * s) for x, y in pts], fill=fill)


def main():
    im = cardstyle.card(tuple(CYAN), "FAVORITES", mark)
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
