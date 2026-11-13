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
One mark: a bookmark ribbon with markdef.heart() knocked out of it, in the
same cyan as the rule. Three ascending stars before 2026-08-30, then three
hearts for about an hour.

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



def bookmark(cx, cy, w, h, notch):
    """A ribbon: square shoulders, two points at the bottom with a V between."""
    x0, x1 = cx - w / 2.0, cx + w / 2.0
    y0, y1 = cy - h / 2.0, cy + h / 2.0
    return [(x0, y0), (x1, y0), (x1, y1), (cx, y1 - notch), (x0, y1)]


def mark(d, cx, cy, k, ribbon, cut, scale=0.95):
    """The ribbon, then the heart punched through it to the stock.

    The heart sits above the notch rather than centered in the ribbon: centered,
    its point crowds the V and the two shapes argue about where the middle is.
    At 0.95, 314px tall, as the tallest console marks are.
    """
    k = k * scale
    d.polygon(bookmark(cx, cy, 232 * k, 330 * k, 68 * k), fill=ribbon)
    hr, hy = 78 * k, cy - 28 * k
    d.polygon([(cx + x * 2 * hr, hy + y * 2 * hr) for x, y in heart(96)],
              fill=cut)


def main():
    im = cardstyle.card(tuple(CYAN), "FAVORITES", mark)
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
