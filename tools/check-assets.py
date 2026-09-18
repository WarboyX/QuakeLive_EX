#!/usr/bin/env python3
"""[QL] Every asset name we register, checked against the names the paks contain.

RE_RegisterModel and RE_RegisterShader return **0** for a name the pak does not
have. Zero is not an error code anyone checks - it is a handle that draws
nothing. So a typo, a Quake 3 path that Quake Live renamed, or an asset someone
assumed exists produces a model that never appears, with no warning on screen
and none in the console. That is the same silent-failure shape as a cvar nothing
reads, and it has already cost a round here: cgame registered three Freeze Tag
ice models under invented names and the pak has no ice meshes at all.

docs/pak-manifest.txt is the list of every file name inside the shipped paks -
names only, no content - which is what makes this checkable without a Quake Live
install.

WHAT IS AND IS NOT CHECKABLE, because the difference decides what a failure here
means:

  models and sounds   real file paths. If the path is not in the manifest, the
                      registration returns 0. Checked strictly.

  shaders             a shader NAME, which may be defined inside a
                      scripts/*.shader file rather than being a file at all. We
                      have the pak's file names but not the contents of its
                      shader scripts, so a name with no matching file is not
                      necessarily wrong - it may be a script-defined shader.
                      Reported separately, as unresolved rather than missing.

Only literal strings are checked. A name built with va() or assembled from a
config string cannot be resolved here, and those are counted so the coverage
figure is honest rather than flattering.

    tools/check-assets.py            check, exit non-zero on a missing file
    tools/check-assets.py --shaders  also list unresolved shader names
"""

import os
import re
import sys

MANIFEST = "docs/pak-manifest.txt"

SOURCES = ("code/cgame", "code/ui", "code/game", "code/renderervk", "code/client")

# trap_R_RegisterModel( "..." ) and friends. The trap_ prefix is the VM side,
# the RE_ prefix the engine side; both end in the same place.
CALLS = {
    "model":  r'(?:trap_R_|RE_)RegisterModel\w*\s*\(\s*"([^"]*)"',
    "shader": r'(?:trap_R_|RE_)RegisterShader\w*\s*\(\s*"([^"]*)"',
    "sound":  r'(?:trap_S_|S_)RegisterSound\w*\s*\(\s*"([^"]*)"',
    "skin":   r'(?:trap_R_|RE_)RegisterSkin\w*\s*\(\s*"([^"]*)"',
}

# Non-literal first arguments, counted so the coverage number is honest.
DYNAMIC = {
    k: v.replace(r'\s*"([^"]*)"', r'\s*(?!")')
    for k, v in CALLS.items()
}

# Sound and image loaders try a list of extensions, so a registration naming one
# is satisfied by any of them. This is the engine's behaviour, not a courtesy.
ALT_EXT = {
    ".wav": (".wav", ".ogg", ".opus"),
    ".ogg": (".ogg", ".wav", ".opus"),
    ".tga": (".tga", ".jpg", ".jpeg", ".png", ".pcx"),
    ".jpg": (".jpg", ".jpeg", ".tga", ".png", ".pcx"),
    ".png": (".png", ".tga", ".jpg", ".jpeg"),
}


def load_manifest():
    """Set of lowercase paths inside the paks, plus a set without extensions."""
    exact, stems = set(), set()
    if not os.path.exists(MANIFEST):
        print("check-assets: %s not found" % MANIFEST, file=sys.stderr)
        sys.exit(2)
    with open(MANIFEST, errors="ignore") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            path = line.split("\t")[-1].strip().lower().replace("\\", "/")
            if not path:
                continue
            exact.add(path)
            stems.add(os.path.splitext(path)[0])
    return exact, stems


def resolves(name, exact, stems):
    """Would the engine find something for this name?"""
    n = name.strip().lower().replace("\\", "/").lstrip("/")
    if not n:
        return False
    if n in exact:
        return True
    root, ext = os.path.splitext(n)
    if ext in ALT_EXT:
        for alt in ALT_EXT[ext]:
            if root + alt in exact:
                return True
    # extensionless: any file with this stem, which is how shaders and the
    # image loader's extension search both behave
    return root in stems or n in stems


def strip_comments(text):
    """Blank out // and /* */ comments, keeping line numbers intact.

    Without this the checker reports code nobody compiles. The first run flagged
    sound/weapons/nailgun/wnalflit.ogg as missing from the paks, which it is -
    and the line registering it has been commented out for as long as the file
    has existed. A checker that cannot tell live code from a commented-out line
    produces exactly the kind of noise that gets a tool ignored.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':                        # a string may contain // or /*
            out.append(c); i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    out.append(" "); i += 1
                    if i < n:
                        out.append(" "); i += 1
                    continue
                out.append(text[i]); i += 1
            if i < n:
                out.append(text[i]); i += 1
            continue
        if c == "/" and i + 1 < n and text[i+1] == "/":
            while i < n and text[i] != "\n":
                out.append(" "); i += 1
            continue
        if c == "/" and i + 1 < n and text[i+1] == "*":
            while i + 1 < n and not (text[i] == "*" and text[i+1] == "/"):
                out.append("\n" if text[i] == "\n" else " "); i += 1
            out.append("  "); i += 2
            continue
        out.append(c); i += 1
    return "".join(out)


def sources():
    for base in SOURCES:
        for root, _dirs, files in os.walk(base):
            for f in files:
                if f.endswith((".c", ".h")):
                    yield os.path.join(root, f)


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    show_shaders = "--shaders" in argv[1:]

    exact, stems = load_manifest()
    missing, unresolved = [], []
    counted = {k: 0 for k in CALLS}
    dynamic = {k: 0 for k in CALLS}

    for path in sources():
        with open(path, errors="ignore") as fh:
            text = strip_comments(fh.read())
        for kind, pat in CALLS.items():
            for m in re.finditer(pat, text):
                name = m.group(1)
                counted[kind] += 1
                if not name:
                    continue                    # "" is the deliberate no-asset
                if resolves(name, exact, stems):
                    continue
                line = text[:m.start()].count("\n") + 1
                # a shader may be defined inside a .shader script we cannot read
                (unresolved if kind == "shader" else missing).append(
                    (kind, name, path, line))
        for kind, pat in DYNAMIC.items():
            dynamic[kind] += len(re.findall(pat, text))

    total = sum(counted.values())
    print("check-assets: %d literal registrations checked against %d pak names"
          % (total, len(exact)))
    for kind in sorted(counted):
        print("   %-7s %4d literal, %4d built at runtime (not checkable here)"
              % (kind, counted[kind], dynamic[kind]))

    if unresolved:
        print("\ncheck-assets: %d shader name(s) with no matching file in the "
              "paks." % len(unresolved))
        print("These are NOT necessarily wrong - a shader can be defined inside "
              "a scripts/*.shader\nfile, and the manifest lists file names, not "
              "shader-script contents.%s"
              % ("" if show_shaders else " Pass --shaders to list them."))
        if show_shaders:
            for kind, name, path, line in sorted(unresolved, key=lambda e: e[1]):
                print("    %-52s %s:%d" % (name, path, line))

    if missing:
        print("\ncheck-assets: %d model/sound/skin name(s) the paks DO NOT "
              "CONTAIN." % len(missing), file=sys.stderr)
        print("These register as handle 0, draw or play nothing, and report "
              "nothing.\n", file=sys.stderr)
        for kind, name, path, line in sorted(missing, key=lambda e: (e[0], e[1])):
            print("    %-7s %-46s %s:%d" % (kind, name, path, line),
                  file=sys.stderr)
        return 1

    print("\ncheck-assets: ok - every literal model, sound and skin name is in "
          "the paks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
