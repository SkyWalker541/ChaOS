#!/usr/bin/env python3
"""Draw every ChaOS logo file from the pixel art below.

    python3 tools/genlogo.py [out_dir]    (default: res/logo)

The logo is concept A, "Handheld burst": a little handheld with pixels
breaking loose from its screen, beside the name with OS in orange. The pixel
grids here are the only source; every file below is drawn from them, scaled
by whole numbers with no smoothing, so each logo pixel stays a crisp square.

Writes:
  mark.png            the handheld alone, 16x16 grid at 16x (256 px), transparent
  mark-64.png         at 4x, 64 px
  mark-32.png         at 2x, 32 px (app icons)
  mark-16.png         at 1x, 16 px (favicons)
  wordmark.png        "ChaOS" alone at 12x, transparent, for dark grounds
  lockup.png          mark and name together at 12x, transparent, for dark grounds
  lockup-light.png    the same for light grounds (dark letters and outline)
  splash.png          1024x768, the lockup on the dark ground (boot splash)
  bootlogo.bmp        the same picture as a 24-bit BMP (the Brick's boot logo)
  icon.svg            the mark as SVG squares (browser tab icon)
  mark.svg            the lockup as SVG squares (web page logo)

Original artwork for ChaOS. No fonts are used: the letters are drawn here
pixel by pixel.
"""
import os
import sys

from PIL import Image

# ---- colours -------------------------------------------------------------------

GROUND = (0x17, 0x15, 0x1C)     # the dark ground
CREAM  = (0xF4, 0xEA, 0xD5)
ORANGE = (0xFF, 0x7A, 0x2F)
CYAN   = (0x39, 0xC5, 0xD6)
SCREEN = (0x26, 0x32, 0x3C)

DARK = {                         # on the dark ground
    "k": (0x3A, 0x34, 0x46),     # the handheld's outline
    "w": CREAM,                  # its body
    "s": SCREEN,                 # its screen
    "a": ORANGE,                 # escaping pixels, and OS
    "b": CYAN,                   # escaping pixels
    "c": CREAM,                  # the letters "Cha"
}
LIGHT = dict(DARK, k=GROUND, c=GROUND)   # on a light ground

# ---- the pixel art ----------------------------------------------------------------

MARK = [
    "...........b....",
    "..........a...a.",
    ".........b..b...",
    ".kkkkkkkkkka....",
    ".kwwwwwwwwk..b..",
    ".kwssssbswk.....",
    ".kwssassswk.a...",
    ".kwsbsssswk.....",
    ".kwssssaswk.....",
    ".kwsssssswk.....",
    ".kwwwwwwwwk.....",
    ".kwkwwwwawk.....",
    ".kkkkwwawwk.....",
    ".kwkwwwwwwk.....",
    ".kwwwwwwwwk.....",
    ".kkkkkkkkkk.....",
]

GLYPHS = {
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "h": ["#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#"],
    "a": [".....", ".....", ".###.", "....#", ".####", "#...#", ".####"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
}


def word(text, codes):
    """The name as a grid: letters 5x7, one empty column between them."""
    rows = [""] * 7
    for i, ch in enumerate(text):
        for r in range(7):
            rows[r] += ("." if i else "") + GLYPHS[ch][r].replace("#", codes[i])
    return rows


WORD = word("ChaOS", "cccaa")

LOCKUP_GAP = 4      # grid columns between the handheld and the name
WORD_DROP = 5       # grid rows from the mark's top to the name's top


def lockup():
    """Mark and name on one grid: the name centred on the handheld's body."""
    w = len(MARK[0]) + LOCKUP_GAP + len(WORD[0])
    rows = []
    for r in range(len(MARK)):
        line = MARK[r] + "." * LOCKUP_GAP
        wr = r - WORD_DROP
        line += WORD[wr] if 0 <= wr < len(WORD) else "." * len(WORD[0])
        rows.append(line)
    assert all(len(x) == w for x in rows)
    return rows


# ---- drawing ------------------------------------------------------------------------

def draw(grid, palette, scale, ground=None):
    """The grid as an image, each cell a scale x scale square."""
    h, w = len(grid), len(grid[0])
    img = Image.new("RGBA", (w * scale, h * scale), ground + (255,) if ground else (0, 0, 0, 0))
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            if ch != ".":
                img.paste(palette[ch] + (255,), (x * scale, y * scale, (x + 1) * scale, (y + 1) * scale))
    return img


def screen(grid, palette, scale, size=(1024, 768)):
    """The grid centred on the dark ground at a fixed size."""
    art = draw(grid, palette, scale)
    img = Image.new("RGBA", size, GROUND + (255,))
    img.alpha_composite(art, ((size[0] - art.width) // 2, (size[1] - art.height) // 2))
    return img


def svg(grid, palette, title):
    """The grid as SVG squares, joining runs of one colour along a row."""
    h, w = len(grid), len(grid[0])
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
           'shape-rendering="crispEdges" role="img" aria-label="%s">' % (w, h, title),
           "<title>%s</title>" % title]
    for y, row in enumerate(grid):
        x = 0
        while x < w:
            ch = row[x]
            if ch == ".":
                x += 1
                continue
            run = 1
            while x + run < w and row[x + run] == ch:
                run += 1
            out.append('<rect x="%d" y="%d" width="%d" height="1" fill="#%02X%02X%02X"/>'
                       % ((x, y, run) + palette[ch]))
            x += run
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "res", "logo")
    os.makedirs(out, exist_ok=True)
    lk = lockup()

    def save(name, img):
        img.save(os.path.join(out, name))
        print("  %-18s %dx%d" % (name, img.width, img.height))

    save("mark.png", draw(MARK, DARK, 16))
    save("mark-64.png", draw(MARK, DARK, 4))
    save("mark-32.png", draw(MARK, DARK, 2))
    save("mark-16.png", draw(MARK, DARK, 1))
    save("wordmark.png", draw(WORD, DARK, 12))
    save("lockup.png", draw(lk, DARK, 12))
    save("lockup-light.png", draw(lk, LIGHT, 12))
    splash = screen(lk, DARK, 12)
    save("splash.png", splash)
    splash.convert("RGB").save(os.path.join(out, "bootlogo.bmp"))
    print("  %-18s %dx%d, 24-bit" % ("bootlogo.bmp", splash.width, splash.height))
    for name, grid, title in (("icon.svg", MARK, "ChaOS"), ("mark.svg", lk, "ChaOS")):
        with open(os.path.join(out, name), "w") as f:
            f.write(svg(grid, DARK, title))
        print("  %s" % name)


if __name__ == "__main__":
    main()
