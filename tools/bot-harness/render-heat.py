#!/usr/bin/env python3
"""
[QL] E136. Heat maps over an overhead view of the map, from any number of
server logs - where bots are, where they die, where they kill from.

    render-heat.py <map.aas> <out.png> <log> [log ...] [options]

  --what presence   every bottrack sample of a living bot (default)
         deaths     where each botkill victim fell (telefrags and team
                    changes left out, here and below)
         kills      where each killer stood (world and suicides left out)
         carrier    where flag carriers fell
  --team 1|2        only this team (the victim's for deaths/carrier, the
                    killer's for kills, the bot's for presence)
  --bsp <map.bsp>   the map's own place names and the two flags on top
  --title TEXT      a caption line
  --scale N         units per pixel (default: the map fits 1800 pixels)
  --blur N          smoothing radius in pixels (default 6)

The logs come from a dedicated server with bot_debugTrack set: bottrack lines
for presence, botkill lines (g_combat.c) for the rest. The colour scale is the
square root of the density, normalised to its 99.5th percentile, so one hot spot
does not wash the rest of the map out; the legend gives the sample count.

Needs PIL and numpy. Reads the map and the logs locally and writes nothing but
the picture.
"""
import importlib.util
import pathlib
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

_spec = importlib.util.spec_from_file_location("rt", pathlib.Path(__file__).resolve().parent / "render-tracks.py")
rt = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rt)

TRACK = re.compile(r"bottrack \d+ \d+ (\d+) (-?\d+) (-?\d+) -?\d+ \d+ (\w)")
KILL = re.compile(r"botkill \d+ (\d+) (\d+) (\d+) (-?\d+) (-?\d+) -?\d+ (-?\d+) (-?\d+) -?\d+ (\d+) (\d+) (\d+)")

# MOD_TELEFRAG and MOD_SWITCH_TEAMS (bg_public.h): a spawn landing on someone
# and a team change are not fights, and at 64 players the telefrags alone put
# a hot spot on every spawn pad
NOT_FIGHTING = (18, 29)

# dark blue -> cyan -> green -> yellow -> red -> white
STOPS = [(0.0, (20, 30, 120)), (0.25, (0, 170, 230)), (0.45, (40, 210, 90)),
         (0.65, (250, 230, 40)), (0.85, (240, 60, 30)), (1.0, (255, 255, 255))]


def colour(v):
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if v <= b:
            f = (v - a) / (b - a)
            return tuple(int(ca[i] + (cb[i] - ca[i]) * f) for i in range(3))
    return STOPS[-1][1]


def points(logs, what, team):
    for log in logs:
        for line in open(log, errors="replace"):
            if what == "presence":
                m = TRACK.match(line)
                if m and m.group(4) != "d" and (not team or int(m.group(1)) == team):
                    yield int(m.group(2)), int(m.group(3))
                continue
            m = KILL.match(line)
            if not m:
                continue
            killer, victim, mod = int(m.group(1)), int(m.group(2)), int(m.group(3))
            kteam, vteam, flag = int(m.group(8)), int(m.group(9)), int(m.group(10))
            if mod in NOT_FIGHTING:
                continue
            if what == "deaths" and (not team or vteam == team):
                yield int(m.group(4)), int(m.group(5))
            elif what == "carrier" and flag and (not team or vteam == team):
                yield int(m.group(4)), int(m.group(5))
            elif what == "kills" and killer != victim and killer < 64 and (not team or kteam == team):
                yield int(m.group(6)), int(m.group(7))


def blur(a, r):
    # three box passes each way: close to a gaussian, and numpy alone
    for _ in range(3):
        for axis in (0, 1):
            c = np.cumsum(np.pad(a, [(r + 1, r) if i == axis else (0, 0) for i in (0, 1)]), axis=axis)
            a = (np.take(c, range(2 * r + 1, c.shape[axis]), axis=axis) -
                 np.take(c, range(0, c.shape[axis] - 2 * r - 1), axis=axis)) / (2 * r + 1)
    return a


def main():
    args = sys.argv[1:]
    opt = {"--what": "presence", "--team": "0", "--bsp": "", "--title": "", "--scale": "0", "--blur": "6"}
    pos = []
    while args:
        a = args.pop(0)
        if a in opt:
            opt[a] = args.pop(0)
        else:
            pos.append(a)
    if len(pos) < 3 or opt["--what"] not in ("presence", "deaths", "kills", "carrier"):
        sys.exit(__doc__)
    aas, out, logs = pos[0], pos[1], pos[2:]
    fl = rt.load_aas_floors(aas)
    xs = [v[0] for t in fl for v in t]
    ys = [v[1] for t in fl for v in t]
    zs = [v[2] for t in fl for v in t]
    x0, x1, y0, y1, z0, z1 = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    scale = float(opt["--scale"]) or max(x1 - x0, y1 - y0) / 1800.0
    W, H = int((x1 - x0) / scale) + 20, int((y1 - y0) / scale) + 20

    def px(x, y):
        return (10 + (x - x0) / scale, 10 + (y1 - y) / scale)

    img = Image.new("RGB", (W, H), (12, 12, 16))
    d = ImageDraw.Draw(img)
    for t in sorted(fl, key=lambda t: max(v[2] for v in t)):
        z = sum(v[2] for v in t) / len(t)
        g = int(40 + 90 * (z - z0) / max(1, z1 - z0))
        d.polygon([px(v[0], v[1]) for v in t], fill=(g, g, int(g * 1.05)))

    grid = np.zeros((H, W))
    n = 0
    for x, y in points(logs, opt["--what"], int(opt["--team"])):
        u, v = px(x, y)
        if 0 <= int(u) < W and 0 <= int(v) < H:
            grid[int(v), int(u)] += 1
            n += 1
    heat = blur(grid, int(opt["--blur"]))
    top = np.percentile(heat[heat > 0], 99.5) if n else 1
    heat = np.sqrt(np.clip(heat / top, 0, 1))
    lut = np.array([colour(i / 255.0) for i in range(256)], dtype=np.uint8)
    rgb = lut[(heat * 255).astype(int)]
    alpha = (np.clip(heat * 1.6, 0, 0.85) * 255).astype(np.uint8)
    alpha[heat < 0.04] = 0
    layer = Image.fromarray(np.dstack([rgb, alpha]), "RGBA")
    img.paste(layer, (0, 0), layer)

    try:
        font = ImageFont.truetype("DejaVuSans-Bold.ttf", 15)
        big = ImageFont.truetype("DejaVuSans-Bold.ttf", 22)
    except OSError:
        font = big = ImageFont.load_default()
    d = ImageDraw.Draw(img)
    if opt["--bsp"]:
        for cls, o, msg in rt.load_entities(opt["--bsp"]):
            x, y = px(o[0], o[1])
            if cls == "target_location" and msg:
                label = re.sub(r"\^.", "", msg)
                d.text((x + 1, y + 1), label, fill=(0, 0, 0), font=font, anchor="mm")
                d.text((x, y), label, fill=(255, 255, 200), font=font, anchor="mm")
            elif cls in ("team_CTF_redflag", "team_CTF_blueflag"):
                c = (255, 40, 40) if "red" in cls else (60, 120, 255)
                d.rectangle([x - 7, y - 7, x + 7, y + 7], fill=c, outline=(255, 255, 255), width=2)
    # caption and legend
    cap = opt["--title"] or "%s, %d log(s)" % (opt["--what"], len(logs))
    d.text((16, 14), cap, fill=(240, 240, 240), font=big)
    d.text((16, 44), "%d samples - colour is sqrt(density), 99.5th percentile = white" % n,
           fill=(200, 200, 200), font=font)
    for i in range(256):
        d.line([(16 + i, 70), (16 + i, 84)], fill=colour(i / 255.0))
    d.text((16, 88), "none", fill=(200, 200, 200), font=font)
    d.text((272, 88), "most", fill=(200, 200, 200), font=font, anchor="ra")
    img.save(out)
    print("render-heat: %s %d samples from %d log(s) -> %s" % (opt["--what"], n, len(logs), out))


if __name__ == "__main__":
    main()
