# renderervk — vendored from Quake3e

Source: [ec-/Quake3e](https://github.com/ec-/Quake3e), `code/renderervk`, GPLv2 —
the same licence as the rest of this tree, so vendoring is fine with attribution.

## Why this is here

`cl_renderer` already dispatches by DLL name (`cl_main.c`, `CVAR_ARCHIVE |
CVAR_LATCH`, falling back to its reset string when the named library is missing)
and `USE_RENDERER_DLOPEN=1`, so a third renderer needs no engine change at all.
It needs to build as `vulkan<arch>.so` / `.dll` and export `GetRefAPI`.

## Status: BUILDS OPT-IN, does not link yet

`make BUILD_RENDERER_VULKAN=1` compiles `code/renderervk`. It is off by default
and no default-build rule touches the directory, so it cannot affect an OpenGL
build — verified: `opengl2x86_64.so` is byte-identical with the target added.

Progress: from twelve distinct classes of blocker down to the interface gap
this file predicted. Two objects compile; the rest stop on missing
`refimport_t` members.

### How the vendored source stays vendored

`tr_q3e_compat.h` collects the declarations Quake3e has and this tree does not,
and the Makefile force-includes it for `renderervk` only (`DO_REF_VK_CC`).
Nothing else in the tree sees it. So far: `color4ub_t` (a union of `rgba[4]`
and `u32` — declaring it as a plain `byte[4]` was rejected by every `.u32`
use, which is the right way to find that out), `MAX_VIDEO_HANDLES`,
`refShutdownCode_t`, `MAX_UINT`, `CONTENTS_NODE`, and `extern refimport_t ri`.

### Patches to vendored files — keep this list short

**`tr_backend.c`, 2 sites.** Quake3e made `refEntity_t.shaderTime` a
`floatint_t` union and added an `intShaderTime` flag to pick a half. This
tree's `refEntity_t` has a plain `float` and no flag, and that struct is shared
with cgame — changing it is an ABI change across the game modules, not a
renderer detail. Collapsed to the float branch, which is what the integer path
degrades to anyway.

### Where it stands

**27 of 28 translation units compile.** Everything that was self-contained is
done:

- the `q_shared` gap is closed — `tr_q3e_compat.c` carries `COM_ParseComplex`
  (Quake3e's shader tokeniser, 215 lines), `crc32_buffer`, `Com_Split`,
  `Com_GenerateHashValue`, `Q_stradd` and the `hash_locase` table, ported
  verbatim from upstream so they can be re-diffed;
- the macros, cvar flags, cvar groups and `tokenType_t` are in the compat
  header;
- `refexport_t` gained the 8 entries Quake3e's renderer fills in, and
  `refimport_t` the 20 it asks for, both **appended** so no existing offset
  moves and renderergl2 is bit-identical;
- `ri.Cvar_CheckRange` is redirected by macro to a second import with
  Quake3e's string-bounds signature, so both renderers keep the one they
  expect.

### The one thing left, and it is a decision, not a task

48 errors remain across 5 files, all from **two shared structs that Quake3e
changed and we did not**:

| | this tree | Quake3e |
|---|---|---|
| `refEntity_t` | `byte shaderRGBA[4]` | `color4ub_t shader` |
| `drawVert_t` | `byte color[4]` | `color4ub_t color` |

Both are shared with cgame, game and ui, so this is not a renderer detail.
Two ways out, and they are genuinely different bets:

**A — align the tree with Quake3e.** Change both structs. ~149 call sites
across 10 files, mechanical (`shaderRGBA` becomes `shader.rgba`), and the
compiler finds every one. Ends with no vendored patches at all and makes future
Quake3e updates cheap. Costs a wide diff through cgame and an ABI change across
the game modules — they are always rebuilt together here, so that is a build
concern rather than a compatibility one.

**B — patch the vendored files.** 48 sites in 5 files. Narrower today, but it
is 48 conflicts every time renderervk is updated from upstream, and it defeats
the reason the source was vendored unmodified.

A is the better bet if the Vulkan renderer is meant to live here long-term.
B is right only if this stays an experiment. **Not doing either silently** —
the `shaderTime` patch already in `tr_backend.c` was the smallest possible
version of this same divergence, and it was two lines; this is forty-eight.

### After that

The windowing restructure — the 4 `VK_*` and 6 `GLimp_*` imports are declared
but not implemented, so it will compile and fail to link until `sdl_glimp.c`
can hand out a `SDL_WINDOW_VULKAN` surface. That is still the real cost, and
it is unchanged by any of the above.

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
