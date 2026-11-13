"""A small renderer for ChaOS's Fancy Pants cards: the shelves that are not
consoles (Recently Played, Muse, Ports) get an object rendered to sit among
the set's photographs (res/cards/fancy/SOURCE.md), from no images at all.

Scenes are signed distance functions, sphere-traced through a perspective
camera, and lit as a product photograph is: a large soft key light from the
upper left with soft shadows, a fill, a rim, ambient occlusion, and glossy
reflections of a studio (a bright softbox, a light ceiling, a dark floor),
Fresnel-weighted, plastic or metal. Supersampled, tone-mapped, given a
photograph's faint grain, cut out on its own alpha, and finished as the set
is: longest side 96% of a 384 frame, centred.

Everything is numpy over all the camera's rays at once.
"""
import numpy as np
from PIL import Image

# ---- vectors ----------------------------------------------------------------


def norm(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def rot(ax, ay, az):
    """A rotation matrix from angles in degrees, applied x, then y, then z."""
    ax, ay, az = np.radians([ax, ay, az])
    rx = np.array([[1, 0, 0], [0, np.cos(ax), -np.sin(ax)], [0, np.sin(ax), np.cos(ax)]])
    ry = np.array([[np.cos(ay), 0, np.sin(ay)], [0, 1, 0], [-np.sin(ay), 0, np.cos(ay)]])
    rz = np.array([[np.cos(az), -np.sin(az), 0], [np.sin(az), np.cos(az), 0], [0, 0, 1]])
    return rz @ ry @ rx


def local(p, pos=(0, 0, 0), m=None):
    """Points p in a part's own frame: moved to pos, turned by m."""
    q = p - np.array(pos, np.float64)
    return q if m is None else q @ m        # m orthonormal: row-vector inverse


# ---- distances (y up, z toward the camera) ---------------------------------


def length(q):
    return np.sqrt(np.sum(q * q, axis=-1))


def sphere(q, r):
    return length(q) - r


def box(q, b, r=0.0):
    """A box of half-sizes b, its edges rounded by r."""
    d = np.abs(q) - (np.array(b) - r)
    return length(np.maximum(d, 0)) + np.minimum(np.max(d, axis=-1), 0) - r


def cyl(q, r, h, round_=0.0, axis=1):
    """A cylinder of radius r, half-height h along `axis`, edges rounded."""
    a = q[..., axis]
    o = np.delete(q, axis, axis=-1)
    d = np.stack([length(o) - (r - round_), np.abs(a) - (h - round_)], -1)
    return np.minimum(np.maximum(d[..., 0], d[..., 1]), 0) + length(np.maximum(d, 0)) - round_


def torus(q, R, r, axis=2):
    a = q[..., axis]
    o = np.delete(q, axis, axis=-1)
    return np.sqrt((length(o) - R) ** 2 + a * a) - r


def capsule(q, a, b, r):
    a, b = np.array(a, np.float64), np.array(b, np.float64)
    pa, ba = q - a, b - a
    h = np.clip(np.sum(pa * ba, -1) / np.dot(ba, ba), 0, 1)
    return length(pa - ba * h[..., None]) - r


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


# ---- materials ---------------------------------------------------------------


class Mat:
    """albedo (0-255 RGB, or a function of the hit's local point giving an
    (n,3) array in 0-1), how glossy (0 matte to 1 mirror), whether metal, and
    how much it shows its own colour unlit (a lit screen)."""

    def __init__(self, albedo, gloss=0.5, metal=False, spec=1.0, emit=0.0):
        self.albedo, self.gloss, self.metal = albedo, gloss, metal
        self.spec, self.emit = spec, emit

    def color(self, p):
        if callable(self.albedo):
            return self.albedo(p)
        return np.broadcast_to((np.array(self.albedo, np.float64) / 255.0) ** 2.2, p.shape)


# ---- the studio --------------------------------------------------------------

KEY = norm(np.array([-0.55, 0.75, 0.62]))
FILL = norm(np.array([0.8, 0.15, 0.6]))
RIM = norm(np.array([0.3, 0.5, -0.8]))


def studio(d):
    """What a glossy surface reflects: a dim ceiling and a dark floor, a big
    softbox up and to the left, a strip light to the right."""
    y = d[..., 1]
    base = np.where(y > 0, 0.20 + 0.25 * y, 0.04 + 0.05 * (1 + y))
    sb = np.clip((np.sum(d * KEY, -1) - 0.86) / 0.06, 0, 1) * 3.2
    strip = np.clip((np.sum(d * FILL, -1) - 0.93) / 0.04, 0, 1) * 1.1
    return (base + sb + strip)[..., None] * np.array([1.0, 1.0, 1.02])


# ---- rendering -----------------------------------------------------------------


def render(scene, out, size=384, ss=3, cam=(0, 0.6, 9.0), look=(0, 0, 0), fov=26,
           grain=0.012, exposure=1.0, debug=None):
    """scene(p) -> (distance, material index); scene.mats lists the Mats."""
    n = size * ss
    cam = np.array(cam, np.float64)
    fw = norm(np.array(look, np.float64) - cam)
    rt = norm(np.cross(fw, [0, 1, 0]))
    up = np.cross(rt, fw)
    j, i = np.mgrid[0:n, 0:n].astype(np.float64)
    u = (i + 0.5) / n * 2 - 1
    v = 1 - (j + 0.5) / n * 2
    f = np.tan(np.radians(fov) / 2)
    rd = norm(fw + rt * (u[..., None] * f) + up * (v[..., None] * f)).reshape(-1, 3)
    ro = np.broadcast_to(cam, rd.shape)

    def dist(p):
        return scene(p)[0]

    # sphere-trace, only the rays still going
    t = np.zeros(len(rd))
    hit = np.zeros(len(rd), bool)
    live = np.arange(len(rd))
    for _ in range(220):
        if not len(live):
            break
        p = ro[live] + rd[live] * t[live, None]
        d = dist(p)
        done = d < 1e-3 * (1 + t[live] * 0.05)
        hit[live[done]] = True
        t[live] += d * 0.92
        live = live[(~done) & (t[live] < 30)]
    idx = np.nonzero(hit)[0]
    p = ro[idx] + rd[idx] * t[idx, None]

    e = 1e-3
    nrm = norm(np.stack([dist(p + [e, 0, 0]) - dist(p - [e, 0, 0]),
                         dist(p + [0, e, 0]) - dist(p - [0, e, 0]),
                         dist(p + [0, 0, e]) - dist(p - [0, 0, e])], -1))
    _, mid = scene(p)

    # soft shadow toward the key light
    sh = np.ones(len(p))
    tt = np.full(len(p), 0.02)
    for _ in range(48):
        h = dist(p + nrm * 0.004 + KEY * tt[:, None])
        sh = np.minimum(sh, 10 * h / tt)
        tt += np.clip(h, 0.01, 0.25)
    sh = np.clip(sh, 0, 1) ** 1.5

    # ambient occlusion along the normal
    ao = np.zeros(len(p))
    for k in range(1, 6):
        hh = 0.04 * k
        ao += (hh - dist(p + nrm * hh)) / (2 ** k)
    ao = np.clip(1 - 2.6 * ao, 0, 1)

    view = -rd[idx]
    col = np.zeros((len(p), 3))
    for k, m in enumerate(scene.mats):
        s = mid == k
        if not s.any():
            continue
        q, nn, vv = p[s], nrm[s], view[s]
        alb = m.color(q)
        ndv = np.clip(np.sum(nn * vv, -1), 0, 1)
        refl = 2 * ndv[:, None] * nn - vv
        f0 = alb if m.metal else np.full_like(alb, 0.04)
        fres = f0 + (1 - f0) * ((1 - ndv) ** 5)[:, None]
        rough = 1 - m.gloss
        diff = (np.clip(np.sum(nn * KEY, -1), 0, 1) * sh[s] * 1.15
                + np.clip(np.sum(nn * FILL, -1), 0, 1) * 0.30
                + (0.5 + 0.5 * nn[:, 1]) * 0.22 * ao[s])
        hk = norm(KEY + vv)
        shin = 2 + 600 * m.gloss ** 3
        spk = np.clip(np.sum(nn * hk, -1), 0, 1) ** shin * (shin + 8) / 60 * sh[s]
        rim = np.clip(np.sum(nn * RIM, -1), 0, 1) ** 2 * 0.35
        env = studio(refl)
        # rougher surfaces see a blurred, dimmer studio
        env = env * (1 - rough * 0.65) + rough * 0.65 * 0.25
        dcol = alb * (diff + rim)[:, None] * (0.0 if m.metal else 1.0)
        scol = (env * ao[s, None] + spk[:, None] * m.spec) * fres
        col[s] = dcol * (1 - fres * (0 if m.metal else 1)) + scol + alb * m.emit
    col = col * exposure

    img = np.zeros((len(rd), 3))
    img[idx] = col
    a = hit.astype(np.float64)
    img = img.reshape(n, n, 3)
    a = a.reshape(n, n)
    # tone map (filmic-ish) and gamma
    img = img / (1 + img * 0.35)
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    # downsample, premultiplied
    img = (img * a[..., None]).reshape(size, ss, size, ss, 3).mean((1, 3))
    a = a.reshape(size, ss, size, ss).mean((1, 3))
    img = img / np.maximum(a[..., None], 1e-6)
    rng = np.random.default_rng(7)
    img = np.clip(img + rng.normal(0, grain, img.shape), 0, 1)
    im = Image.fromarray((img * 255).astype(np.uint8)).convert("RGBA")
    im.putalpha(Image.fromarray((a * 255).astype(np.uint8)))
    if debug:
        im.save(debug)
    return finish(im, out)


def finish(im, out):
    box_ = im.getchannel("A").getbbox()
    im = im.crop(box_)
    k = 369.0 / max(im.size)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    o = Image.new("RGBA", (384, 384), (0, 0, 0, 0))
    o.paste(im, ((384 - im.width) // 2, (384 - im.height) // 2), im)
    o.save(out)
    print(out, o.size, "ink", o.getchannel("A").getbbox())
    return o


# ---- textures on flat faces ----------------------------------------------------


class Tex:
    """A picture laid on a part's face: a point's local (x, y) inside
    [x0, x1] x [y0, y1] reads the picture; outside it, `base`."""

    def __init__(self, im, x0, x1, y0, y1, base=(0, 0, 0)):
        a = np.asarray(im.convert("RGB"), np.float64) / 255.0
        self.a = a ** 2.2
        self.h, self.w = a.shape[:2]
        self.x0, self.x1, self.y0, self.y1 = x0, x1, y0, y1
        self.base = (np.array(base, np.float64) / 255.0) ** 2.2

    def __call__(self, q):
        u = (q[:, 0] - self.x0) / (self.x1 - self.x0)
        v = (self.y1 - q[:, 1]) / (self.y1 - self.y0)
        inside = (u >= 0) & (u < 1) & (v >= 0) & (v < 1)
        out = np.broadcast_to(self.base, q.shape).copy()
        xi = np.clip((u * self.w).astype(int), 0, self.w - 1)
        yi = np.clip((v * self.h).astype(int), 0, self.h - 1)
        out[inside] = self.a[yi[inside], xi[inside]]
        return out
