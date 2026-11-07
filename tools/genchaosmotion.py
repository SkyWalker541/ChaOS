#!/usr/bin/env python3
"""ChaOS's boot and shutdown: the logo's pixels flying in, and flying out.

Run from the repo root:  python3 tools/genchaosmotion.py   (or make chaosboot)

One set of pixels and one set of paths for both, so the two are the same
gesture played forwards and backwards:

  - Every square of the lockup (the handheld and "ChaOS", tools/genlogo.py's
    grid, 12 px a square, centred on the ground as splash.png is) gets a
    direction, a distance and a delay from a fixed seed.

  - Boot, 0.60 s at 60 fps, the length of TortOS's flyby it replaces: each
    square starts out along its direction, off where it lands, and flies in
    to its place, decelerating, the squares arriving one after another, the
    whole logo together by about 0.55 s. Then the last frame holds until the
    shelf draws. Rendered here to PNG frames (build/chaos-boot-frames) and
    packed as plastron's splash plays them on the Brick: res/boot/
    chaos-splash-brick.bin, the "TSPZ" format of plastron's
    scripts/make-splash-frames.py --brick (GPL-3.0, by Eric Reinsmidt), whose
    packing is repeated in pack_tspz below. tools/chaos-image.sh puts it in
    the image as /usr/share/splash/frames.bin.

  - Shutdown, 0.90 s, the length of TortOS's send-off: the logo whole, then
    each square leaves along the same direction it came in by, accelerating
    and fading, until the ground is empty. Drawn live by the launcher
    (anim_poweroff in src/main.c), from src/chaos_mark.h, written here.
"""
import math
import os
import random
import struct
import sys
import zlib

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from genlogo import DARK, GROUND, lockup   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, FPS = 1024, 768, 60
CELL = 12                       # px a square, as splash.png
BOOT_FRAMES = 36                # 0.60 s, as TortOS's flyby
SEED = 541

# Boot: a square's flight, as fractions of a second
BOOT_DELAY_MAX = 0.20           # the last to set off
BOOT_FLIGHT = 0.34              # each one's time in the air


def cells():
    """The lockup's squares: (x, y) of the top-left in px, colour, and a path:
    unit direction, distance in px, and a 0..1 delay."""
    grid = lockup()
    gw, gh = len(grid[0]) * CELL, len(grid) * CELL
    ox, oy = (W - gw) // 2, (H - gh) // 2
    rnd = random.Random(SEED)
    out = []
    for gy, row in enumerate(grid):
        for gx, ch in enumerate(row):
            if ch == ".":
                continue
            x, y = ox + gx * CELL, oy + gy * CELL
            # Away from the logo's centre, scattered about that, so the logo
            # gathers from every side rather than from a few streams
            cx, cy = x + CELL / 2 - W / 2, y + CELL / 2 - H / 2
            base = math.atan2(cy, cx) if (cx or cy) else rnd.uniform(0, 2 * math.pi)
            ang = base + rnd.uniform(-1.1, 1.1)
            dist = rnd.uniform(360, 760)
            delay = rnd.random()
            out.append((x, y, DARK[ch], math.cos(ang), math.sin(ang), dist, delay))
    return out


def ease_out(k):
    return 1 - (1 - k) ** 3


def boot_frame(cs, t):
    img = Image.new("RGB", (W, H), GROUND)
    px = img.load()
    for x, y, col, dx, dy, dist, delay in cs:
        k = (t - delay * BOOT_DELAY_MAX) / BOOT_FLIGHT
        if k <= 0:
            continue
        k = min(1.0, k)
        off = dist * (1 - ease_out(k))
        a = min(1.0, k / 0.35)          # fades in over the first third
        sx, sy = int(round(x + dx * off)), int(round(y + dy * off))
        c = tuple(int(g + (v - g) * a) for v, g in zip(col, GROUND))
        for yy in range(max(0, sy), min(H, sy + CELL)):
            for xx in range(max(0, sx), min(W, sx + CELL)):
                px[xx, yy] = c
    return img


# ---- plastron's Brick splash format (scripts/make-splash-frames.py --brick) ----

def sub_filter(band, stride):
    out = bytearray(band)
    for start in range(0, len(band), stride):
        for i in range(4, stride):
            out[start + i] = (band[start + i] - band[start + i - 4]) & 0xff
    return bytes(out)


def pack_tspz(path, frames, w, h, fps):
    """"TSPZ", u16 w, u16 h, u16 frames, u16 fps; u32 offset per frame; per
    frame the band of rows changed from the one before (frame 0 whole) as u16
    first row, u16 rows, u32 length, then zlib of the band's XRGB bytes, each
    less the same byte one pixel to the left. Frames from the first that
    already matches the last are dropped: the splash holds its last."""
    stride = w * 4
    last = frames[-1]
    keep = len(frames) - 1
    while keep > 1 and frames[keep - 1] == last:
        keep -= 1
    frames = frames[:keep] + [last]
    encoded = []
    for n, f in enumerate(frames):
        if n == 0:
            first, rows = 0, h
        else:
            prev = frames[n - 1]
            changed = [y for y in range(h) if f[y * stride:(y + 1) * stride] != prev[y * stride:(y + 1) * stride]]
            first, rows = (changed[0], changed[-1] - changed[0] + 1) if changed else (0, 0)
        band = bytes(f[first * stride:(first + rows) * stride])
        data = zlib.compress(sub_filter(band, stride), 9) if rows else b""
        encoded.append(struct.pack("<HHI", first, rows, len(data)) + data)
    header = struct.pack("<4sHHHH", b"TSPZ", w, h, len(encoded), fps)
    offset = len(header) + 4 * len(encoded)
    table = bytearray()
    for e in encoded:
        table += struct.pack("<I", offset)
        offset += len(e)
    with open(path, "wb") as out:
        out.write(header + table + b"".join(encoded))
    return len(encoded), offset


def xrgb(img):
    """The frame as the splash wants it: B, G, R, 0xFF per pixel."""
    r, g, b = img.split()
    a = Image.new("L", img.size, 255)
    return bytearray(Image.merge("RGBA", (b, g, r, a)).tobytes())


def write_header(cs, path):
    with open(path, "w") as f:
        f.write("/* ChaOS's logo as squares, and each one's path. Written by\n"
                " * tools/genchaosmotion.py, which also renders the boot from the\n"
                " * same numbers: edit that, not this. */\n")
        f.write("#ifndef CHAOS_MARK_H\n#define CHAOS_MARK_H\n\n")
        f.write("#define CHAOS_CELL %d     /* px a square, on a %dx%d screen */\n" % (CELL, W, H))
        f.write("#define CHAOS_GROUND_R %d\n#define CHAOS_GROUND_G %d\n#define CHAOS_GROUND_B %d\n\n" % GROUND)
        f.write("typedef struct {\n\tshort x, y;              /* top-left, px */\n"
                "\tunsigned char r, g, b;\n\tfloat dx, dy, dist;      /* its way out: direction, px */\n"
                "\tfloat delay;              /* 0..1: when it leaves, among the others */\n} chaos_cell;\n\n")
        f.write("static const chaos_cell CHAOS_CELLS[] = {\n")
        for x, y, c, dx, dy, dist, delay in cs:
            f.write("\t{ %d, %d, %d, %d, %d, %.4ff, %.4ff, %.1ff, %.4ff },\n" % (x, y, c[0], c[1], c[2], dx, dy, dist, delay))
        f.write("};\n#define CHAOS_NCELLS ((int)(sizeof CHAOS_CELLS / sizeof CHAOS_CELLS[0]))\n\n#endif\n")


def main():
    cs = cells()
    write_header(cs, os.path.join(ROOT, "src", "chaos_mark.h"))
    tmp = os.path.join(ROOT, "build", "chaos-boot-frames")
    os.makedirs(tmp, exist_ok=True)
    frames = []
    for n in range(BOOT_FRAMES):
        img = boot_frame(cs, n / FPS)
        img.save(os.path.join(tmp, "f%04d.png" % n))
        frames.append(xrgb(img))
    # The last frame is the logo exactly, whatever the rounding before it
    final = boot_frame(cs, 10.0)
    final.save(os.path.join(tmp, "f%04d.png" % BOOT_FRAMES))
    frames[-1] = xrgb(final)
    os.makedirs(os.path.join(ROOT, "res", "boot"), exist_ok=True)
    out = os.path.join(ROOT, "res", "boot", "chaos-splash-brick.bin")
    n, size = pack_tspz(out, frames, W, H, FPS)
    print("%d squares; %s: %d frames, %.2f MB" % (len(cs), os.path.relpath(out, ROOT), n, size / 1e6))


if __name__ == "__main__":
    main()
