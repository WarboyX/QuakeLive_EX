#!/usr/bin/env python3
"""Per-possession CTF evidence from a run: what each carrier did, one pickup at a time.

usage: ctf-possessions.py MAP.bsp RUN_DIR OUT_PREFIX "TITLE" [--json OUT.json]

[QL] E226. The E225 heat maps overlaid every carrier sample of a match, which
shows where carriers were but not in what order, so it cannot tell a loop from
two possessions that crossed. This splits the log into possessions - one bot
holding the flag from pickup to capture, death or match end - and for each:

  - its path, coloured by time (dark = pickup, bright = end), arrows along it,
    a diamond where its waypoint (via area) changed, and how it ended:
    star = capture, cross = death, square = still holding at match end;
  - survival seconds (censored when the match ended first);
  - damage taken while carrying (sum of health drops between samples);
  - progress home in AAS travel units (start minus the best reached), and
    reversals (travel home rising by 150+, about 1.5 s, over its running best);
  - waypoint changes;
  - living teammates within 600 units, averaged over its samples.

Fields come from the bottrack / ctftrack lines run.py enables (bot_debugTrack).
Writes OUT_PREFIX-possessions-<team>.png (small multiples, longest first) and,
with --json, the per-possession table. Needs numpy and Pillow, and the map's
.bsp (supplied, never committed) for the floor render shared with heatmap.py.
"""
import json, math, os, re, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

args = sys.argv[1:]
jout = None
if '--json' in args:
    k = args.index('--json'); jout = args[k + 1]; del args[k:k + 2]
bsp_path, run_dir, out_prefix, title = args[:4]

import livewindow   # [QL] E230: the recorded live window, not the whole log
log = [l for _, l in livewindow.lines(run_dir)]

bt = re.compile(r'bottrack (\d+) (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) (\w) (\d+) (\d+) (\S+)')
ct = re.compile(r'ctftrack (\d+) (\d+) hp(-?\d+) flag(\d) mate(-?\d+) via(\d+) home(\d+) node(\w+)')
cap = re.compile(r'"?(\w+)\^7 captured the (RED|BLUE) flag')

pos = {}          # client -> (t, team, x, y, z, state)
names = {}
team_of = {}
now = 0
open_poss = {}    # client -> possession dict
done = []

def close(c, how, t):
    p = open_poss.pop(c, None)
    if p:
        p['end'] = how; p['t1'] = t
        done.append(p)

for line in log:
    m = bt.search(line)
    if m:
        t, c, tm, x, y, z, sp, st, ltg, area, name = m.groups()
        t, c, tm = int(t), int(c), int(tm)
        now = max(now, t)
        names[name] = c; team_of[c] = tm
        pos[c] = (t, tm, float(x), float(y), float(z), st)
        if st == 'd' and c in open_poss:
            close(c, 'death', t)
        continue
    m = ct.search(line)
    if m:
        t, c, hp, fl, mate, via, home, node = m.groups()
        t, c, hp, via, home = int(t), int(c), int(hp), int(via), int(home)
        if fl == '0':
            if c in open_poss and t - open_poss[c]['last'] > 0:
                close(c, 'death' if hp <= 0 else 'drop', t)
            continue
        if hp <= 0:
            close(c, 'death', t); continue
        p = open_poss.get(c)
        if p and t - p['last'] > 1500:      # a gap: a new pickup by the same bot
            close(c, 'death', p['last']); p = None
        if not p:
            p = open_poss[c] = dict(client=c, team=team_of.get(c), t0=t, last=t, path=[], hp=[], home=[],
                                    via=[], mates=[])
        p['last'] = t
        x, y = (pos[c][2], pos[c][3]) if c in pos else (None, None)
        if x is not None:
            p['path'].append((t, x, y))
        p['hp'].append(hp); p['home'].append(home); p['via'].append(via)
        tm = team_of.get(c)
        near = sum(1 for o, q in pos.items() if o != c and q[1] == tm and q[5] != 'd' and x is not None
                   and abs(q[0] - t) <= 300 and math.hypot(q[2] - x, q[3] - y) <= 600)
        p['mates'].append(near)
        continue
    m = cap.search(line)
    if m and m.group(1) in names:
        close(names[m.group(1)], 'capture', now)
for c in list(open_poss):
    close(c, 'held at end', now)

rows = []
for p in done:
    if len(p['path']) < 2:
        continue
    hp = p['hp']; home = [h for h in p['home'] if h > 0]
    dmg = sum(max(0, a - b) for a, b in zip(hp, hp[1:]))
    best = None; rev = 0
    for h in home:
        if best is None or h < best:
            best = h
        elif h - best >= 150:
            rev += 1; best = h       # count each climb once
    vias = [v for v in p['via']]
    changes = sum(1 for a, b in zip(vias, vias[1:]) if a != b and b != 0)
    rows.append(dict(client=p['client'], team={1: 'red', 2: 'blue'}.get(p['team'], '?'),
                     start_ms=p['t0'], seconds=round((p['t1'] - p['t0']) / 1000, 2), end=p['end'],
                     censored=p['end'] == 'held at end', damage=dmg,
                     progress=(home[0] - min(home)) if home else 0, start_home=home[0] if home else 0,
                     reversals=rev, waypoint_changes=changes,
                     mates_near=round(sum(p['mates']) / max(1, len(p['mates'])), 2), path=p['path'],
                     via=vias))

def summary(team):
    r = [x for x in rows if x['team'] == team]
    if not r:
        return dict(team=team, possessions=0)
    n = len(r); caps = sum(x['end'] == 'capture' for x in r)
    return dict(team=team, possessions=n, captures=caps, deaths=sum(x['end'] == 'death' for x in r),
                median_seconds=sorted(x['seconds'] for x in r)[n // 2],
                mean_damage=round(sum(x['damage'] for x in r) / n), mean_progress=round(sum(x['progress'] for x in r) / n),
                reversals_per_possession=round(sum(x['reversals'] for x in r) / n, 2),
                waypoint_changes_per_possession=round(sum(x['waypoint_changes'] for x in r) / n, 2),
                mates_near=round(sum(x['mates_near'] for x in r) / n, 2))

summ = [summary('red'), summary('blue')]
for s in summ:
    print(json.dumps(s))
if jout:
    json.dump(dict(summary=summ, possessions=[{k: v for k, v in x.items() if k not in ('path', 'via')} for x in rows]),
              open(jout, 'w'), indent=1)

# ---- drawing --------------------------------------------------------------
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import importlib.util
spec = importlib.util.spec_from_file_location('floors', os.path.join(HERE, 'heatmap.py'))
# heatmap.py is a script; reuse only its floor renderer by running it as a module with our args
sys.argv = ['heatmap.py', bsp_path, run_dir, '/dev/null', '']
floors = {}
src = open(os.path.join(HERE, 'heatmap.py')).read()
cut = src.index('# ---- heat ----')
exec(compile(src[:cut], 'heatmap.py', 'exec'), floors)
base, to_px, W, H = floors['base'], floors['to_px'], floors['W'], floors['H']
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 22)
    small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 17)
    big = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 30)
except OSError:
    font = small = big = ImageFont.load_default()
CELL = 640
sc = CELL / W
cellbase = base.resize((CELL, int(H * sc)))

def ramp(f, team):
    lo, hi = ((90, 20, 20), (255, 230, 120)) if team == 'red' else ((20, 40, 110), (150, 255, 255))
    return tuple(int(a + (b - a) * f) for a, b in zip(lo, hi))

def draw_possession(r):
    im = cellbase.copy(); d = ImageDraw.Draw(im)
    pts = [(to_px(x, y)[0] * sc, to_px(x, y)[1] * sc) for _, x, y in r['path']]
    n = len(pts)
    for i in range(n - 1):
        d.line([pts[i], pts[i + 1]], fill=ramp(i / max(1, n - 1), r['team']), width=4)
    for i in range(4, n - 1, 8):            # arrows every 2 s
        (x0, y0), (x1, y1) = pts[i - 1], pts[i + 1]
        a = math.atan2(y1 - y0, x1 - x0)
        if math.hypot(x1 - x0, y1 - y0) < 1:
            continue
        tip = pts[i]
        for s in (0.5, -0.5):               # two barbs pointing back along the path
            d.line([tip, (tip[0] - 12 * math.cos(a + s), tip[1] - 12 * math.sin(a + s))],
                   fill=(255, 255, 255), width=3)
    via = r['via']
    for i in range(1, min(len(via), n)):
        if via[i] != via[i - 1] and via[i] != 0:
            x, y = pts[i]; d.polygon([(x, y - 9), (x + 9, y), (x, y + 9), (x - 9, y)], fill=(255, 255, 255), outline=(0, 0, 0))
    x, y = pts[0]; d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(0, 0, 0), outline=(255, 255, 255), width=3)
    x, y = pts[-1]
    if r['end'] == 'capture':
        star = [(x + 14 * math.cos(math.pi / 2 + k * math.pi / 5) * (1 if k % 2 == 0 else 0.45),
                 y - 14 * math.sin(math.pi / 2 + k * math.pi / 5) * (1 if k % 2 == 0 else 0.45)) for k in range(10)]
        d.polygon(star, fill=(255, 230, 60), outline=(0, 0, 0))
    elif r['end'] == 'death':
        d.line([(x - 11, y - 11), (x + 11, y + 11)], fill=(255, 255, 255), width=5)
        d.line([(x - 11, y + 11), (x + 11, y - 11)], fill=(255, 255, 255), width=5)
    else:
        d.rectangle([x - 8, y - 8, x + 8, y + 8], outline=(255, 255, 255), width=3)
    out = Image.new('RGB', (CELL, im.size[1] + 56), (10, 11, 14))
    out.paste(im, (0, 56)); d2 = ImageDraw.Draw(out)
    d2.text((10, 4), f"{r['team']} bot {r['client']} at {r['start_ms'] / 1000:.0f}s: {r['end']} after {r['seconds']:.1f}s"
            + (' (censored)' if r['censored'] else ''), font=font, fill=(240, 240, 240))
    d2.text((10, 31), f"dmg {r['damage']}  progress {r['progress']}/{r['start_home']}  reversals {r['reversals']}  "
            f"waypoint changes {r['waypoint_changes']}  mates near {r['mates_near']}", font=small, fill=(175, 180, 190))
    return out

for team in ('red', 'blue'):
    r = sorted([x for x in rows if x['team'] == team], key=lambda x: -x['seconds'])[:12]
    if not r:
        continue
    cells = [draw_possession(x) for x in r]
    cw, ch = cells[0].size; cols = 3; nrows = (len(cells) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (cw + 8) + 8, nrows * (ch + 8) + 120), (10, 11, 14))
    for i, c in enumerate(cells):
        sheet.paste(c, (8 + (i % cols) * (cw + 8), 112 + (i // cols) * (ch + 8)))
    d = ImageDraw.Draw(sheet)
    s = summary(team)
    d.text((14, 12), f'{title} - {team} carriers, longest {len(cells)} possessions', font=big, fill=(255, 255, 255))
    d.text((14, 56), f"{s['possessions']} possessions, {s['captures']} captured, {s['deaths']} died; median {s['median_seconds']}s held; "
           f"mean damage {s['mean_damage']}, progress {s['mean_progress']}, reversals {s['reversals_per_possession']}, "
           f"waypoint changes {s['waypoint_changes_per_possession']}, teammates within 600u {s['mates_near']}", font=small, fill=(185, 190, 200))
    d.text((14, 82), 'path dark -> bright over time; arrows every 2 s; diamond = waypoint changed; '
           'circle = pickup; star = capture, cross = death, square = held at end', font=small, fill=(150, 155, 165))
    sheet.save(f'{out_prefix}-possessions-{team}.png', optimize=True)
    print(f'{out_prefix}-possessions-{team}.png')
