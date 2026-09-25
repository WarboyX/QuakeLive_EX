#!/usr/bin/env python3
"""
[QL] E132. Compares a botfiles inv.h with ours, and rewrites its MODELINDEX_*
numbers to ours.

    fix-inv.py <botfiles/inv.h>            rewrite in place, list what changed
    fix-inv.py --check <botfiles/inv.h>    list differences only, exit 1 if any

MODELINDEX_* is an index into bg_itemlist. botlib uses it to tie each item
entity on the map to its definition in botfiles/items.c, so a wrong number
gives bots the wrong idea of what an item is - the red flag read as the blue
flag, and one team never attacked. code/game/inv.h carries Quake Live's
numbering (item_armor_jacket at 4 shifts everything after it by one from
Quake 3's). INVENTORY_* numbers are compared too: those index the inventory
array our game code fills in for the bot, and they must agree with the
botfiles or every item and weapon weight reads the wrong slot. Those are
reported, never rewritten - a mismatch there is a question for the code.
"""
import re
import sys
import pathlib

OURS = pathlib.Path(__file__).resolve().parent.parent.parent / "code/game/inv.h"
DEF = re.compile(r"#define\s+((?:MODELINDEX|INVENTORY)_\w+)\s+(\d+)")


def defs(text):
    return {m.group(1): int(m.group(2)) for m in DEF.finditer(text)}


def main():
    args = sys.argv[1:]
    check = "--check" in args
    args = [a for a in args if a != "--check"]
    if len(args) != 1:
        sys.exit(__doc__)
    path = pathlib.Path(args[0])
    theirs_text = path.read_text(errors="replace")
    ours, theirs = defs(OURS.read_text()), defs(theirs_text)
    diff = [(n, theirs[n], ours[n]) for n in sorted(theirs) if n in ours and theirs[n] != ours[n]]
    for n, t, o in diff:
        print("%-32s theirs %3d  ours %3d%s" % (n, t, o, "" if n.startswith("MODELINDEX") or check else "  (INVENTORY: not rewritten)"))
    if check:
        sys.exit(1 if diff else 0)
    fixed = [0]

    def sub(m):
        if m.group(1).startswith("MODELINDEX") and m.group(1) in ours and int(m.group(2)) != ours[m.group(1)]:
            fixed[0] += 1
            return m.group(0)[:m.start(2) - m.start(0)] + str(ours[m.group(1)])
        return m.group(0)
    path.write_text(DEF.sub(sub, theirs_text))
    print("fix-inv: %d MODELINDEX numbers rewritten in %s" % (fixed[0], path))


if __name__ == "__main__":
    main()
