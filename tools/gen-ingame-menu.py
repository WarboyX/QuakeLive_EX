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
# [QL] E114. Boxed buttons speak the same language as the rest of the frame.
#
# E113 gave them a dark red fill, copied from the Apply button on Quake Live's
# player options page. On screen it sat under Quake Live's header, which is a
# different, lighter red, and the two reds fought: two reds that are nearly the
# same read as a mistake rather than as a scheme. The only red that belongs here
# is the header art.
#
# Everything else in this menu already uses one vocabulary - translucent black,
# a muted gold rule, and full gold for "this one": the selected tab is black at
# 0.9 with a gold underline. Buttons now use exactly that. Idle is the nav bar's
# black with the page rule's gold edge; hover is the selected tab's black with a
# gold edge and gold text.
BTN_BACK = "0 0 0 .6"
BTN_HOT = "0 0 0 .9"
BTN_EDGE = ".35 .3 .12 1"
BTN_EDGE_HOT = GOLD

FRAME_W = 560
TAB_Y = 64
TAB_H = 16

# name, label, page menu it opens (None = it is an action, not a page)
TABS = [
    ("match",    "Current Match", "io_ig_match"),
    ("vote",     "Call Vote",     "io_ig_vote"),
    ("admin",    "Admin",         "io_ig_admin"),
    ("addbot",   "Add Bot",       "io_ig_addbot"),
    ("controls", "Controls",      "io_ig_controls"),
    ("settings", "Player",        "io_ig_settings"),   # E118: was "Settings"
    ("advanced", "Advanced",      "io_ig_advanced"),
    # E117: no Leave tab. Leaving is on the Current Match page, which asks first.
]

PAGES = [t[2] for t in TABS]
NTABS = len(TABS)


def close_all_pages(exclude=None, indent="                "):
    """
    [QL] E112. Close every page AND every sub-page, not just the tab pages.

    Menu_PaintAll walks Menus[] and paints every menu whose visible flag is set;
    there is no stack and no "topmost". So a menu that is not explicitly closed
    keeps drawing, and the result is two pages superimposed - both sets of rows
    in the same place, which is what "it double draws" was.

    Sub-pages are included because Advanced opens them and a tab press from
    there has to clear them as well.
    """
    names = list(PAGES) + [m for m, _, _, _, _ in SUBPAGES]
    return "".join("%sclose %s\n" % (indent, n) for n in names if n != exclude)


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


# [QL] E113. Tab widths follow their labels, inside a padded bar.
#
# Eight equal 70px tabs put "Current Match" - the longest label, ~65px at
# textscale .18 - in a 70px cell with its text centred, which leaves it about two
# pixels from the frame's left edge: flush against the border, no padding, and
# visibly further left than every other tab. Equal widths only look even when the
# labels are.
#
# So each tab is its label plus the same padding either side, the bar is inset
# from both frame edges, and whatever width is left over is shared out evenly.
# TAB_CHAR is measured from the screenshot of the built menu (65 virtual px for
# 13 characters), not assumed.
TAB_CHAR = 5
TAB_PAD = 12
TAB_INSET = 6


def tab_layout():
    widths = [len(t[1]) * TAB_CHAR + TAB_PAD * 2 for t in TABS]
    spare = FRAME_W - TAB_INSET * 2 - sum(widths)
    if spare < 0:
        raise SystemExit("gen-ingame-menu: tab labels no longer fit the bar - "
                         "shorten one or drop TAB_PAD")
    each = spare // len(widths)
    widths = [w + each for w in widths]
    xs, x = [], TAB_INSET
    for w in widths:
        xs.append(x)
        x += w
    return list(zip(xs, widths))


def nav_items():
    out = []
    layout = tab_layout()
    for i, (key, label, page) in enumerate(TABS):
        n = i + 1
        x, TAB_W_I = layout[i]
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
       n, x, TAB_Y, TAB_W_I, TAB_H,
       close_all_pages(page), select_tab(i), page,
       n, x + 2, TAB_Y, TAB_W_I - 4, TAB_H + 1,
       SEL_BACK if i == 0 else OFF_BACK,
       n, x + 2, TAB_Y + TAB_H, TAB_W_I - 4,
       GOLD if i == 0 else OFF_BACK,
       n, x, TAB_Y, TAB_W_I, TAB_H, label, TAB_W_I // 2,
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

        // [QL] E115. Put the tab bar back to Current Match every time this
        // opens. setitemcolor changes persist on the item, so the bar kept
        // whatever the last tab click left - while _UI_SetActiveMenu always
        // opens the Current Match PAGE. Reopening the menu showed one page
        // under another tab's highlight.
        //
        // onOpen rather than Quake Live's onClose: Menus_ActivateByName runs
        // onOpen on every way back in, including after Leave, which disconnects
        // without ever closing this frame through a path that runs onClose.
        onOpen {
%s        }

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

        // [QL] E113. A footer bar the width of the frame, with RESUME as a real
        // button in it. It was bare text floating below the panel with nothing
        // around it - it did not read as part of the menu, or as clickable.
        // Styled like every other button in the menu - see BTN_BACK - rather
        // than in a red of its own, which fought the header.
        itemDef { name ig_foot  rect 0 396 %d 36  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.75  border 1  bordercolor 0 0 0 0.5 }
        itemDef {
            name ig_resume  text "RESUME"  type ITEM_TYPE_BUTTON  textscale .25
            rect %d 401 160 26  textalign ITEM_ALIGN_CENTER  textalignx 80  textaligny 17
            style WINDOW_STYLE_FILLED  backcolor %s  border 1  bordersize 1  bordercolor %s
            forecolor %s  visible 1
            action { play "sound/misc/menu1.wav" ; uiScript closeingame }
            mouseEnter { setitemcolor ig_resume backcolor %s ; setitemcolor ig_resume forecolor %s ; setitemcolor ig_resume bordercolor %s }
            mouseExit  { setitemcolor ig_resume backcolor %s ; setitemcolor ig_resume forecolor %s ; setitemcolor ig_resume bordercolor %s }
        }
%s    }
""" % (FRAME_W, GOLD, select_tab(0), FRAME_W, FRAME_W, FRAME_W, FRAME_W,
       (FRAME_W - 160) // 2, BTN_BACK, BTN_EDGE, WHITE,
       BTN_HOT, GOLD, BTN_EDGE_HOT, BTN_BACK, WHITE, BTN_EDGE,
       nav_items())


# ---------------------------------------------------------------- pages -----
# Every page is the same shell: a title, a rule, then its own body. Written
# here rather than repeated so a change to the shell is one change.
PAGE_W, PAGE_H = 560, 292


def page(name, title, body, subtitle=None, onopen=None):
    sub = ""
    if subtitle:
        sub = ('        itemDef { text "%s"  textscale .19  rect 20 26 520 14  textaligny 11\n'
               '                  textalign ITEM_ALIGN_CENTER  textalignx 260\n'
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
%s
        // [QL] E113. Centred, like everything else on the page. The rows form a
        // two-column layout that meets at the page's centre line, and a title
        // hard against the left edge sat over nothing.
        itemDef { name pgtitle  text "%s"  textscale .26  rect 20 0 520 24  textaligny 17
                  textalign ITEM_ALIGN_CENTER  textalignx 260
                  forecolor %s  visible 1  decoration }
        itemDef { name pgrule  text ""  rect 20 24 520 1  style WINDOW_STYLE_FILLED
                  backcolor .35 .3 .12 1  visible 1  decoration }
%s%s    }
""" % (name, PAGE_W, PAGE_H, GOLD,
       ("        onOpen { %s }\n" % onopen) if onopen else "",
       title, GOLD, sub, body)


def boxbutton(name, label, x, y, w, action):
    """A button with a visible box, for actions rather than navigation."""
    return """        itemDef {
            name %s  text "%s"  type ITEM_TYPE_BUTTON  textscale .21
            rect %d %d %d 22  textalign ITEM_ALIGN_CENTER  textalignx %d  textaligny 14
            style WINDOW_STYLE_FILLED  backcolor %s  border 1  bordersize 1  bordercolor %s
            forecolor %s  visible 1
            action { play "sound/misc/menu1.wav" ; %s }
            mouseEnter { setitemcolor %s backcolor %s ; setitemcolor %s forecolor %s ; setitemcolor %s bordercolor %s }
            mouseExit  { setitemcolor %s backcolor %s ; setitemcolor %s forecolor %s ; setitemcolor %s bordercolor %s }
        }
""" % (name, label, x, y, w, w // 2, BTN_BACK, BTN_EDGE, WHITE, action,
       name, BTN_HOT, name, GOLD, name, BTN_EDGE_HOT,
       name, BTN_BACK, name, WHITE, name, BTN_EDGE)


def button(name, label, x, y, w, action, hidden=False):
    """
    Every button in the menu, boxed. There used to be two styles - boxed actions
    and bare-text buttons - side by side on the same pages, which reads as
    unfinished rather than as a hierarchy. One style, the frame's palette.

    24 tall with the text centred: at textscale .22 the cap height is ~10 and
    the engine draws the baseline ~1.2 below textaligny, so 16 centres it.
    """
    return """        itemDef {
            name %s  text "%s"  type ITEM_TYPE_BUTTON  textscale .22
            rect %d %d %d 24  textalign ITEM_ALIGN_CENTER  textalignx %d  textaligny 16
            style WINDOW_STYLE_FILLED  backcolor %s  border 1  bordersize 1  bordercolor %s
            forecolor %s  visible %d
            action { play "sound/misc/menu1.wav" ; %s }
            mouseEnter { setitemcolor %s backcolor %s ; setitemcolor %s forecolor %s ; setitemcolor %s bordercolor %s }
            mouseExit  { setitemcolor %s backcolor %s ; setitemcolor %s forecolor %s ; setitemcolor %s bordercolor %s }
        }
""" % (name, label, x, y, w, w // 2, BTN_BACK, BTN_EDGE, WHITE, 0 if hidden else 1, action,
       name, BTN_HOT, name, GOLD, name, BTN_EDGE_HOT,
       name, BTN_BACK, name, WHITE, name, BTN_EDGE)


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
    #
    # [QL] E116. The same centred form as every settings row since E113 - the
    # label ends at the centre gutter, the key starts just after it. This page
    # was built separately and missed that pass, so it was the one page still
    # hugging the left edge.
    return """        itemDef { text "%s"  textscale .21  rect 24 %d %d 17  textaligny 12
                  textalign ITEM_ALIGN_RIGHT  textalignx %d
                  forecolor %s  visible 1  decoration }
        itemDef { name %s  type ITEM_TYPE_BIND  text ""  cvar "%s"
                  rect %d %d 220 17  textscale .21  textaligny 12  textalignx 2
                  forecolor 1 1 1 1  visible 1 }
""" % (label_text, y, LABEL_END - 24, LABEL_END - 24, WHITE, name, command, CTRL_X, y)



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

# [QL] E113. A centred form: labels right-aligned so they END at the gutter,
# controls starting just after it. Labels on the far left with values floating at
# x=290 is two ragged columns with a gap between them that grows with the
# shortest label; this meets at the page's centre line, which is where the eye
# already is. LABEL_END and CTRL_X are relative to the page, whose centre is 280.
LABEL_END = 270
CTRL_X = 290
# The engine draws a slider at a fixed SLIDER_WIDTH (ui_shared.h: 96), whatever
# its rect says, so the rect is set to match and the value box goes after it.
SLIDER_W = 96
VALUE_X = CTRL_X + SLIDER_W + 12
VALUE_W = 56


def row_label(text, y):
    return ('        itemDef { text "%s"  textscale .21  rect 24 %d %d %d  textaligny 13\n'
            '                  textalign ITEM_ALIGN_RIGHT  textalignx %d\n'
            '                  forecolor %s  visible 1  decoration }\n'
            % (text, y, LABEL_END - 24, ROW_H - 1, LABEL_END - 24, WHITE))


def row(kind, cvar, text, y, extra=None):
    """One settings row: the label on the left, the control at x=290."""
    nm = "r_" + re.sub(r"[^a-z0-9]", "", cvar.lower())
    out = row_label(text, y)
    if kind == "slider":
        dflt, lo, hi = extra
        out += ('        itemDef { name %s  type ITEM_TYPE_SLIDER  text ""\n'
                '                  cvarFloat "%s" %s %s %s\n'
                '                  rect %d %d %d %d  textscale .21  textaligny 13\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, dflt, lo, hi, CTRL_X, y, SLIDER_W, ROW_H - 1))
        # [QL] E113. The value, beside the slider, and typeable.
        #
        # A slider says where in its range it is and nothing about the number,
        # and for sensitivity or FOV the number is the whole point - players
        # carry them between configs. This box shows the cvar as it stands (the
        # field re-reads it every frame, so it follows the thumb while dragging)
        # and takes a typed value on Enter, including decimals and a leading
        # minus since E113 taught ITEM_TYPE_NUMERICFIELD both. A typed value is
        # not clamped to the slider's range; the cvar's own range check is the
        # authority, same as typing it at the console.
        out += ('        itemDef { name %sv  type ITEM_TYPE_NUMERICFIELD  text ""  cvar "%s"\n'
                '                  maxchars 7  maxpaintchars 7\n'
                '                  rect %d %d %d %d  textscale .19  textaligny 12  textalignx 4\n'
                '                  style WINDOW_STYLE_FILLED  backcolor 0 0 0 .55\n'
                '                  border 1  bordersize 1  bordercolor .35 .3 .12 1\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, VALUE_X, y, VALUE_W, ROW_H - 1))
    elif kind == "yesno":
        out += ('        itemDef { name %s  type ITEM_TYPE_YESNO  text ""  cvar "%s"\n'
                '                  rect %d %d 70 %d  textscale .21  textaligny 13  textalignx 2\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, CTRL_X, y, ROW_H - 1))
    else:
        vals = extra or cvarlist(cvar)
        if vals is None:
            raise SystemExit("gen-ingame-menu: no value list for %s - add one to "
                             "docs/ql-cvar-semantics.txt or pass it explicitly" % cvar)
        out += ('        itemDef { name %s  type ITEM_TYPE_MULTI  text ""  cvar "%s"\n'
                '                  cvarFloatList { %s }\n'
                '                  rect %d %d 250 %d  textscale .21  textaligny 13  textalignx 2\n'
                '                  forecolor 1 1 1 1  visible 1 }\n'
                % (nm, cvar, vals, CTRL_X, y, ROW_H - 1))
    return out


def rows_page(menu, title, subtitle, rows, footer=None, extra="", y0=40):
    """A page that is a column of settings rows, plus any buttons it needs."""
    b = ""
    y = y0
    for r in rows:
        b += row(r[0], r[1], r[2], y, r[3] if len(r) > 3 else None)
        y += ROW_H
    if footer:
        b += ('        itemDef { text "%s"  textscale .17  rect 24 %d 512 16  textaligny 12\n'
              '                  textalign ITEM_ALIGN_CENTER  textalignx 256\n'
              '                  forecolor %s  visible 1  decoration }\n' % (footer, y + 6, DIM))
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
    # Shortened to fit its column: the old two-line note was ~280px of text in a
    # 240px box and ran off the panel. Centred under the buttons it explains.
    for i, t in enumerate(["The live scoreboard is on TAB -", "this page shows who is here."]):
        b += ('        itemDef { text "%s"  textscale .17  rect 300 %d 240 16  textaligny 12\n'
              '                  textalign ITEM_ALIGN_CENTER  textalignx 120\n'
              '                  forecolor %s  visible 1  decoration }\n' % (t, 184 + i * 17, DIM))
    # [QL] E117. Leave lives here now, not on a tab of its own.
    #
    # It still asks first. The Leave page existed because disconnecting has no
    # undo, and moving the button must not quietly drop that. Instead of a page,
    # the button turns into a confirm/cancel pair in place: `hide` and `show`
    # act on items in this menu by name, and the page's onOpen puts the plain
    # button back - `open` runs onOpen on every tab switch, so a half-confirmed
    # Leave never survives leaving the page.
    #
    # The question replaces the button IN PLACE and the confirm pair appears on
    # the row BELOW it. Put CONFIRM where LEAVE MATCH was and a double-click
    # lands its second click on CONFIRM - disconnecting through the very step
    # meant to stop it. Here the second click hits plain text and does nothing.
    b += button("ig_leave", "LEAVE MATCH", 300, 236, 240,
                "hide ig_leave ; show ig_leaveyes ; show ig_leaveno ; show ig_leaveask")
    b += ('        itemDef { name ig_leaveask  text "Disconnect from this server?"  textscale .19\n'
          '                  rect 300 240 240 16  textaligny 12\n'
          '                  textalign ITEM_ALIGN_CENTER  textalignx 120\n'
          '                  forecolor %s  visible 0  decoration }\n' % WHITE)
    b += button("ig_leaveyes", "CONFIRM", 300, 262, 116, "uiScript Leave", hidden=True)
    b += button("ig_leaveno", "CANCEL", 424, 262, 116,
                "hide ig_leaveyes ; hide ig_leaveno ; hide ig_leaveask ; show ig_leave",
                hidden=True)
    return page("io_ig_match", "CURRENT MATCH", b,
                "Who is here, and which side you are on.",
                onopen="show ig_leave ; hide ig_leaveyes ; hide ig_leaveno ; hide ig_leaveask")


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
    # [QL] E116. Centred form, like the settings pages - it was built separately
    # and still had its labels on the left edge and values floating at x=200.
    b = ""
    for i, (lb, nm, od) in enumerate([("Bot", "ig_botname", "UI_BOTNAME"),
                                      ("Skill", "ig_botskill", "UI_BOTSKILL"),
                                      ("Team", "ig_botteam", "UI_REDBLUE")]):
        y = 60 + i * 36
        b += ('        itemDef { text "%s"  textscale .21  rect 24 %d %d 20  textaligny 15\n'
              '                  textalign ITEM_ALIGN_RIGHT  textalignx %d\n'
              '                  forecolor %s  visible 1  decoration }\n'
              % (lb, y, LABEL_END - 24, LABEL_END - 24, WHITE))
        b += ('        itemDef { name %s  type ITEM_TYPE_OWNERDRAW  ownerdraw %s\n'
              '                  rect %d %d 246 20  textscale .24  textaligny 15  textalignx 2\n'
              '                  forecolor 1 1 1 1  visible 1 }\n' % (nm, od, CTRL_X, y))
    b += button("ig_addbot", "ADD BOT", 200, 176, 160, "uiScript addBot")
    for i, t in enumerate(["Team only applies in a team gametype; elsewhere the bot joins the",
                           "free-for-all. Bots need the server to allow them."]):
        b += ('        itemDef { text "%s"  textscale .17  rect 24 %d 512 16  textaligny 12\n'
              '                  textalign ITEM_ALIGN_CENTER  textalignx 256\n'
              '                  forecolor %s  visible 1  decoration }\n' % (t, 214 + i * 17, DIM))
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
    # Centred pair, like Apply/Back. The hint moved up into the subtitle: there
    # is no room for it under the buttons, and beside them it was the one piece
    # of text on the page that did not line up with anything.
    b += button("ig_ctlsave", "SAVE", 150, y + 8, 120, "uiScript saveControls")
    b += button("ig_ctlreload", "RELOAD", 290, y + 8, 120, "uiScript loadControls")
    return page("io_ig_controls", "CONTROLS", b,
                "Click a binding, then press the key you want to use.")


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
    # Model block: preview in the left column, the picker in the control column,
    # the label ending at the same gutter as every row below it.
    b = ('        itemDef { text "Player model"  textscale .21  rect 190 57 80 16  textaligny 13\n'
         '                  textalign ITEM_ALIGN_RIGHT  textalignx 80\n'
         '                  forecolor %s  visible 1  decoration }\n' % WHITE)
    # LISTBOX_IMAGE over FEEDER_Q3HEADS with horizontalscroll is how Quake
    # Live's own basic page builds it - 210 wide at 26 per head is eight across.
    b += """        itemDef {
            name ig_models  rect %d 44 210 43  type ITEM_TYPE_LISTBOX
            style WINDOW_STYLE_EMPTY  elementwidth 26  elementheight 26
            elementtype LISTBOX_IMAGE  feeder FEEDER_Q3HEADS
            horizontalscroll
            border 1  bordercolor .35 .3 .12 1  visible 1
        }
        itemDef { name ig_modelpreview  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_PLAYERMODEL
                  rect 60 42 120 86  visible 1 }
""" % CTRL_X
    # [QL] E118. Name and handicap, so this page covers everything Player Setup
    # did and the Player Setup button could go. Both sit in the gap between the
    # model picker and the sliders; their labels are held right of the model
    # preview (x 60-180), ending at the same gutter as every other row.
    #
    # The name field is bound straight to the live "name" cvar, the same way
    # io_playersetup does it, so it applies as you type. Routing it through
    # ui_Name and ui_SetName was an earlier bug: the field showed nothing and
    # APPLY never wrote the name through.
    for lb, y in (("Name", 94), ("Handicap", 112)):
        b += ('        itemDef { text "%s"  textscale .21  rect 190 %d 80 16  textaligny 13\n'
              '                  textalign ITEM_ALIGN_RIGHT  textalignx 80\n'
              '                  forecolor %s  visible 1  decoration }\n' % (lb, y, WHITE))
    b += ('        itemDef { name ig_name  type ITEM_TYPE_EDITFIELD  text ""  cvar "name"\n'
          '                  maxchars 36  maxpaintchars 22\n'
          '                  rect %d 94 246 16  textscale .2  textaligny 12  textalignx 4\n'
          '                  style WINDOW_STYLE_FILLED  backcolor 0 0 0 .55\n'
          '                  border 1  bordersize 1  bordercolor .35 .3 .12 1\n'
          '                  forecolor 1 1 1 1  visible 1 }\n' % CTRL_X)
    # Player Setup's own list, copied rather than rebuilt so both menus agree.
    b += ('        itemDef { name ig_handicap  type ITEM_TYPE_MULTI  text ""  cvar "handicap"\n'
          '                  cvarFloatList { "None" 100 "90" 90 "80" 80 "70" 70 "60" 60 "50" 50 "40" 40 "30" 30 "20" 20 "10" 10 }\n'
          '                  rect %d 112 200 16  textscale .21  textaligny 13  textalignx 2\n'
          '                  forecolor 1 1 1 1  visible 1 }\n' % CTRL_X)
    y = 132
    for kind, cvar, text, extra in [
        ("slider", "sensitivity",   "Mouse sensitivity", ("5", "1", "30")),
        ("slider", "cg_fov",        "Field of view",     ("100", "75", "130")),
        ("slider", "r_gamma",       "Brightness",        ("1", "0.5", "3")),
        ("slider", "s_volume",      "Master volume",     ("0.8", "0", "1")),
        ("slider", "s_musicvolume", "Music",             ("0.25", "0", "1")),
    ]:
        b += row(kind, cvar, text, y, extra)
        y += ROW_H
    # The crosshair preview is 20 square against a 16-tall row, so it gets its
    # own slot rather than being squeezed into one and drawn over its neighbours.
    b += row_label("Crosshair", y + 4)
    b += ('        itemDef { name ig_xhair  type ITEM_TYPE_OWNERDRAW  ownerdraw UI_CROSSHAIR\n'
          '                  rect %d %d 20 20  visible 1 }\n' % (CTRL_X, y + 2))
    y += 26
    b += row("slider", "cg_crosshairSize", "Crosshair size", y, ("32", "8", "64"))
    y += ROW_H - 1
    for i, t in enumerate(["Resolution, texture detail and everything else Quake Live keeps on its",
                           "advanced page are under the Advanced tab - and on the render menu."]):
        b += ('        itemDef { text "%s"  textscale .17  rect 24 %d 512 16  textaligny 12\n'
              '                  textalign ITEM_ALIGN_CENTER  textalignx 256\n'
              '                  forecolor %s  visible 1  decoration }\n' % (t, y + i * 17, DIM))
    return page("io_ig_settings", "PLAYER", b,
                "Who you are, and the settings you change most.")


# --- Advanced ----------------------------------------------------------------
# A hub rather than a page of its own settings: the render menus already exist,
# are already organised, and duplicating their rows here would be two places to
# change one setting.
def heading(text, y):
    """A centred group heading, for pages built from groups of buttons."""
    return ('        itemDef { text "%s"  textscale .21  rect 24 %d 512 16  textaligny 12\n'
            '                  textalign ITEM_ALIGN_CENTER  textalignx 256\n'
            '                  forecolor %s  visible 1  decoration }\n' % (text, y, WHITE))


def page_advanced():
    """
    A hub. The sub-pages hold the rows; this only has to reach them.

    [QL] E115. Three groups, each named for what is actually in it. The second
    heading used to read "Ours, not Quake Live's" over a row that ended in a
    QUAKE LIVE MENU button - the one thing on the page that is Quake Live's and
    not ours. Now Quake Live's options, this port's own pages, and Quake Live's
    own menu are three separate headings, and nothing sits under the wrong one.
    Button grids are centred on the page like every other page since E113.

    E116: the headings are Normal Options (Quake Live's own settings, minus the
    ones our code does not read), Exclusive Options (pages only this port has)
    and Original Menu (Quake Live's own in-game menu, reached unchanged).
    """
    cols = [28, 200, 372]           # three 160-wide buttons, 12 apart, centred
    b = heading("Normal Options", 44)
    for i, (menu, title) in enumerate(SUBPAGE_INDEX):
        b += button("adv_" + menu, title, cols[i % 3], 64 + (i // 3) * 28, 160,
                    "close io_ig_advanced ; open %s" % menu)
    # These are full-size menus that cover this page, so they leave it open
    # underneath - closing them is what brings you back here (see E112).
    b += heading("Exclusive Options", 156)
    # E118: Player Setup is gone from here - its name, handicap and model picker
    # are all on the Player tab now. It stays on the main menu, which has no
    # Player tab. The four that remain are the renderer's, as a centred 2x2.
    b += button("ig_advrender", "RENDER OPTIONS", 114, 176, 160, "open io_renderoptions")
    b += button("ig_advwater", "WATER", 286, 176, 160, "open io_water")
    b += button("ig_advrt", "RAY TRACING", 114, 204, 160, "open io_raytracing")
    b += button("ig_advsurf", "SURFACE DETAIL", 286, 204, 160, "open io_surfacedetail")
    b += heading("Original Menu", 240)
    b += button("ig_advql", "QUAKE LIVE MENU", 200, 260, 160,
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
        ("slider", "r_gamma",             "Brightness",     ("1", "0.5", "3")),
        ("multi",  "r_overbrightBits",    "Overbright"),
        ("multi",  "r_mapOverbrightBits", "Map overbright"),
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
        ("slider", "cg_crosshairSize",       "Size",       ("32", "8", "64")),
        ("slider", "cg_crosshairBrightness", "Brightness", ("1", "0", "1")),
        ("yesno",  "cg_crosshairPulse",      "Pulse on pickup"),
        ("yesno",  "cg_crosshairHealth",     "Colour by health"),
        ("multi",  "cg_drawCrosshairNames",  "Show player names"),
    ], "The crosshair shape itself is on the Settings tab, beside its preview."),
    ("io_ig_hud", "HUD", "What is drawn on screen while you play.", [
        ("slider", "cg_fov",                 "Field of view",      ("100", "75", "130")),
        ("slider", "cg_zoomfov",             "Zoom field of view", ("30", "10", "90")),
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
        ("slider", "cg_brassTime",       "Brass time (ms)",  ("2500", "0", "5000")),
        ("multi",  "cg_trueLightning",   "True lightning"),
        ("multi",  "cg_lightningStyle",  "Lightning style"),
        ("yesno",  "cg_lightningImpact", "Lightning impact"),
        ("multi",  "cg_plasmaStyle",     "Plasma style"),
        ("multi",  "cg_rocketStyle",     "Rocket style"),
        ("slider", "cg_railTrailTime",   "Rail trail (ms)",  ("400", "0", "2000")),
        ("slider", "r_railWidth",        "Rail width",       ("16", "0", "64")),
        ("slider", "r_railCoreWidth",    "Rail core width",  ("6", "0", "24")),
    ], None),
    ("io_ig_effects", "EFFECTS", "Impacts, smoke and world detail.", [
        ("slider", "r_railSegmentLength",     "Rail segment length", ("32", "1", "128")),
        ("yesno",  "cg_simpleItems",          "Simple items"),
        ("slider", "cg_impactSparksVelocity", "Impact spark speed",  ("128", "-128", "128")),
        ("yesno",  "cg_bubbleTrail",          "Underwater bubbles"),
        ("yesno",  "cg_damagePlum",           "Damage numbers"),
        ("slider", "cg_smokeRadius_RL",       "Rocket smoke",        ("32", "0", "64")),
        ("slider", "cg_smokeRadius_GL",       "Grenade smoke",       ("64", "0", "64")),
        ("slider", "cg_smokeRadius_NG",       "Nailgun smoke",       ("16", "0", "32")),
        ("yesno",  "cg_smoke_SG",             "Shotgun smoke"),
        ("slider", "cg_kickScale",            "View kick",           ("0.25", "0", "1")),
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
        ("yesno",  "cg_specFov",           "Use followed player FOV"),
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
        # Boxed and centred on the page's centre line, like the rows above.
        back = "close %s ; open io_ig_advanced" % menu
        if any(r[1].startswith("r_") for r in rows):
            # Resolution, fullscreen and colour depth are latched by the
            # renderer; without Apply the page looks like it did nothing.
            # Lighting and Bloom are the same kind of page: brightness and
            # overbright only take effect on vid_restart, and those pages had
            # a Back and no Apply, so a change there did nothing until the
            # next map or restart. Any page with a renderer row gets the pair.
            extra = boxbutton("ap_" + menu, "APPLY", 150, 266, 120, 'exec "vid_restart"')
            extra += boxbutton("bk_" + menu, "BACK", 290, 266, 120, back)
        else:
            extra = boxbutton("bk_" + menu, "BACK", 220, 266, 120, back)
        out += rows_page(menu, title, sub, rows, footer, extra)
    return out


# ---------------------------------------------------------------- fit --------
# [QL] E116. Every label must fit inside its own rect.
#
# check-menus.py compares rects, and a rect can be perfectly placed while the text
# drawn from it runs straight out of the side: the Current Match footnote was a
# 240-wide rect holding ~277px of text, which ran off the panel. Nothing in the
# toolchain could see it, and the widths were being judged by eye.
#
# Glyph widths measured off screenshots of the built menu, in virtual pixels per
# unit of textscale: "Scoreboard and match detail stay on the HUD - TAB shows
# them" at .17 is 277px, "JOIN GAME" at .22 is 65px. Capitals and digits 35,
# lower case 29, spaces and punctuation 16 reproduce both (276 and 65). An
# estimate, so the check allows 4% of slack rather than claiming to the pixel.
GLYPH_UPPER, GLYPH_LOWER, GLYPH_OTHER = 35, 29, 16


def text_width(text, scale):
    w = 0
    for ch in text:
        if ch.isupper() or ch.isdigit():
            w += GLYPH_UPPER
        elif ch.islower():
            w += GLYPH_LOWER
        else:
            w += GLYPH_OTHER
    return w * scale


def check_text_fit(block):
    problems = []
    # One chunk per itemDef, cut at the next itemDef or menuDef. Matching braces
    # with a regex does not work here: single-line itemDefs close on the same
    # line and actions nest braces, so a brace pattern runs on into the next
    # item and pairs one item's text with another's rect.
    for body in re.split(r"\bitemDef\s*\{|\bmenuDef\s*\{", block)[1:]:
        t = re.search(r'\btext "([^"]+)"', body)
        r = re.search(r"\brect (-?\d+) (-?\d+) (\d+) (\d+)", body)
        if not t or not r:
            continue
        scale = re.search(r"\btextscale ([\d.]+)", body)
        scale = float(scale.group(1)) if scale else 0.25
        ax = re.search(r"\btextalignx (-?\d+)", body)
        ax = int(ax.group(1)) if ax else 0
        align = re.search(r"\btextalign (ITEM_ALIGN_\w+)", body)
        align = align.group(1) if align else "ITEM_ALIGN_LEFT"
        x, w = int(r.group(1)), int(r.group(3))
        tw = text_width(t.group(1), scale) * 1.04
        if align == "ITEM_ALIGN_RIGHT":
            lo, hi = x + ax - tw, x + ax
        elif align == "ITEM_ALIGN_CENTER":
            lo, hi = x + ax - tw / 2, x + ax + tw / 2
        else:
            lo, hi = x + ax, x + ax + tw
        if lo < x - 1 or hi > x + w + 1:
            problems.append('"%s" is ~%dpx in a %dpx rect' % (t.group(1), tw, w))
    return problems


def main():
    load_semantics()
    block = (BEGIN
             + frame()
             + page_match() + page_vote() + page_admin() + page_addbot()
             + page_controls() + page_settings() + page_advanced()
             + advanced_subpages()
             + END)

    overflow = check_text_fit(block)
    if overflow:
        sys.exit("gen-ingame-menu: text wider than its box:\n  " + "\n  ".join(overflow))

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
