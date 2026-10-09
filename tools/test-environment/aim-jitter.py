#!/usr/bin/env python3
"""Measure a bot's view motion while it tracks an enemy, from bot_debugAim logs.

usage: aim-jitter.py LABEL RUN_DIR [RUN_DIR ...] [--label LABEL RUN_DIR ...]

[QL] E230. A1 (E227) tracks with fast exponential correction and no deadzone,
the shape the old BotAimSweep comment warned would glue the view to the 10 Hz
think staircase. This turns the "botaim" lines (one per input frame, see
bot_debugAim) into numbers. It is a proxy, not a substitute for watching:

  error      mean |view - ideal| in degrees (ideal includes the deliberate
             drift/accuracy offsets, so 0 is not the goal)
  jerk       mean |change in yaw rate| between frames, deg/s per frame
  rev/s      yaw direction flips per second where both steps exceed 0.05 deg -
             small back-and-forth corrections
  rev.5      the same above 0.5 deg a frame (20 deg/s) - the size a spectator
             would see as a shiver
  still      fraction of frames the view does not move at all (< 0.01 deg) -
             the deadzone's hold, which then steps
  10hz       share of yaw-velocity power between 8 and 12 Hz, from 64-frame
             windows - energy at the think rate, i.e. the staircase

Only frames with an enemy, in runs of at least 16 consecutive frames on the same
enemy, count. Frames are whatever the server ran at (sv_fps 40 -> 25 ms).
"""
import math, os, re, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import livewindow

groups, cur = [], None
for a in sys.argv[1:]:
    if a == '--label':
        cur = None; continue
    if cur is None:
        cur = [a, []]; groups.append(cur)
    else:
        cur[1].append(a)

rx = re.compile(r'botaim (\d+) (\d+) (-?\d+) (\S+) (\S+) (\S+) (\S+)')

def wrap(d):
    return (d + 180.0) % 360.0 - 180.0

def segments(run):
    seg, last = [], None
    for _, l in livewindow.lines(run):
        m = rx.search(l)
        if not m:
            continue
        t, c, e = int(m.group(1)), int(m.group(2)), int(m.group(3))
        vy, vp, iy, ip = (float(m.group(i)) for i in range(4, 8))
        if e < 0 or (last and (e != last[1] or t - last[0] > 60)):
            if len(seg) >= 16:
                yield seg
            seg = []
        if e >= 0:
            seg.append((t, vy, vp, iy, ip))
        last = (t, e)
    if len(seg) >= 16:
        yield seg

def measure(runs):
    err, jerk, still, n, secs = [], [], 0, 0, 0.0
    revs = {'rev': 0, 'rev5': 0}
    p10, ptot = 0.0, 0.0
    for run in runs:
        for s in segments(run):
            t = np.array([x[0] for x in s], float) / 1000.0
            vy = np.array([x[1] for x in s]); iy = np.array([x[3] for x in s])
            vp = np.array([x[2] for x in s]); ip = np.array([x[4] for x in s])
            dt = np.diff(t); dt[dt <= 0] = 0.025
            dy = np.array([wrap(a) for a in np.diff(vy)])
            rate = dy / dt
            err += list(np.hypot([wrap(a) for a in vy - iy], [wrap(a) for a in vp - ip]))
            jerk += list(np.abs(np.diff(rate)))
            still += int(np.sum(np.abs(dy) < 0.01)); n += len(dy)
            secs += t[-1] - t[0]
            for thr, key in ((0.05, 'rev'), (0.5, 'rev5')):
                big = np.abs(dy) > thr
                revs[key] += int(np.sum(big[1:] & big[:-1] & (np.sign(dy[1:]) != np.sign(dy[:-1]))))
            for k in range(0, len(rate) - 63, 32):
                w = rate[k:k + 64] - rate[k:k + 64].mean()
                f = np.fft.rfftfreq(64, d=float(np.median(dt)))
                p = np.abs(np.fft.rfft(w * np.hanning(64))) ** 2
                p10 += p[(f >= 8) & (f <= 12)].sum(); ptot += p[1:].sum()
    return dict(frames=n, seconds=round(secs, 1), error=np.mean(err) if err else 0,
                jerk=np.mean(jerk) if jerk else 0, reversals=revs['rev'] / secs if secs else 0,
                reversals5=revs['rev5'] / secs if secs else 0,
                still=still / n if n else 0, hz10=p10 / ptot if ptot else 0)

print(f"{'':28} {'frames':>7} {'secs':>6} {'error':>7} {'jerk':>8} {'rev/s':>6} {'rev.5':>6} {'still':>6} {'10Hz':>6}")
for label, runs in groups:
    m = measure(runs)
    print(f"{label:28} {m['frames']:7d} {m['seconds']:6.0f} {m['error']:6.2f}d {m['jerk']:8.1f} "
          f"{m['reversals']:6.2f} {m['reversals5']:6.2f} {m['still']:6.1%} {m['hz10']:6.1%}")
