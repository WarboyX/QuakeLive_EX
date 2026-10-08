#!/usr/bin/env python3
"""Side-by-side heat maps of one team's play pooled over several runs, per build.

usage: heatmap-compare.py MAP.bsp OUT.png "TITLE" --group NAME RUN_DIR:TEAM [RUN_DIR:TEAM ...] --group NAME ...

[QL] E229. TEAM is red or blue: the team under test in that run (for a
bot_tacticsTeams head-to-head, the layer-on team). Rows: presence, deaths,
carrier paths. Columns: one per group, pooled over its runs, each normalised on
its own (so read where, not how much - counts are in the captions). The floor
render and framing come from heatmap.py, using the first run.
"""
import os, re, sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
bsp, out, title = args[:3]
groups = []
for a in args[3:]:
    if a == '--group':
        groups.append([None, []])
    elif groups[-1][0] is None:
        groups[-1][0] = a
    else:
        d, t = a.rsplit(':', 1)
        groups[-1][1].append((d, {'red': 1, 'blue': 2}[t]))

def live_log(run):
    log = open(os.path.join(run, 'server.log'), errors='replace').read().splitlines()
    st = [i for i, l in enumerate(log) if 'InitGame:' in l[:40]]
    return log[st[-1]:] if st else log

bt = re.compile(r'bottrack (\d+) (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) (\w)')
ct = re.compile(r'ctftrack (\d+) (\d+) hp(-?\d+) flag(\d)')

def collect(run, team):
    alive, deaths, carrier = [], [], []
    prev, held = {}, {}
    caps = {'mine': 0, 'theirs': 0}
    for line in live_log(run):
        m = ct.search(line)
        if m:
            held[int(m.group(2))] = (int(m.group(1)), m.group(4) != '0'); continue
        m = bt.search(line)
        if m:
            t, c, tm, x, y, z, sp, s = m.groups(); t, c, tm = int(t), int(c), int(tm)
            r = (t, c, tm, float(x), float(y), float(z), s)
            if tm == team:
                if s != 'd': alive.append(r)
                p = prev.get(c)
                if s == 'd' and p is not None and p[6] != 'd': deaths.append(p)
                h = held.get(c)
                if h and h[1] and t - h[0] <= 1100: carrier.append(r)
            prev[c] = r
            continue
        m = re.search(r'captured the (RED|BLUE) flag', line)
        if m:
            # capturing the RED flag scores for blue
            caps['mine' if (m.group(1) == 'RED') == (team == 2) else 'theirs'] += 1
    return alive, deaths, carrier, caps

# floors and framing from heatmap.py, framed on the first run
first = groups[0][1][0][0]
sys.argv = ['heatmap.py', bsp, first, '/dev/null', '']
ns = {}
src = open(os.path.join(HERE, 'heatmap.py')).read()
exec(compile(src[:src.index('# ---- heat ----')], 'heatmap.py', 'exec'), ns)
basearr, to_px, W, H = ns['basearr'], ns['to_px'], ns['W'], ns['H']
src2 = src[src.index('# ---- heat ----'):src.index('# flag stands')]
exec(compile(src2, 'heatmap.py', 'exec'), ns)      # blur, density, cmap, overlay
density, overlay = ns['density'], ns['overlay']
flags = {}
exec(compile(src[src.index('# flag stands'):src.index('try:\n    font')], 'heatmap.py', 'exec'), ns)
flags = ns['flags']
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 28)
    small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 21)
    big = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 34)
except OSError:
    font = small = big = ImageFont.load_default()

def panel(arr, heading, sub):
    im = Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for name, o in flags.items():
        px, py = to_px(o[0], o[1]); c = (255, 70, 70) if 'red' in name else (80, 150, 255)
        d.ellipse([px - 13, py - 13, px + 13, py + 13], outline=(255, 255, 255), width=4)
        d.ellipse([px - 9, py - 9, px + 9, py + 9], fill=c)
    o = Image.new('RGB', (W, H + 84), (10, 11, 14)); o.paste(im, (0, 84)); d2 = ImageDraw.Draw(o)
    d2.text((14, 6), heading, font=font, fill=(240, 240, 240)); d2.text((14, 46), sub, font=small, fill=(170, 175, 185))
    return o

cols = []
for name, runs in groups:
    A, D, C = [], [], []; caps = {'mine': 0, 'theirs': 0}
    for run, team in runs:
        a, d, c, k = collect(run, team); A += a; D += d; C += c
        caps['mine'] += k['mine']; caps['theirs'] += k['theirs']
    cols.append([
        panel(overlay(density(A, 6)), f'{name}: presence', f'{len(runs)} matches, {len(A)} live samples of the tested team'),
        panel(overlay(density(D, 9)), f'{name}: deaths', f'{len(D)} deaths of the tested team'),
        panel(overlay(density(C, 6), gamma=0.45), f'{name}: carrier paths',
              f'{len(C)} carrier samples; captures {caps["mine"]} for, {caps["theirs"]} against'),
    ])
pw, ph = cols[0][0].size
sheet = Image.new('RGB', (len(cols) * (pw + 10) + 10, 3 * (ph + 10) + 100), (10, 11, 14))
for ci, col in enumerate(cols):
    for ri, p in enumerate(col):
        sheet.paste(p, (10 + ci * (pw + 10), 100 + ri * (ph + 10)))
d = ImageDraw.Draw(sheet)
d.text((16, 14), title, font=big, fill=(255, 255, 255))
d.text((16, 58), 'Tested team only, pooled over side-swapped matches; each panel normalised on its own - compare where, '
       'read counts in the captions. Upper floors drawn over lower ones.', font=small, fill=(170, 175, 185))
sheet.save(out, optimize=True)
print(out, sheet.size)
