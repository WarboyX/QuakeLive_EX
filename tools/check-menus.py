#!/usr/bin/env python3
"""Parse-check .menu files the way the engine's tokeniser does.

A malformed menu does not look malformed. The parser keeps going, so a stray or
missing brace does not stop a load - it merges the next menu into the current
one, and what you see on screen is a menu with the wrong items in it, or a
button that opens nothing because the menu it names no longer exists. That is
U9: ItemParse_cvarFloatList had no sign handling, ate '-' as a value, walked off
the end of its own list and swallowed two closing braces, and io_createserver
and io_joinserver became one menu named io_joinserver. Nothing said so where
anyone was looking.

So this checks the shape before the engine has to:

  - braces balance, per file and per menuDef
  - every menuDef has a name, and no name is defined twice
  - every menu named by open/close/conditionalopen/toggle exists somewhere in
    the set being checked
  - cvarFloatList and cvarStrList close cleanly, are not empty, and hold an
    even number of entries once commas are skipped the way the engine skips
    them - the exact shape that produced U9

It is a shape check, not the engine's parser: it does not evaluate cvars or
resolve assets. Shape is checked on our own menus only - pass a directory of
Quake Live's for the menu names they define, so that open/close targets living
in pak00 resolve instead of being reported as missing. Theirs are not checked:
they are not ours to fix, and they reuse names deliberately across alternative
hud sets, which for us would be a mistake.

    tools/check-menus.py                     # the normal case - nothing else needed
    tools/check-menus.py /path/to/ql/ui      # check against the real files instead
    tools/check-menus.py --dump-names /path/to/ql/ui > docs/ql-menu-names.txt

The names Quake Live's menus define are checked in as docs/ql-menu-names.txt
and read by default, so the plain form resolves everything and the extracted
ui/ directory is only needed to regenerate that list after a game patch.

Exit status is 1 if anything was reported, so it can gate a build.
"""

import glob
import os
import re
import sys

OURS = "content/pak01/**/*.menu"

# Menu names Quake Live's own menus define. Names only - see the header of that
# file. Read by default so the check needs nothing but the repo.
QL_NAMES = "docs/ql-menu-names.txt"

# Script commands whose argument is a menu name.
MENU_REFS = ("open", "close", "conditionalopen", "toggle")


def strip_comments(text):
    """Remove // and /* */ comments without eating them inside quotes."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = text.find('"', i + 1)
            if j < 0:
                out.append(text[i:])
                break
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            if j < 0:
                break
            out.append("\n")
            i = j + 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def tokenize(text):
    """(token, line) pairs. Quoted strings are one token; braces are their own."""
    tokens = []
    line = 1
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j = text.find('"', i + 1)
            if j < 0:
                tokens.append((text[i:], line))
                break
            tokens.append((text[i:j + 1], line))
            line += text.count("\n", i, j)
            i = j + 1
        elif c in "{}":
            tokens.append((c, line))
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '{}"':
                j += 1
            tokens.append((text[i:j], line))
            i = j
    return tokens


def check_file(path, problems, defined, referenced, shape):
    raw = open(path, "r", errors="replace").read()
    tokens = tokenize(strip_comments(raw))

    depth = 0
    menu_depth = None
    menu_name = None
    menu_line = 0
    item_depth = None
    item_has_rect = False
    item_line = 0

    def report(line, msg):
        if shape:
            problems.append("%s:%d: %s" % (path, line, msg))

    for idx, (tok, line) in enumerate(tokens):
        low = tok.lower()

        if tok == "{":
            depth += 1
            continue
        if tok == "}":
            depth -= 1
            if depth < 0:
                report(line, "closing brace with nothing open")
                return
            if item_depth is not None and depth < item_depth:
                item_depth = None
            if menu_depth is not None and depth < menu_depth:
                if menu_name is None:
                    report(menu_line, "menuDef has no name")
                menu_depth = None
                menu_name = None
            continue

        if low == "menudef":
            menu_depth = depth + 1
            menu_name = None
            menu_line = line
            continue

        if low == "itemdef":
            item_depth = depth + 1
            item_has_rect = False
            item_line = line
            continue

        if low == "rect" and item_depth is not None:
            item_has_rect = True
            continue

        if low == "name" and menu_depth is not None and item_depth is None:
            if idx + 1 < len(tokens):
                menu_name = tokens[idx + 1][0].strip('"')
                if shape and menu_name in defined and defined[menu_name][1]:
                    report(line, "menu '%s' already defined at %s"
                           % (menu_name, defined[menu_name][0]))
                if menu_name not in defined or shape:
                    defined[menu_name] = ("%s:%d" % (path, line), shape)
            continue

        # open/close/toggle name a menu only at the start of a script statement.
        # "close" is also a perfectly ordinary item name, and setitemcolor's
        # first argument is an item - neither is a menu reference.
        if low in MENU_REFS and idx + 1 < len(tokens):
            prev = tokens[idx - 1][0] if idx else "{"
            if prev in ("{", ";"):
                target = tokens[idx + 1][0].strip('"')
                if target and target not in "{};" and not target.startswith("$"):
                    referenced.setdefault(target, []).append("%s:%d" % (path, line))
            continue

        # The U9 shape: a list that must be name/value pairs and must close.
        if low in ("cvarfloatlist", "cvarstrlist"):
            if idx + 1 >= len(tokens) or tokens[idx + 1][0] != "{":
                report(line, "%s is not followed by '{'" % tok)
                continue
            j = idx + 2
            count = 0
            nested = False
            joined = False
            prev_quoted = False
            while j < len(tokens) and tokens[j][0] != "}":
                t = tokens[j][0]
                if t == "{":
                    report(line, "%s ran past its closing brace" % tok)
                    nested = True
                    break
                # ItemParse_cvarStrList skips these, so they are not entries.
                if t in (",", ";"):
                    prev_quoted = False
                    j += 1
                    continue
                quoted = t.startswith('"')
                if quoted and prev_quoted:
                    # botlib's PS_ReadString concatenates adjacent quoted
                    # strings the way a C compiler does, so these are one token
                    # to the engine, not two entries.
                    joined = True
                else:
                    count += 1
                prev_quoted = quoted
                j += 1
            if nested:
                continue
            if j >= len(tokens):
                report(line, "%s is never closed" % tok)
                continue
            if joined:
                # The engine's own diagnostic. A list written without commas
                # collapses into one token and ends up empty or short.
                report(line, "%s has adjacent quoted strings with no comma between "
                             "them - the tokeniser joins those into one entry" % tok)
            elif count == 0:
                report(line, "%s is empty" % tok)
            elif count % 2:
                report(line, "%s has %d entries - name/value pairs must be even"
                       % (tok, count))

    if depth > 0 and shape:
        problems.append("%s: %d brace(s) left open at end of file" % (path, depth))


def main(argv):
    paths = sorted(glob.glob(OURS, recursive=True))
    if not paths:
        print("no menus found under %s" % OURS)
        return 0

    args = [a for a in argv[1:] if a != "--dump-names"]
    dump = "--dump-names" in argv[1:]

    extra = []
    for d in args:
        extra += sorted(glob.glob(os.path.join(d, "**", "*.menu"), recursive=True))

    problems = []
    defined = {}
    referenced = {}

    # Quake Live's names, unless real files were supplied to check against.
    known = 0
    if not extra and os.path.exists(QL_NAMES):
        for raw in open(QL_NAMES):
            raw = raw.strip()
            if not raw or raw.startswith("#"):
                continue
            name = raw.split("\t")[0]
            defined.setdefault(name, (QL_NAMES, False))
            known += 1

    # Shape is checked on our files only. Quake Live's are read for the menu
    # names they define, so open/close targets resolve - they are not ours to
    # fix, they legitimately reuse names across alternative hud sets, and their
    # itemDefs follow conventions of their own.
    # In dump mode our own menus are left out entirely, so a name we also define
    # (main) is not attributed to us and dropped from the list - the dump has to
    # round-trip.
    for path in ([] if dump else paths):
        check_file(path, problems, defined, referenced, True)
    for path in extra:
        check_file(path, problems, defined, referenced, False)

    if dump:
        for name, (where, ours) in sorted(defined.items()):
            if not ours:
                print("%s\t%s" % (name, os.path.basename(where.rsplit(":", 1)[0])))
        return 0

    ours = set(paths)
    unresolved = []
    for target, sites in sorted(referenced.items()):
        if target in defined:
            continue
        # only mention references made from our own files
        mine = [s for s in sites if s.rsplit(":", 1)[0] in ours]
        if mine:
            unresolved.append("%s: opens menu '%s', not defined in the set checked"
                              % (", ".join(mine), target))

    for line in problems:
        print(line)
    for line in unresolved:
        print(line)

    print("\n%d menu file(s) checked (%d ours, %d reference), %d menu(s) defined "
          "(%d of them names from %s), %d problem(s), %d unresolved reference(s)"
          % (len(paths) + len(extra), len(paths), len(extra), len(defined),
             known, QL_NAMES if known else "-", len(problems), len(unresolved)))
    if unresolved and not extra and not known:
        print("note: Quake Live's menu names were not available, so a target that "
              "lives in pak00 shows up here. Pass its ui/ directory, or regenerate "
              "%s." % QL_NAMES)
    return 1 if problems or unresolved else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
