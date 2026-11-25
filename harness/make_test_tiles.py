#!/usr/bin/env python3
"""Make stand-in map tiles for the PC harness (no internet there).

They copy OpenStreetMap's standard style colours (land, residential, parks, water, roads with
casings, buildings, labels) so the theme recolouring can be judged, but the "town" itself is
made up. Tiles join seamlessly because everything is drawn in world (Web Mercator) pixels.
Saved as 8-bit palette PNGs, like the real ones.

Usage: make_test_tiles.py <out dir> <lat> <lon> <zmin> <zmax> [radius tiles]
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

LAND = (242, 239, 233)
RESID = (224, 223, 223)
PARK = (200, 250, 204)
FOREST = (173, 209, 158)
WATER = (170, 211, 223)
BUILD = (217, 208, 201)
BUILD_LINE = (196, 182, 171)
MINOR = (255, 255, 255)
MINOR_CASE = (187, 187, 187)
PRIMARY = (252, 214, 164)
PRIMARY_CASE = (160, 107, 0)
MOTORWAY = (232, 146, 162)
MOTORWAY_CASE = (220, 42, 103)
TEXT = (51, 51, 51)

SS = 2  # supersampling


def world_px(lat, lon, z):
    n = 256 * (1 << z)
    s = math.sin(math.radians(lat))
    return (lon + 180) / 360 * n, (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n


def m_per_px(lat, z):
    return 156543.03392 * math.cos(math.radians(lat)) / (1 << z)


def font(px):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, px)
    return ImageFont.load_default()


def draw_tile(lat0, lon0, z, tx, ty):
    S = 256 * SS
    img = Image.new("RGB", (S, S), LAND)
    d = ImageDraw.Draw(img)
    ox, oy = tx * 256, ty * 256
    cx, cy = world_px(lat0, lon0, z)
    mpp = m_per_px(lat0, z)

    def P(mx, my):  # metres east/south of the town centre -> tile pixels (supersampled)
        return ((cx + mx / mpp - ox) * SS, (cy + my / mpp - oy) * SS)

    def W(m, minpx):  # width in metres, at least minpx screen px
        return max(minpx, m / mpp) * SS

    rnd = random.Random(42)
    # residential blocks and parks on a 400 m grid around town
    if z >= 11:
        for gx in range(-12, 12):
            for gy in range(-12, 12):
                r = random.Random(gx * 1000 + gy)
                kind = r.random()
                x0, y0 = gx * 400 + 20, gy * 400 + 20
                col = PARK if kind < 0.12 else (FOREST if kind < 0.18 else RESID)
                d.rectangle([P(x0, y0), P(x0 + 360, y0 + 360)], fill=col)
    # a lake and a river
    lx, ly = P(-1800, 900)
    rr = 700 / mpp * SS
    d.ellipse([lx - rr, ly - rr * 0.7, lx + rr, ly + rr * 0.7], fill=WATER)
    pts = [P(-6000 + i * 300, 2600 + 500 * math.sin(i / 3.0)) for i in range(41)]
    d.line(pts, fill=WATER, width=int(W(60, 2)))
    # buildings
    if z >= 15:
        for gx in range(-30, 30):
            for gy in range(-30, 30):
                r = random.Random(gx * 7919 + gy)
                if r.random() < 0.55:
                    bx, by = gx * 100 + 25 + r.random() * 20, gy * 100 + 25 + r.random() * 20
                    d.rectangle([P(bx, by), P(bx + 22 + r.random() * 14, by + 16 + r.random() * 12)], fill=BUILD,
                                outline=BUILD_LINE, width=SS)
    # minor street grid every 100 m
    if z >= 14:
        for i in range(-30, 31):
            for a, b in ((P(i * 100, -3000), P(i * 100, 3000)), (P(-3000, i * 100), P(3000, i * 100))):
                d.line([a, b], fill=MINOR_CASE, width=int(W(9, 3) + 2 * SS))
        for i in range(-30, 31):
            for a, b in ((P(i * 100, -3000), P(i * 100, 3000)), (P(-3000, i * 100), P(3000, i * 100))):
                d.line([a, b], fill=MINOR, width=int(W(9, 3)))
    # primary roads every 1.6 km, motorway diagonal
    if z >= 9:
        for i in range(-8, 9):
            for a, b in ((P(i * 1600, -40000), P(i * 1600, 40000)), (P(-40000, i * 1600), P(40000, i * 1600))):
                d.line([a, b], fill=PRIMARY_CASE, width=int(W(16, 2 if z < 12 else 5) + 2 * SS))
                d.line([a, b], fill=PRIMARY, width=int(W(16, 2 if z < 12 else 5)))
    m = [P(-60000, -30000), P(60000, 30000)]
    d.line(m, fill=MOTORWAY_CASE, width=int(W(26, 3 if z < 12 else 7) + 2 * SS))
    d.line(m, fill=MOTORWAY, width=int(W(26, 3 if z < 12 else 7)))
    # labels with white halos
    if z >= 13:
        f = font(11 * SS)
        for i in range(-8, 9, 2):
            x, y = P(i * 1600 + 40, -700)
            d.text((x, y), f"{['Oak','Elm','Main','Hazel','Pine','Madison','Central','Greenback','Cedar'][abs(i)//2 % 9]} Ave",
                   font=f, fill=TEXT, stroke_width=2 * SS, stroke_fill=(255, 255, 255))
    # town names only once they are far enough apart not to pile up on the nodes
    if 12 <= z <= 14:
        f = font(13 * SS)
        for name, (mx, my) in (("Riverbend", (0, 0)), ("Lakeside", (-2600, 1800)), ("Northgate", (3500, -4200))):
            x, y = P(mx, my)
            d.text((x, y), name, font=f, fill=(0, 0, 0), stroke_width=2 * SS, stroke_fill=(255, 255, 255))
    img = img.resize((256, 256), Image.LANCZOS)
    return img.quantize(colors=128, method=Image.MEDIANCUT, dither=Image.NONE)


def main():
    out, lat, lon, zmin, zmax = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    radius = int(sys.argv[6]) if len(sys.argv) > 6 else 2
    n = 0
    for z in range(zmin, zmax + 1):
        wx, wy = world_px(lat, lon, z)
        ctx, cty = int(wx // 256), int(wy // 256)
        for tx in range(ctx - radius, ctx + radius + 1):
            for ty in range(cty - radius, cty + radius + 1):
                p = os.path.join(out, str(z), str(tx))
                os.makedirs(p, exist_ok=True)
                draw_tile(lat, lon, z, tx, ty).save(os.path.join(p, f"{ty}.png"), optimize=True)
                n += 1
    print(f"{n} tiles in {out}")


if __name__ == "__main__":
    main()
