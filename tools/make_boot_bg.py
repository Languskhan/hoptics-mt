#!/usr/bin/env python3
"""Generate the Hoptics MT boot-screen background art (original, procedural, reproducible).

usage: make_boot_bg.py <design> <out.png> [--w 240 --h 320 --seed N]
(--w 320 --h 240 for the landscape boards: assets/boot_bg_<design>_320x240.png)
designs: ridgelines | constellation | aurora

Everything is drawn from math (no source images), at 2x and downsampled for clean anti-aliasing.
The center (logo + title) is kept calm so text stays readable; detail lives toward the edges.
Colors come from the Midnight theme (HopticsUI.cpp applyPalette).
"""
import argparse
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

BG_TOP = np.array([0x0E, 0x16, 0x21], float)
BG_BOT = np.array([0x07, 0x0B, 0x10], float)
CYAN = np.array([0x22, 0xD3, 0xEE], float)
BLUE = np.array([0x60, 0xA5, 0xFA], float)
VIOLET = np.array([0xA7, 0x8B, 0xFA], float)
GREEN = np.array([0x34, 0xD3, 0x99], float)
TEAL = np.array([0x1E, 0x6F, 0x7E], float)


def base(W, H, logo_cx, logo_cy):
    y = np.linspace(0, 1, H)[:, None, None]
    img = BG_TOP * (1 - y) + BG_BOT * y
    img = np.broadcast_to(img, (H, W, 3)).copy()
    # soft glow behind the logo
    yy, xx = np.mgrid[0:H, 0:W]
    r = np.hypot(xx - logo_cx, (yy - logo_cy) * 1.05) / (W * 0.42)
    glow = np.clip(1 - r, 0, 1) ** 2.2
    img += glow[..., None] * (np.array([0x0F, 0x2A, 0x33], float) - BG_TOP) * 0.9
    return img, xx, yy


def calm_mask(xx, yy, W, H, logo_cy, title_y):
    """1 at the edges, ~0.15 behind the logo/title, so detail never fights the text."""
    d_logo = np.hypot((xx - W / 2) / (W * 0.36), (yy - logo_cy) / (H * 0.20))
    d_title = np.hypot((xx - W / 2) / (W * 0.62), (yy - title_y) / (H * 0.075))
    m = np.minimum(d_logo, d_title)
    return 0.15 + 0.85 * np.clip((m - 0.55) / 0.9, 0, 1)


def add(img, layer_rgb, alpha):
    img += (layer_rgb - img) * np.clip(alpha, 0, 1)[..., None]


def value_noise(W, H, cells, rng):
    gw, gh = cells + 3, int(cells * H / W) + 3
    g = rng.random((gh, gw))
    ys = np.linspace(0, gh - 3, H)
    xs = np.linspace(0, gw - 3, W)
    x0, y0 = xs.astype(int), ys.astype(int)
    fx, fy = xs - x0, ys - y0
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    a = g[y0][:, x0]
    b = g[y0][:, x0 + 1]
    c = g[y0 + 1][:, x0]
    d = g[y0 + 1][:, x0 + 1]
    top = a + (b - a) * fx[None, :]
    bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def glow_dots(W, H, pts, rng, scale):
    """Return (rgb, alpha) layers with soft glowing dots (colored halo, crisp core)."""
    halo = Image.new("RGB", (W, H), (0, 0, 0))
    core = Image.new("RGB", (W, H), (0, 0, 0))
    dh, dc = ImageDraw.Draw(halo), ImageDraw.Draw(core)
    for x, y, r, c in pts:
        rgb = tuple(int(v) for v in c)
        dh.ellipse([x - r * 2.6, y - r * 2.6, x + r * 2.6, y + r * 2.6], fill=tuple(int(v * 0.55) for v in c))
        dc.ellipse([x - r, y - r, x + r, y + r], fill=rgb)
    halo = np.asarray(halo.filter(ImageFilter.GaussianBlur(3.2 * scale)), float)
    core = np.asarray(core, float)
    lum = np.maximum(halo.max(axis=2), core.max(axis=2))
    a = np.clip(lum / 200.0, 0, 1)
    rgb = np.where(core.max(axis=2, keepdims=True) > 0, core, halo / np.maximum(a[..., None], 1e-3))
    return np.clip(rgb, 0, 255), a


def ridgelines(W, H, s, rng, logo_cx, logo_cy, title_y):
    img, xx, yy = base(W, H, logo_cx, logo_cy)
    calm = calm_mask(xx, yy, W, H, logo_cy, title_y)
    # terrain: a few broad hills plus soft noise
    h = 0.55 * value_noise(W, H, 3, rng) + 0.3 * value_noise(W, H, 6, rng)
    peaks = [(0.16, 0.10, 0.17), (0.86, 0.20, 0.15), (0.12, 0.86, 0.16), (0.82, 0.88, 0.18), (0.52, 0.97, 0.12)]
    for px, py, pr in peaks:
        h += 0.55 * np.exp(-(((xx / W - px) / pr) ** 2 + ((yy / H - py) / (pr * W / H)) ** 2))
    step = 0.045
    f = h / step
    gy, gx = np.gradient(f)
    grad = np.hypot(gx, gy) + 1e-6
    d = np.abs(f - np.round(f)) / grad  # pixel distance to the nearest iso-line
    width = 0.9 * s
    line = np.clip(1 - d / width, 0, 1)
    major = (np.round(f).astype(int) % 5 == 0)
    a = line * np.where(major, 0.42, 0.20) * calm
    add(img, np.where(major[..., None], TEAL * 1.25, TEAL), a)
    # repeaters on the hilltops, linked by faint dashed paths
    nodes = []
    for px, py, pr in peaks:
        nodes.append((px * W, py * H))
    lay = Image.new("L", (W, H), 0)
    dl = ImageDraw.Draw(lay)
    order = [0, 1, 3, 4, 2, 0]
    for i in range(len(order) - 1):
        (x0, y0), (x1, y1) = nodes[order[i]], nodes[order[i + 1]]
        n = int(math.hypot(x1 - x0, y1 - y0) / (7 * s))
        for k in range(n):
            if k % 2 == 0:
                t0, t1 = k / n, (k + 1) / n
                dl.line([x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1],
                        fill=255, width=max(1, int(1 * s)))
    a = np.asarray(lay, float) / 255 * 0.22 * calm
    add(img, CYAN, a)
    pts = [(x, y, 2.2 * s, CYAN if i % 2 == 0 else GREEN) for i, (x, y) in enumerate(nodes)]
    col, a = glow_dots(W, H, pts, rng, s)
    add(img, col, a * 0.95)
    return img


def poisson(W, H, r, k, rng, reject):
    pts = []
    tries = 0
    while len(pts) < 400 and tries < 20000:
        tries += 1
        p = (rng.random() * W, rng.random() * H)
        if reject(*p):
            continue
        if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 > r * r for q in pts):
            pts.append(p)
    return pts


def constellation(W, H, s, rng, logo_cx, logo_cy, title_y):
    img, xx, yy = base(W, H, logo_cx, logo_cy)
    calm = calm_mask(xx, yy, W, H, logo_cy, title_y)

    def reject(x, y):
        return math.hypot((x - logo_cx) / (W * 0.36), (y - logo_cy) / (H * 0.21)) < 1 or \
            (abs(y - title_y) < H * 0.10 and abs(x - W / 2) < W * 0.47)

    pts = poisson(W, H, 40 * s, 30, rng, reject)
    lay = Image.new("L", (W, H), 0)
    dl = ImageDraw.Draw(lay)
    seen = set()
    for i, p in enumerate(pts):
        dists = sorted(((math.hypot(p[0] - q[0], p[1] - q[1]), j) for j, q in enumerate(pts) if j != i))
        for dist, j in dists[:3]:
            key = (min(i, j), max(i, j))
            if key in seen or dist > 70 * s:
                continue
            seen.add(key)
            v = int(255 * max(0.25, 1 - dist / (70 * s)))
            dl.line([p, pts[j]], fill=v, width=max(1, int(0.9 * s)))
    a = np.asarray(lay, float) / 255 * 0.26 * calm
    add(img, BLUE * 0.8 + CYAN * 0.2, a)
    # faint radio ping rings around the logo
    rings = Image.new("L", (W, H), 0)
    dr = ImageDraw.Draw(rings)
    for rr in (78, 112, 150):
        R = rr * s
        dr.ellipse([logo_cx - R, logo_cy - R, logo_cx + R, logo_cy + R], outline=255, width=max(1, int(0.8 * s)))
    a = np.asarray(rings, float) / 255 * 0.07
    add(img, CYAN, a)
    pal = [CYAN, CYAN, BLUE, VIOLET, GREEN]
    dots = [(x, y, (0.9 + 1.0 * rng.random()) * s, pal[rng.integers(len(pal))]) for x, y in pts]
    col, a = glow_dots(W, H, dots, rng, s)
    add(img, col, a * (0.35 + 0.65 * calm))
    return img


def aurora(W, H, s, rng, logo_cx, logo_cy, title_y):
    img, xx, yy = base(W, H, logo_cx, logo_cy)
    calm = calm_mask(xx, yy, W, H, logo_cy, title_y)
    # fine dot grid, like map paper
    g = 16 * s
    gx = np.abs(((xx + g / 2) % g) - g / 2)
    gyy = np.abs(((yy + g / 2) % g) - g / 2)
    dot = np.clip(1 - np.hypot(gx, gyy) / (0.9 * s), 0, 1)
    add(img, np.array([0x3A, 0x4C, 0x60], float), dot * 0.55 * calm)

    # flowing ribbons: a soft band follows each wave, tinted cyan -> violet across the screen
    def ribbon(base_y, amp, freq, phase, thick, strength, c0, c1, tilt):
        u = xx / W
        center = base_y + amp * np.sin(2 * math.pi * (freq * u + phase)) + 0.35 * amp * np.sin(
            2 * math.pi * (2.3 * freq * u + phase * 1.7)) + tilt * (u - 0.5)
        dist = (yy - center) / thick
        band = np.exp(-dist * dist) * strength
        edge = np.exp(-((yy - center) / (0.9 * s)) ** 2) * strength * 1.4  # crisp bright core line
        col = c0[None, None, :] * (1 - u[..., None]) + c1[None, None, :] * u[..., None]
        add(img, col, np.clip(band + edge, 0, 1) * calm)

    ribbon(H * 0.80, 14 * s, 0.9, 0.10, 16 * s, 0.22, CYAN, VIOLET, -18 * s)
    ribbon(H * 0.86, 10 * s, 1.3, 0.55, 12 * s, 0.16, GREEN, CYAN, -10 * s)
    ribbon(H * 0.12, 9 * s, 1.1, 0.30, 11 * s, 0.14, VIOLET, BLUE, 14 * s)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("design", choices=["ridgelines", "constellation", "aurora"])
    ap.add_argument("out")
    ap.add_argument("--w", type=int, default=240)
    ap.add_argument("--h", type=int, default=320)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    s = 2  # supersample
    W, H = a.w * s, a.h * s
    rng = np.random.default_rng(a.seed)
    # logo and title where HopticsUI::createBootScreen puts them, so they sit on the calm part
    if a.w > a.h:  # landscape panels (T-Deck Plus, ThinkNode M9): smaller logo near the top
        logo_cx, logo_cy = W / 2, 66 * s
        title_y = H / 2 + 6 * s
    else:
        logo_cx, logo_cy = W / 2, H / 2 - 40 * s
        title_y = H / 2 + 62 * s
    img = {"ridgelines": ridgelines, "constellation": constellation, "aurora": aurora}[a.design](
        W, H, s, rng, logo_cx, logo_cy, title_y)
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").resize((a.w, a.h), Image.LANCZOS)
    out.save(a.out, optimize=True)
    print(f"{a.out}: {a.design} {a.w}x{a.h}")


if __name__ == "__main__":
    main()
