# renderervk — vendored from Quake3e

Source: [ec-/Quake3e](https://github.com/ec-/Quake3e), `code/renderervk`, GPLv2 —
the same licence as the rest of this tree, so vendoring is fine with attribution.

## Why this is here

`cl_renderer` already dispatches by DLL name (`cl_main.c`, `CVAR_ARCHIVE |
CVAR_LATCH`, falling back to its reset string when the named library is missing)
and `USE_RENDERER_DLOPEN=1`, so a third renderer needs no engine change at all.
It needs to build as `vulkan<arch>.so` / `.dll` and export `GetRefAPI`.

## Status: NOT WIRED UP

Vendored only. `BUILD_RENDERER_VULKAN` defaults to 0 and nothing in the default
build touches this directory. It cannot break an OpenGL build.

## The gap to close

Quake3e is `REF_API_VERSION 8`; this tree is 9, and its `refexport_t` carries
Quake Live additions Quake3e has never seen:

| Export | Note |
|---|---|
| `Font_DrawString` | QL TrueType text |
| `TextBounds` | QL TrueType text |
| `GetGlyphInfo` | QL TrueType text |
| `SetCompositionFont` | QL TrueType text |
| `Get_Advertisements` | QL in-map advertising |

The font *core* is shared in `renderercommon` (`tr_fontstash.c`, `tr_stbtt.c`),
but the backend half — `renderergl2/tr_font_gl.c`, which uploads the glyph atlas
and draws through GL — has no Vulkan equivalent. That file is the single largest
piece of real work and should be read before anyone estimates the rest.

## Milestone 1 (the only one worth aiming at first)

Builds, links, and clears the screen, with the five exports above stubbed. That
answers the two questions that actually decide feasibility — whether this tree's
`refexport_t` can be satisfied, and whether `sdl_glimp.c` can be split so it
requests `SDL_WINDOW_VULKAN` and a surface instead of a GL context. Everything
after it is incremental.

## Known obstacles beyond the API

1. `sdl_glimp.c` requests a GL context unconditionally.
2. Quake3e expects some of its own engine-side helpers; each needs checking
   against this tree rather than assumed present.
3. `r_cubeMapping` and the rest of renderergl2's additions have no counterpart —
   a Vulkan build simply will not have them.
