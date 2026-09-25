#!/usr/bin/env python3
"""
[QL] E124. Generator for the render pages: Render Options, Lighting & Ray
Tracing, Water and Surface Detail.

They were hand-written in an older style - a flat dark box, labels hard left,
bare-text buttons - while the in-game menu (tools/gen-ingame-menu.py) had moved
to Quake Live's header art, a tab bar, a centred two-column form and boxed
buttons. The same four pages open from both the main menu's RENDER entry and
the in-game Advanced page, so they looked like a different game in both places.

They now use the in-game menu's vocabulary, imported from that generator rather
than copied, so a change to the palette or a button is one change:

  - the frame: dimmed backdrop, header.tga, ql_logo.tga, content panel, footer;
  - a tab bar across the four pages (it replaces the "LIGHTING >" style links);
  - the centred form: label ends at the gutter, control starts after it;
  - boxed buttons, black and muted gold, gold on hover.

Each page is a whole menu of its own - frame, tabs with its own tab lit, rows,
footer - rather than a page inside a shared frame. Every existing "open
io_water" in main.menu and in the in-game Advanced page keeps working without
also having to open and re-tab a frame.

    python3 tools/gen-render-menu.py && python3 tools/check-menus.py

It rewrites the block between the io_render markers in content/pak01/ui/main.menu.
"""
import importlib.util
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("ig", HERE / "gen-ingame-menu.py")
ig = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ig)

MENU = HERE.parent / "content/pak01/ui/main.menu"
BEGIN = "    // >>> GENERATED io_render BEGIN - tools/gen-render-menu.py\n"
END = "    // <<< GENERATED io_render END\n"

GOLD, WHITE, DIM = ig.GOLD, ig.WHITE, ig.DIM
W = ig.FRAME_W                  # 560, the in-game frame's width
LABEL_END, CTRL_X = ig.LABEL_END, ig.CTRL_X
ROW_H = ig.ROW_H                # 17
PANEL_Y, PANEL_BOTTOM = 82, 440
FOOT_Y = 444
TITLE_Y = PANEL_Y + 4
ROWS_Y = PANEL_Y + 32

VK = ('cvarTest "cl_renderer"  showCvar { "vulkan" }')
GL = ('cvarTest "cl_renderer"  hideCvar { "vulkan" }')

# The four pages, by suffix. Each is generated twice (E125):
#   main   io_<suffix>     from the main menu's RENDER entry. CLOSE closes it and
#                          the main menu is underneath.
#   ingame io_igr_<suffix> from the in-game Advanced page. It REPLACES the
#                          in-game frame (Advanced closes io_ingame before opening
#                          it), so there is one dim and one tab bar, not two
#                          stacked; BACK reopens io_ingame and io_ig_advanced.
#                          The frame no longer resets its tabs on open, so
#                          Advanced is still the lit tab when you come back.
TABS = [
    ("renderoptions", "Render Options"),
    ("raytracing",    "Lighting & Ray Tracing"),
    ("water",         "Water"),
    ("surfacedetail", "Surface Detail"),
]

VARIANTS = {
    "main":   {"prefix": "io_"},
    "ingame": {"prefix": "io_igr_"},
}
V = VARIANTS["main"]    # the variant being generated; set by main()


def name(suffix):
    return V["prefix"] + suffix


def close_all():
    return " ; ".join("close %s" % name(t[0]) for t in TABS)


def leave_action():
    """What BACK/CLOSE does, and what ESC does, for the current variant."""
    if V is VARIANTS["ingame"]:
        return (close_all() + " ; open io_ingame ; open io_ig_advanced",
                close_all() + " ; uiScript closeingame")
    return close_all(), close_all()

APPLY = 'exec "vid_restart"'


# ---------------------------------------------------------------- frame ------
def tab_layout():
    widths = [len(t[1]) * ig.TAB_CHAR + ig.TAB_PAD * 2 for t in TABS]
    spare = W - ig.TAB_INSET * 2 - sum(widths)
    if spare < 0:
        raise SystemExit("gen-render-menu: tab labels no longer fit the bar")
    each = spare // len(widths)
    xs, x = [], ig.TAB_INSET
    for w in widths:
        xs.append((x, w + each))
        x += w + each
    return xs


def tabs(current):
    """The tab bar with `current` lit. Static per page - each page is its own menu."""
    out = []
    for i, ((suffix, label), (x, w)) in enumerate(zip(TABS, tab_layout())):
        menu = name(suffix)
        on = menu == current
        out.append("""        itemDef {
            name rtab%d  rect %d %d %d %d  style WINDOW_STYLE_FILLED  visible 1  type ITEM_TYPE_BUTTON
            action { play "sound/misc/menu1.wav" ; %s ; open %s }
        }
        itemDef { name rtab%do  rect %d %d %d %d  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor %s }
        itemDef { name rtab%du  rect %d %d %d 1  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor %s }
        itemDef { name rtab%dt  rect %d %d %d %d  text "%s"  textscale .18
                  style WINDOW_STYLE_EMPTY  textalign ITEM_ALIGN_CENTER  textalignx %d  textaligny 13
                  forecolor %s  visible 1  decoration }
""" % (i, x, ig.TAB_Y, w, ig.TAB_H, close_all(), menu,
       i, x + 2, ig.TAB_Y, w - 4, ig.TAB_H + 1, ig.SEL_BACK if on else ig.OFF_BACK,
       i, x + 2, ig.TAB_Y + ig.TAB_H, w - 4, GOLD if on else ig.OFF_BACK,
       i, x, ig.TAB_Y, w, ig.TAB_H, label, w // 2, GOLD if on else WHITE))
    return "".join(out)


def framed(menu, title, body, buttons):
    """
    One render page: the in-game frame's art and geometry, this page's tab lit,
    a title and rule, the body, and a footer bar holding `buttons`.
    """
    return """
    // check-menus: overlap-ok - layered like io_ingame: the backdrop under
    // everything and overhanging the frame, the logo overhanging it left, each
    // tab four items on one rect, and rows gated on cl_renderer sharing a y.
    menuDef {
        name "%s"
        rect 40 0 %d 480
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_EMPTY
        focusColor %s
        disableColor .5 .5 .5 1

        onESC { %s }

        // One dim, the same as io_ingame's ig_dim. In a game this page stands
        // in for that frame rather than sitting on it, so this is the only one.
        itemDef { name rdim  rect -220 0 1080 480  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.55 }
        itemDef { name rheader  rect 0 0 %d 64  background "ui/assets/main_menu/header.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1
                  border 1  bordercolor 0 0 0 0.5 }
        itemDef { name rlogo  rect -20 0 256 64  background "ui/assets/main_menu/ql_logo.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1 }
        itemDef { name rbar  rect 0 63 %d 18  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.75  border 1  bordercolor 0 0 0 0.5 }
%s        itemDef { name rpage  rect 0 %d %d %d
                  background "ui/assets/main_menu/content_background.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration
                  forecolor 1 1 1 1  backcolor 1 1 1 1  border 1  bordercolor 0 0 0 0.5 }
        itemDef { name rtitle  text "%s"  textscale .26  rect 20 %d 520 24  textaligny 17
                  textalign ITEM_ALIGN_CENTER  textalignx 260
                  forecolor %s  visible 1  decoration }
        itemDef { name rrule  text ""  rect 20 %d 520 1  style WINDOW_STYLE_FILLED
                  backcolor .35 .3 .12 1  visible 1  decoration }
%s        itemDef { name rfoot  rect 0 %d %d 34  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.75  border 1  bordercolor 0 0 0 0.5 }
%s    }
""" % (menu, W, GOLD, leave_action()[1], W, W, tabs(menu),
       PANEL_Y, W, PANEL_BOTTOM - PANEL_Y,
       title, TITLE_Y, GOLD, TITLE_Y + 24, body, FOOT_Y, W, footer(buttons))


def footbutton(nm, label, x, w, action):
    """The in-game frame's RESUME button, exactly: 26 tall, textscale .25."""
    return """        itemDef {
            name %s  text "%s"  type ITEM_TYPE_BUTTON  textscale .25
            rect %d %d %d 26  textalign ITEM_ALIGN_CENTER  textalignx %d  textaligny 17
            style WINDOW_STYLE_FILLED  backcolor %s  border 1  bordersize 1  bordercolor %s
            forecolor %s  visible 1
            action { play "sound/misc/menu1.wav" ; %s }
            mouseEnter { setitemcolor %s backcolor %s ; setitemcolor %s forecolor %s ; setitemcolor %s bordercolor %s }
            mouseExit  { setitemcolor %s backcolor %s ; setitemcolor %s forecolor %s ; setitemcolor %s bordercolor %s }
        }
""" % (nm, label, x, FOOT_Y + 4, w, w // 2, ig.BTN_BACK, ig.BTN_EDGE, WHITE, action,
       nm, ig.BTN_HOT, nm, GOLD, nm, ig.BTN_EDGE_HOT, nm, ig.BTN_BACK, nm, WHITE, nm, ig.BTN_EDGE)


def footer(buttons):
    """Footer buttons centred in the bar, 12 apart, sized like RESUME."""
    buttons = [b if b != "leave" else
               (("rback", "BACK", 120, leave_action()[0]) if V is VARIANTS["ingame"]
                else ("rclose", "CLOSE", 120, leave_action()[0]))
               for b in buttons]
    total = sum(w for _, _, w, _ in buttons) + 12 * (len(buttons) - 1)
    x = (W - total) // 2
    out = ""
    for nm, label, w, action in buttons:
        out += footbutton(nm, label, x, w, action)
        x += w + 12
    return out


# ---------------------------------------------------------------- rows -------
def gate_attr(gate):
    return {"vk": "  " + VK, "gl": "  " + GL}.get(gate, "")


def lbl(text, y, gate=None):
    return ('        itemDef { text "%s"  textscale .21  rect 24 %d %d %d  textaligny 13\n'
            '                  textalign ITEM_ALIGN_RIGHT  textalignx %d\n'
            '                  forecolor %s  visible 1  decoration%s }\n'
            % (text, y, LABEL_END - 24, ROW_H - 1, LABEL_END - 24, WHITE, gate_attr(gate)))


def heading(text, y, gate=None):
    return ('        itemDef { text "%s"  textscale .2  rect 24 %d 512 %d  textaligny 13\n'
            '                  textalign ITEM_ALIGN_CENTER  textalignx 256\n'
            '                  forecolor %s  visible 1  decoration%s }\n'
            % (text, y, ROW_H - 1, GOLD, gate_attr(gate)))


def helpline(text, y, gate=None, test=None):
    extra = gate_attr(gate)
    if test:
        extra += '  cvarTest "%s"  showCvar { "%s" }' % test
    return ('        itemDef { text "%s"  textscale .17  rect 24 %d 512 %d  textaligny 12\n'
            '                  textalign ITEM_ALIGN_CENTER  textalignx 256\n'
            '                  forecolor %s  visible 1  decoration%s }\n'
            % (text, y, ROW_H - 1, DIM, extra))


def control(kind, cvar, y, values=None, gate=None, action=None):
    nm = "c_" + re.sub(r"[^a-z0-9]", "", cvar.lower())
    g = gate_attr(gate)
    act = ("\n                  action { %s }" % action) if action else ""
    if kind == "yesno":
        return ('        itemDef { name %s  type ITEM_TYPE_YESNO  text ""  cvar "%s"\n'
                '                  rect %d %d 70 %d  textscale .21  textaligny 13  textalignx 2\n'
                '                  forecolor 1 1 1 1  visible 1%s%s }\n'
                % (nm, cvar, CTRL_X, y, ROW_H - 1, act, g))
    if kind == "slider":
        dflt, lo, hi = values
        return ('        itemDef { name %s  type ITEM_TYPE_SLIDER  text ""\n'
                '                  cvarFloat "%s" %s %s %s\n'
                '                  rect %d %d %d %d  textscale .21  textaligny 13\n'
                '                  forecolor 1 1 1 1  visible 1%s }\n'
                '        itemDef { name %sv  type ITEM_TYPE_NUMERICFIELD  text ""  cvar "%s"\n'
                '                  maxchars 7  maxpaintchars 7\n'
                '                  rect %d %d %d %d  textscale .19  textaligny 12  textalignx 4\n'
                '                  style WINDOW_STYLE_FILLED  backcolor 0 0 0 .55\n'
                '                  border 1  bordersize 1  bordercolor .35 .3 .12 1\n'
                '                  forecolor 1 1 1 1  visible 1%s }\n'
                % (nm, cvar, dflt, lo, hi, CTRL_X, y, ig.SLIDER_W, ROW_H - 1, g,
                   nm, cvar, ig.VALUE_X, y, ig.VALUE_W, ROW_H - 1, g))
    listkw = "cvarStrList" if kind == "str" else "cvarFloatList"
    return ('        itemDef { name %s  type ITEM_TYPE_MULTI  text ""  cvar "%s"\n'
            '                  %s { %s }\n'
            '                  rect %d %d 250 %d  textscale .21  textaligny 13  textalignx 2\n'
            '                  forecolor 1 1 1 1  visible 1%s%s }\n'
            % (nm, cvar, listkw, values, CTRL_X, y, ROW_H - 1, act, g))


def status(label_text, y, variants):
    """A read-only row: one label, and one coloured text per value of a cvar."""
    out = lbl(label_text, y)
    for text, color, cvar, value in variants:
        out += ('        itemDef { text "%s"  textscale .21  rect %d %d 250 %d  textaligny 13\n'
                '                  forecolor %s  visible 1  decoration  cvarTest "%s"  showCvar { "%s" } }\n'
                % (text, CTRL_X, y, ROW_H - 1, color, cvar, value))
    return out


def layout(spec):
    """
    Lay a page out top to bottom on the ROW_H grid. Spec entries:
      ("h", text[, gate])                     section heading
      ("help", text[, gate[, (cvar, value)]]) centred grey note
      (kind, cvar, label, values[, gate[, action]])   a settings row
      ("status", label, variants)             read-only coloured row
      ("presets", [(name, label, action)])    a row of boxed buttons
      ("alt", specA, specB)                   two blocks sharing the same y
                                              (e.g. Vulkan rows / OpenGL2 rows)
    """
    b, y = "", ROWS_Y
    for e in spec:
        kind = e[0]
        if kind == "h":
            b += heading(e[1], y, e[2] if len(e) > 2 else None)
        elif kind == "help":
            b += helpline(e[1], y, e[2] if len(e) > 2 else None, e[3] if len(e) > 3 else None)
        elif kind == "status":
            b += status(e[1], y, e[2])
        elif kind == "presets":
            n = len(e[1])
            w = 104
            x = (W - (n * w + (n - 1) * 10)) // 2
            for name, label, action in e[1]:
                b += ig.boxbutton(name, label, x, y - 2, w, action)
                x += w + 10
            y += 8   # a 22-tall button row takes a little more than a row
        elif kind == "alt":
            ba, ya = layout_at(e[1], y)
            bb, yb = layout_at(e[2], y)
            b += ba + bb
            y = max(ya, yb)
            continue
        else:
            _, cvar, text, values = e[:4]
            gate = e[4] if len(e) > 4 else None
            action = e[5] if len(e) > 5 else None
            b += lbl(text, y, gate) + control(kind, cvar, y, values, gate, action)
        y += ROW_H
    return b, y


def layout_at(spec, y0):
    global ROWS_Y
    saved = ROWS_Y
    ROWS_Y = y0
    try:
        return layout(spec)
    finally:
        ROWS_Y = saved


def page(menu, title, spec, buttons):
    body, y = layout(spec)
    if y > PANEL_BOTTOM - 2:
        raise SystemExit("gen-render-menu: %s runs to y=%d, past the panel (%d) - "
                         "split the page rather than shrinking the pitch" % (menu, y, PANEL_BOTTOM))
    return framed(menu, title, body, buttons)


# ---------------------------------------------------------------- pages ------
CLOSE = "leave"          # BACK in a game, CLOSE on the main menu - see footer()
APPLY_BTN = ("rapply", "APPLY (restart video)", 210, APPLY)


def render_options():
    # [QL] E111. The r_mode list is Quake Live's own, out of
    # docs/ql-cvar-semantics.txt rather than invented: -2 is Native Desktop, -1
    # is Custom, 0..27 its fixed modes.
    modes = ('"Native Desktop" -2 "Custom" -1 "640x480 (4:3)" 5 "800x600 (4:3)" 9 '
             '"1024x768 (4:3)" 12 "1152x864 (4:3)" 13 "1280x720 (16:9)" 14 "1280x800 (16:10)" 16 '
             '"1280x1024 (5:4)" 17 "1440x900 (16:10)" 18 "1600x900 (16:9)" 19 "1680x1050 (16:10)" 21 '
             '"1600x1200 (4:3)" 22 "1920x1080 (16:9)" 23 "1920x1200 (16:10)" 24 "2048x1536 (4:3)" 26 '
             '"2560x1600 (16:10)" 27')
    spec = [
        ("presets", [("pclassic", "CLASSIC", 'exec "exec classic.cfg"'),
                     ("pgloss", "GLOSS", 'exec "exec gloss.cfg"'),
                     ("pvoodoo", "VOODOO", 'exec "exec voodoo.cfg"'),
                     ("pmodern", "DEFAULTS", 'exec "exec modern.cfg"')]),
        ("h", "OUTPUT"),
        ("str", "cl_renderer", "Renderer", '"OpenGL 2", "opengl2", "Vulkan", "vulkan"'),
        ("multi", "r_mode", "Resolution", modes),
        ("yesno", "r_fullscreen", "Fullscreen", None),
        ("multi", "r_colorbits", "Colour depth",
         '"Driver default" 0 "16-bit (3dfx look)" 16 "10-bit per channel" 30'),
        ("multi", "r_dither", "Dither", '"Off" 0 "Ordered (stable)" 1 "Temporal (best)" 2'),
        ("h", "IMAGE"),
        # [QL] E124. Brightness belongs on the render page too - it was only on
        # the in-game Lighting page. It applies live now that the renderer's
        # cvar group is tracked (cl_main.c CL_RefCvarCheckGroup).
        ("slider", "r_gamma", "Brightness", ("1", "0.5", "3")),
        ("multi", "r_ext_multisample", "Antialiasing", '"Off" 0 "2x MSAA" 2 "4x MSAA" 4 "8x MSAA" 8'),
        ("str", "r_textureMode", "Texture filter",
         '"Trilinear", "GL_LINEAR_MIPMAP_LINEAR", "Bilinear", "GL_LINEAR_MIPMAP_NEAREST", '
         '"Nearest", "GL_NEAREST"'),
        # [QL] The action is not decoration. Anisotropy is gated on a second
        # cvar, r_ext_texture_filter_anisotropic, and classic.cfg sets that to 0
        # on purpose - so after CLASSIC this control could read 16x and do
        # nothing. Setting it here turns the gate back on.
        ("multi", "r_ext_max_anisotropy", "Anisotropic filter", '"Off" 1 "2x" 2 "4x" 4 "8x" 8 "16x" 16',
         "vk", 'exec "r_ext_texture_filter_anisotropic 1"'),
        ("yesno", "r_bloom", "Bloom", None, "vk"),
        ("yesno", "r_flares", "Lens flares", None),
        ("h", "POST PROCESSING"),
        ("alt",
         [("multi", "r_rts", "Real-time shading",
           '"Off (default)" 0 "On, float target" 1 "On, packed float" 2', "vk"),
          ("yesno", "r_postProcess", "Post processing", None, "vk")],
         [("yesno", "r_hdr", "HDR framebuffer", None, "gl"),
          ("yesno", "r_toneMap", "Tonemapping", None, "gl")]),
    ]
    return page(name("renderoptions"), "RENDER OPTIONS", spec, [APPLY_BTN, CLOSE])


def raytracing():
    # [QL] Two status rows, one cvar each, two exclusive variants per row. One
    # line per outcome does not work: cvarTest takes a single cvar, and
    # "supported but not active" is a condition on two of them.
    spec = [
        ("h", "STATUS"),
        ("status", "This GPU", [("supports ray queries", ".4 1 .4 1", "r_rtAvailable", "1"),
                                ("does not support ray queries", "1 .4 .4 1", "r_rtAvailable", "0")]),
        ("status", "This run", [("active", ".4 1 .4 1", "r_rtActive", "1"),
                                ("not active - turn it on below and APPLY", GOLD, "r_rtActive", "0")]),
        ("help", "Needs acceleration structure, ray query, deferred host operations,", None,
         ("r_rtAvailable", "0")),
        ("help", "buffer device address and a Vulkan 1.1 loader.", None, ("r_rtAvailable", "0")),
        ("h", "DEVICE"),
        ("yesno", "r_rt", "Enable ray query", None),
        ("help", "Latched: builds the world acceleration structure at map load. Needs APPLY."),
        # [QL] Not gated on r_rtActive: cvarTest takes one cvar. A control that
        # is present but inert until ray query is on beats one that vanishes;
        # the STATUS rows say whether it can work. Lists, not sliders, so the
        # value and the default are both visible.
        ("h", "AMBIENT OCCLUSION"),
        ("multi", "r_rtao", "Ambient occlusion", '"Off" 0 "On" 1 "Debug (show occlusion)" 2'),
        ("multi", "r_rtaoRadius", "Radius",
         '"16 (tight creases)" 16 "32" 32 "64 (default)" 64 "128" 128 "256 (wide)" 256 '
         '"512" 512 "1024" 1024 "2048" 2048 "4096 (whole rooms)" 4096'),
        ("multi", "r_rtaoIntensity", "Strength", '"0.25 (subtle)" 0.25 "0.5" 0.5 "0.8 (default)" 0.8 "1.0 (full)" 1'),
        # [QL] Labelled by cost: the trace is full resolution, so 16 rays at 4K
        # is 133 million ray queries in one draw - past a driver watchdog.
        ("multi", "r_rtaoSamples", "Rays per pixel", '"2 (fastest)" 2 "4 (default)" 4 "8 (heavy)" 8 "16 (very heavy)" 16'),
        ("multi", "r_rtaoDenoise", "Denoise", '"Off (raw, grainy)" 0 "On (default)" 1 "Wide (smoothest)" 2'),
        # [QL] Occlusion is an ambient term; where a rocket is the light, its
        # contact shadow reads as dirt. Cleared over the light's own falloff.
        ("multi", "r_rtaoLights", "Lights clear AO", '"Off" 0 "Half" 0.5 "Full (default)" 1'),
    ]
    reset = ("raoreset", "RESET AO", 120,
             'exec "set r_rtao 0 ; set r_rtaoRadius 64 ; set r_rtaoIntensity 0.8 ; '
             'set r_rtaoSamples 4 ; set r_rtaoDenoise 1 ; set r_rtaoLights 1"')
    return page(name("raytracing"), "LIGHTING & RAY TRACING", spec, [reset, APPLY_BTN, CLOSE])


def water():
    # [QL] R19. Screen space: only what is on screen can be reflected, so a wall
    # behind you never appears in the water. The trace fades toward the screen
    # edges rather than stopping at them.
    #
    # [QL] R28. IMPACTS is a separate system from WAVES and shares no setting
    # with it; "the splashes are too small" was sending people to WAVES. Foam is
    # under IMPACTS because it is driven by impact energy alone. Every impact
    # value is a MULTIPLIER on what the weapon asked for, so a rocket stays
    # bigger than a pellet. E124: 4/4 with foam off is the baseline.
    spec = [
        ("h", "REFLECTION"),
        ("multi", "r_ssr", "Water reflections", '"Off (default)" 0 "Subtle" 0.35 "Half" 0.5 "Full" 1'),
        ("multi", "r_ssrDistance", "Trace distance", '"512 (near)" 512 "1024 (default)" 1024 "2048" 2048 "4096 (far)" 4096'),
        ("multi", "r_ssrSteps", "Trace steps", '"16 (fastest)" 16 "24 (default)" 24 "32" 32 "64" 64 "128 (sharpest)" 128'),
        ("multi", "r_ssrThickness", "Thickness", '"4 (thin)" 4 "8" 8 "16" 16 "24 (default)" 24 "32 (forgiving)" 32'),
        ("multi", "r_ssrDebug", "Debug view",
         '"Off" 0 "1 mask" 1 "2 ray hit" 2 "3 reflection" 3 "4 waves" 4 "5 ripples + foam" 5'),
        # [QL] The surface never moves a vertex - Quake Live's water is
        # subdivided at compile time. What ripples is the normal.
        ("h", "WAVES (WIND CHOP)"),
        ("yesno", "r_waterWaves", "Waves", None),
        ("multi", "r_waterWaveSteepness", "Steepness",
         '"0.02 (glassy)" 0.02 "0.05" 0.05 "0.08 (default)" 0.08 "0.12" 0.12 "0.2 (choppy)" 0.2'),
        ("multi", "r_waterWaveHeight", "Height", '"0 (flat)" 0 "1" 1 "2 (default)" 2 "4" 4 "8 (swell)" 8'),
        ("multi", "r_waterWaveScale", "Wavelength", '"48 (fine)" 48 "96 (default)" 96 "192" 192 "384 (broad)" 384'),
        ("multi", "r_waterWaveSpeed", "Speed", '"0 (frozen)" 0 "0.5 (slow)" 0.5 "1 (default)" 1 "2 (fast)" 2'),
        ("h", "IMPACTS (SPLASHES)"),
        ("multi", "r_waterRippleSize", "Splash size",
         '"1 (small)" 1 "2" 2 "4 (default)" 4 "6" 6 "8 (wide)" 8 "12" 12 "16 (huge)" 16'),
        ("multi", "r_waterRippleHeight", "Splash strength",
         '"0 (none)" 0 "1 (soft)" 1 "2" 2 "4 (default)" 4 "6" 6 "8 (hard)" 8 "12" 12 "16 (violent)" 16'),
        ("multi", "r_waterRippleWaves", "Ripple rings", '"2 (few, fat)" 2 "5 (default)" 5 "8" 8 "12 (fine)" 12'),
        ("multi", "r_waterRippleLife", "Ripple lifetime", '"1 (brief)" 1 "2.2 (default)" 2.2 "4 (slow)" 4 "6 (lingering)" 6'),
        ("multi", "r_waterFoam", "Foam", '"Off (default)" 0 "Subtle" 0.5 "On" 1 "Heavy" 2'),
    ]
    reset = ("rwreset", "RESET WATER", 130,
             'exec "set r_ssr 0 ; set r_ssrDistance 1024 ; set r_ssrSteps 24 ; set r_ssrThickness 24 ; '
             'set r_ssrDebug 0 ; set r_waterWaves 1 ; set r_waterWaveSteepness 0.08 ; '
             'set r_waterWaveHeight 2 ; set r_waterWaveScale 96 ; set r_waterWaveSpeed 1 ; '
             'set r_waterFoam 0 ; set r_waterRippleSize 4 ; set r_waterRippleHeight 4 ; '
             'set r_waterRippleWaves 5 ; set r_waterRippleLife 2.2"')
    return page(name("water"), "WATER", spec, [reset, CLOSE])


def surface_detail():
    # [QL] R20 Stage A: a normal map derived from each dynamically lit
    # surface's own texture. Latched, because the strength is baked into image
    # bytes at shader-parse time.
    #
    # [QL] R25: authored normal maps under STATIC light need a map compiled
    # with q3map2 -deluxe; Quake Live's own maps carry no deluxemaps, so this is
    # our maps only. Live: specialization constants.
    #
    # [QL] R27: stochastic hex-tiling. "Force on all" is labelled debug - it
    # wrecks anything with structure (a sign becomes three signs).
    #
    # renderergl2's material maps have no Vulkan consumer, so they swap in
    # for the Vulkan-only rows rather than being offered and ignored.
    vk_rows = [
        ("h", "DERIVED NORMAL MAPS", "vk"),
        ("yesno", "r_qlNormalMaps", "Normal perturbation", None, "vk"),
        ("multi", "r_qlNormalScale", "Strength",
         '"0.25 (subtle)" 0.25 "0.5 (default)" 0.5 "1.0" 1 "1.5" 1.5 "2.0 (hard)" 2', "vk"),
        ("multi", "r_qlNormalMaxTilt", "Max edge tilt",
         '"8 (flattest)" 8 "12" 12 "15 (default)" 15 "22" 22 "30 (bands on grazing light)" 30', "vk"),
        ("help", "Strength shapes gradients, max tilt caps painted edges. Both need APPLY.", "vk"),
        ("h", "STATIC BUMP (deluxemaps)", "vk"),
        ("multi", "r_deluxeMapping", "Deluxemap shading",
         '"Off" 0 "On (default)" 1 "Debug: light direction" 2 "Debug: perturbed normal" 3 '
         '"Debug: modulation only" 4 "Debug: the normal map itself" 5 "Debug: parallax offset" 6', "vk"),
        ("multi", "r_qlBumpScale", "Static bump strength",
         '"0.25 (subtle)" 0.25 "0.5" 0.5 "0.75" 0.75 "1.0 (default)" 1', "vk"),
        ("multi", "r_qlParallax", "Parallax depth",
         '"Off" 0 "0.02 (shallow)" 0.02 "0.04 (default)" 0.04 "0.08" 0.08 "0.12 (deep)" 0.12', "vk"),
        ("multi", "r_qlBumpSpecular", "Bump specular",
         '"Off" 0 "0.25" 0.25 "0.5 (default)" 0.5 "1.0" 1 "2.0 (wet)" 2', "vk"),
        ("multi", "r_qlNaturalTextures", "Natural textures",
         '"Off" 0 "Shaders that ask (default)" 1 "Debug: force on all" 2', "vk"),
    ]
    gl_rows = [
        ("h", "MATERIAL MAPS", "gl"),
        ("yesno", "r_cubeMapping", "Cubemap reflections", None, "gl"),
        ("multi", "r_cubemapSize", "Cubemap size", '"64" 64 "128" 128 "256" 256 "512" 512', "gl"),
        ("yesno", "r_specularMapping", "Specular mapping", None, "gl"),
        ("yesno", "r_normalMapping", "Normal mapping", None, "gl"),
    ]
    spec = [
        ("h", "DYNAMIC LIGHTS"),
        ("multi", "r_dlightMode", "Dynamic lights", '"Off surfaces" 0 "On surfaces" 1 "With shadows" 2'),
        ("yesno", "cg_beamLights", "Beam weapon lights", None),
        ("help", "A rail or lightning beam lights the corridor it crosses."),
        ("alt", vk_rows, gl_rows),
        # [QL] E113. r_ambientScale is CVAR_CHEAT - it lifts models standing in
        # shadow - so outside sv_cheats 1 the write is refused. It stays because
        # it works on a /devmap, where the test maps are judged, and says so.
        ("slider", "r_ambientScale", "Ambient (devmap only)", ("0.6", "0", "2")),
    ]
    return page(name("surfacedetail"), "SURFACE DETAIL", spec, [APPLY_BTN, CLOSE])


# ---------------------------------------------------------------- prompt -----
# [QL] E126. The hardware prompt. OpenGL 2 stays the default for low-end
# machines; CL_CheckHardwarePrompt (cl_main.c) probes Vulkan once from the main
# menu and opens io_hwprompt on a discrete GPU or one with ray query. The GPU's
# name comes from ui_hwGpuName - an item with a cvar and no text paints the
# cvar, the same trick as the main menu's build stamp.
PW, PH = 440, 200


def popup(menu, title, lines, buttons, esc):
    """A centred dialog in the frame's palette: dim, title bar, panel, buttons."""
    x0, y0 = (640 - PW) // 2, 130
    body = ""
    y = 44
    for ln in lines:
        text, color, extra = ln[0], ln[1], (ln[2] if len(ln) > 2 else "")
        if text is None:        # the GPU name, painted from its cvar
            body += ('        itemDef { name hwgpu  cvar "ui_hwGpuName"  textscale .22  rect 20 %d %d 18\n'
                     '                  textaligny 14  textalign ITEM_ALIGN_CENTER  textalignx %d\n'
                     '                  forecolor %s  visible 1  decoration }\n'
                     % (y, PW - 40, (PW - 40) // 2, GOLD))
        else:
            body += ('        itemDef { text "%s"  textscale .2  rect 20 %d %d 18  textaligny 14\n'
                     '                  textalign ITEM_ALIGN_CENTER  textalignx %d\n'
                     '                  forecolor %s  visible 1  decoration%s }\n'
                     % (text, y, PW - 40, (PW - 40) // 2, color, extra))
        if not (len(ln) > 3 and ln[3] == "same"):
            y += 18
    # A slot is a button (name, label, width, action), or a list of gated
    # variants of one button - (name, label, width, action, gate) with gates
    # that exclude each other - drawn on the same spot, exactly one visible.
    slots = [b if isinstance(b, list) else [b] for b in buttons]
    total = sum(v[0][2] for v in slots) + 16 * (len(slots) - 1)
    bx = (PW - total) // 2
    for variants in slots:
        for v in variants:
            nm, label, w, action = v[:4]
            btn = ig.boxbutton(nm, label, bx, PH - 34, w, action)
            if len(v) > 4:
                btn = btn.replace("            forecolor 1 1 1 1  visible 1\n",
                                  "            forecolor 1 1 1 1  visible 1  " + v[4] + "\n", 1)
            body += btn
        bx += variants[0][2] + 16
    return """
    // check-menus: overlap-ok - a dialog: the dim deliberately overhangs the
    // frame to cover the whole screen, and the title bar and panel are layers.
    menuDef {
        name "%s"
        rect %d %d %d %d
        visible MENU_FALSE
        fullScreen 0
        style WINDOW_STYLE_EMPTY
        focusColor %s
        popup

        onESC { %s }

        itemDef { name hwdim  rect %d %d 1280 480  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.6 }
        itemDef { name hwpanel  rect 0 0 %d %d  background "ui/assets/main_menu/content_background.tga"
                  style WINDOW_STYLE_FILLED  visible 1  decoration  backcolor 1 1 1 1
                  border 1  bordercolor .35 .3 .12 1 }
        itemDef { name hwbar  rect 0 0 %d 30  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor 0 0 0 0.75 }
        itemDef { name hwtitle  text "%s"  textscale .24  rect 0 0 %d 30  textaligny 20
                  textalign ITEM_ALIGN_CENTER  textalignx %d  forecolor %s  visible 1  decoration }
        itemDef { name hwrule  rect 20 30 %d 1  style WINDOW_STYLE_FILLED  visible 1  decoration
                  backcolor .35 .3 .12 1 }
%s    }
""" % (menu, x0, y0, PW, PH, GOLD, esc, -x0 - 320, -y0, PW, PH, PW, title, PW, PW // 2, GOLD,
       PW - 40, body)


def hw_prompts():
    done = 'exec "seta cl_hwPrompt 1"'
    step1 = popup(
        "io_hwprompt", "HIGH-END HARDWARE DETECTED",
        [("We found a graphics card that can run the Vulkan renderer:", WHITE),
         (None, None),
         ("Switch to Vulkan? It is faster on this card and unlocks", WHITE),
         ("the advanced effects. OpenGL 2 stays under Render Options.", DIM)],
        [("hwyes", "YES", 120, done + ' ; exec "seta cl_renderer vulkan" ; '
                                    'close io_hwprompt ; open io_hwprompt2'),
         ("hwno", "NO", 120, done + " ; close io_hwprompt")],
        done + " ; close io_hwprompt")
    # Two YES buttons on one spot, gated on ray query: the plain one leaves
    # ray-traced AO out, which needs VK_KHR_ray_query and would only print
    # "not available" on a card without it.
    rq = 'cvarTest "ui_hwRayQuery"  showCvar { "1" }'
    norq = 'cvarTest "ui_hwRayQuery"  showCvar { "0" }'
    step2 = popup(
        "io_hwprompt2", "ADVANCED RENDERING",
        [("Turn on the advanced rendering as well?", WHITE),
         ("Ray-traced ambient occlusion, water reflections and waves.", GOLD, "  " + rq, "same"),
         ("Water reflections and waves (no ray query on this card).", GOLD, "  " + norq),
         ("Each one can be changed or turned off under Render Options.", DIM),
         ("The game restarts its video to apply this.", DIM)],
        # YES is two buttons on one slot: advanced.cfg where the card has ray
        # query, advanced_norq.cfg (no ray-traced AO) where it does not.
        [[("hw2yes", "YES", 120, 'close io_hwprompt2 ; exec "exec advanced.cfg ; vid_restart"', rq),
          ("hw2yesb", "YES", 120, 'close io_hwprompt2 ; exec "exec advanced_norq.cfg ; vid_restart"',
           norq)],
         ("hw2no", "NO", 120, 'close io_hwprompt2 ; exec "vid_restart"')],
        'close io_hwprompt2 ; exec "vid_restart"')
    return step1 + step2


def main():
    global V
    pages = ""
    for key in ("main", "ingame"):
        V = VARIANTS[key]
        pages += render_options() + raytracing() + water() + surface_detail()
    block = (BEGIN
             + "    // Generated - edit tools/gen-render-menu.py, not this block.\n"
             + pages
             + hw_prompts()
             + END)

    overflow = ig.check_text_fit(block)
    if overflow:
        sys.exit("gen-render-menu: text wider than its box:\n  " + "\n  ".join(overflow))

    text = MENU.read_text()
    if BEGIN not in text or END not in text:
        sys.exit("gen-render-menu: markers not found in %s" % MENU)
    start = text.index(BEGIN)
    end = text.index(END) + len(END)
    MENU.write_text(text[:start] + block + text[end:])
    print("gen-render-menu: wrote %d pages x %d variants into %s"
          % (len(TABS), len(VARIANTS), MENU.name))
    print("gen-render-menu: now run tools/check-menus.py and tools/check-menu-defaults.py")


if __name__ == "__main__":
    main()
