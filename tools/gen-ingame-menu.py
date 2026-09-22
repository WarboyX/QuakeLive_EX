#!/usr/bin/env python3
"""
[QL] E110. Generator for the in-game menu, which replaces Quake Live's.

WHY GENERATED. Quake Live's nav bar is four itemDefs per tab - an invisible hit
area, a dark backing, a one-pixel underline and the label - and every tab's
action recolours all three coloured parts of every OTHER tab so the selected one
stands out. That is 4 items and 3*N setitemcolor lines per tab: for eight tabs,
around 200 lines of markup in which one wrong index is a tab that highlights its
neighbour. Quake Live's own ingame.menu writes all of it out by hand, and the
result is 486 lines for seven tabs and no content.

Writing that by hand once is a bad idea; writing it again every time a tab is
added or reordered is how the highlight ends up pointing at the wrong tab. The
tab table below is the whole definition - add a row and the markup follows.

It rewrites the block between the markers in content/pak01/ui/main.menu, so the
menu file stays the single thing the game loads and nothing has to be wired up
at run time.

    python3 tools/gen-ingame-menu.py

Run tools/check-menus.py afterwards. It is the only thing that will tell you the
output is well-formed - a .menu that fails to parse keeps going and merges into
the next one, which does not look like a parse error on screen.
"""
import re
import sys
import pathlib

MENU = pathlib.Path(__file__).resolve().parent.parent / "content/pak01/ui/main.menu"
BEGIN = "    // >>> GENERATED io_ingame BEGIN - tools/gen-ingame-menu.py\n"
END = "    // <<< GENERATED io_ingame END\n"

# Quake Live's own in-game palette, taken from its ingame.menu rather than
# picked: gold is 0.964 0.815 0 1 and the selected tab's backing is black at
# 0.9. Matching it is the point - this menu replaces theirs and should not
# announce that it is a different menu.
GOLD = "0.964 0.815 0 1"
WHITE = "1 1 1 1"
DIM = ".72 .72 .72 1"
SEL_BACK = "0 0 0 0.9"
OFF_BACK = "1 1 1 0"

FRAME_W = 560
NTABS = 8
TAB_W = FRAME_W // NTABS      # 70
TAB_Y = 64
TAB_H = 16

# name, label, page menu it opens (None = it is an action, not a page)
TABS = [
    ("match",    "Current Match", "io_ig_match"),
    ("vote",     "Call Vote",     "io_ig_vote"),
    ("admin",    "Admin",         "io_ig_admin"),
    ("addbot",   "Add Bot",       "io_ig_addbot"),
    ("controls", "Controls",      "io_ig_controls"),
    ("settings", "Settings",      "io_ig_settings"),
    ("advanced", "Advanced",      "io_ig_advanced"),
    ("leave",    "Leave",         "io_ig_leave"),
]

PAGES = [t[2] for t in TABS]


def close_all_pages(exclude=None):
    return "".join("                close %s\n" % p for p in PAGES if p != exclude)


def select_tab(idx):
    """The colour state that makes tab `idx` the selected one."""
    out = []
    for i in range(NTABS):
        n = i + 1
        out.append("                setitemcolor ignav%dt forecolor %s\n"
                   % (n, GOLD if i == idx else WHITE))
    for i in range(NTABS):
        n = i + 1
        out.append("                setitemcolor ignav%do backcolor %s\n"
                   % (n, SEL_BACK if i == idx else OFF_BACK))
    for i in range(NTABS):
        n = i + 1
        out.append("                setitemcolor ignav%du backcolor %s\n"
                   % (n, GOLD if i == idx else OFF_BACK))
    return "".join(out)


def nav_items():
    out = []
    for i, (key, label, page) in enumerate(TABS):
        n = i + 1
        x = i * TAB_W
        out.append("""
        // --- tab %d: %s ---
        itemDef {
            name ignav%d
            rect %d %d %d %d
            style WINDOW_STYLE_FILLED
            visible 1
            type ITEM_TYPE_BUTTON
            action {
                play "sound/misc/menu1.wav"
%s%s                open %s
            }
        }
        itemDef { name ignav%do  rect %d %d %d %d  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor %s }
        itemDef { name ignav%du  rect %d %d %d 1  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor %s }
        itemDef { name ignav%dt  rect %d %d %d %d  text "%s"  textscale .18
                  style WINDOW_STYLE_EMPTY  textalign ITEM_ALIGN_CENTER  textalignx %d  textaligny 13
                  forecolor %s  visible 1  decoration }
""" % (n, label,
       n, x, TAB_Y, TAB_W, TAB_H,
       close_all_pages(page), select_tab(i), page,
       n, x + 2, TAB_Y, TAB_W - 4, TAB_H + 1,
       SEL_BACK if i == 0 else OFF_BACK,
       n, x + 2, TAB_Y + TAB_H, TAB_W - 4,
       GOLD if i == 0 else OFF_BACK,
       n, x, TAB_Y, TAB_W, TAB_H, label, TAB_W // 2,
       GOLD if i == 0 else WHITE))
    return "".join(out)


def frame():
    return """    // [QL] E110. The frame: dimmed backdrop, Quake Live's header and logo, the
    // nav bar, and the content panel every page draws inside.
    //
    // THE ART IS QUAKE LIVE'S, BY NAME ONLY. header.tga, ql_logo.tga and
    // content_background.tga are loaded out of the player's own pak00 - the
    // same arrangement as the shader overrides in ql_enhanced.shader. Nothing
    // is copied and nothing is shipped; checked against docs/pak-manifest.txt
    // before being written here, because RE_RegisterShaderNoMip returns 0 for a
    // name the pak does not have and draws nothing while reporting nothing.
    //
    // The geometry is Quake Live's too - rect 40 0 560 480, header 64 tall, nav
    // bar at 63, content panel at 82 - so this reads as the game's own menu
    // rather than as a mod's panel sitting on top of it.
    //
    // check-menus: overlap-ok - this frame is layered by design. The dimming
    // backdrop sits under everything and deliberately overhangs the frame, the
    // logo overhangs it to the left the way Quake Live's does, and each tab is
    // four items sharing one rect. Per-item waivers would be forty copies of
    // this sentence. Kept against the menuDef because the waiver is matched
    // positionally and drifts out of range if it moves up into the notes above.
    menuDef {
        name "io_ingame"
        rect 40 0 %d 480
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_EMPTY
        focusColor %s
        disableColor .5 .5 .5 1

        onESC { uiScript closeingame }

        itemDef { name ig_dim  rect -220 0 1080 480  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.55 }
        itemDef { name ig_header  rect 0 0 %d 64  background "ui/assets/main_menu/header.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1
                  border 1  bordercolor 0 0 0 0.5 }
        itemDef { name ig_logo  rect -20 0 256 64  background "ui/assets/main_menu/ql_logo.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1 }
        itemDef { name ig_bar  rect 0 63 %d 18  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.75  border 1  bordercolor 0 0 0 0.5 }
        itemDef { name ig_page  rect 0 82 %d 310
                  background "ui/assets/main_menu/content_background.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration
                  forecolor 1 1 1 1  backcolor 1 1 1 1  border 1  bordercolor 0 0 0 0.5 }

        // RESUME, bottom right of the frame. ESC does the same thing; this is
        // for the player who is looking for a button rather than a key.
        itemDef {
            name ig_resume  text "RESUME"  type ITEM_TYPE_BUTTON  textscale .25
            rect %d 400 120 26  textalign ITEM_ALIGN_CENTER  textalignx 60  textaligny 19
            forecolor %s  visible 1
            action { play "sound/misc/menu1.wav" ; uiScript closeingame }
            mouseEnter { setitemcolor ig_resume forecolor %s }
            mouseExit  { setitemcolor ig_resume forecolor %s }
        }
%s    }
""" % (FRAME_W, GOLD, FRAME_W, FRAME_W, FRAME_W, FRAME_W - 140, WHITE, GOLD, WHITE, nav_items())


# ---------------------------------------------------------------- pages -----
# Every page is the same shell: a title, a rule, then its own body. Written
# here rather than repeated so a change to the shell is one change.
PAGE_W, PAGE_H = 560, 292


def page(name, title, body, subtitle=None):
    sub = ""
    if subtitle:
        sub = ('        itemDef { text "%s"  textscale .19  rect 20 26 520 14  textaligny 11\n'
               '                  forecolor %s  visible 1  decoration }\n' % (subtitle, DIM))
    return """
    menuDef {
        name "%s"
        rect 40 99 %d %d
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_EMPTY
        focusColor %s

        // Pages never handle ESC themselves. The frame owns it, so one key
        // closes the whole menu from any tab instead of walking back a level.
        onESC { uiScript closeingame }

        itemDef { name pgtitle  text "%s"  textscale .26  rect 20 0 520 24  textaligny 17
                  forecolor %s  visible 1  decoration }
        itemDef { name pgrule  text ""  rect 20 24 520 1  style WINDOW_STYLE_FILLED
                  backcolor .35 .3 .12 1  visible 1  decoration }
%s%s    }
""" % (name, PAGE_W, PAGE_H, GOLD, title, GOLD, sub, body)


def button(name, label, x, y, w, action, color=WHITE, hot=GOLD):
    return """        itemDef {
            name %s  text "%s"  type ITEM_TYPE_BUTTON  textscale .22
            rect %d %d %d 24  textaligny 17  textalignx 8  forecolor %s  visible 1
            action { play "sound/misc/menu1.wav" ; %s }
            mouseEnter { setitemcolor %s forecolor %s }
            mouseExit  { setitemcolor %s forecolor %s }
        }
""" % (name, label, x, y, w, color, action, name, hot, name, color)


def listbox(name, x, y, w, h, feeder, action=None):
    act = ("            action { %s }\n" % action) if action else ""
    return """        itemDef {
            name %s  rect %d %d %d %d  type ITEM_TYPE_LISTBOX
            style WINDOW_STYLE_EMPTY  elementwidth %d  elementheight 16
            elementtype LISTBOX_TEXT  feeder %s
            textscale .2  textaligny 12  forecolor 1 1 1 1  backcolor 0 0 0 .35
            border 1  bordercolor .35 .3 .12 1  visible 1
%s        }
""" % (name, x, y, w, h, w, feeder, act)


def label(text, x, y, w, scale=".21", color=None, h=16):
    """
    Height defaults to 16 because every caller stacks these 16 apart. At 18 each
    row overlapped the next by two pixels - invisible on screen and a real
    finding, since an overlap is how a row ends up drawn through its neighbour.
    """
    return ('        itemDef { text "%s"  textscale %s  rect %d %d %d %d  textaligny 12\n'
            '                  forecolor %s  visible 1  decoration }\n'
            % (text, scale, x, y, w, h, color or DIM))


def multi(name, cvar, values, x, y, w=200):
    return """        itemDef { name %s  type ITEM_TYPE_MULTI  text ""  cvar "%s"
                  cvarFloatList { %s }  rect %d %d %d 18  textscale .21  textaligny 13
                  textalignx 2  forecolor 1 1 1 1  visible 1 }
""" % (name, cvar, values, x, y, w)


def bind(name, label_text, command, y):
    # 17 tall against a 17px step: the rows sit flush without overlapping. At
    # 18 every row ran one pixel into the next, which is invisible and is still
    # two items drawing over each other.
    return """        itemDef { text "%s"  textscale .21  rect 24 %d 200 17  textaligny 12
                  forecolor %s  visible 1  decoration }
        itemDef { name %s  type ITEM_TYPE_BIND  text ""  cvar "%s"
                  rect 230 %d 220 17  textscale .21  textaligny 12  textalignx 2
                  forecolor 1 1 1 1  visible 1 }
""" % (label_text, y, DIM, name, command, y)



# ---------------------------------------------------------------- rows -------
# [QL] E111. Value lists come from docs/ql-cvar-semantics.txt, which is Quake
# Live's own menus answering what each value MEANS, harvested names-and-labels
# only. Inventing a scale and calling it Quake Live's is explicitly worse than
# leaving a cvar alone - cg_hitBeep defaults to 2 and neither the name nor the
# default says whether 1 and 3 are louder, different or off. So a row whose cvar
# is in that file gets Quake Live's labels; one that is not gets a yes/no or an
# explicit list written here, and never a guessed one.
SEMANTICS = {}


def load_semantics():
    f = pathlib.Path(__file__).resolve().parent.parent / "docs/ql-cvar-semantics.txt"
    for line in f.read_text().splitlines():
        if line.startswith("#") or "\t" not in line:
            continue
        name, rest = line.split("\t", 1)
        pairs = []
        for part in rest.split(", "):
            if "=" not in part:
                continue
            val, lab = part.split("=", 1)
            pairs.append((val.strip(), lab.strip()))
        if pairs:
            SEMANTICS[name.strip().lower()] = pairs


def cvarlist(cvar):
    """Quake Live's own value list for a cvar, as a cvarFloatList body."""
    pairs = SEMANTICS.get(cvar.lower())
    if not pairs:
        return None
    return " ".join('"%s" %s' % (lab.replace('"', "'"), val) for val, lab in pairs)


ROW_H = 17


def row_label(text, y):
    return ('        itemDef { text "%s"  textscale .21  rect 24 %d 250 %d  textaligny 13\n'
            '                  forecolor %s  visible 1  decoration }\n'
            % (text, y, ROW_H - 1, WHITE))


def row(kind, cvar, text, y, extra=None):
    """One settings row: the label on the left, the control at x=290."""
    nm = "r_" + re.sub(r"[^a-z0-9]", "", cvar.lower())
    out = row_label(text, y)
    if kind == "slider":
        dflt, lo, hi = extra
        out += ('        itemDef { name %s  type ITEM_TYPE_SLIDER  text ""\n'
                '                  cvarFloat "%s" %s %s %s\n'
                '                  rect 290 %d 200 %d  textscale .21  textaligny 13\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, dflt, lo, hi, y, ROW_H - 1))
    elif kind == "yesno":
        out += ('        itemDef { name %s  type ITEM_TYPE_YESNO  text ""  cvar "%s"\n'
                '                  rect 290 %d 70 %d  textscale .21  textaligny 13  textalignx 2\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, y, ROW_H - 1))
    else:
        vals = extra or cvarlist(cvar)
        if vals is None:
            raise SystemExit("gen-ingame-menu: no value list for %s - add one to "
                             "docs/ql-cvar-semantics.txt or pass it explicitly" % cvar)
        out += ('        itemDef { name %s  type ITEM_TYPE_MULTI  text ""  cvar "%s"\n'
                '                  cvarFloatList { %s }\n'
                '                  rect 290 %d 250 %d  textscale .21  textaligny 13  textalignx 2\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, vals, y, ROW_H - 1))
    return out


def rows_page(menu, title, subtitle, rows, footer=None, extra="", y0=40):
    """A page that is a column of settings rows, plus any buttons it needs."""
    b = ""
    y = y0
    for r in rows:
        b += row(r[0], r[1], r[2], y, r[3] if len(r) > 3 else None)
        y += ROW_H
    if footer:
        b += label(footer, 24, y + 6, 500, ".17")
    return page(menu, title, b + extra, subtitle)


# --- Current Match -----------------------------------------------------------
# Team changes live here rather than on a tab of their own: joining, spectating
# and switching sides are things you do TO the match you are in.
def page_match():
    b = label("Players in this match", 20, 44, 250, ".21", WHITE)
    b += listbox("ig_players", 20, 64, 260, 190, "FEEDER_PLAYER_LIST")
    b += label("Your team", 300, 44, 240, ".21", WHITE)
    b += button("ig_join", "JOIN GAME", 300, 64, 240,
                'exec "cmd team free" ; uiScript closeingame')
    b += button("ig_spec", "SPECTATE", 300, 92, 240,
                'exec "cmd team s" ; uiScript closeingame')
    b += button("ig_red", "JOIN RED", 300, 120, 240,
                'exec "cmd team red" ; uiScript closeingame')
    b += button("ig_blue", "JOIN BLUE", 300, 148, 240,
                'exec "cmd team blue" ; uiScript closeingame')
    b += label("Scoreboard and match detail stay on the HUD - TAB shows them", 300, 184, 240, ".17")
    b += label("live, which no menu page can do while it is covering them.", 300, 200, 240, ".17")
    return page("io_ig_match", "CURRENT MATCH", b,
                "Who is here, and which side you are on.")


# --- Call Vote ---------------------------------------------------------------
def page_vote():
    b = label("Map", 20, 44, 250, ".21", WHITE)
    b += listbox("ig_votemaps", 20, 64, 260, 150, "FEEDER_CVMAPS")
    b += button("ig_votemap", "CALL VOTE: MAP", 20, 222, 260, "uiScript voteMap")
    b += label("Player", 300, 44, 240, ".21", WHITE)
    b += listbox("ig_voteplayers", 300, 64, 240, 90, "FEEDER_PLAYER_LIST")
    b += button("ig_votekick", "CALL VOTE: KICK", 300, 160, 240, "uiScript voteKick")
    b += label("Quick votes", 300, 192, 240, ".21", WHITE)
    # Straight through the console. callvote is a server command and the server
    # is the thing that validates it, so there is nothing for the UI to check.
    b += button("ig_voterestart", "RESTART MATCH", 300, 212, 240,
                'exec "cmd callvote restart" ; uiScript closeingame')
    b += button("ig_voteshuffle", "SHUFFLE TEAMS", 300, 240, 240,
                'exec "cmd callvote shuffle" ; uiScript closeingame')
    return page("io_ig_vote", "CALL VOTE", b,
                "The server decides whether a vote is allowed - these only ask.")


# --- Admin -------------------------------------------------------------------
def page_admin():
    b = label("Select a player", 20, 44, 250, ".21", WHITE)
    b += listbox("ig_adminplayers", 20, 64, 260, 190, "FEEDER_PLAYER_LIST")
    col, y = 300, 64
    for nm, lb, script in [
        ("ig_akick", "KICK", "kickPlayer"),
        ("ig_atban", "TEMP BAN", "tempbanPlayer"),
        ("ig_aban", "BAN", "banPlayer"),
        ("ig_amute", "MUTE", "mutePlayer"),
        ("ig_aunmute", "UNMUTE", "unmutePlayer"),
        ("ig_ared", "PUT ON RED", "putred"),
        ("ig_ablue", "PUT ON BLUE", "putblue"),
        ("ig_aspec", "PUT TO SPEC", "putspec"),
    ]:
        b += button(nm, lb, col, y, 240, "uiScript %s" % script)
        y += 28
    b += label("These need referee or admin rights", 20, 258, 260, ".17")
    b += label("on the server.", 20, 274, 260, ".17")
    return page("io_ig_admin", "ADMIN", b, "Acts on the player selected on the left.")


# --- Add Bot -----------------------------------------------------------------
def page_addbot():
    b = label("Bot", 24, 60, 160, ".21", WHITE)
    b += ('        itemDef { name ig_botname  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_BOTNAME\n'
          '                  rect 200 60 300 20  textscale .24  textaligny 15  textalignx 2\n'
          '                  forecolor 1 1 1 1  visible 1 }\n')
    b += label("Skill", 24, 96, 160, ".21", WHITE)
    b += ('        itemDef { name ig_botskill  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_BOTSKILL\n'
          '                  rect 200 96 300 20  textscale .24  textaligny 15  textalignx 2\n'
          '                  forecolor 1 1 1 1  visible 1 }\n')
    b += label("Team", 24, 132, 160, ".21", WHITE)
    b += ('        itemDef { name ig_botteam  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_REDBLUE\n'
          '                  rect 200 132 300 20  textscale .24  textaligny 15  textalignx 2\n'
          '                  forecolor 1 1 1 1  visible 1 }\n')
    b += button("ig_addbot", "ADD BOT", 24, 176, 200, "uiScript addBot")
    b += label("Team only applies in a team gametype; elsewhere the bot joins the", 24, 214, 500, ".17")
    b += label("free-for-all. Bots need the server to allow them.", 24, 230, 500, ".17")
    return page("io_ig_addbot", "ADD BOT", b, "Click a row to cycle through its choices.")


# --- Controls ----------------------------------------------------------------
# The bindings a player actually rebinds mid-match. A full control list is the
# whole of Quake Live's ingame_controls and belongs on its own page, not here.
def page_controls():
    b = ""
    y = 44
    for nm, lb, cmd in [
        ("ig_bfwd", "Forward", "+forward"),
        ("ig_bback", "Backpedal", "+back"),
        ("ig_bleft", "Move left", "+moveleft"),
        ("ig_bright", "Move right", "+moveright"),
        ("ig_bjump", "Jump", "+moveup"),
        ("ig_bcrouch", "Crouch", "+movedown"),
        ("ig_battack", "Attack", "+attack"),
        ("ig_bnext", "Next weapon", "weapnext"),
        ("ig_bprev", "Previous weapon", "weapprev"),
        ("ig_bzoom", "Zoom", "+zoom"),
        ("ig_bscores", "Scoreboard", "+scores"),
        ("ig_bchat", "Chat", "messagemode"),
    ]:
        b += bind(nm, lb, cmd, y)
        y += 17
    b += button("ig_ctlsave", "SAVE", 24, y + 8, 120, "uiScript saveControls")
    b += button("ig_ctlreload", "RELOAD", 154, y + 8, 120, "uiScript loadControls")
    b += label("Click a binding, then press the key you want.", 290, y + 12, 250, ".17")
    return page("io_ig_controls", "CONTROLS", b, "The keys worth changing without leaving the game.")


# --- Settings (basic) --------------------------------------------------------
def page_settings():
    """
    [QL] E111. Quake Live's BASIC options page, as near as our code allows.

    Sliders where a slider is right. Mouse sensitivity, volume and music were
    cvarFloatLists, which is wrong twice over: a value between the listed stops
    displays as BLANK - a player on sensitivity 4.5 saw an empty row with no way
    to tell what it was - and a continuous quantity presented as five stops is
    not the setting the player actually has. Quake Live uses a slider for all
    three (ingame_controls.menu: cvarfloat "sensitivity" 5 1 30); so does this.

    cg_railStyle is on Quake Live's basic page and is deliberately NOT here: it
    is QL-ASSET in docs/cvar-manifest.txt, registered and read by nothing of
    ours. A row for it would take a value and change nothing.

    Resolution lives on Advanced > Video and on the render menu, both of which
    have the Apply this page would otherwise need. Putting a latched setting
    next to eight live ones is how "I changed it and nothing happened" starts.
    """
    b = label("Player model", 24, 44, 250, ".21", WHITE)
    # LISTBOX_IMAGE over FEEDER_Q3HEADS is how Quake Live's own basic page does
    # the model picker; both it and UI_PLAYERMODEL are implemented in ui_main.c.
    b += """        itemDef {
            name ig_models  rect 24 64 250 56  type ITEM_TYPE_LISTBOX
            style WINDOW_STYLE_EMPTY  elementwidth 26  elementheight 26
            elementtype LISTBOX_IMAGE  feeder FEEDER_Q3HEADS
            border 1  bordercolor .35 .3 .12 1  visible 1
        }
        itemDef { name ig_modelpreview  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_PLAYERMODEL
                  rect 420 40 120 86  visible 1 }
"""
    y = 132
    for kind, cvar, text, extra in [
        ("slider", "sensitivity",   "Mouse sensitivity", ("5", "1", "30")),
        ("slider", "cg_fov",        "Field of view",     ("100", "75", "130")),
        ("slider", "r_gamma",       "Brightness",        ("1.3", "1", "3")),
        ("slider", "s_volume",      "Master volume",     ("0.8", "0", "1")),
        ("slider", "s_musicvolume", "Music",             ("0.25", "0", "1")),
    ]:
        b += row(kind, cvar, text, y, extra)
        y += ROW_H
    # The crosshair preview is 20 square against a 16-tall row, so it gets its
    # own slot rather than being squeezed into one and drawn over its neighbours.
    b += row_label("Crosshair", y + 4)
    b += ('        itemDef { name ig_xhair  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_CROSSHAIR\n'
          '                  rect 290 %d 20 20  visible 1 }\n' % (y + 2))
    y += 26
    b += row("multi", "cg_crosshairSize", "Crosshair size", y)
    y += ROW_H - 1
    b += label("Resolution, texture detail and everything else Quake Live keeps on its", 24, y, 520, ".17")
    b += label("advanced page are under the Advanced tab - and on the render menu.", 24, y + 17, 520, ".17")
    return page("io_ig_settings", "SETTINGS", b,
                "The common ones, applied as you change them.")


# --- Advanced ----------------------------------------------------------------
# A hub rather than a page of its own settings: the render menus already exist,
# are already organised, and duplicating their rows here would be two places to
# change one setting.
def page_advanced():
    """A hub. The sub-pages hold the rows; this only has to reach them."""
    b = label("Quake Live's advanced options, minus the ones our code does not read.",
              24, 44, 500, ".19")
    x, y = 24, 68
    for i, (menu, title) in enumerate(SUBPAGE_INDEX):
        b += button("adv_" + menu, title, x, y, 160, "open %s" % menu)
        x += 172
        if x > 380:
            x = 24
            y += 28
    y += 34
    b += label("Ours, not Quake Live's", 24, y, 300, ".21", WHITE)
    y += 20
    b += button("ig_advrender", "RENDER OPTIONS", 24, y, 160, "open io_renderoptions")
    b += button("ig_advwater", "WATER", 196, y, 160, "open io_water")
    b += button("ig_advrt", "RAY TRACING", 368, y, 160, "open io_raytracing")
    y += 28
    b += button("ig_advsurf", "SURFACE DETAIL", 24, y, 160, "open io_surfacedetail")
    b += button("ig_advplayer", "PLAYER SETUP", 196, y, 160, "open io_playersetup")
    b += button("ig_advql", "QUAKE LIVE MENU", 368, y, 160,
                "open ingame ; open ingame_about")
    return page("io_ig_advanced", "ADVANCED", b, "Grouped the way Quake Live groups them.")



# --- Advanced sub-pages ------------------------------------------------------
# [QL] E111. The options Quake Live's own ingame_options_basic/_advanced expose,
# minus the ones our code does not actually read.
#
# THAT SUBTRACTION IS THE POINT. Quake Live's two options pages name 134 cvars.
# Cross-referenced against docs/cvar-manifest.txt, 34 of them are QL-ASSET
# here - registered so a config or a stock client finds the name, and read by
# nothing in our code. A row for one of those is the exact failure this tree
# keeps paying for: it sets, it displays, it persists, and it does nothing.
# cg_railStyle is on that list, which is why the rail style row Quake Live puts
# on its BASIC page is absent from ours.
#
# The 34 are the backlog, not a gap in this menu. When one gets wired, add its
# row; until then its absence is the honest state.
SUBPAGES = [
    ("io_ig_video", "VIDEO", "Resolution and texture detail. Most of these need Apply.", [
        ("multi",  "r_mode",                    "Resolution"),
        ("yesno",  "r_fullscreen",              "Fullscreen"),
        ("multi",  "r_windowedMode",            "Windowed mode"),
        ("multi",  "r_displayRefresh",          "Refresh rate"),
        ("multi",  "r_swapInterval",            "Vertical sync",
                   '"Off" 0 "On" 1 "Adaptive" -1'),
        ("multi",  "r_picmip",                  "Texture detail",
                   '"Highest" 0 "High" 1 "Medium" 2 "Low" 3'),
        ("multi",  "r_texturebits",             "Texture depth",
                   '"Default" 0 "16-bit" 16 "32-bit" 32'),
        ("yesno",  "r_ext_compressed_textures", "Compress textures"),
        ("multi",  "r_subdivisions",            "Curve detail"),
        ("multi",  "r_lodbias",                 "Model detail"),
        ("yesno",  "r_fastsky",                 "Fast sky"),
    ], None),
    ("io_ig_light", "LIGHTING", "Brightness, shadows and the overall tone.", [
        ("slider", "r_gamma",             "Brightness",     ("1.3", "1", "3")),
        ("multi",  "r_overbrightBits",    "Overbright"),
        ("multi",  "r_mapOverbrightBits", "Map overbright"),
        ("multi",  "r_ambientScale",      "Ambient light"),
        ("yesno",  "r_dynamicLight",      "Dynamic lights"),
        ("multi",  "cg_shadows",          "Shadows",
                   '"Off" 0 "Blob" 1 "Simple" 2 "Stencil" 3'),
        ("yesno",  "cg_deadBodyDarken",   "Darken dead bodies"),
        ("yesno",  "cg_vignette",         "Vignette"),
    ], None),
    ("io_ig_bloom", "BLOOM & POST", "Drawn by the OpenGL2 renderer.", [
        ("multi",  "r_enableBloom",          "Bloom"),
        ("slider", "r_bloomIntensity",       "Bloom intensity",  ("0.6", "0", "2")),
        ("slider", "r_bloomBrightThreshold", "Bright threshold", ("0.6", "0", "1")),
        ("slider", "r_bloomSaturation",      "Bloom saturation", ("1", "0", "2")),
        ("slider", "r_bloomSceneIntensity",  "Scene intensity",  ("1", "0", "2")),
        ("slider", "r_bloomSceneSaturation", "Scene saturation", ("1", "0", "2")),
        ("yesno",  "r_enablePostProcess",    "Post processing"),
        ("yesno",  "r_enableColorCorrect",   "Colour correction"),
    ], "The Vulkan renderer has its own effects under Render Options."),
    ("io_ig_crosshair", "CROSSHAIR", "Size, brightness and what it reacts to.", [
        ("multi",  "cg_crosshairSize",       "Size"),
        ("multi",  "cg_crosshairBrightness", "Brightness"),
        ("yesno",  "cg_crosshairPulse",      "Pulse on pickup"),
        ("yesno",  "cg_crosshairHealth",     "Colour by health"),
        ("multi",  "cg_drawCrosshairNames",  "Show player names"),
    ], "The crosshair shape itself is on the Settings tab, beside its preview."),
    ("io_ig_hud", "HUD", "What is drawn on screen while you play.", [
        ("slider", "cg_fov",                 "Field of view",      ("100", "75", "130")),
        ("slider", "cg_zoomfov",             "Zoom field of view", ("22.5", "10", "90")),
        ("multi",  "cg_drawGun",             "Draw gun"),
        ("multi",  "cg_guny",                "Gun height"),
        ("yesno",  "cg_drawFPS",             "Show FPS"),
        ("multi",  "cg_lagometer",           "Lagometer"),
        ("multi",  "cg_speedometer",         "Speedometer"),
        ("yesno",  "cg_drawAttacker",        "Show attacker"),
        ("multi",  "cg_drawTeamOverlay",     "Team overlay"),
        ("multi",  "cg_weaponBar",           "Weapon bar"),
        ("multi",  "cg_drawFullWeaponBar",   "Weapon bar shows"),
        ("multi",  "cg_leveltimerdirection", "Match timer"),
    ], None),
    ("io_ig_weapons", "WEAPONS", "Switching, and how each weapon looks.", [
        ("yesno",  "cg_autoSwitch",      "Auto switch on pickup"),
        ("yesno",  "cg_switchOnEmpty",   "Switch when empty"),
        ("yesno",  "cg_switchToEmpty",   "Allow switch to empty"),
        ("yesno",  "cg_muzzleFlash",     "Muzzle flash"),
        ("multi",  "cg_brassTime",       "Ejected brass"),
        ("multi",  "cg_trueLightning",   "True lightning"),
        ("multi",  "cg_lightningStyle",  "Lightning style"),
        ("yesno",  "cg_lightningImpact", "Lightning impact"),
        ("multi",  "cg_plasmaStyle",     "Plasma style"),
        ("multi",  "cg_rocketStyle",     "Rocket style"),
        ("multi",  "cg_railTrailTime",   "Rail trail time"),
        ("multi",  "r_railWidth",        "Rail width"),
        ("multi",  "r_railCoreWidth",    "Rail core width"),
    ], None),
    ("io_ig_effects", "EFFECTS", "Impacts, smoke and world detail.", [
        ("multi",  "r_railSegmentLength",     "Rail segments"),
        ("yesno",  "cg_simpleItems",          "Simple items"),
        ("multi",  "cg_impactSparksVelocity", "Impact sparks"),
        ("yesno",  "cg_bubbleTrail",          "Underwater bubbles"),
        ("yesno",  "cg_damagePlum",           "Damage numbers"),
        ("multi",  "cg_smokeRadius_RL",       "Rocket smoke"),
        ("multi",  "cg_smokeRadius_GL",       "Grenade smoke"),
        ("multi",  "cg_smokeRadius_NG",       "Nailgun smoke"),
        ("yesno",  "cg_smoke_SG",             "Shotgun smoke"),
        ("multi",  "cg_kickScale",            "View kick"),
        ("yesno",  "cg_waterWarp",            "Underwater warp"),
        ("multi",  "cg_flagStyle",            "Flag style"),
    ], None),
    ("io_ig_sound", "SOUND", "Levels, and the sounds that carry information.", [
        ("slider", "s_volume",      "Master volume", ("0.8", "0", "1")),
        ("slider", "s_musicvolume", "Music",         ("0.25", "0", "1")),
        ("slider", "s_voiceVolume", "Voice chat",    ("1", "0", "1")),
        ("yesno",  "s_doppler",     "Doppler"),
        ("multi",  "cg_announcer",  "Announcer"),
        ("yesno",  "cg_chatbeep",   "Chat beep"),
        ("multi",  "cg_hitbeep",    "Hit beep"),
    ], None),
    ("io_ig_game", "GAME", "Behaviour that is not about how it looks.", [
        ("yesno",  "cg_allowTaunt",        "Allow taunts"),
        ("yesno",  "cg_zoomToggle",        "Zoom is a toggle"),
        ("yesno",  "cg_zoomScaling",       "Scale zoom sensitivity"),
        ("multi",  "cg_useItemMessage",    "Use-item message"),
        ("multi",  "cg_useItemWarning",    "Use-item warning"),
        ("yesno",  "cg_forceEnemyModel",   "Force enemy model"),
        ("yesno",  "cg_forceTeamModel",    "Force team model"),
        ("yesno",  "cg_followKiller",      "Follow killer"),
        ("multi",  "cg_followPowerup",     "Follow powerup"),
        ("slider", "cg_specFov",           "Spectator FOV", ("100", "75", "130")),
        ("yesno",  "cl_allowConsoleChat",  "Console chat"),
        ("multi",  "cl_demoRecordMessage", "Demo record message"),
    ], None),
]

SUBPAGE_INDEX = [(m, t) for m, t, _, _, _ in SUBPAGES]


def advanced_subpages():
    out = ""
    for menu, title, sub, rows, footer in SUBPAGES:
        # Every sub-page carries its own way back: it opens OVER the Advanced
        # page rather than replacing it in the tab bar, so the bar still reads
        # Advanced and there is nothing up there to click to return.
        extra = button("bk_" + menu, "BACK", 400, 264, 140,
                       "close %s ; open io_ig_advanced" % menu)
        if menu == "io_ig_video":
            # Resolution, fullscreen and colour depth are latched by the
            # renderer; without this the page looks like it did nothing.
            extra += button("ap_" + menu, "APPLY (restarts renderer)", 24, 264, 250,
                            'exec "vid_restart"')
        out += rows_page(menu, title, sub, rows, footer, extra)
    return out


# --- Leave -------------------------------------------------------------------
# A page rather than a tab that acts immediately: Leave sits in the same row as
# seven navigation tabs, and a tab that disconnects on click is a misclick with
# no undo.
def page_leave():
    b = label("Leaving drops you back to the main menu. If this is a server you", 24, 60, 500, ".21", WHITE)
    b += label("joined, you will have to find it again to come back.", 24, 82, 500, ".21", WHITE)
    b += button("ig_leaveyes", "LEAVE MATCH", 24, 130, 220, "uiScript Leave",
                color=".85 .55 .55 1", hot="1 .4 .4 1")
    b += button("ig_leaveno", "STAY", 264, 130, 220, "uiScript closeingame")
    return page("io_ig_leave", "LEAVE MATCH", b, "This one asks first.")


def main():
    load_semantics()
    block = (BEGIN
             + frame()
             + page_match() + page_vote() + page_admin() + page_addbot()
             + page_controls() + page_settings() + page_advanced() + page_leave()
             + advanced_subpages()
             + END)

    text = MENU.read_text()
    if BEGIN not in text or END not in text:
        sys.exit("gen-ingame-menu: markers not found in %s - add them around the "
                 "io_ingame block first" % MENU)

    start = text.index(BEGIN)
    end = text.index(END) + len(END)
    MENU.write_text(text[:start] + block + text[end:])

    menus = 1 + len(PAGES) + len(SUBPAGES)
    print("gen-ingame-menu: wrote %d menus (%d tabs) into %s"
          % (menus, NTABS, MENU.name))
    print("gen-ingame-menu: now run tools/check-menus.py")


if __name__ == "__main__":
    main()
