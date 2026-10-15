#!/usr/bin/env python3
"""[QL] Harvest docs/ql-cvar-semantics.txt from a Quake Live install's ui/ files.

A cvar's default tells you almost nothing. cg_killBeep's default does not say
whether 1 is louder than 2 or a different sound entirely, so wiring one meant
inventing a scheme and calling it Quake Live's - which is why E97 wired four
cvars and stopped. Quake Live's own menus answer the question: every
ITEM_TYPE_MULTI control carries a cvarFloatList or cvarStrList mapping each
value to the label the player sees.

This pulls out the name and the value/label pairs and nothing else. No menu
markup, no layout, no art. That keeps the output the same standing as
docs/pak-manifest.txt and docs/ql-menu-names.txt - facts about names, checked in
so they can be verified rather than guessed - and leaves pak00 where it is.

    tools/dump-cvar-semantics.py <path to a QL ui/ directory> [> docs/ql-cvar-semantics.txt]

Point it at the ui/ inside pak00, extracted to somewhere OUTSIDE this repo.
pak00.pk3 and its contents are never committed or shipped.
"""

import glob
import os
import re
import sys

HEADER = '''# ql-cvar-semantics.txt - what Quake Live's own menus say each cvar's values MEAN.
#
# Harvested from the cvarFloatList / cvarStrList entries in pak00's ui/*.menu
# files by tools/dump-cvar-semantics.py. NAMES AND LABELS ONLY - no menu markup,
# no layout, no art, nothing that is Quake Live's to distribute. Same standing as
# docs/pak-manifest.txt and docs/ql-menu-names.txt.
#
# WHY THIS FILE EXISTS. A cvar's default tells you almost nothing. cg_hitBeep
# defaults to "2" and neither that nor the name says whether 1 and 3 are louder,
# different, or off - so wiring one up meant inventing a scheme and calling it
# Quake Live's. That was the stated reason only four cvars got wired in E97.
# These lists are Quake Live answering the question itself.
#
# Read the VALUES, not just the count. Several are bitfields and the labels give
# it away: cg_specItemTimers runs 0 / 1 "Power-ups Only" / 7 "PU/MH/RA" / 15
# "All", which is three bits, not four settings. cg_drawItemPickups 0/3/7 and
# cg_crosshairHitStyle 0/1/2/6/7/8 are the same shape. A switch statement on
# those would be wrong in a way that looks right.
#
# A cvar being absent here means only that no menu exposes it - cg_hitBeep is
# absent and is still unwired for exactly that reason.
#
# Format: <cvar><TAB><value>=<label>, <value>=<label>, ...

'''

PAIR = re.compile(
    r'cvar\s+"([^"]+)".{0,400}?(cvarFloatList|cvarStrList)\s*\{([^}]*)\}', re.S)


def harvest(ui_dir):
    found = {}
    pats = [os.path.join(ui_dir, "**", "*.menu"), os.path.join(ui_dir, "*.menu")]
    for pat in pats:
        for path in glob.glob(pat, recursive=True):
            with open(path, errors="ignore") as fh:
                text = fh.read()
            for m in PAIR.finditer(text):
                name, body = m.group(1), m.group(3)
                if name in found:
                    continue
                vals = [(lab, val) for lab, val
                        in re.findall(r'"([^"]*)"\s*(-?[\d.]+)?', body)
                        if lab and val]
                if vals:
                    found[name] = vals
    return found


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    ui_dir = argv[1]
    if not os.path.isdir(ui_dir):
        print("dump-cvar-semantics: %s is not a directory" % ui_dir,
              file=sys.stderr)
        return 2

    found = harvest(ui_dir)
    if not found:
        print("dump-cvar-semantics: no cvarFloatList/cvarStrList found under %s "
              "- is that the ui/ directory?" % ui_dir, file=sys.stderr)
        return 1

    sys.stdout.write(HEADER)
    for name in sorted(found):
        pairs = ", ".join("%s=%s" % (val, lab) for lab, val in found[name])
        sys.stdout.write("%s\t%s\n" % (name, pairs))

    print("dump-cvar-semantics: %d cvars" % len(found), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
