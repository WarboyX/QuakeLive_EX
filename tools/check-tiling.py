#!/usr/bin/env python3
"""[QL] Does a texture tile? Measure the wrap seam against the sheet's own grain.

A tiling texture has nothing special about the edge between the last column and
the first: stepping off the right edge and back onto the left should be as
ordinary a step as any other. So that is what this measures - the mean absolute
difference across the wrap, over the mean absolute difference of a typical
interior column step. A ratio near 1 means the seam is invisible; a large ratio
means there is a line on the wall exactly one texture apart, forever.

This is worth a tool rather than an eye because the failure is periodic: at a
distance the seams line up into a grid and read as architecture, which is the
last thing you would suspect the texture of. And because the arithmetic answers
in a second what a rebuild-and-look round costs in minutes.

Usage:  tools/check-tiling.py [files...]      (default: content/testmaps/textures/*.tga)

Exits non-zero if any sheet's seam is more than THRESHOLD times its interior,
so it can gate a build. Sheets that are not meant to tile in one axis - a
staircase ramps and then has to fall back - are listed in NOT_TILING with the
axis they are excused on.
"""
import glob
import os
import struct
import sys

THRESHOLD = 1.5

# name -> axes that are deliberately discontinuous. 'y' means the sheet is not
# meant to tile vertically.
NOT_TILING = {
    "steps": "y",   # eight terraces climbing; the top step cannot meet the bottom
}


def load_tga(path):
    """Uncompressed 24/32-bit TGA, bottom-up, BGR(A). Only what this tree writes."""
    with open(path, "rb") as f:
        data = f.read()
    idlen, cmaptype, imgtype = data[0], data[1], data[2]
    w, h, bpp, desc = struct.unpack("<HHBB", data[12:18])
    if cmaptype != 0 or imgtype != 2 or bpp not in (24, 32):
        raise SystemExit("%s: not an uncompressed truecolour TGA" % path)
    px = bpp // 8
    off = 18 + idlen
    rows = []
    for y in range(h):
        start = off + y * w * px
        rows.append(data[start:start + w * px])
    if not (desc & 0x20):
        rows.reverse()
    return w, h, px, rows


def col(rows, x, px, n):
    """Channel values of column x, first n channels per texel."""
    return [rows[y][x * px + c] for y in range(len(rows)) for c in range(n)]


def row(rows, y, px, n):
    r = rows[y]
    return [r[x * px + c] for x in range(len(r) // px) for c in range(n)]


def mad(a, b):
    return sum(abs(p - q) for p, q in zip(a, b)) / float(len(a) or 1)


def measure(path):
    w, h, px, rows = load_tga(path)
    n = 3                                    # RGB only: alpha is height, not colour

    seam_x = mad(col(rows, w - 1, px, n), col(rows, 0, px, n))
    seam_y = mad(row(rows, h - 1, px, n), row(rows, 0, px, n))

    # The comparison is a HIGH PERCENTILE of the interior steps, not the median.
    # A median says what a step across a brick face costs, and brick's wrap
    # column lands on a mortar joint, so against the median every masonry
    # texture ever made fails. What actually matters is whether the wrap is a
    # KIND of edge the sheet does not otherwise contain: brick's seam measures
    # 14.4 and its own mortar joints measure 14.6, so it tiles, while a
    # non-wrapping noise lattice puts a step at the wrap that is larger than
    # anything else in the sheet.
    dx = sorted(mad(col(rows, x, px, n), col(rows, x + 1, px, n)) for x in range(w - 1))
    dy = sorted(mad(row(rows, y, px, n), row(rows, y + 1, px, n)) for y in range(h - 1))
    typ_x = dx[int(len(dx) * 0.99)]
    typ_y = dy[int(len(dy) * 0.99)]
    return seam_x, seam_y, typ_x, typ_y


def main(argv):
    files = argv[1:]
    if not files:
        here = os.path.dirname(os.path.abspath(__file__))
        pat = os.path.join(here, "..", "content", "testmaps", "textures", "*.tga")
        files = sorted(glob.glob(pat))
    if not files:
        print("check-tiling: no textures found", file=sys.stderr)
        return 1

    print("%-20s %9s %9s   %9s %9s" % ("texture", "seam x", "seam y", "p99 x", "p99 y"))
    bad = []
    for path in files:
        name = os.path.basename(path)
        stem = name.rsplit(".", 1)[0]
        material = stem.rsplit("_", 1)[0] if stem.endswith(("_d", "_n")) else stem
        excused = NOT_TILING.get(material, "")
        sx, sy, tx, ty = measure(path)
        flags = []
        # A floor of 0.5 keeps a perfectly smooth sheet from failing on a ratio
        # of two near-zero numbers.
        if "x" not in excused and sx > max(tx, 0.5) * THRESHOLD:
            flags.append("x")
        if "y" not in excused and sy > max(ty, 0.5) * THRESHOLD:
            flags.append("y")
        mark = ""
        if flags:
            mark = "   <-- SEAM in " + "".join(flags)
            bad.append((name, "".join(flags)))
        elif excused:
            mark = "   (%s excused)" % excused
        print("%-20s %9.2f %9.2f   %9.2f %9.2f%s" % (name, sx, sy, tx, ty, mark))

    if bad:
        print("\ncheck-tiling: %d sheet(s) do not tile" % len(bad), file=sys.stderr)
        return 1
    print("\ncheck-tiling: all %d sheet(s) tile" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
