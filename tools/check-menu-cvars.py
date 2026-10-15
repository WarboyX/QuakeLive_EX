#!/usr/bin/env python3
"""
check-menu-cvars.py - fail on a menu row bound to a cvar no code names.

[QL] E183. The in-game Bloom & Post page carried eight of Quake Live's cvars
(r_enableBloom, r_enablePostProcess, ...) that neither of our renderers
registers or reads. A row like that sets a value, shows it, saves it - and
nothing happens. "Post processing: Yes" sat there while r_fbo, the real
switch, was 0, and the tester's water stayed plain. The same scan found the
create-server page writing g_teamsize (the game reads "teamsize") and
pmove_HookPullVelocity (it reads "pmove_velocity_gh").

This is the menu-side twin of tools/dead-cvars.py, which catches the
registered-but-unread direction. Here: every cvar a menu of ours names
(cvar, cvarFloat, cvarTest) must appear as a string literal somewhere in our
C code - registered by the engine, a renderer, the game, cgame or ui. Quake
Live's own menus are not checked; they name what the real binary had.

Usage: tools/check-menu-cvars.py            (exit 1 on any unknown name)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MENUS = sorted((ROOT / "content/pak01/ui").glob("*.menu"))
# Engine-side names the code builds rather than spells, and cvars a menu may
# legitimately read only to test (none yet). Keep this short and say why.
ALLOW = {
}


def main():
    names = {}
    for m in MENUS:
        text = m.read_text(errors="replace")
        for n in re.findall(r'\b(?:cvar|cvarFloat|cvarTest)\s+"([A-Za-z_]\w*)"', text):
            names.setdefault(n, m.name)
    code = []
    for f in (ROOT / "code").rglob("*.[ch]"):
        p = str(f)
        if "/SDL2/" in p or "/spirv/" in p or "/libs/" in p:
            continue
        code.append(f.read_text(errors="replace"))
    blob = "\n".join(code)
    known = {s.lower() for s in re.findall(r'"([A-Za-z_]\w*)"', blob)}
    bad = sorted(n for n in names if n.lower() not in known and n not in ALLOW)
    if bad:
        print("check-menu-cvars: %d menu cvar(s) no code names - the row would do nothing:" % len(bad))
        for n in bad:
            print("  %-28s (%s)" % (n, names[n]))
        return 1
    print("check-menu-cvars: ok - %d menu cvars, every one named in the code" % len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main())
