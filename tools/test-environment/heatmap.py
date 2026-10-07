#!/usr/bin/env python3
"""Heat maps of bot matches over a top-down render of the map's floors.

usage: heatmap.py MAP.bsp RUN_DIR OUT_PREFIX "TITLE"

[QL] E225. Reads the bottrack / ctftrack lines and CTF broadcasts that a
tools/test-environment/run.py run leaves in server.log, and draws four panels
over a top-down render of the map's own floors: each team's presence, combat,
deaths, and flag carriers' paths. Writes OUT_PREFIX-sheet.png and one PNG per
panel. Floors above the highest point any bot stood are left out, so castle
roofs do not hide the interiors; upper floors still cover lower ones.

The .bsp is the map you supply (never commit one from pak00). Needs numpy and
Pillow only.
"""
import re, struct, sys, math, json, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

bsp_path, run_dir, out_prefix, title = sys.argv[1:5]
PX = 1300                      # output width of each panel's map area
d = open(bsp_path, 'rb').read()
lumps = [struct.unpack('<ii', d[8 + i * 8:16 + i * 8]) for i in range(17)]

def lump(i):
    o, l = lumps[i]
    return d[o:o + l]

# shaders: skip what is not a visible floor
sh = lump(1)
shaders = []
for i in range(len(sh) // 72):
    name = sh[i * 72:i * 72 + 64].split(b'\0')[0].decode('latin1').lower()
    flags, contents = struct.unpack('<ii', sh[i * 72 + 64:i * 72 + 72])
    skip = (flags & 0x4) or (flags & 0x80) or (contents & 0x1) == 0 or any(
        k in name for k in ('sky', 'caulk', 'nodraw', 'clip', 'trigger', 'hint', 'skip', 'fog', 'water', 'flare'))
    shaders.append(skip)

vb = lump(10)
nv = len(vb) // 44
verts = np.frombuffer(vb, dtype=np.dtype([('p', '<f4', 3), ('st', '<f4', 2), ('lm', '<f4', 2),
                                          ('n', '<f4', 3), ('c', 'u1', 4)]), count=nv)
P = verts['p'].astype(np.float64)
idx = np.frombuffer(lump(11), dtype='<i4')
sb = lump(13)
tris = []
for i in range(len(sb) // 104):
    f = struct.unpack('<iiiiiiii', sb[i * 104:i * 104 + 32])
    shader, fog, typ, fv, nvs, fi, nis = f[:7]
    pw, ph = struct.unpack('<ii', sb[i * 104 + 96:i * 104 + 104])
    if shader < 0 or shader >= len(shaders) or shaders[shader]:
        continue
    if typ in (1, 3):
        for k in range(0, nis, 3):
            tris.append((fv + idx[fi + k], fv + idx[fi + k + 1], fv + idx[fi + k + 2]))
    elif typ == 2 and pw > 1 and ph > 1:      # patch control grid, coarse
        for y in range(ph - 1):
            for x in range(pw - 1):
                a = fv + y * pw + x
                tris.append((a, a + 1, a + pw)); tris.append((a + 1, a + pw + 1, a + pw))
T = np.array(tris)
A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
N = np.cross(B - A, C - A)
nl = np.linalg.norm(N, axis=1) + 1e-9
nz = N[:, 2] / nl
zc = (A[:, 2] + B[:, 2] + C[:, 2]) / 3

# ---- match data -------------------------------------------------------------
log = open(os.path.join(run_dir, 'server.log'), errors='replace').read().splitlines()
# the live match starts at the last map load (warmup admits bots first, then map_restart)
starts = [i for i, l in enumerate(log) if l.startswith('InitGame:') or 'InitGame:' in l[:40]]
log = log[starts[-1]:] if starts else log
track = []          # (t, client, team, x, y, z, state)
carrier = []        # (t, client, x, y) while holding the flag - from bottrack + ctftrack flag1
flagheld = {}
ct = re.compile(r'ctftrack (\d+) (\d+) hp(-?\d+) flag(\d)')
bt = re.compile(r'bottrack (\d+) (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) (\w)')
last = {}
for line in log:
    m = ct.search(line)
    if m:
        flagheld[int(m.group(2))] = (int(m.group(1)), m.group(4) != '0')   # flag1/flag2: which flag
        continue
    m = bt.search(line)
    if m:
        t, c, tm, x, y, z, sp, st = m.groups()
        t, c, tm = int(t), int(c), int(tm)
        rec = (t, c, tm, float(x), float(y), float(z), st)
        track.append(rec)
        fh = flagheld.get(c)
        if fh and fh[1] and t - fh[0] <= 600:
            carrier.append(rec)
res = json.load(open(os.path.join(run_dir, 'result.json')))
# live window only: the warmup admits bots before g_doWarmup 0
t0 = min(r[0] for r in track)
deaths = []
prev = {}
for r in track:
    p = prev.get(r[1])
    if r[6] == 'd' and p is not None and p[6] != 'd':
        deaths.append(p)        # where it was standing just before it died
    prev[r[1]] = r
fights = [r for r in track if r[6] == 'f']
alive = [r for r in track if r[6] != 'd']

# ---- frame: everything any bot touched, plus a margin ------------------------
xs = np.array([r[3] for r in alive]); ys = np.array([r[4] for r in alive]); zs = np.array([r[5] for r in alive])
x0, x1 = np.percentile(xs, 0.2) - 300, np.percentile(xs, 99.8) + 300
y0, y1 = np.percentile(ys, 0.2) - 300, np.percentile(ys, 99.8) + 300
zcap = np.percentile(zs, 99.9) + 72         # nothing drawn above where bots stand: roofs
scale = PX / (x1 - x0)
W, H = PX, int((y1 - y0) * scale)

def to_px(x, y):
    return (x - x0) * scale, (y1 - y) * scale

# ---- base map: floors by height, painter's order, lit from above -------------
keep = (nz > 0.55) & (zc < zcap) & (np.maximum(np.maximum(A[:, 0], B[:, 0]), C[:, 0]) > x0) & \
       (np.minimum(np.minimum(A[:, 0], B[:, 0]), C[:, 0]) < x1)
order = np.argsort(zc[keep])
ka, kb, kc, kz, kn = A[keep][order], B[keep][order], C[keep][order], zc[keep][order], nz[keep][order]
zlo, zhi = np.percentile(zs, 1) - 64, zcap
base = Image.new('RGB', (W, H), (14, 16, 20))
dr = ImageDraw.Draw(base)
for a, b, c, z, n in zip(ka, kb, kc, kz, kn):
    h = min(1, max(0, (z - zlo) / (zhi - zlo)))
    v = int(55 + 120 * h) * (0.75 + 0.25 * n)
    col = (int(v * 0.92), int(v * 0.95), int(v))
    dr.polygon([to_px(*a[:2]), to_px(*b[:2]), to_px(*c[:2])], fill=col)
basearr = np.asarray(base).astype(np.float64) / 255

# ---- heat -------------------------------------------------------------------
def blur(a, sigma):
    r = int(3 * sigma)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2); k /= k.sum()
    a = np.apply_along_axis(lambda v: np.convolve(v, k, 'same'), 0, a)
    return np.apply_along_axis(lambda v: np.convolve(v, k, 'same'), 1, a)

def density(recs, sigma=6):
    g = np.zeros((H, W))
    for r in recs:
        px, py = to_px(r[3], r[4])
        if 0 <= px < W and 0 <= py < H:
            g[int(py), int(px)] += 1
    return blur(g, sigma)

STOPS = [(0, (0, 0, 0)), (.25, (60, 15, 110)), (.5, (190, 40, 80)), (.75, (250, 140, 20)), (1, (252, 250, 160))]
def cmap(v):
    out = np.zeros(v.shape + (3,))
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        m = (v >= a) & (v <= b)
        f = ((v - a) / (b - a))[m][:, None]
        out[m] = np.array(ca) * (1 - f) + np.array(cb) * f
    return out / 255

def overlay(g, colour=None, gamma=0.5):
    top = np.percentile(g[g > 0], 99.5) if (g > 0).any() else 1
    v = np.clip(g / top, 0, 1) ** gamma
    col = cmap(v) if colour is None else np.ones(g.shape + (3,)) * np.array(colour) / 255
    alpha = np.clip(v * 1.4, 0, 0.92)[..., None]
    return basearr * (1 - alpha) + col * alpha

def team_overlay(red, blue):
    tr = np.percentile(red[red > 0], 99.5); tb = np.percentile(blue[blue > 0], 99.5)
    vr = np.clip(red / tr, 0, 1) ** 0.5; vb = np.clip(blue / tb, 0, 1) ** 0.5
    col = np.stack([vr * 1.0 + vb * 0.25, vr * 0.25 + vb * 0.45, vb * 1.0 + vr * 0.2], -1)
    alpha = np.clip(np.maximum(vr, vb) * 1.4, 0, 0.92)[..., None]
    col = col / np.maximum(col.max(-1, keepdims=True), 1e-6)
    return basearr * (1 - alpha) + col * alpha

# flag stands from the entity lump
ents = lump(0).decode('latin1')
flags = {}
for blk in re.findall(r'\{([^}]*)\}', ents):
    cn = re.search(r'"classname"\s+"([^"]+)"', blk); og = re.search(r'"origin"\s+"([^"]+)"', blk)
    if cn and og and cn.group(1) in ('team_CTF_redflag', 'team_CTF_blueflag'):
        flags[cn.group(1)] = [float(v) for v in og.group(1).split()]

try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 30)
    small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 22)
except OSError:
    font = small = ImageFont.load_default()

def panel(arr, heading, sub, marks=None):
    im = Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    for name, o in flags.items():
        px, py = to_px(o[0], o[1])
        c = (255, 70, 70) if 'red' in name else (80, 150, 255)
        dr.ellipse([px - 13, py - 13, px + 13, py + 13], outline=(255, 255, 255), width=4)
        dr.ellipse([px - 9, py - 9, px + 9, py + 9], fill=c)
    if marks:
        for (x, y, c) in marks:
            px, py = to_px(x, y)
            dr.ellipse([px - 3, py - 3, px + 3, py + 3], fill=c)
    out = Image.new('RGB', (W, H + 90), (10, 11, 14))
    out.paste(im, (0, 90))
    d2 = ImageDraw.Draw(out)
    d2.text((16, 10), heading, font=font, fill=(240, 240, 240))
    d2.text((16, 52), sub, font=small, fill=(170, 175, 185))
    return out

red = [r for r in alive if r[2] == 1]; blue = [r for r in alive if r[2] == 2]
secs = (max(r[0] for r in track) - t0) / 1000
pol = {1: 'red ours / blue stock', 2: 'blue ours / red stock', 3: 'both ours'}
tt = res.get('settings', {}).get('bot_tacticsTeams') if isinstance(res.get('settings'), dict) else None
fs = res['final']
panels = [
    panel(team_overlay(density(red), density(blue)), 'Where each team spent its time',
          f'red and blue presence, {len(alive)} live samples (4 per bot per second)'),
    # bottrack 'f' is every live node that is not a seek node: fight, chase,
    # battle-item AND retreat - which is also how an escort follows its carrier
    # while shooting. So this is "in a battle node", not "not escorting" (E226).
    panel(overlay(density(fights, 7)), 'Bots in a battle node',
          f'{len(fights)} samples in fight/chase/retreat/battle-item - retreat includes escorting while shooting'),
    panel(overlay(density(deaths, 9)), 'Where bots died',
          f'{len(deaths)} deaths; dots = each death, coloured by the victim\'s team',
          [(r[3], r[4], (255, 90, 90) if r[2] == 1 else (100, 160, 255)) for r in deaths]),
    panel(overlay(density(carrier, 6), gamma=0.45), 'Flag carriers\' paths',
          f'{len(carrier)} samples holding the enemy flag; dots by carrier\'s team',
          [(r[3], r[4], (255, 90, 90) if r[2] == 1 else (100, 160, 255)) for r in carrier[::2]]),
]
pw, ph = panels[0].size
sheet = Image.new('RGB', (pw * 2 + 30, ph * 2 + 30 + 110), (10, 11, 14))
for i, p in enumerate(panels):
    sheet.paste(p, (10 + (i % 2) * (pw + 10), 120 + (i // 2) * (ph + 10)))
d3 = ImageDraw.Draw(sheet)
d3.text((20, 18), title, font=font, fill=(255, 255, 255))
caps = re.findall(r'captured the (RED|BLUE)', '\n'.join(log))
nr = len({r[1] for r in red}); nb = len({r[1] for r in blue})
d3.text((20, 62), f'japanesecastles, {nr}v{nb} bots, {secs:.0f} game seconds. Captures: {len(caps)} '
        f'(red took blue\'s {caps.count("BLUE")}, blue took red\'s {caps.count("RED")}). '
        f'Top-down; upper floors drawn over lower ones, lighter = higher.', font=small, fill=(180, 185, 195))
sheet.save(out_prefix + '-sheet.png', optimize=True)
for i, p in enumerate(panels):
    p.save(f'{out_prefix}-{i + 1}.png', optimize=True)
print(out_prefix + '-sheet.png', sheet.size, 'deaths', len(deaths), 'carrier', len(carrier), 'caps', caps)
