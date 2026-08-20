#!/usr/bin/env python3
"""
stub-report.py - inventory every empty function body in the tree and check it
against a reviewed verdict.

Why this exists
---------------
An empty function is indistinguishable from a finished one at the call site. The
compiler is happy, the linker is happy, and the only way to find out is to play
the game and notice that nothing happened. This branch has lost rounds to exactly
that: a shader that never registered, a cvar nothing read, a scoreboard verb with
no handler. tools/dead-cvars.py does this for cvars; this does it for functions.

The point is NOT that every empty function is a bug. Most of them are correct:
code/null/ is a set of deliberate do-nothing drivers so the dedicated server can
link without a client, a spawn-point entity's whole job is to exist, and plenty
of Quake 3 functions ship empty upstream. The point is that each one should have
been *looked at once* and the verdict written down, so that:

  - a genuinely missing implementation is on a list instead of in the game, and
  - a new empty function added next month shows up as unclassified rather than
    blending into the 77 that were already there.

Verdicts
--------
  BY-DESIGN        Empty is the correct implementation (null drivers, marker
                   entities, dummy handlers).
  UPSTREAM         Empty in Quake 3 / ioquake3 / Quake3e too. Not ours to fill,
                   and filling it would diverge from the base for no reason.
  WIRED-ELSEWHERE  The behaviour exists, just not here (the real work is in
                   another function, and the note says which).
  GAP              Genuinely missing. These are the ones to fix.

Usage
-----
  tools/stub-report.py             report and exit non-zero on unclassified
  tools/stub-report.py --gaps      list only the open GAP entries
  tools/stub-report.py --list      dump every stub with its verdict

Add a new verdict by putting a line in docs/stub-manifest.txt:

  VERDICT<tab>path/to/file.c:FunctionName<tab>one-line reason
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "docs", "stub-manifest.txt")

# Vendored third-party trees. Their stubs are not ours to classify and listing
# them would bury the ones that are.
SKIP_PREFIXES = (
    "code/SDL2", "code/libjpeg", "code/zlib", "code/botlib",
    "code/renderercommon/vulkan", "code/tools", "code/libcurl",
    "code/freetype", "code/opus", "code/ogg",
)

VERDICTS = ("BY-DESIGN", "UPSTREAM", "WIRED-ELSEWHERE", "GAP")

FUNC_RE = re.compile(
    r'^(?:static\s+)?(?:void|int|qboolean|float|char\s*\*|const\s+char\s*\*)\s+'
    r'\*?(\w+)\s*\(.*\)\s*\{\s*$'
)


def strip_comments(lines):
    """Blank out // and /* */ comments, line by line.

    Doing this properly matters: the first cut treated any line starting with
    '*' as a block-comment continuation, which is also what a pointer
    dereference looks like - `*x = *x * scale;` - so functions with a real body
    were reported as empty. A scanner with false positives is worse than no
    scanner, because it trains you to skim the output.
    """
    out = []
    in_block = False
    for line in lines:
        res = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end == -1:
                    i = len(line)
                else:
                    in_block = False
                    i = end + 2
                continue
            if line.startswith("//", i):
                break
            if line.startswith("/*", i):
                in_block = True
                i += 2
                continue
            res.append(line[i])
            i += 1
        out.append("".join(res))
    return out


def find_stubs():
    """Every function whose body is empty apart from comments."""
    found = []
    for dirpath, _, filenames in os.walk(os.path.join(ROOT, "code")):
        rel_dir = os.path.relpath(dirpath, ROOT).replace(os.sep, "/")
        if any(rel_dir.startswith(p) for p in SKIP_PREFIXES):
            continue
        for name in sorted(filenames):
            if not name.endswith(".c"):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            with open(path, errors="ignore") as fh:
                raw = fh.read()
            lines = raw.split("\n")
            stripped = strip_comments(lines)
            for i, line in enumerate(lines):
                m = FUNC_RE.match(line)
                if not m:
                    continue
                # first real token after the brace, comments already removed
                body = []
                for j in range(i + 1, min(i + 8, len(stripped))):
                    t = stripped[j].strip()
                    if not t:
                        continue
                    body.append(t)
                    break
                if body == ["}"]:
                    found.append((rel, m.group(1), i + 1))
    return found


def load_manifest():
    verdicts = {}
    if not os.path.exists(MANIFEST):
        return verdicts
    with open(MANIFEST) as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                sys.stderr.write("stub-manifest: malformed line: %s\n" % line)
                continue
            verdict, key = parts[0].strip(), parts[1].strip()
            reason = parts[2].strip() if len(parts) > 2 else ""
            if verdict not in VERDICTS:
                sys.stderr.write("stub-manifest: unknown verdict %r on %s\n" % (verdict, key))
                continue
            verdicts[key] = (verdict, reason)
    return verdicts


def main():
    args = sys.argv[1:]
    stubs = find_stubs()
    verdicts = load_manifest()

    classified = []
    unclassified = []
    for rel, func, line in stubs:
        key = "%s:%s" % (rel, func)
        if key in verdicts:
            v, reason = verdicts[key]
            classified.append((v, key, line, reason))
        else:
            unclassified.append((key, line))

    if "--gaps" in args:
        gaps = [c for c in classified if c[0] == "GAP"]
        for _, key, line, reason in sorted(gaps):
            print("%s:%d\t%s" % (key.rsplit(":", 1)[0], line, reason))
        return 0

    if "--list" in args:
        for v, key, line, reason in sorted(classified):
            print("%-16s %s:%d\t%s" % (v, key.rsplit(":", 1)[0], line, reason))
        for key, line in sorted(unclassified):
            print("%-16s %s:%d" % ("UNCLASSIFIED", key.rsplit(":", 1)[0], line))
        return 0

    counts = {}
    for v, _, _, _ in classified:
        counts[v] = counts.get(v, 0) + 1

    print("stub-report: %d empty function bodies outside vendored trees" % len(stubs))
    for v in VERDICTS:
        if counts.get(v):
            print("  %-16s %d" % (v, counts[v]))

    gaps = sorted(c for c in classified if c[0] == "GAP")
    if gaps:
        print("\nOpen GAPs (%d) - genuinely missing implementations:" % len(gaps))
        for _, key, line, reason in gaps:
            print("  %s:%d\n      %s" % (key.rsplit(":", 1)[0], line, reason))

    if unclassified:
        print("\nUNCLASSIFIED (%d) - new empty functions with no recorded verdict." % len(unclassified))
        print("Look at each one and add a line to docs/stub-manifest.txt:\n")
        for key, line in sorted(unclassified):
            path, func = key.rsplit(":", 1)
            print("  %s:%d  %s" % (path, line, func))
        print("\n  VERDICT<tab>%s<tab>reason      (VERDICT: %s)"
              % (sorted(unclassified)[0][0], " | ".join(VERDICTS)))
        return 1

    print("\nstub-report: ok - every empty function has a recorded verdict")
    return 0


if __name__ == "__main__":
    sys.exit(main())
