#!/usr/bin/env python3
"""
[QL] E135. Where bots go, fight, stall, loop and bump - by the map's own
place names - and what share of attack and carrier runs pass a given place.

    zone-report.py <map.bsp> <server log> [--via REGEX] [--from S] [--to S]

The log is a dedicated server's console with bot_debugTrack set. Each sample
is placed at the nearest target_location (the names the team overlay shows),
and "(W)"/"(E)" says which half of the map it is in.

  per place    samples, % fighting, % stalled travelling, loops, bumps
  --via REGEX  of all ATTACK runs (a bot's unbroken stretch with LTG_GETFLAG)
               and CARRIER runs (LTG_RUSHBASE), the share that passed
               through a place whose name matches - e.g. --via Garden for
               "how many flank through the gardens"

Reads the BSP and the log locally; nothing from either is written anywhere.
"""
import collections
import importlib.util
import math
import pathlib
import re
import sys

_here = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("rt", _here / "render-tracks.py")
rt = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rt)

LTG_GETFLAG, LTG_RUSHBASE = 4, 5


def main():
    args = sys.argv[1:]
    opt = {"--via": "", "--from": "0", "--to": "1e9"}
    pos = []
    while args:
        a = args.pop(0)
        if a in opt:
            opt[a] = args.pop(0)
        else:
            pos.append(a)
    if len(pos) != 2:
        sys.exit(__doc__)
    bsp, log = pos
    locs = [(re.sub(r"\^.", "", m), o) for c, o, m in rt.load_entities(bsp) if c == "target_location" and m]
    if not locs:
        sys.exit("no target_location entities in %s" % bsp)

    cache = {}

    def where(x, y, z):
        k = (x // 32, y // 32, z // 32)
        if k not in cache:
            b = min(locs, key=lambda l: (l[1][0] - x) ** 2 + (l[1][1] - y) ** 2 + ((l[1][2] - z) * 2) ** 2)
            cache[k] = "%s (%s)" % (b[0], "W" if b[1][0] < 0 else "E")
        return cache[k]

    tracks = rt.read_tracks(log, float(opt["--from"]), float(opt["--to"]))
    n = collections.Counter()
    fight = collections.Counter()
    stall = collections.Counter()
    travel = collections.Counter()
    loops = collections.Counter()
    runs = {LTG_GETFLAG: [0, 0], LTG_RUSHBASE: [0, 0]}
    via = re.compile(opt["--via"]) if opt["--via"] else None
    for t in tracks.values():
        pts = t["pts"]
        cur, passed = None, False
        for p in pts:
            w = where(p[1], p[2], p[3])
            n[w] += 1
            if p[5] == "f":
                fight[w] += 1
            elif p[5] == "s":
                travel[w] += 1
                if p[4] < 100:
                    stall[w] += 1
            # runs: an unbroken stretch of one job, ended by death or a job change
            job = p[6] if p[5] != "d" else None
            if job != cur:
                if cur in runs:
                    runs[cur][0] += 1
                    runs[cur][1] += passed
                cur, passed = job, False
            if via and cur in runs and via.search(w):
                passed = True
        if cur in runs:
            runs[cur][0] += 1
            runs[cur][1] += passed
        for p in rt.find_loops(pts):
            loops[where(p[1], p[2], p[3])] += 1
    bumps = collections.Counter()
    for line in open(log, errors="replace"):
        m = re.match(r"botbump (\d+) \d+ (-?\d+) (-?\d+) (-?\d+)", line)
        if m and float(opt["--from"]) <= int(m.group(1)) / 1000.0 <= float(opt["--to"]):
            bumps[where(int(m.group(2)), int(m.group(3)), int(m.group(4)))] += 1

    print("%-26s %7s %6s %7s %6s %6s" % ("place", "samples", "fight%", "stall%", "loops", "bumps"))
    for w, c in n.most_common():
        print("%-26s %7d %5.0f%% %6.0f%% %6d %6d" % (w, c, 100.0 * fight[w] / c,
              100.0 * stall[w] / max(travel[w], 1), loops[w], bumps[w]))
    print("TOTAL %d samples, %d loops, %d bumps" % (sum(n.values()), sum(loops.values()), sum(bumps.values())))
    if via:
        for job, name in ((LTG_GETFLAG, "attack"), (LTG_RUSHBASE, "carrier")):
            total, hit = runs[job]
            print("%s runs through /%s/: %d of %d (%.0f%%)" % (name, opt["--via"], hit, total,
                                                            100.0 * hit / max(total, 1)))


if __name__ == "__main__":
    main()
