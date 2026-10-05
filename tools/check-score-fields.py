#!/usr/bin/env python3
"""Check that every scoreboard emitter and its parser agree on field count.

This mismatch has now cost four separate rounds of debugging:

  - scores_ffa read field 17 into the wrong member, so WEAP drew 0% for
    everyone.
  - scores_ft read 16 fields where the emitter wrote 17, shifting the tail.
  - fixing that added the missing read but left the "i++ // unknown field"
    that had been standing in for it, so it then read 18 for 17 - and every
    entry after the first drifted, which blanked the team scoreboards.
  - tinfo emitted Quake 3's six fields per player where the client expected
    Quake Live's one, which was fatal on joining any team gametype.

None of these produce an error. The parser reads whatever is next in the
argv list, so a drift of one field shows up as a wrong number, a blank row,
or a client number of 85 - never as "the scoreboard message is malformed".

Counting them is mechanical, so it should not be done by eye.

Usage:  python3 tools/check-score-fields.py
Exit:   0 when every pair agrees, 1 otherwise.
"""

import os
import re
import sys

GAME_DIR = "code/game"
PARSER = "code/cgame/cg_servercmds.c"

# emitter file -> (parser function, which Com_sprintf(entry,...) in that file).
# A file can build more than one kind of row: the scoreboard row and a
# separate per-weapon or per-stat row that has its own verb and parser.
PAIRS = [
    ("g_gametype_ffa.c", 0, "CG_ParseScoreEntry_Ffa", "scores_ffa"),
    ("g_gametype_ffa.c", 1, "CG_ParseScoreEntry_Sm", "smscores"),
    ("g_gametype_tdm.c", 0, "CG_ParseScoreEntry_Tdm", "scores_tdm"),
    ("g_gametype_ca.c", 2, "CG_ParseScoreEntry_Ca", "scores_ca"),
    ("g_gametype_ctf.c", 0, "CG_ParseScoreEntry_Ctf", "scores_ctf"),
    ("g_gametype_ft.c", 0, "CG_ParseScoreEntry_Ft", "scores_ft"),
    ("g_gametype_rr.c", 0, "CG_ParseScoreEntry_Rr", "scores_rr"),
    ("g_gametype_race.c", 0, "CG_ParseScoreEntry_Race", "scores_race"),
]


def emitter_counts(path):
    """Every Com_sprintf(entry, sizeof(entry), "...") format's %i/%d count."""
    text = open(path, encoding="utf-8", errors="replace").read()
    counts = []
    for m in re.finditer(
        r'Com_sprintf\(\s*entry,\s*sizeof\(entry\),\s*((?:"[^"]*"\s*)+)', text
    ):
        fmt = "".join(re.findall(r'"([^"]*)"', m.group(1)))
        counts.append(fmt.count("%i") + fmt.count("%d"))
    return counts


def parser_counts(path):
    """Reads performed by each CG_ParseScoreEntry_* helper.

    A bare "i++;" counts: skipping a field consumes one just as a read does,
    which is exactly how the Freeze Tag entry ended up one over.
    """
    text = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(
        r"static int (CG_ParseScoreEntry_\w+)\(score_t \*sp, int (\w+)\) \{(.*?)\n\}",
        text,
        re.S,
    ):
        name, var, body = m.group(1), m.group(2), m.group(3)
        reads = len(re.findall(r"CG_Argv\(\s*" + var + r"\+\+\s*\)", body))
        reads += len(re.findall(r"^\s*" + var + r"\+\+;", body, re.M))
        out[name] = reads
    return out


def main():
    if not os.path.isdir(GAME_DIR) or not os.path.isfile(PARSER):
        sys.stderr.write("run this from the repository root\n")
        return 2

    parsers = parser_counts(PARSER)
    emitters = {}
    bad = 0

    for fname, index, func, verb in PAIRS:
        path = os.path.join(GAME_DIR, fname)
        if path not in emitters:
            emitters[path] = emitter_counts(path)
        counts = emitters[path]

        if index >= len(counts):
            print("%-14s ?  no Com_sprintf(entry) #%d in %s" % (verb, index, fname))
            bad += 1
            continue
        if func not in parsers:
            print("%-14s ?  parser %s not found" % (verb, func))
            bad += 1
            continue

        wrote, read = counts[index], parsers[func]
        if wrote == read:
            print("%-14s ok  %2d fields" % (verb, wrote))
        else:
            print(
                "%-14s MISMATCH  emitter writes %d, %s reads %d"
                % (verb, wrote, func, read)
            )
            bad += 1

    # scores_ad is not a per-row scoreboard: one fixed message, the round
    # history then two team scores, sent from one place (AD_SendScores, K4)
    # and read by CG_InitScores with a loop and two fixed reads. The stock
    # cgame parses the same shape, so the count is pinned, not just matched.
    ad_src = open(os.path.join(GAME_DIR, "g_gametype_ad.c"), encoding="utf-8", errors="replace").read()
    sends = re.findall(r'va\(\s*"scores_ad((?: %d)+)"', ad_src)
    pt = open(PARSER, encoding="utf-8", errors="replace").read()
    m = re.search(r"void CG_InitScores\(void\) \{(.*?)\n\}", pt, re.S)
    if len(sends) != 1:
        print("%-14s ?  expected one sender, found %d" % ("scores_ad", len(sends)))
        bad += 1
    elif not m:
        print("%-14s ?  parser CG_InitScores not found" % "scores_ad")
        bad += 1
    else:
        wrote = sends[0].count("%d")
        loop = re.search(r"for \(i = 0; i < (\d+); i\+\+\)\s*\{?\s*cg\.adScores\[i\] = atoi\(CG_Argv\(i \+ 1\)\)", m.group(1))
        fixed = sorted(int(x) for x in re.findall(r"CG_Argv\((\d+)\)", m.group(1)))
        read = (int(loop.group(1)) if loop else 0) + len(fixed)
        contiguous = loop and fixed == list(range(int(loop.group(1)) + 1, int(loop.group(1)) + 1 + len(fixed)))
        if wrote == read == 22 and contiguous:
            print("%-14s ok  %2d fields (20 history + 2 team scores, stock shape)" % ("scores_ad", wrote))
        else:
            print("%-14s MISMATCH  emitter writes %d, CG_InitScores reads %d%s; stock is 22"
                  % ("scores_ad", wrote, read, "" if contiguous else ", not contiguous"))
            bad += 1

    print()
    if bad:
        print("%d mismatch(es) - a scoreboard will show wrong or blank rows" % bad)
        return 1
    print("all scoreboard emitters and parsers agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
