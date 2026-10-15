"""The measured live window of a run.py run, shared by the analysis tools.

[QL] E230. heatmap.py, heatmap-compare.py and ctf-possessions.py counted
captures over the whole log after the last map load. run.py measures a match
between two recorded snapshots (result.json "initial" and "final", game ms),
and an event after the final snapshot - one A1 capture at ~609.3 s against a
608.35 s cut-off - made two analyses of the same match disagree (3-9 vs 4-9).
Everything is now cut to the recorded snapshots.

lines(run) yields (game_ms, line) for the live window only. Broadcast lines carry
no time of their own; each takes the time of the latest bottrack/ctftrack line
before it, which is how the order in the log already places them.
"""
import json, os, re

_T = re.compile(r'(?:bottrack|ctftrack) (\d+) ')


def window(run):
    try:
        r = json.load(open(os.path.join(run, 'result.json')))
        return int(r['initial']['time']), int(r['final']['time'])
    except (OSError, KeyError, ValueError, TypeError):
        return None, None


def lines(run):
    log = open(os.path.join(run, 'server.log'), errors='replace').read().splitlines()
    st = [i for i, l in enumerate(log) if 'InitGame:' in l[:40]]
    log = log[st[-1]:] if st else log
    lo, hi = window(run)
    now = 0
    for l in log:
        m = _T.search(l)
        if m:
            now = int(m.group(1))
        if lo is not None and not (lo <= now <= hi):
            continue
        yield now, l
