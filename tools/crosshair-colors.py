#!/usr/bin/env python3
"""
[QL] E131. Samples cg_crosshairColor's 26 colours from Quake Live's colour bar.

    python3 tools/crosshair-colors.py <path to menu/art/fx_base.png from pak00>

Prints the table that goes in code/cgame/cg_crosshaircolors.h. The
image is in pak00 and is never committed; only the numbers are.

The menu's colour slider draws fx_base across its whole width and puts the
thumb for value v at (v-1)/25 of that width, so v is sampled at that column,
clamped inside the bar's border, averaged over 3 columns of the two brightest
rows. The bar is shaded, so each hue is scaled to full brightness. The grey
tail has no hue: 25 is white and 26 black, by hand (see the comment in
cg_crosshaircolors.h). Needs Pillow.
"""
import sys
from PIL import Image


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    im = Image.open(sys.argv[1]).convert("RGB")
    w, h = im.size
    mid = h // 2
    # The coloured region: the columns of the middle row that are not border.
    lit = [x for x in range(w) if sum(im.getpixel((x, mid))) > 0]
    lo, hi = lit[0] + 1, lit[-1] - 1
    for v in range(1, 27):
        c = min(max(round((v - 1) / 25 * (w - 1)), lo), hi)
        px = [im.getpixel((x, y)) for x in (c - 1, c, c + 1) for y in (mid - 1, mid)]
        r, g, b = (sum(p[i] for p in px) / len(px) for i in range(3))
        m = max(r, g, b)
        if m - min(r, g, b) < 0.15 * m:
            out = (1, 1, 1) if v == 25 else (0, 0, 0)
        else:
            out = (r / m, g / m, b / m)
        print("\t{ %.2ff, %.2ff, %.2ff },\t// %d" % (out + (v,)))


if __name__ == "__main__":
    main()
