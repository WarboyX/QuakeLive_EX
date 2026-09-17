#!/usr/bin/env python3
"""Check every registered-but-unread cvar against its recorded verdict.

Registering a cvar creates it, gives it a default, exposes it to configs and
lists it in \\cvarlist. None of that makes it do anything. A cvar nothing reads
looks exactly like a working setting: you set it, it takes the value, and the
game ignores it.

That has already produced real bugs here - g_spawnItemWeapons was registered,
exposed as a gamerule, documented, set to 0 in the shipped instagib configs,
and read by no code at all, so instagib servers kept spawning weapons.

240 cvars in this tree are unread, and they are STAYING. Nearly all of them are
Quake Live's own, transcribed from the real binary's cvar table so that a
factory file, a server config or a stock client finds the name and flags it
expects; wiring one up means implementing the feature behind it, not deleting
the name. So a flat list of 240 is useless - it is the same list every time,
nobody reads it, and a new one added tomorrow disappears into it.

This works the way tools/stub-report.py does for empty function bodies instead:
every unread cvar has a one-time reviewed verdict in docs/cvar-manifest.txt, and
a cvar that is unread and NOT in that file is UNCLASSIFIED and fails the build.
The 240 become a backlog you can read; the 241st becomes a build failure.

A cvar counts as consumed if any of these mention it:
  - C code, via its vmCvar_t symbol (.integer / .value / .string)
  - C code, via its name as a string (trap_Cvar_VariableStringBuffer etc.)
  - any menu, hud or cfg asset, ours or Quake Live's

Pass a directory of Quake Live's own ui/ files as the first argument to include
them; without it, anything only its menus reference is reported as unread and
needs a QL-ASSET verdict.

    tools/dead-cvars.py [/path/to/ql/ui]
    tools/dead-cvars.py --list          print the unread set as manifest rows
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

MANIFEST = "docs/cvar-manifest.txt"

VERDICTS = {
    "QL-FEATURE": "Quake Live feature this port has not implemented yet",
    "NETWORKED":  "the consumer is a remote client or the server browser",
    "QL-ASSET":   "read by Quake Live's own pak00 menus, not visible from here",
    "GAP":        "genuinely nothing - these are the ones to fix",
}


def slurp(patterns):
    out = []
    for pat in patterns:
        for path in glob.glob(pat, recursive=True):
            if os.path.isfile(path):
                with open(path, errors="ignore") as fh:
                    out.append(fh.read())
    return "\n".join(out)


def find_unread(assets_extra=None):
    """[(module, name, flags)] for every registered cvar nothing consumes."""
    code = slurp(CODE)
    asset_pats = list(ASSETS)
    if assets_extra:
        asset_pats.append(os.path.join(assets_extra, "**", "*"))
    assets = slurp(asset_pats)

    unread = []
    counts = {}
    for module, table in TABLES.items():
        with open(table) as fh:
            src = fh.read()
        entries = re.findall(
            r'\{\s*&(\w+)\s*,\s*"([^"]+)"\s*,\s*"[^"]*"\s*,\s*([^,]+),', src)
        counts[module] = len(entries)
        for var, name, flags in entries:
            by_symbol = re.search(
                r"\b%s\s*\.\s*(?:integer|value|string)\b" % re.escape(var), code)
            # >1 because the registration row itself contains the name
            by_name = len(re.findall(r'"%s"' % re.escape(name), code)) > 1
            if not by_symbol and not by_name and name not in assets:
                unread.append((module, name, flags.strip()))
    return unread, counts


def load_manifest():
    """{name: (verdict, reason)}. Keyed on the cvar name, which is unique."""
    out = {}
    if not os.path.exists(MANIFEST):
        return out
    with open(MANIFEST) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            verdict, name, reason = parts[0].strip(), parts[1].strip(), parts[2].strip()
            out[name] = (verdict, reason)
    return out


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    args = [a for a in argv[1:] if not a.startswith("--")]
    want_list = "--list" in argv[1:]

    unread, counts = find_unread(args[0] if args else None)
    if not args and not want_list:
        print("note: no Quake Live ui/ directory given, so cvars only its own\n"
              "      menus reference are counted as unread.\n")

    if want_list:
        # Regenerating the manifest: emit rows to be edited and checked in.
        for module, name, flags in sorted(unread, key=lambda e: (e[0], e[1])):
            print("QL-FEATURE\t%s\t%s, %s" % (name, module, flags))
        return 0

    manifest = load_manifest()

    unclassified = []
    by_verdict = {}
    for module, name, flags in unread:
        if name in manifest:
            verdict = manifest[name][0]
            by_verdict.setdefault(verdict, []).append(name)
        else:
            unclassified.append((module, name, flags))

    total = sum(counts.values())
    print("dead-cvars: %d of %d registered cvars are read by nothing" %
          (len(unread), total))
    for verdict in sorted(by_verdict):
        print("  %-12s %4d   %s" %
              (verdict, len(by_verdict[verdict]), VERDICTS.get(verdict, "")))

    bad = [v for v in by_verdict if v not in VERDICTS]
    if bad:
        print("\ndead-cvars: unknown verdict(s) in %s: %s" %
              (MANIFEST, ", ".join(sorted(bad))), file=sys.stderr)
        return 1

    # A stale row is worth knowing about too: it means a cvar got wired up and
    # the manifest still calls it unread, which is a backlog item silently done.
    live = set(n for _, n, _ in unread)
    stale = sorted(n for n in manifest if n not in live)
    if stale:
        print("\ndead-cvars: %d manifest row(s) for cvars that ARE read now - "
              "wired up since, remove the row:" % len(stale))
        for name in stale:
            print("    %s" % name)

    if unclassified:
        print("\ndead-cvars: %d UNCLASSIFIED cvar(s) - registered, read by "
              "nothing, no verdict in %s." % (len(unclassified), MANIFEST),
              file=sys.stderr)
        print("A cvar that looks like a setting and does nothing has cost this "
              "tree real bugs twice.\nDecide what it is and add a row:\n",
              file=sys.stderr)
        for module, name, flags in sorted(unclassified, key=lambda e: (e[0], e[1])):
            print("    QL-FEATURE\t%s\t%s, %s" % (name, module, flags),
                  file=sys.stderr)
        return 1

    print("\ndead-cvars: ok - every unread cvar has a recorded verdict")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
