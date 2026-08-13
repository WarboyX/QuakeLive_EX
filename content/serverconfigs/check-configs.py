#!/usr/bin/env python3
"""Check that the server configs in this folder are switchable.

A config only sets what it cares about, so exec'ing one after another leaves
behind anything the second one does not mention. That is how an "FFA" server
ended up with instagib still on and no weapons spawning: ffa.cfg never
mentioned g_instaGib or g_spawnItemWeapons, so nothing put them back.

A cvar is safe if either:
  - common.cfg resets it to its stock default (every config execs common.cfg
    first, so the reset always runs), or
  - every mode config sets it, so whichever one you switch to overwrites it.

Anything else leaks across a mode switch. Run from anywhere; exits non-zero
with the offending cvars listed.
"""

import glob
import os
import re
import sys

INCLUDES = {"common.cfg", "a2m-common.cfg"}


def cvars_set(path):
    with open(path) as fh:
        # strip // comments so a commented-out example is not counted
        text = re.sub(r"//.*", "", fh.read())
    return set(re.findall(r"^\s*set\s+(\S+)", text, re.M))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    os.chdir(here)

    configs = sorted(os.path.basename(p) for p in glob.glob("*.cfg"))
    if "common.cfg" not in configs:
        print("check-configs: common.cfg missing", file=sys.stderr)
        return 1

    reset = cvars_set("common.cfg")
    modes = [c for c in configs if c not in INCLUDES]
    if not modes:
        print("check-configs: no mode configs found", file=sys.stderr)
        return 1

    per_mode = {m: cvars_set(m) for m in modes}
    set_by_every_mode = set.intersection(*per_mode.values())

    leaks = {}
    for name in configs:
        if name == "common.cfg":
            continue
        for cvar in cvars_set(name):
            if cvar in reset or cvar in set_by_every_mode:
                continue
            leaks.setdefault(cvar, []).append(name)

    if leaks:
        print("check-configs: these cvars leak across a mode switch:", file=sys.stderr)
        for cvar, files in sorted(leaks.items()):
            print("  %-24s set by %s" % (cvar, ", ".join(files)), file=sys.stderr)
        print(
            "\nAdd each to the reset block in common.cfg with its stock default,\n"
            "or set it in every mode config.",
            file=sys.stderr,
        )
        return 1

    print(
        "check-configs: ok - %d mode configs, %d cvars reset in common.cfg, "
        "%d set by every mode" % (len(modes), len(reset), len(set_by_every_mode))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
