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
        sub = ('        itemDef { text "%s"  textscale .19  rect 20 26 520 16  textaligny 12\n'
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
    b = label("Field of view", 24, 48, 200, ".21", WHITE)
    b += multi("ig_fov", "cg_fov", '"90" 90 "100" 100 "110" 110 "120" 120 "130" 130', 230, 48)
    b += label("Mouse sensitivity", 24, 70, 200, ".21", WHITE)
    b += multi("ig_sens", "sensitivity",
               '"1" 1 "2" 2 "3" 3 "5" 5 "8" 8 "12" 12', 230, 70)
    b += label("Crosshair", 24, 92, 200, ".21", WHITE)
    b += ('        itemDef { name ig_xhair  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_CROSSHAIR\n'
          '                  rect 230 88 24 24  visible 1 }\n')
    b += label("Crosshair size", 24, 114, 200, ".21", WHITE)
    b += multi("ig_xsize", "cg_crosshairSize", '"16" 16 "24" 24 "32" 32 "48" 48', 230, 114)
    b += label("Draw gun", 24, 136, 200, ".21", WHITE)
    b += ('        itemDef { name ig_gun  type ITEM_TYPE_YESNO  text ""  cvar "cg_drawGun"\n'
          '                  rect 230 136 70 18  textscale .21  textaligny 13  textalignx 2\n'
          '                  forecolor 1 1 1 1  visible 1 }\n')
    b += label("Simple items", 24, 158, 200, ".21", WHITE)
    b += ('        itemDef { name ig_simple  type ITEM_TYPE_YESNO  text ""  cvar "cg_simpleItems"\n'
          '                  rect 230 158 70 18  textscale .21  textaligny 13  textalignx 2\n'
          '                  forecolor 1 1 1 1  visible 1 }\n')
    b += label("Master volume", 24, 180, 200, ".21", WHITE)
    b += multi("ig_vol", "s_volume", '"Off" 0 "Low" 0.3 "Medium" 0.6 "High" 1', 230, 180)
    b += label("Music", 24, 202, 200, ".21", WHITE)
    b += multi("ig_music", "s_musicvolume", '"Off" 0 "Low" 0.2 "Medium" 0.5 "High" 1', 230, 202)
    b += label("Brightness", 24, 224, 200, ".21", WHITE)
    b += multi("ig_gamma", "r_gamma", '"Dark" 1 "Normal" 1.3 "Bright" 1.6 "Very bright" 2', 230, 224)
    b += label("These take effect immediately. Anything needing a restart lives", 24, 252, 500, ".17")
    b += label("under Advanced, which says so where it applies.", 24, 268, 500, ".17")
    return page("io_ig_settings", "SETTINGS", b, "The common ones, applied as you change them.")


# --- Advanced ----------------------------------------------------------------
# A hub rather than a page of its own settings: the render menus already exist,
# are already organised, and duplicating their rows here would be two places to
# change one setting.
def page_advanced():
    b = label("Renderer", 24, 48, 240, ".21", WHITE)
    b += button("ig_advrender", "RENDER OPTIONS", 24, 68, 240, "open io_renderoptions")
    b += button("ig_advwater", "WATER", 24, 96, 240, "open io_water")
    b += button("ig_advrt", "RAY TRACING", 24, 124, 240, "open io_raytracing")
    b += button("ig_advsurf", "SURFACE DETAIL", 24, 152, 240, "open io_surfacedetail")
    b += label("Player", 300, 48, 240, ".21", WHITE)
    b += button("ig_advplayer", "PLAYER SETUP", 300, 68, 240, "open io_playersetup")
    b += label("Quake Live's own menu", 300, 104, 240, ".21", WHITE)
    b += button("ig_advql", "QUAKE LIVE MENU", 300, 124, 240, "open ingame ; open ingame_about")
    b += label("Opens on top of this one, for anything not reproduced here.", 300, 152, 240, ".17")
    b += label("These pages open over the frame rather than inside it - they are the", 24, 218, 500, ".17")
    b += label("same menus the main menu uses, so a setting is in one place only and", 24, 234, 500, ".17")
    b += label("cannot disagree with itself depending on where you opened it.", 24, 250, 500, ".17")
    return page("io_ig_advanced", "ADVANCED", b, "Everything with its own page.")


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
    block = (BEGIN
             + frame()
             + page_match() + page_vote() + page_admin() + page_addbot()
             + page_controls() + page_settings() + page_advanced() + page_leave()
             + END)

    text = MENU.read_text()
    if BEGIN not in text or END not in text:
        sys.exit("gen-ingame-menu: markers not found in %s - add them around the "
                 "io_ingame block first" % MENU)

    start = text.index(BEGIN)
    end = text.index(END) + len(END)
    MENU.write_text(text[:start] + block + text[end:])

    menus = 1 + len(PAGES)
    print("gen-ingame-menu: wrote %d menus (%d tabs) into %s"
          % (menus, NTABS, MENU.name))
    print("gen-ingame-menu: now run tools/check-menus.py")


if __name__ == "__main__":
    main()
