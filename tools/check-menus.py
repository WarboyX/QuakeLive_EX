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


# [QL] R24. Written in a comment on, or just above, an itemDef whose overlap is
# deliberate - a button over its highlight bar, a full-page fade.
WAIVER = "check-menus: overlap-ok"


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


def exclusive(a, b):
    """
    True when two items can never be on screen together, so sharing space is
    deliberate rather than a collision.

    The only form of this the engine gives us is cvarTest: an item draws when
    its cvar's value is in showCvar (or is not in hideCvar). Two items keyed on
    the SAME cvar with disjoint show sets are alternatives - the status rows at
    the top of the ray tracing page are exactly that, and the comment there
    explains why they had to be written per cvar rather than per outcome.
    Different cvars prove nothing: both can be true at once.
    """
    if not a["test"] or a["test"] != b["test"]:
        return False
    sa, sb = a.get("showcvar"), b.get("showcvar")
    if sa and sb:
        return not (sa & sb)
    ha, hb = a.get("hidecvar"), b.get("hidecvar")
    if sa and hb:
        return sa <= hb
    if sb and ha:
        return sb <= ha
    return False


def check_geometry(menu_name, menu_rect, items, report, waived=(), menu_line=0):
    """
    [QL] R24. Two ways a .menu is wrong that parse perfectly.

    An item whose rect overlaps another's draws over it - which is how the ray
    tracing page ended up printing its help text through two of its own rows.
    An item that extends past its menuDef's rect draws outside the window's
    border, which is how APPLY ended up below the frame.

    Both are invisible to every other check in this file and to the engine.
    Rects are relative to the parent window, so the frame is 0 0 w h.
    """
    live = [i for i in items if i["rect"] and not i["hidden"]]

    # A waiver on or just above the menuDef covers the whole page, for one that
    # is layered by design - the main menu draws every button over its own
    # highlight bar and a fade over all of them. Per-item waivers there would be
    # a dozen copies of one reason.
    page_waived = any(menu_line - 6 <= m <= menu_line + 2 for m in waived)

    if menu_rect and not page_waived:
        mw, mh = menu_rect[2], menu_rect[3]
        for i in live:
            x, y, w, h = i["rect"]
            who = i["name"] or "item"
            if x < 0 or y < 0 or x + w > mw or y + h > mh:
                report(i["line"], "%s '%s' at %g %g %g %g falls outside the menu's "
                       "%g x %g frame - it draws past the border"
                       % (menu_name or "?", who, x, y, w, h, mw, mh))

    for ai in range(len(live)):
        for bi in range(ai + 1, len(live)):
            a, b = live[ai], live[bi]
            ax, ay, aw, ah = a["rect"]
            bx, by, bw, bh = b["rect"]
            ox = min(ax + aw, bx + bw) - max(ax, bx)
            oy = min(ay + ah, by + bh) - max(ay, by)
            if ox <= 0 or oy <= 0:
                continue
            if exclusive(a, b):
                continue
            # An overlap can be the point: a button drawn over its own
            # highlight bar, a fade that covers the page. Those say so.
            if page_waived:
                continue
            # The marker normally sits in a comment just above the itemDef, so
            # look a few lines back as well as inside it.
            if any(a["line"] - 5 <= m <= a.get("end", a["line"]) or
                   b["line"] - 5 <= m <= b.get("end", b["line"]) for m in waived):
                continue
            report(b["line"], "%s '%s' overlaps '%s' (line %d) by %g x %g - "
                   "they draw on top of each other"
                   % (menu_name or "?", b["name"] or "item",
                      a["name"] or "item", a["line"], ox, oy))


def check_file(path, problems, defined, referenced, shape):
    raw = open(path, "r", errors="replace").read()
    tokens = tokenize(strip_comments(raw))

    # Comments are stripped before tokenising, so the waiver is read off the raw
    # text. Deliberately a comment and not a keyword: the engine must never see
    # it, and it should sit where the reason for the overlap is written down.
    waived = set()
    for n, ln in enumerate(raw.splitlines(), 1):
        if WAIVER in ln:
            waived.add(n)

    depth = 0
    menu_depth = None
    menu_name = None
    menu_line = 0
    item_depth = None
    item_has_rect = False
    item_line = 0

    # [QL] Geometry. The parser has no opinion about where an item lands, so a
    # row appended at a y that already belongs to something else draws on top of
    # it and nothing anywhere says so - see R24. These two lists carry the
    # current menuDef's frame and its items until the menuDef closes.
    menu_rect = None
    items = []
    cur = None

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
                if cur is not None:
                    cur["end"] = line
                cur = None
            if menu_depth is not None and depth < menu_depth:
                if menu_name is None:
                    report(menu_line, "menuDef has no name")
                if shape:
                    check_geometry(menu_name, menu_rect, items, report, waived,
                                   menu_line)
                menu_depth = None
                menu_name = None
                menu_rect = None
                items = []
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
            cur = {"line": line, "end": line, "rect": None, "test": None,
                   "hidden": False, "name": None, "showcvar": None,
                   "hidecvar": None}
            items.append(cur)
            continue

        if low == "rect":
            nums = []
            for k in range(idx + 1, min(idx + 5, len(tokens))):
                try:
                    nums.append(float(tokens[k][0]))
                except ValueError:
                    break
            if item_depth is not None:
                item_has_rect = True
                if cur is not None and len(nums) == 4:
                    cur["rect"] = nums
            elif menu_depth is not None and len(nums) == 4:
                menu_rect = nums
            continue

        if item_depth is not None and cur is not None:
            if low == "cvartest" and idx + 1 < len(tokens):
                cur["test"] = tokens[idx + 1][0].strip('"').lower()
                continue
            if low in ("showcvar", "hidecvar"):
                vals = []
                k = idx + 2 if idx + 1 < len(tokens) and tokens[idx + 1][0] == "{" else None
                while k is not None and k < len(tokens) and tokens[k][0] != "}":
                    vals.append(tokens[k][0].strip('"'))
                    k += 1
                cur[low] = set(vals)
                continue
            if low == "visible" and idx + 1 < len(tokens):
                v = tokens[idx + 1][0].strip('"').lower()
                if v in ("0", "menu_false", "no", "false"):
                    cur["hidden"] = True
                continue
            if low == "name" and idx + 1 < len(tokens):
                cur["name"] = tokens[idx + 1][0].strip('"')
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
