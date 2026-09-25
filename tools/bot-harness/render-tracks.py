#!/usr/bin/env python3
"""
[QL] E133. An overhead picture of a map with the bots' movement drawn on it.

    render-tracks.py <map.aas|map.bsp> <server log> <out.png> [--from S] [--to S] [--scale U]

The log is a dedicated server's console output with bot_debugTrack set (lines
"bottrack <ms> <client> <team> <x> <y> <z> <speed> <state> <ltg> <goalarea>
<name>").

The floor is best taken from the map's AAS: every grounded area's footprint,
drawn at its floor height, lowest first and shaded by height - exactly the
space the bots navigate, on every level. It works on a shipped, optimized AAS
(Quake Live's keep areas and reachabilities but no face geometry). Given a BSP
instead, its upward-facing drawn triangles are used, which shows roofs over
rooms.

Drawn on top:
  paths    a line per bot, red or blue by team, faint so traffic reads as
           brightness: a corridor everybody uses glows
  stalls   yellow dot wherever a bot that was TRAVELLING (not fighting) was
           under 100 ups - a doorway every bot stalls in shows up as a smear
  bumps    cyan dot where a bot walked into another player's body
           ("botbump" lines, from the same bot_debugTrack)
  loops    magenta ring where a bot came back within 96 units of a point it
           left 4-10 s earlier, having gone 256+ away, on the same goal -
           the "goes in circles till it has a path" case. Defending, camping
           and patrolling are left out; they circle a spot by design.

It also writes <out>-stalls.png, the stall dots alone on the map, which is the
view to read first. Needs Pillow. The BSP is read locally and never copied.
"""
import math
import re
import struct
import sys
from PIL import Image, ImageDraw

LUMP_SHADERS, LUMP_DRAWVERTS, LUMP_DRAWINDEXES, LUMP_SURFACES = 1, 10, 11, 13
SURF_SKY, SURF_NODRAW = 0x4, 0x80


def load_bsp(path):
    d = open(path, "rb").read()
    if d[:4] != b"IBSP":
        sys.exit("not a BSP: %s" % path)

    def lump(n):
        off, ln = struct.unpack_from("<ii", d, 8 + 8 * n)
        return d[off:off + ln]

    shaders = []
    sh = lump(LUMP_SHADERS)
    for i in range(len(sh) // 72):
        name = sh[i * 72:i * 72 + 64].split(b"\0")[0].decode("latin1")
        sflags, cflags = struct.unpack_from("<ii", sh, i * 72 + 64)
        shaders.append((name, sflags, cflags))
    vb = lump(LUMP_DRAWVERTS)
    verts = [struct.unpack_from("<3f", vb, i * 44) for i in range(len(vb) // 44)]
    ib = lump(LUMP_DRAWINDEXES)
    idx = struct.unpack("<%di" % (len(ib) // 4), ib)
    sb = lump(LUMP_SURFACES)
    tris = []
    for i in range(len(sb) // 104):
        f = struct.unpack_from("<12i", sb, i * 104)
        shader, _, stype, fv, nv, fi, ni = f[:7]
        pw, ph = struct.unpack_from("<2i", sb, i * 104 + 96)
        name, sflags, cflags = shaders[shader] if 0 <= shader < len(shaders) else ("", 0, 0)
        if sflags & (SURF_SKY | SURF_NODRAW) or not (cflags & 1):  # solid only
            continue
        if stype in (1, 3):
            for k in range(0, ni, 3):
                tris.append(tuple(verts[fv + idx[fi + k + j]] for j in range(3)))
        elif stype == 2 and pw > 1 and ph > 1:  # patch: its control grid, as quads
            for y in range(ph - 1):
                for x in range(pw - 1):
                    a, b = verts[fv + y * pw + x], verts[fv + y * pw + x + 1]
                    c, e = verts[fv + (y + 1) * pw + x], verts[fv + (y + 1) * pw + x + 1]
                    tris += [(a, b, e), (a, e, c)]
    return tris


def load_aas_floors(path):
    """Ground faces of an AAS, as polygons. The header after ident/version is
    XORed with (i * 119) - AAS_DData in be_aas_file.c."""
    d = bytearray(open(path, "rb").read())
    if d[:4] != b"EAAS":
        sys.exit("not an AAS: %s" % path)
    for i in range(8, 12 + 14 * 8):
        d[i] ^= ((i - 8) & 0xff) * 119 & 0xff

    def lump(n):
        off, ln = struct.unpack_from("<ii", d, 12 + 8 * n)
        return bytes(d[off:off + ln])
    ab, sb = lump(7), lump(8)
    # aas_area_t: areanum, numfaces, firstface, mins[3], maxs[3], center[3]
    # aas_areasettings_t: contents, areaflags, presencetype, cluster, ...
    # Every grounded area's footprint at its floor height. Areas are convex and
    # mostly axis-aligned, so the box is close to the real outline, and this
    # works on an optimized AAS - which has areas but no faces - so the shipped
    # file, the one the bots actually use, is the one drawn.
    polys = []
    for i in range(1, len(ab) // 48):
        a = struct.unpack_from("<3i9f", ab, i * 48)
        flags = struct.unpack_from("<7i", sb, i * 28)[1]
        if not flags & 1:  # AREA_GROUNDED
            continue
        x0, y0, z0, x1, y1 = a[3], a[4], a[5], a[6], a[7]
        polys.append([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)])
    return polys


def floors(tris):
    out = []
    for a, b, c in tris:
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        ln = math.sqrt(sum(x * x for x in n)) or 1
        if abs(n[2] / ln) > 0.5:  # walkable-ish, either winding
            out.append((a, b, c))
    return out


def read_tracks(path, t0, t1):
    tr = {}
    for line in open(path, errors="replace"):
        m = re.match(r"bottrack (\d+) (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) (\w) (\d+) (\d+) (.*)", line)
        if not m:
            continue
        ms = int(m.group(1)) / 1000.0
        if ms < t0 or ms > t1:
            continue
        cl = int(m.group(2))
        tr.setdefault(cl, {"team": int(m.group(3)), "name": re.sub(r"\^.", "", m.group(11).strip()), "pts": []})
        tr[cl]["pts"].append((ms, int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7)),
                              m.group(8), int(m.group(9)), int(m.group(10))))
    return tr


ROAMING_JOBS = (3, 7, 8, 9)  # LTG_DEFENDKEYAREA, CAMP, CAMPORDER, PATROL: circling on purpose


def find_loops(pts):
    loops = []
    for i, p in enumerate(pts):
        if p[5] != "s" or p[6] in ROAMING_JOBS:
            continue
        for j in range(i - 1, -1, -1):
            q = pts[j]
            if q[5] != "s" or (q[6], q[7]) != (p[6], p[7]):
                break
            dt = p[0] - q[0]
            if dt < 4:
                continue
            if dt > 10:
                break
            if math.hypot(p[1] - q[1], p[2] - q[2]) <= 96 and \
               any(math.hypot(r[1] - q[1], r[2] - q[2]) > 256 for r in pts[j:i]):
                loops.append(p)
                break
    return loops


def main():
    args = sys.argv[1:]
    opt = {"--from": 0.0, "--to": 1e9, "--scale": 0.0}
    pos = []
    while args:
        a = args.pop(0)
        if a in opt:
            opt[a] = float(args.pop(0))
        else:
            pos.append(a)
    if len(pos) != 3:
        sys.exit(__doc__)
    bsp, log, out = pos
    fl = load_aas_floors(bsp) if bsp.lower().endswith(".aas") else floors(load_bsp(bsp))
    xs = [v[0] for t in fl for v in t]
    ys = [v[1] for t in fl for v in t]
    zs = [v[2] for t in fl for v in t]
    x0, x1, y0, y1, z0, z1 = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    scale = opt["--scale"] or max(x1 - x0, y1 - y0) / 1800.0  # units per pixel
    W, H = int((x1 - x0) / scale) + 20, int((y1 - y0) / scale) + 20

    def px(x, y):
        return (10 + (x - x0) / scale, 10 + (y1 - y) / scale)

    base = Image.new("RGB", (W, H), (12, 12, 16))
    d = ImageDraw.Draw(base)
    for t in sorted(fl, key=lambda t: max(v[2] for v in t)):
        z = sum(v[2] for v in t) / len(t)
        g = int(50 + 150 * (z - z0) / max(1, z1 - z0))
        d.polygon([px(v[0], v[1]) for v in t], fill=(g, g, int(g * 1.05)))

    tracks = read_tracks(log, opt["--from"], opt["--to"])
    img = base.copy()
    lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(lay)
    stalls_img = base.copy()
    sd = ImageDraw.Draw(stalls_img)
    nstall = nloop = 0
    for cl, t in tracks.items():
        col = (255, 60, 60, 70) if t["team"] == 1 else (70, 130, 255, 70)
        pts = t["pts"]
        for a, b in zip(pts, pts[1:]):
            # a respawn or teleport is a jump, not a path
            if a[5] == "d" or b[5] == "d" or math.hypot(a[1] - b[1], a[2] - b[2]) > 400:
                continue
            ld.line([px(a[1], a[2]), px(b[1], b[2])], fill=col, width=2)
        for p in pts:
            if p[5] == "s" and p[4] < 100:
                nstall += 1
                x, y = px(p[1], p[2])
                ld.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 230, 40, 160))
                sd.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 230, 40))
        for p in find_loops(pts):
            nloop += 1
            x, y = px(p[1], p[2])
            ld.ellipse([x - 9, y - 9, x + 9, y + 9], outline=(255, 40, 255, 255), width=3)
    nbump = 0
    for line in open(log, errors="replace"):
        m = re.match(r"botbump (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+)", line)
        if m and opt["--from"] <= int(m.group(1)) / 1000.0 <= opt["--to"]:
            nbump += 1
            x, y = px(int(m.group(3)), int(m.group(4)))
            ld.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(40, 240, 255, 170))
    img.paste(lay, (0, 0), lay)
    img.save(out)
    stalls_out = re.sub(r"(\.\w+)?$", r"-stalls\1", out, count=1)
    stalls_img.save(stalls_out)
    samples = sum(len(t["pts"]) for t in tracks.values())
    print("render-tracks: %d bots, %d samples, %d stalled, %d loops, %d bumps -> %s, %s  (%.1f units/pixel)"
          % (len(tracks), samples, nstall, nloop, nbump, out, stalls_out, scale))


if __name__ == "__main__":
    main()
