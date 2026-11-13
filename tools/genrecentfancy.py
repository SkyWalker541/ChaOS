#!/usr/bin/env python3
"""Draw res/cards/fancy/RECENT.png, Recently Played's card in Fancy Pants.

That set is photographs of each console, cut out (res/cards/fancy/SOURCE.md),
and Recently Played is not a console, so there is nothing to photograph and
no photograph free to use was found that would sit beside them. This renders
one instead: a twin-bell alarm clock, lit and shaded as an object is (per-
pixel normals, a soft key light from the upper left, a studio's environment,
specular highlights), with no third-party images. Its case is Recently
Played's cyan; the clock reads 10:09, as clocks in photographs do.

Trimmed to its own alpha, longest side 96% of a 384 frame, centred, as the
rest of the set is.

    tools/genrecentfancy.py
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "cards", "fancy", "RECENT.png")
N = 1600                       # canvas (4x of 400)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
img = np.zeros((N, N, 3), np.float32)
alpha = np.zeros((N, N), np.float32)
L = np.array([-0.45, -0.6, 0.66]); L /= np.linalg.norm(L)
H = L + np.array([0, 0, 1.0]); H /= np.linalg.norm(H)

def shade(nx, ny, nz, base, spec=1.0, shin=60, amb=0.32, env=0.35):
    lam = np.clip(nx*L[0] + ny*L[1] + nz*L[2], 0, 1)
    sp = np.clip(nx*H[0] + ny*H[1] + nz*H[2], 0, 1) ** shin * spec
    # a studio's soft environment: light from above, dark below
    sky = np.clip(0.5 - 0.5*ny, 0, 1)[..., None]
    b = np.array(base, np.float32)/255.0
    c = b*(amb + (1-amb)*lam[..., None]) + env*sky*(0.55*b + 0.45) * (1-nz[..., None])*0.8
    c = c + sp[..., None]
    return np.clip(c, 0, 1)

def put(mask, col):
    global img, alpha
    m = mask[..., None]
    img = img*(1-m) + col*m
    alpha = np.maximum(alpha, mask)

def sphere(cx, cy, r, base, **k):
    dx, dy = (xx-cx)/r, (yy-cy)/r
    d2 = dx*dx + dy*dy
    m = np.clip((1 - np.sqrt(d2)) * r / 1.5, 0, 1)
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    put(m, shade(dx, dy, nz, base, **k))

def dome(cx, cy, r, base, tilt, **k):
    """A bell: the top of a sphere, cut flat across its mouth, which faces
    down and out (tilt), with the dark inside of the mouth showing."""
    dx, dy = (xx-cx)/r, (yy-cy)/r
    d2 = dx*dx + dy*dy
    # the mouth's line, rotated by tilt: keep what is above it
    ca, sa = np.cos(tilt), np.sin(tilt)
    along = dx*ca + dy*sa          # across the mouth
    up = -dx*sa + dy*ca            # toward the mouth (positive = below the cut)
    cut = 0.32
    m = np.clip((1 - np.sqrt(d2))*r/1.5, 0, 1)*np.clip((cut - up)*r/1.5, 0, 1)
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    put(m, shade(dx, dy, nz, base, **k))
    # the mouth: a dark ellipse, the bell's inside, just at the cut
    w = np.sqrt(max(1 - cut*cut, 0))
    e = (along/w)**2 + ((up - cut + 0.02)/0.13)**2
    me = np.clip((1 - e)*12, 0, 1)*np.clip((1 - np.sqrt(d2))*r/1.5, 0, 1)
    put(me, np.ones_like(img)*np.array([0.16, 0.17, 0.2]))

def rod(x0, y0, x1, y1, w, base, **k):
    vx, vy = x1-x0, y1-y0; ln = np.hypot(vx, vy); ux, uy = vx/ln, vy/ln
    px, py = xx-x0, yy-y0
    t = np.clip(px*ux + py*uy, 0, ln)
    ex, ey = px - t*ux, py - t*uy
    d = np.hypot(ex, ey)/(w/2)
    m = np.clip((1-d)*w/3, 0, 1)
    nperp = np.clip(d, -1, 1)
    sgn = np.sign(ex*(-uy) + ey*ux)
    nx = (-uy)*sgn*nperp; ny = ux*sgn*nperp
    nz = np.sqrt(np.clip(1-nperp**2, 0, 1))
    put(m, shade(nx, ny, nz, base, **k))

CASE = (36, 168, 214)          # Recently Played's cyan, as painted metal
CHROME = (200, 205, 212)
cx, cy, R = 800, 900, 520

# bells, behind everything: chrome domes, tilted out
for s in (-1, 1):
    bx, by = cx + s*400, cy - 470
    rod(cx + s*190, cy - 430, bx - s*40, by + 60, 34, CHROME, shin=80)
    dome(bx, by, 215, CHROME, s*0.55, shin=90, spec=1.1, env=0.6)
    sphere(bx - s*0.0, by - 205, 26, CHROME, shin=90)
# the hammer and its post
rod(cx, cy - 500, cx, cy - 690, 30, CHROME, shin=80)
sphere(cx, cy - 700, 40, CHROME, shin=90)
# feet
for s in (-1, 1):
    rod(cx + s*300, cy + 400, cx + s*420, cy + 600, 60, CASE, shin=40, spec=0.6)
    sphere(cx + s*425, cy + 610, 48, CHROME, shin=90)
# the case: a disc with a rounded rim, its face recessed in it
dx, dy = xx-cx, yy-cy
r = np.hypot(dx, dy)
m = np.clip((R - r)/1.5, 0, 1)
RIM = 120
t = np.clip((r - (R-RIM))/RIM, 0, 1)            # 0 inner edge of rim, 1 outer
ang = (t*2 - 1) * (np.pi/2) * 0.95              # a half round across the rim
ux, uy = dx/np.maximum(r, 1), dy/np.maximum(r, 1)
nx = ux*np.sin(ang); ny = uy*np.sin(ang); nz = np.cos(ang)
put(m, shade(nx, ny, nz, CASE, shin=50, spec=0.9, env=0.45))
# a chrome bezel ring inside the rim
br0, br1 = R-RIM-6, R-RIM+22
mb = np.clip((br1 - r)/1.5, 0, 1)*np.clip((r - br0)/1.5, 0, 1)
tb = np.clip((r-br0)/(br1-br0), 0, 1); ab = (tb*2-1)*(np.pi/2)*0.9
put(mb, shade(ux*np.sin(ab), uy*np.sin(ab), np.cos(ab), CHROME, shin=100, spec=1.2, env=0.7))
# the face: warm white, a touch darker toward the rim (the bezel's shadow)
FR = br0
mf = np.clip((FR - r)/1.5, 0, 1)
face = np.array([246, 244, 238], np.float32)/255.0
v = 1 - 0.16*np.clip((r/FR - 0.72)/0.28, 0, 1)**2
put(mf, face*v[..., None])

im = Image.fromarray((img*255).astype(np.uint8)).convert("RGBA")
im.putalpha(Image.fromarray((alpha*255).astype(np.uint8)))
d = ImageDraw.Draw(im)
INK = (34, 36, 44, 255)
for i in range(60):
    a = math.radians(i*6)
    big = i % 5 == 0
    r0 = FR - (70 if big else 34); r1 = FR - 18
    w = 14 if big else 5
    d.line([(cx + r0*math.sin(a), cy - r0*math.cos(a)), (cx + r1*math.sin(a), cy - r1*math.cos(a))], fill=INK, width=w)
# hands at 10:09, the shadow first
def hand(a_deg, ln, w, col, tail=40, off=(0, 0)):
    a = math.radians(a_deg)
    sx, sy = math.sin(a), -math.cos(a)
    px, py = -sy, sx
    pts = [(cx - sx*tail + px*w/2, cy - sy*tail + py*w/2), (cx + sx*ln + px*w/4, cy + sy*ln + py*w/4),
           (cx + sx*(ln+10), cy + sy*(ln+10)),
           (cx + sx*ln - px*w/4, cy + sy*ln - py*w/4), (cx - sx*tail - px*w/2, cy - sy*tail - py*w/2)]
    return [(x+off[0], y+off[1]) for x, y in pts]
sh = Image.new("RGBA", im.size, (0, 0, 0, 0)); sd = ImageDraw.Draw(sh)
for a, ln, w in ((304, 230, 30), (54, 320, 22)):
    sd.polygon(hand(a, ln, w, None, off=(14, 18)), fill=(0, 0, 0, 90))
sd.line([(cx+14, cy+18), (cx+14 + 340*math.sin(math.radians(170)), cy+18 - 340*math.cos(math.radians(170)))], fill=(0, 0, 0, 70), width=6)
sh = sh.filter(ImageFilter.GaussianBlur(10))
fm = Image.fromarray((mf*255).astype(np.uint8))
im.alpha_composite(Image.composite(sh, Image.new("RGBA", im.size, (0, 0, 0, 0)), fm))
d = ImageDraw.Draw(im)
d.polygon(hand(304, 230, 30, None), fill=INK)
d.polygon(hand(54, 320, 22, None), fill=INK)
a = math.radians(170)
d.line([(cx - 60*math.sin(a), cy + 60*math.cos(a)), (cx + 345*math.sin(a), cy - 345*math.cos(a))], fill=(214, 52, 44, 255), width=7)
d.ellipse([cx-30, cy-30, cx+30, cy+30], fill=(214, 52, 44, 255))
d.ellipse([cx-12, cy-12, cx+12, cy+12], fill=(240, 200, 190, 255))
# the glass: a soft sweep of reflection across the upper left
gl = Image.new("L", im.size, 0); gd = ImageDraw.Draw(gl)
gd.ellipse([cx-FR*0.95, cy-FR*0.98, cx+FR*0.55, cy+FR*0.15], fill=70)
gd.ellipse([cx-FR*0.85, cy-FR*0.70, cx+FR*0.85, cy+FR*0.55], fill=0)
gl = gl.filter(ImageFilter.GaussianBlur(30))
gl = Image.fromarray((np.array(gl, np.float32)*mf).astype(np.uint8))
im.alpha_composite(Image.composite(Image.new("RGBA", im.size, (255, 255, 255, 255)), Image.new("RGBA", im.size, (0, 0, 0, 0)), gl))

# trim, 96% of a 384 frame, centred (res/cards/fancy/SOURCE.md)
box = im.getchannel("A").getbbox(); im = im.crop(box)
k = 369.0/max(im.size); im = im.resize((round(im.width*k), round(im.height*k)), Image.LANCZOS)
out = Image.new("RGBA", (384, 384), (0, 0, 0, 0)); out.paste(im, ((384-im.width)//2, (384-im.height)//2), im)
out.save(OUT)
print(OUT, out.size, "ink", out.getchannel("A").getbbox())
