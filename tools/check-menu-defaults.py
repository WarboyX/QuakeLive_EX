#!/usr/bin/env python3
"""
check-menu-defaults.py - a menu row's "(default)" must be the default the code
registers, and the registered default must be one of the row's values.

Why
---
A multi-choice row (cvarFloatList / cvarStrList) draws BLANK when the cvar holds
a value that is not in its list, and a label saying "(default)" on a value the
code does not default to sends every "reset to default" to the wrong place.
Both have shipped: r_ssrSteps and r_ssrThickness defaulted to 24 while their
rows offered 16/32/64/128 and 4/8/16/32 with "(default)" on 32 and 8, so both
rows were blank on every install; and the water splash defaults moved while the
labels stayed. The ui does warn at runtime ("not one of its listed values"),
but only for the rows a player happens to open, in a console nobody reads.

This reads every Cvar_Get("name", "default", ...) in code/ and every multi row
in content/pak01/ui/*.menu, and fails if:
  - a row marks a value "(default)" that is not the registered default, or
  - the registered default is not in the row's list at all (the row is blank
    out of the box).

A cvar registered with different defaults in two places (renderervk and
renderergl2, say) is checked against each; a row only has to agree with one.
Cvars the code does not register (Quake Live's own, read by the ui from pak00
menus) are skipped - there is nothing to compare against.

Usage: tools/check-menu-defaults.py        exit 1 on any mismatch
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GET = re.compile(r'Cvar_Get\s*\(\s*"([^"]+)"\s*,\s*"([^"]*)"')
# vmCvar tables: {&cg_foo, "cg_foo", "1", ...}
TABLE = re.compile(r'\{\s*&\w+\s*,\s*"([^"]+)"\s*,\s*"([^"]*)"')
ITEM = re.compile(r'itemDef\s*\{')
CVAR = re.compile(r'\bcvar\s+"([^"]+)"')
LIST = re.compile(r'\bcvar(Float|Str)List\s*\{([^}]*)\}')
# "label" value, and cvarStrList's "label", "value" with commas
PAIR = re.compile(r'"([^"]*)"\s*,?\s*("[^"]*"|-?[0-9.]+)')


def num(v):
    # g_main.c's vmCvar table was transcribed with C literals: "275.0f"
    v = v.strip()
    if v[-1:] in ('f', 'F') and len(v) > 1:
        v = v[:-1]
    try:
        return float(v)
    except ValueError:
        return None


def same(a, b):
    na, nb = num(a), num(b)
    if na is not None and nb is not None:
        return abs(na - nb) < 1e-6
    return a.strip().lower() == b.strip().lower()


def registered():
    defaults = {}
    for root, _dirs, files in os.walk(os.path.join(REPO, 'code')):
        for f in files:
            if not f.endswith('.c'):
                continue
            text = open(os.path.join(root, f), errors='replace').read()
            for rx in (GET, TABLE):
                for name, val in rx.findall(text):
                    defaults.setdefault(name.lower(), set()).add(val)
    return defaults


def rows():
    ui = os.path.join(REPO, 'content', 'pak01', 'ui')
    for f in sorted(os.listdir(ui)):
        if not f.endswith('.menu'):
            continue
        path = os.path.join(ui, f)
        text = open(path, errors='replace').read()
        starts = [m.start() for m in ITEM.finditer(text)] + [len(text)]
        for a, b in zip(starts, starts[1:]):
            body = text[a:b]
            c, l = CVAR.search(body), LIST.search(body)
            if not c or not l:
                continue
            values = [(lab, v.strip('"')) for lab, v in PAIR.findall(l.group(2))]
            yield f, text.count('\n', 0, a) + 1, c.group(1), values


def main():
    defaults = registered()
    problems = 0
    checked = 0
    for f, line, cvar, values in rows():
        regs = defaults.get(cvar.lower())
        if not regs:
            continue
        checked += 1
        where = 'content/pak01/ui/%s:%d %s' % (f, line, cvar)
        marked = [v for lab, v in values if '(default)' in lab.lower()]
        for v in marked:
            if not any(same(v, r) for r in regs):
                print('%s: row marks %s "(default)", code registers %s'
                      % (where, v, ' / '.join(sorted(regs))))
                problems += 1
        if not any(same(v, r) for _lab, v in values for r in regs):
            print('%s: registered default %s is not in the row\'s list - the row '
                  'draws blank out of the box' % (where, ' / '.join(sorted(regs))))
            problems += 1
    if problems:
        print('\ncheck-menu-defaults: %d problem(s) in %d rows' % (problems, checked))
        return 1
    print('check-menu-defaults: ok - %d multi rows agree with the registered defaults'
          % checked)
    return 0


if __name__ == '__main__':
    sys.exit(main())
