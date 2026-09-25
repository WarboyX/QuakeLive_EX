#!/usr/bin/env python3
"""
[QL] E119. Generator for the replacement scoreboard, content/pak01/ui/io_scoreboard.menu.

WHAT IT IS. The TAB scoreboard in the same visual language as our in-game menu -
Quake Live's header art, a black info bar, the content panel, muted-gold rules
and gold headings - so the two stop looking like they came from different games.

HOW IT IS SWITCHED ON. cgame parses this file alongside Quake Live's own
scoreboards, and CG_SetEndScoreboardMenu uses ours only while cg_ioScoreboard is
non-zero. That cvar is deliberately unadvertised: no menu row, no description,
default 0. Test builds turn it on from autoexec.cfg; a release leaves it off by
dropping that one line. Everything Quake Live ships keeps working untouched.

WHAT IT REUSES, AND WHY THAT MATTERS. It draws nothing of its own. The rows come
from cgame's existing feeders (FEEDER_SCOREBOARD, FEEDER_REDTEAM_LIST,
FEEDER_BLUETEAM_LIST) and the header from existing ownerdraws (CG_MAP_NAME,
CG_GAME_TYPE, CG_LEVELTIMER, CG_RED_SCORE, CG_BLUE_SCORE). Every sort, highlight,
scroll and follow-the-local-player behaviour cgame already applies to Quake
Live's board applies to this one for free, because it is the same data path.

THREE THINGS IN cgame THIS HAS TO RESPECT, all found by reading cg_draw.c:

  - A list column j asks the feeder for field j. To show ping (field 13 on the
    FFA board) the list must declare 14 columns, unused ones included.
  - CG_DrawScoreboardHeadings re-lays out any board that has more than 13
    columns AND an item whose text contains both "K/D" and "PING". So every
    column heading here is its own item, and none holds both strings.
  - CG_OffsetScoreboardList pushes every scoreboard list down to clear Quake
    Live's header band. Our lists are placed correctly already, so cgame skips
    menus named io_*.

    python3 tools/gen-scoreboard.py

Run tools/check-menus.py afterwards.
"""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "content/pak01/ui/io_scoreboard.menu"

# One palette and one text-fit check for both generated menus, imported rather
# than copied so the scoreboard cannot drift from the in-game menu.
_spec = importlib.util.spec_from_file_location("gim", ROOT / "tools/gen-ingame-menu.py")
gim = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gim)
GOLD, WHITE, DIM, BTN_EDGE = gim.GOLD, gim.WHITE, gim.DIM, gim.BTN_EDGE

W = 560                     # same width as the in-game menu frame
X, Y, H = 40, 36, 408       # centred; leaves the HUD's top and bottom edges clear
INFO_Y, PANEL_Y = 43, 62

# Team identity is carried by a thin rule under each banner, not by fills.
# E114 removed competing reds from the menu; a scoreboard needs to say which side
# is which, but a stripe does that without putting a second red block under
# Quake Live's header.
RED_RULE = ".78 .16 .1 1"
BLUE_RULE = ".16 .38 .85 1"


def deco(text, x, y, w, h=16, scale=".2", align="ITEM_ALIGN_LEFT", color=None, ax=None):
    if ax is None:
        ax = {"ITEM_ALIGN_LEFT": 0, "ITEM_ALIGN_CENTER": w // 2, "ITEM_ALIGN_RIGHT": w}[align]
    return ('        itemDef { text "%s"  textscale %s  rect %d %d %d %d  textaligny %d\n'
            '                  textalign %s  textalignx %d\n'
            '                  forecolor %s  visible 1  decoration }\n'
            % (text, scale, x, y, w, h, h - 4, align, ax, color or GOLD))


BG_WAIVER = "        // check-menus: overlap-ok - background layer; things are drawn on it\n"


def fill(name, x, y, w, h, back, border=None):
    """
    A filled background layer. Each one carries its OWN overlap waiver.

    The first version waived whole boards instead, and that switched off every
    overlap check on them - which is how column headings overlapping each other
    and a heading running past the frame got through with "0 problems". A waiver
    on the background only covers pairs that involve the background, so text
    against text is still checked.
    """
    b = ""
    if border:
        b = "  border 1  bordersize 1  bordercolor %s" % border
    return (BG_WAIVER +
            '        itemDef { name %s  rect %d %d %d %d  style WINDOW_STYLE_FILLED  visible 1  decoration\n'
            '                  backcolor %s%s }\n' % (name, x, y, w, h, back, b))


def ownerdraw(name, od, x, y, w, h, scale, align):
    # "align" rather than "textalign": cgame's ownerdraws take item->alignment,
    # which is what Quake Live's own scoreboards set (ingame_scoreboard_tdm.menu).
    #
    # x, y, w, h here describe the BOX the text should sit in, like every other
    # item. A text ownerdraw does not use it that way: CG_DrawText draws with
    # its y as the text BASELINE (fontstash FONS_ALIGN_BASELINE), and
    # textaligny does not apply to ownerdraws. Passing the box top put the map
    # name, gametype and clock above the info strip, straddling the header's
    # bottom edge - reported from a 2000x1250 laptop, but it is the same at
    # every resolution. So the emitted rect starts at the baseline: the box
    # bottom less a descender's worth of the box (a fifth of its height), and
    # runs only to the box bottom, so check-menus still sees it inside its box.
    base = y + h - (h + 4) // 5
    return ('        itemDef { name %s  ownerdraw %s  rect %d %d %d %d  textscale %s\n'
            '                  align %d  forecolor %s  visible 1  decoration }\n'
            % (name, od, x, base, w, y + h - base, scale, align, WHITE))


def listbox(name, feeder, x, y, w, h, cols):
    """
    cols is a list of (feeder field, pos, width, maxchars) for the fields shown;
    every other field up to the highest one is declared with zero width, because
    the list asks the feeder for field j by its column index j. The list painter
    skips zero-width columns (E132); it used to draw their text at the row's
    left edge, under the head icons.

    outlinecolor is the selected row's fill (Item_ListBox_Paint). It was unset,
    so all zero: a click or right-click selected the row and nothing showed it.
    """
    n = max(c[0] for c in cols) + 1
    table = {c[0]: c[1:] for c in cols}
    spec = "  ".join("%d %d %d" % table.get(j, (0, 0, 0)) for j in range(n))
    return """        itemDef {
            name %s  rect %d %d %d %d  type ITEM_TYPE_LISTBOX
            style WINDOW_STYLE_EMPTY  elementwidth %d  elementheight 18
            elementtype LISTBOX_TEXT  feeder %s
            columns %d  %s
            textscale .2  forecolor 1 1 1 1  backcolor 0 0 0 .35
            outlinecolor 0.964 0.815 0 .28
            border 1  bordersize 1  bordercolor %s  visible 1
        }
""" % (name, x, y, w, h, w, feeder, n, spec, BTN_EDGE)


def frame(name, body):
    """The shell every board shares: header art, info bar, content panel."""
    return """
    menuDef {
        name "%s"
        rect %d %d %d %d
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_EMPTY

        // check-menus: overlap-ok - background layer (header art under the logo)
        itemDef { name sb_header  rect 0 0 %d 44  background "ui/assets/main_menu/header.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1
                  border 1  bordercolor 0 0 0 0.5 }
        // No overhang, unlike the in-game menu's logo: an item past the frame is
        // only allowed under a page-wide waiver, and one of those hides every
        // real overlap on the board.
        itemDef { name sb_logo  rect 0 0 176 44  background "ui/assets/main_menu/ql_logo.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1 }
%s%s%s%s        // check-menus: overlap-ok - background layer; the whole board sits on it
        itemDef { name sb_panel  rect 0 %d %d %d
                  background "ui/assets/main_menu/content_background.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 1 1 1 1  border 1  bordercolor 0 0 0 0.5 }
%s    }
""" % (name, X, Y, W, H, W,
       fill("sb_info", 0, INFO_Y, W, 18, "0 0 0 0.75", "0 0 0 0.5"),
       ownerdraw("sb_map", "CG_MAP_NAME", 10, INFO_Y + 2, 170, 14, ".18", 0),
       ownerdraw("sb_type", "CG_GAME_TYPE", 190, INFO_Y + 2, 180, 14, ".18", 1),
       ownerdraw("sb_time", "CG_LEVELTIMER", 470, INFO_Y + 2, 80, 14, ".18", 2),
       PANEL_Y, W, H - PANEL_Y, body)


# ---------------------------------------------------------------- FFA --------
# Field map from CG_FeederColumnsFfa: 0 model icon, 3 ready icon, 5 name,
# 7 score, 8 kills/deaths, 9 damage, 11 accuracy, 12 time, 13 ping.
FFA_COLS = [(0, 4, 14, 1), (3, 20, 12, 1), (5, 36, 196, 22), (7, 240, 44, 6),
            (8, 290, 60, 8), (9, 356, 52, 7), (11, 414, 44, 5), (12, 462, 36, 4),
            (13, 504, 32, 5)]
# Each heading sits over its column and is exactly as wide as it, so headings
# cannot overlap each other and cannot run past the frame. Keyed by feeder field.
FFA_HEAD = {5: "PLAYER", 7: "SCORE", 8: "K/D", 9: "DMG", 11: "ACC", 12: "TIME", 13: "PING"}


def board_ffa():
    lx = 10
    b = ""
    for field, pos, w, _ in FFA_COLS:
        if field in FFA_HEAD:
            b += deco(FFA_HEAD[field], lx + pos, PANEL_Y + 10, w, 16, ".2")
    b += fill("sb_rule", 10, PANEL_Y + 28, 540, 1, BTN_EDGE)
    b += listbox("sb_list", "FEEDER_SCOREBOARD", lx, PANEL_Y + 32, 540, H - PANEL_Y - 44,
                 FFA_COLS)
    return frame("io_score_ffa", b)


# ---------------------------------------------------------------- TEAM -------
# Two lists side by side, red left and blue right, as Quake Live lays them out.
# Each team gametype routes the list fields differently (cg_main.c):
#   TDM / FT : 6 score, 7 K/D (net in some modes), 8 damage (thaws in FT), 9 ping
#   CA       : 6 score, 7 K/D, 8 damage, 10 accuracy, 11 ping
#   CTF family (CTF, 1FCTF, HAR, DOM, AD) : 6 score, 7 K/D, 8 captures, 11 ping
TEAM_VARIANTS = {
    "io_score_tdm": ([(6, "SCORE"), (7, "K/D"), (8, "DMG"), (9, "PING")]),
    "io_score_ca":  ([(6, "SCORE"), (7, "K/D"), (8, "DMG"), (11, "PING")]),
    "io_score_ctf": ([(6, "SCORE"), (7, "K/D"), (8, "CAPS"), (11, "PING")]),
}
TEAM_LIST_W = 268
# Positions within one 268-wide team list, for the four stat columns shown.
# Ping was 18 wide, narrower than its own "PING" heading at .17 (~24px); the
# text-fit check catches that now that headings are sized to their columns.
STAT_POS = [(128, 32, 5), (162, 42, 7), (206, 32, 6), (240, 26, 4)]


def board_team(name, stats):
    b = ""
    for side, (lx, label, rule, score_od, feeder) in enumerate([
            (8, "RED", RED_RULE, "CG_RED_SCORE", "FEEDER_REDTEAM_LIST"),
            (284, "BLUE", BLUE_RULE, "CG_BLUE_SCORE", "FEEDER_BLUETEAM_LIST")]):
        top = PANEL_Y + 8
        b += fill("sb_band%d" % side, lx, top, TEAM_LIST_W, 26, "0 0 0 .55")
        b += fill("sb_team%d" % side, lx, top + 26, TEAM_LIST_W, 2, rule)
        b += deco(label, lx + 8, top + 5, 120, 18, ".24", color=WHITE)
        b += ownerdraw("sb_score%d" % side, score_od, lx + TEAM_LIST_W - 90, top + 3,
                       82, 22, ".32", 2)
        cols = [(0, 4, 14, 1), (3, 20, 12, 1), (5, 36, 88, 10)]
        head = [("PLAYER", 36, 88)]
        for (field, text), (pos, w, mx) in zip(stats, STAT_POS):
            cols.append((field, pos, w, mx))
            head.append((text, pos, w))
        for text, pos, w in head:
            b += deco(text, lx + pos, top + 34, w, 14, ".17")
        b += fill("sb_rule%d" % side, lx, top + 50, TEAM_LIST_W, 1, BTN_EDGE)
        b += listbox("sb_list%d" % side, feeder, lx, top + 54, TEAM_LIST_W,
                     H - PANEL_Y - 74, cols)
    return frame(name, b)


# ---------------------------------------------------------------- popups -----
# [QL] E120. The right-click menu and the stats panel. cgame positions the menu
# beside the clicked row, paints both after the board, and runs every button's
# "io_sbaction <verb>" against the player it was opened on (cg_newdraw.c).
#
# Rows are 18 tall on a 19px pitch. Kick and Ban do not act on the first click:
# they reveal a confirm row at the BOTTOM of the menu, away from the button that
# armed it, so a double-click cannot confirm through the safeguard (the same
# reasoning as E117's Leave).

CTX_W = 150


def ctx_button(name, label, y, action, x=6, w=None):
    return gim.button(name, label, x, y, w or CTX_W - 12, action).replace(
        "rect %d %d %d 24" % (x, y, w or CTX_W - 12),
        "rect %d %d %d 18" % (x, y, w or CTX_W - 12)).replace(
        "textaligny 16", "textaligny 13").replace("textscale .22", "textscale .19")


def cvar_text(name, cvar, x, y, w, h=16, scale=".19", align="ITEM_ALIGN_LEFT", color=None):
    """A text item with no text of its own: Item_Text_Paint shows the cvar."""
    ax = {"ITEM_ALIGN_LEFT": 0, "ITEM_ALIGN_CENTER": w // 2, "ITEM_ALIGN_RIGHT": w}[align]
    return ('        itemDef { name %s  cvar "%s"  textscale %s  rect %d %d %d %d  textaligny %d\n'
            '                  textalign %s  textalignx %d  forecolor %s  visible 1  decoration }\n'
            % (name, cvar, scale, x, y, w, h, h - 4, align, ax, color or WHITE))


def ctx_menu():
    act = lambda verb: 'exec "io_sbaction %s"' % verb
    arm = lambda what: ('show sbc_ask ; show sbc_yes ; show sbc_no ; exec "io_sbaction arm %s"' % what)
    b = cvar_text("sbc_name", "io_sb_name", 6, 4, CTX_W - 12, 16, ".2", color=GOLD)
    b += fill("sbc_rule", 6, 22, CTX_W - 12, 1, BTN_EDGE)
    y = 27
    for name, label, verb in [("sbc_stats", "VIEW STATS", "stats"),
                              ("sbc_follow", "FOLLOW", "follow"),
                              ("sbc_vote", "VOTE KICK", "votekick")]:
        b += ctx_button(name, label, y, act(verb))
        y += 19
    b += deco("ADMIN", 6, y + 2, CTX_W - 12, 14, ".16", color=DIM)
    y += 18
    for name, label, action in [("sbc_mute", "MUTE", act("mute")),
                                ("sbc_unmute", "UNMUTE", act("unmute")),
                                ("sbc_kick", "KICK (THIS MATCH)", arm("tempban")),
                                ("sbc_ban", "BAN", arm("ban"))]:
        b += ctx_button(name, label, y, action)
        y += 19
    third = (CTX_W - 12 - 8) // 3
    for i, (name, label, verb) in enumerate([("sbc_red", "RED", "red"),
                                             ("sbc_blue", "BLUE", "blue"),
                                             ("sbc_spec", "SPEC", "spec")]):
        b += ctx_button(name, label, y, act(verb), x=6 + i * (third + 4), w=third)
    y += 24
    # confirm row, hidden until Kick or Ban arms it
    ask = cvar_text("sbc_ask", "io_sb_pending", 6, y, CTX_W - 12, 16, ".17",
                    "ITEM_ALIGN_CENTER").replace("visible 1", "visible 0")
    b += ask
    half = (CTX_W - 12 - 4) // 2
    b += gim.button("sbc_yes", "CONFIRM", 6, y + 18, half, act("confirm"), hidden=True).replace(
        "rect 6 %d %d 24" % (y + 18, half), "rect 6 %d %d 18" % (y + 18, half)).replace(
        "textaligny 16", "textaligny 13").replace("textscale .22", "textscale .19")
    b += gim.button("sbc_no", "CANCEL", 10 + half, y + 18, half,
                    'hide sbc_ask ; hide sbc_yes ; hide sbc_no ; exec "io_sbaction disarm"',
                    hidden=True).replace(
        "rect %d %d %d 24" % (10 + half, y + 18, half),
        "rect %d %d %d 18" % (10 + half, y + 18, half)).replace(
        "textaligny 16", "textaligny 13").replace("textscale .22", "textscale .19")
    h = y + 18 + 18 + 6
    return """
    menuDef {
        name "io_score_ctx"
        rect 0 0 %d %d
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_FILLED
        backcolor 0 0 0 .9
        border 1
        bordersize 1
        bordercolor %s
        focusColor %s
%s    }
""" % (CTX_W, h, BTN_EDGE, GOLD, b)


STATS_ROWS = [("Team", "io_sb_s_team"), ("Score", "io_sb_s_score"),
              ("Kills", "io_sb_s_kills"), ("Deaths", "io_sb_s_deaths"),
              ("K/D ratio", "io_sb_s_kd"), ("Net", "io_sb_s_net"),
              ("Damage", "io_sb_s_damage"), ("Accuracy", "io_sb_s_acc"),
              ("Time played", "io_sb_s_time"), ("Ping", "io_sb_s_ping")]


def stats_menu():
    w = 260
    b = cvar_text("sss_name", "io_sb_s_name", 10, 8, w - 20, 20, ".26", "ITEM_ALIGN_CENTER", GOLD)
    b += fill("sss_rule", 10, 30, w - 20, 1, BTN_EDGE)
    y = 38
    for label, cv in STATS_ROWS:
        b += deco(label, 10, y, 112, 16, ".19", "ITEM_ALIGN_RIGHT", WHITE)
        b += cvar_text("sss_" + cv[-6:].strip("_"), cv, 134, y, 116, 16, ".19")
        y += 17
    # Objective line gets the full width: "3 caps  2 assists  5 defends" does not
    # fit a value column, and its length is not known until it is filled in.
    b += deco("Objectives", 10, y + 2, w - 20, 16, ".19", "ITEM_ALIGN_CENTER", DIM)
    b += cvar_text("sss_obj", "io_sb_s_obj", 10, y + 18, w - 20, 16, ".18", "ITEM_ALIGN_CENTER")
    y += 42
    b += gim.button("sss_close", "CLOSE", (w - 100) // 2, y, 100, "close io_score_stats")
    h = y + 30
    return """
    menuDef {
        name "io_score_stats"
        rect %d %d %d %d
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_FILLED
        backcolor 0 0 0 .92
        border 1
        bordersize 1
        bordercolor %s
        focusColor %s
%s    }
""" % (X + (W - w) // 2, Y + 80, w, h, BTN_EDGE, GOLD, b)


def main():
    text = '#include "ui/menudef.h"\n\n'
    text += ("// GENERATED by tools/gen-scoreboard.py - edit that, not this.\n"
             "// Used only while cg_ioScoreboard is non-zero; see the generator.\n\n")
    text += "{\n"
    body = board_ffa()
    for name, stats in TEAM_VARIANTS.items():
        body += board_team(name, stats)
    # Popups after every board: Display_CaptureItem walks menus from the highest
    # index down, so being defined last is what makes them take the click.
    body += ctx_menu() + stats_menu()
    text += body + "}\n"

    overflow = gim.check_text_fit(body)
    if overflow:
        sys.exit("gen-scoreboard: text wider than its box:\n  " + "\n  ".join(overflow))

    OUT.write_text(text)
    print("gen-scoreboard: wrote %d boards to %s" % (1 + len(TEAM_VARIANTS), OUT.name))


if __name__ == "__main__":
    main()
