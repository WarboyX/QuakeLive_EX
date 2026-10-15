#!/usr/bin/env python3
"""Which carrier routes exist, which are refused and why, which win - by place.

usage: route-candidates.py MAP.bsp RUN_DIR [RUN_DIR ...]

[QL] E231. Reads the "routepick" lines bot_debugRoutes prints (one per carrier
route decision) inside each run's recorded live window, names every candidate
waypoint by the nearest target_location, and counts per place and team:

  offered   decisions where the waypoint was a candidate at all
  ok        candidates with a valid cost (eligible)
  U/A/B/L   refused: unreachable / already there / not closer to home / too long
  picked    decisions that chose it
  beat dir  of its eligible offers, how often its cost was below going straight home

and how often the carrier went straight home. Answers the question the R1
report left open: is a route (the gardens) absent, refused, or outscored.
"""
import os, re, struct, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import livewindow


def location_name(msg):
    """[QL] E231: strip colour codes, but keep the side they encode - japanesecastles
    names both gardens "Garden", red (^1) and blue (^4)."""
    name = re.sub(r'\^.', '', msg).strip()
    side = 'Red ' if msg.startswith('^1') else 'Blue ' if msg.startswith('^4') else ''
    return name if not side or side.strip() in name else side + name

bsp, runs = sys.argv[1], sys.argv[2:]
d = open(bsp, 'rb').read()
o, l = struct.unpack('<ii', d[8:16])
locs = []
for blk in re.findall(r'\{([^}]*)\}', d[o:o + l].decode('latin1')):
    cn = re.search(r'"classname"\s+"([^"]+)"', blk); og = re.search(r'"origin"\s+"([^"]+)"', blk)
    ms = re.search(r'"message"\s+"([^"]+)"', blk)
    if cn and og and ms and cn.group(1) == 'target_location':
        locs.append((tuple(float(v) for v in og.group(1).split()), location_name(ms.group(1))))

def place(xyz):
    return min(locs, key=lambda L: sum((a - b) ** 2 for a, b in zip(L[0], xyz)))[1] if locs else '?'

rx = re.compile(r'routepick \d+ \d+ (\d) direct (-?\d+) (-?\d+) (-?\d+) pick (\d+) (-?\d+) \|(.*)')
stats = {1: defaultdict(lambda: defaultdict(int)), 2: defaultdict(lambda: defaultdict(int))}
decisions = {1: 0, 2: 0}; direct = {1: 0, 2: 0}; off = 0
cache = {}
for run in runs:
    for _, line in livewindow.lines(run):
        if 'routepick' not in line:
            continue
        if line.rstrip().endswith(' off'):
            off += 1; continue
        m = rx.search(line)
        if not m:
            continue
        team, dcost, pick = int(m.group(1)), int(m.group(4)), int(m.group(5))
        decisions[team] += 1
        if pick == 0:
            direct[team] += 1
        for cand in m.group(7).split():
            f = cand.split(':')
            if len(f) < 5:
                continue
            key = (f[1], f[2], f[3])
            if key not in cache:
                cache[key] = place((float(f[1]), float(f[2]), float(f[3])))
            name = cache[key]; s = stats[team][name]
            s['offered'] += 1
            if f[4] in ('U', 'A', 'B', 'L'):
                s[f[4]] += 1
            else:
                s['ok'] += 1
                if int(f[4]) < dcost:
                    s['beat'] += 1
            if f[0] == str(pick):
                s['picked'] += 1

for team in (1, 2):
    n = decisions[team]
    if not n:
        continue
    print(f"\n{'red' if team == 1 else 'blue'} carriers: {n} route decisions, straight home {direct[team]} ({direct[team] / n:.0%})")
    print(f"  {'waypoint place':26} {'offered':>8} {'ok':>6} {'U':>5} {'A':>5} {'B':>6} {'L':>6} {'beat dir':>9} {'picked':>7}")
    for name, s in sorted(stats[team].items(), key=lambda kv: -kv[1]['picked'] * 1000 - kv[1]['offered']):
        print(f"  {name:26} {s['offered']:8d} {s['ok']:6d} {s['U']:5d} {s['A']:5d} {s['B']:6d} {s['L']:6d} "
              f"{s['beat']:9d} {s['picked']:7d}")
if off:
    print(f"\n{off} decisions with detours switched off (bot_ctfDetours)")
