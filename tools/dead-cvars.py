#!/usr/bin/env python3
"""List cvars that are registered but consumed nowhere.

Registering a cvar creates it, gives it a default, exposes it to configs and
lists it in \cvarlist. None of that makes it do anything. A cvar nothing reads
looks exactly like a working setting: you set it, it takes the value, and the
game ignores it.

That has already produced real bugs here - g_spawnItemWeapons was registered,
exposed as a gamerule, documented, set to 0 in the shipped instagib configs,
and read by no code at all, so instagib servers kept spawning weapons.

A cvar counts as consumed if any of these mention it:
  - C code, via its vmCvar_t symbol (.integer / .value / .string)
  - C code, via its name as a string (trap_Cvar_VariableStringBuffer etc.)
  - any menu, hud or cfg asset, ours or Quake Live's

Pass a directory of Quake Live's own ui/ files as the first argument to include
them; without it, anything only its menus reference is reported as dead.

    tools/dead-cvars.py [/path/to/ql/ui]
"""

import glob
import os
import re
import sys

TABLES = {
    "game": "code/game/g_main.c",
    "cgame": "code/cgame/cg_main.c",
    "ui": "code/ui/ui_main.c",
}

CODE = ("code/game/*.c", "code/cgame/*.c", "code/ui/*.c",
        "code/qcommon/*.c", "code/client/*.c", "code/server/*.c")

ASSETS = ("content/pak01/**/*.menu", "content/pak01/**/*.cfg",
          "content/serverconfigs/*.cfg")


def slurp(patterns):
    out = []
    for pat in patterns:
        for path in glob.glob(pat, recursive=True):
            if os.path.isfile(path):
                with open(path, errors="ignore") as fh:
                    out.append(fh.read())
    return "\n".join(out)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    code = slurp(CODE)
    asset_pats = list(ASSETS)
    if len(sys.argv) > 1:
        asset_pats.append(os.path.join(sys.argv[1], "**", "*"))
    else:
        print("note: no Quake Live ui/ directory given, so cvars only its own\n"
              "      menus reference will be reported as dead.\n")
    assets = slurp(asset_pats)

    total = 0
    for module, table in TABLES.items():
        with open(table) as fh:
            src = fh.read()
        entries = re.findall(r'\{\s*&(\w+)\s*,\s*"([^"]+)"\s*,\s*"[^"]*"', src)

        dead = []
        for var, name in entries:
            by_symbol = re.search(r"\b%s\s*\.\s*(?:integer|value|string)\b" % re.escape(var), code)
            # >1 because the registration row itself contains the name
            by_name = len(re.findall(r'"%s"' % re.escape(name), code)) > 1
            if not by_symbol and not by_name and name not in assets:
                dead.append(name)

        total += len(dead)
        print("%s: %d of %d cvars are never read" % (module, len(dead), len(entries)))
        for name in sorted(dead):
            print("    %s" % name)
        print()

    print("total unread: %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
