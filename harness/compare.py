#!/usr/bin/env python3
"""Compare two folders of harness screenshots pixel by pixel.

Usage: compare.py <before dir> <after dir> [diff dir]

Prints every screen that changed with the number of differing pixels and the box they sit in, and
lists screens that only exist on one side. With a diff dir it also writes <name>.png for each
changed screen: before | after | changed pixels in magenta. Exit code 1 when anything differs.
"""
import os
import sys

from PIL import Image, ImageChops


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    a, b = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else None
    if out:
        os.makedirs(out, exist_ok=True)
    na = {f for f in os.listdir(a) if f.endswith(".ppm")}
    nb = {f for f in os.listdir(b) if f.endswith(".ppm")}
    changed = 0
    for f in sorted(na & nb):
        ia, ib = Image.open(os.path.join(a, f)).convert("RGB"), Image.open(os.path.join(b, f)).convert("RGB")
        if ia.size != ib.size:
            print(f"{f}: size {ia.size} -> {ib.size}")
            changed += 1
            continue
        diff = ImageChops.difference(ia, ib).convert("L").point(lambda v: 255 if v else 0)
        box = diff.getbbox()
        if not box:
            continue
        n = sum(1 for v in diff.getdata() if v)
        print(f"{f}: {n} pixels differ in {box}")
        changed += 1
        if out:
            w, h = ia.size
            sheet = Image.new("RGB", (w * 3 + 8, h), (40, 40, 40))
            sheet.paste(ia, (0, 0))
            sheet.paste(ib, (w + 4, 0))
            mark = ia.copy()
            mark.paste((255, 0, 255), mask=diff)
            sheet.paste(mark, (2 * w + 8, 0))
            sheet.save(os.path.join(out, f[:-4] + ".png"))
    for f in sorted(na - nb):
        print(f"{f}: only in {a}")
    for f in sorted(nb - na):
        print(f"{f}: only in {b}")
    print(f"{len(na & nb)} compared, {changed} changed, {len(na ^ nb)} on one side only")
    return 1 if changed or (na ^ nb) else 0


if __name__ == "__main__":
    sys.exit(main())
