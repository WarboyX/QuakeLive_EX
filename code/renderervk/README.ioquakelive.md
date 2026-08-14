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

## The gap, measured

Both interfaces were extracted from `renderercommon/tr_public.h` on each side and
diffed. These are counts, not estimates.

### What the renderer must export to us — 5 additions

`Font_DrawString` · `TextBounds` · `GetGlyphInfo` · `SetCompositionFont` ·
`Get_Advertisements`

Small. Quake3e's `GetRefAPI` fills its own `refexport_t`; ours needs these five
appended and the version bumped 8 → 9.

### What we must provide the renderer — 24 additions (the real work)

Quake3e's `refimport_t` has 58 entries against our 41, and 24 of theirs have no
counterpart here. They fall into three groups:

**Vulkan windowing — does not exist in this tree at all**
`VK_CreateSurface` · `VK_GetInstanceProcAddr` · `VKimp_Init` · `VKimp_Shutdown`

**Windowing moved into refimport.** Quake3e drives the window from the *engine*
side; this tree still does it inside the renderer, which is why none of these
exist here:
`GLimp_Init` · `GLimp_Shutdown` · `GLimp_EndFrame` · `GLimp_InitGamma` ·
`GLimp_SetGamma` · `GL_GetProcAddress`

**Straightforward wrappers over things we already have**
`Cvar_VariableString` · `Cvar_VariableStringBuffer` · `Cvar_SetGroup` ·
`Cvar_CheckGroup` · `Cvar_ResetGroup` · `Com_RealTime` · `Microseconds` ·
`FreeAll` · `CL_IsMinimized` · `CL_SetScaling` · `CL_SaveJPG` ·
`CL_SaveJPGToBuffer` · `CL_LoadJPG` · `Sys_SetClipboardBitmap`

### What this means for the estimate

The earlier framing — "stub five exports and link it" — was wrong, and it was
wrong because I had not measured. The export side is indeed five stubs. The
**import** side is 24 functions, and four of them are an entire Vulkan
windowing layer this tree has never had.

The 14 wrappers are an afternoon. The 6 windowing entries mean restructuring
`sdl_glimp.c` so the engine owns the window and the renderer asks for it, which
is an architectural change to shared code that the OpenGL renderer also uses —
so it has to keep working throughout. That is the pole in the tent, not fonts.

## Old scoping (kept — the gap to close)

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

## Text is not optional

Stubbing the five font exports is valid only long enough to prove the link. The
console is this renderer's debugging surface: a Vulkan build that cannot draw
text cannot report its own errors, and every diagnostic in the tree
(`cg_debugShotgun`, cvar readouts, the console itself) goes with it. Fonts are
part of the first *usable* build, not polish afterwards.

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
