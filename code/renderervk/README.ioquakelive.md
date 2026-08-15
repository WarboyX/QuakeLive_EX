# renderervk — vendored from Quake3e

Source: [ec-/Quake3e](https://github.com/ec-/Quake3e), `code/renderervk`, GPLv2 —
the same licence as the rest of this tree, so vendoring is fine with attribution.

## Why this is here

`cl_renderer` already dispatches by DLL name (`cl_main.c`, `CVAR_ARCHIVE |
CVAR_LATCH`, falling back to its reset string when the named library is missing)
and `USE_RENDERER_DLOPEN=1`, so a third renderer needs no engine change at all.
It needs to build as `vulkan<arch>.so` / `.dll` and export `GetRefAPI`.

## Status: RUNS. Draws the world. Cannot draw text.

`make BUILD_RENDERER_VULKAN=1` builds `vulkan<arch>.so` / `.dll` alongside
`opengl2`. It is off by default and no default-build rule touches the
directory, so it cannot affect an OpenGL build — verified: `opengl2x86_64.so`
is byte-identical with the target added.

Select it with `\cl_renderer vulkan` and `\vid_restart`.

Verified end to end on 2026-08-15 against Mesa lavapipe under Xvfb: instance,
physical device, swapchain (IMMEDIATE, 3 images, `B8G8R8A8_UNORM`), shaders,
60 frames of the main menu, clean shutdown, and a screenshot with the menu
background art in it. **No text** — see below.

If the window cannot be created (no Vulkan driver, or SDL built without Vulkan
support for the current video driver), it prints why, sets `cl_renderer` back
to `opengl2`, and exits. `cl_renderer` is `CVAR_ARCHIVE`, so without that reset
a machine with no Vulkan driver would fail to start on every subsequent launch.

### How the vendored source stays vendored

`tr_q3e_compat.h` collects the declarations Quake3e has and this tree does not,
and the Makefile force-includes it for `renderervk` only (`DO_REF_VK_CC`).
Nothing else in the tree sees it: `MAX_VIDEO_HANDLES`, `refShutdownCode_t`,
`MAX_UINT`, `CONTENTS_NODE`, the cvar flags and groups, `tokenType_t`,
`extern refimport_t ri`, and the two `VK_QL_Fill*` declarations.

`tr_q3e_compat.c` carries the `q_shared` functions Quake3e has and this tree
does not — `COM_ParseComplex` (its shader tokeniser, 215 lines), `crc32_buffer`,
`Com_Split`, `Com_GenerateHashValue`, `Q_stradd`, the `hash_locase` table —
ported verbatim from upstream so they can be re-diffed.

Two files here are ours rather than vendored:

- **`vk_window.c`** — SDL windowing. Quake3e's engine owns the window and hands
  the renderer `VKimp_Init`, `VKimp_Shutdown`, `VK_CreateSurface` and
  `VK_GetInstanceProcAddr`; this tree does the opposite, `sdl_glimp.c` is
  compiled *into* renderergl2. Rather than restructure `sdl_glimp.c` — shared
  code the OpenGL renderer depends on, which has to keep working throughout —
  this provides the same four entry points from inside the Vulkan module, and
  `GetRefAPI` points `ri` at them after copying the engine's import table. The
  vendored source is unchanged: it still calls `ri.VKimp_Init()`, it just
  reaches code in its own module. That also means `sdl_glimp.c` is not in this
  link at all, which is why the window cvars (`r_mode`, `r_fullscreen`,
  `r_custom*`) are registered here.
- **`vk_ql_exports.c`** — the five `refexport_t` entries Quake Live has and
  Quake3e has never heard of: four for the TrueType text system and one for
  in-map advertisements. Left null the engine calls through a null pointer the
  first time anything draws text, which is immediately.

### Patches to vendored files — keep this list short

| file | sites | why |
|---|---|---|
| `tr_backend.c` | 2 | Quake3e made `refEntity_t.shaderTime` a `floatint_t` union with an `intShaderTime` flag; this tree has a plain `float` in a struct shared with cgame. Collapsed to the float branch, which is what the integer path degrades to anyway. |
| `tr_init.c` | 1 | `Com_Error( errorParm_t )` vs `int`. |
| `tr_init.c` | 2 | `VK_QL_FillImports( &ri )` and `VK_QL_FillExports( &re )` in `GetRefAPI` — the whole of the interface bridge, two lines. |
| `tr_init.c` | 1 | `vk_release_resources()` guarded on `vk.device`. `vk_shutdown()` just below already guards itself against a Vulkan that never came up; `vk_release_resources()` does not, and it runs first. Upstream never reaches it in that state because Quake3e's engine does not tear the renderer down mid-init — this tree's `Com_Error` unwinds through `CL_Shutdown` → `RE_Shutdown`, so a failure inside `VKimp_Init` used to die on a null `qvkDeviceWaitIdle` instead of printing why. |

The `refEntity_t.shaderRGBA` / `drawVert_t.color` divergence was resolved by
**option A** for `drawVert_t` (the tree now has Quake3e's `color4ub_t` union,
verified layout-identical: size 44, align 4, colour at offset 40) and by
**option B** for `refEntity_t` (26 patched sites across `tr_shade.c`,
`tr_shade_calc.c` and `tr_surface.c`), because that struct is shared with cgame
and changing it is an ABI change across the game modules rather than a renderer
detail.

### Engine side

`refexport_t` gained 8 entries and `refimport_t` 20, both **appended** so no
existing offset moves and renderergl2 is unaffected. `CL_InitRef` now fills the
14 non-windowing imports — the six windowing ones are deliberately left null,
because the renderer fills those itself. Leaving all 20 null is what made the
first run segfault inside `R_Init`.

Four of the fourteen are honest no-ops rather than ports, each documented at
the definition in `cl_main.c`: cvar groups (Quake3e's automatic-`vid_restart`
mechanism, which this tree's OpenGL renderer does not have either), render
scaling (`r_fbo` + `r_renderScale`, off by default), clipboard bitmaps, and
JPEG screenshots — that last one prints a warning rather than writing an empty
file.

### What is left, in order

1. **Text.** `tr_font_gl.c` builds the glyph atlas as a `GL_R8` texture with a
   swizzle and re-emits glyph quads through `RE_StretchPic`. The second half is
   backend-agnostic; the atlas is not. A `tr_font_vk.c` doing the same upload
   into a vk image is the whole job. Until then the console and every menu are
   blank, and the renderer prints that once so it is not mistaken for a bug.
2. `r_dither` range. renderervk's own `r_dither` is 0–1; this tree's is 0–2
   (off / ordered / temporal), so selecting temporal logs *"cvar 'r_dither' out
   of range (max 1), setting to 1"*.
3. Windows build of the Vulkan target has not been exercised.


## Original status note: NOT WIRED UP

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
