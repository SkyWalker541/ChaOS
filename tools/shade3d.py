"""Shaded, lit objects for Fancy Pants cards, drawn with no images: the set is
photographs of consoles, and the shelves that are not consoles (Recently
Played, Muse, Ports) get an object rendered to sit among them.

Each shape is drawn from per-pixel normals and lit the same way: a soft key
light from the upper left, a studio's environment brighter above than below,
Blinn specular. A canvas is drawn large and finished as the set is: trimmed to
its own alpha, longest side 96% of a 384 frame, centred
(res/cards/fancy/SOURCE.md). tools/genrecentfancy.py came first and keeps its
own copy of the same lighting.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

L = np.array([-0.45, -0.6, 0.66]); L /= np.linalg.norm(L)
H = L + np.array([0, 0, 1.0]); H /= np.linalg.norm(H)


def rgb(c):
    return np.array(c, np.float32) / 255.0


class Canvas:
    def __init__(self, n=1600):
        self.n = n
        self.yy, self.xx = np.mgrid[0:n, 0:n].astype(np.float32)
        self.img = np.zeros((n, n, 3), np.float32)
        self.alpha = np.zeros((n, n), np.float32)

    # ---- lighting --------------------------------------------------------
    def shade(self, nx, ny, nz, base, spec=1.0, shin=60, amb=0.32, env=0.35):
        lam = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
        sp = np.clip(nx * H[0] + ny * H[1] + nz * H[2], 0, 1) ** shin * spec
        sky = np.clip(0.5 - 0.5 * ny, 0, 1)[..., None]
        b = rgb(base)
        c = b * (amb + (1 - amb) * lam[..., None]) \
            + env * sky * (0.55 * b + 0.45) * (1 - nz[..., None]) * 0.8
        return np.clip(c + sp[..., None], 0, 1)

    def put(self, mask, col):
        m = mask[..., None]
        self.img = self.img * (1 - m) + col * m
        self.alpha = np.maximum(self.alpha, mask)

    # ---- shapes ----------------------------------------------------------
    def sphere(self, cx, cy, r, base, **k):
        dx, dy = (self.xx - cx) / r, (self.yy - cy) / r
        d2 = dx * dx + dy * dy
        m = np.clip((1 - np.sqrt(d2)) * r / 1.5, 0, 1)
        nz = np.sqrt(np.clip(1 - d2, 0, 1))
        self.put(m, self.shade(dx, dy, nz, base, **k))

    def rod(self, x0, y0, x1, y1, w, base, **k):
        vx, vy = x1 - x0, y1 - y0
        ln = np.hypot(vx, vy); ux, uy = vx / ln, vy / ln
        px, py = self.xx - x0, self.yy - y0
        t = np.clip(px * ux + py * uy, 0, ln)
        ex, ey = px - t * ux, py - t * uy
        d = np.hypot(ex, ey) / (w / 2)
        m = np.clip((1 - d) * w / 3, 0, 1)
        sgn = np.sign(ex * (-uy) + ey * ux)
        nx, ny = (-uy) * sgn * d, ux * sgn * d
        nz = np.sqrt(np.clip(1 - d * d, 0, 1))
        self.put(m, self.shade(nx, ny, nz, base, **k))

    def rrect_sdf(self, cx, cy, hw, hh, r):
        qx = np.abs(self.xx - cx) - (hw - r)
        qy = np.abs(self.yy - cy) - (hh - r)
        out = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
        return out + np.minimum(np.maximum(qx, qy), 0) - r

    def ellipse_sdf(self, cx, cy, rx, ry):
        # good enough near the edge for an antialiased mask and a bevel
        dx, dy = (self.xx - cx) / rx, (self.yy - cy) / ry
        return (np.sqrt(dx * dx + dy * dy) - 1) * min(rx, ry)

    def slab(self, sdf, base, tilt=(0.0, 0.0), bevel=40, **k):
        """A face bounded by sdf (<0 inside), facing the viewer tipped by
        tilt (nx, ny), its edge rounded over `bevel` px."""
        m = np.clip(-sdf / 1.5, 0, 1)
        gy, gx = np.gradient(sdf)
        gl = np.maximum(np.hypot(gx, gy), 1e-6)
        gx, gy = gx / gl, gy / gl
        e = np.clip(-sdf / bevel, 0, 1)
        ang = (1 - e) * (np.pi / 2) * 0.92
        nx = gx * np.sin(ang) + tilt[0] * np.cos(ang)
        ny = gy * np.sin(ang) + tilt[1] * np.cos(ang)
        nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
        self.put(m, self.shade(nx, ny, nz, base, **k))
        return m

    def flat(self, sdf, col, vignette=None):
        m = np.clip(-sdf / 1.5, 0, 1)
        c = np.ones_like(self.img) * rgb(col)
        if vignette is not None:
            c = c * vignette[..., None]
        self.put(m, c)
        return m

    # ---- finishing ------------------------------------------------------
    def image(self):
        im = Image.fromarray((self.img * 255).astype(np.uint8)).convert("RGBA")
        im.putalpha(Image.fromarray((self.alpha * 255).astype(np.uint8)))
        return im


def overlay(im, layer, mask_arr=None, blur=0):
    """Composite `layer` (RGBA) over im, clipped to mask_arr (0..1)."""
    if blur:
        layer = layer.filter(ImageFilter.GaussianBlur(blur))
    if mask_arr is not None:
        a = np.array(layer.getchannel("A"), np.float32) * mask_arr
        layer.putalpha(Image.fromarray(a.astype(np.uint8)))
    im.alpha_composite(layer)


def finish(im, out):
    box = im.getchannel("A").getbbox()
    im = im.crop(box)
    k = 369.0 / max(im.size)
    im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
    o = Image.new("RGBA", (384, 384), (0, 0, 0, 0))
    o.paste(im, ((384 - im.width) // 2, (384 - im.height) // 2), im)
    o.save(out)
    print(out, o.size, "ink", o.getchannel("A").getbbox())
    return o
