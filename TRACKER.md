# ioquakelive — issue and feature tracker

Working notes for the port. Open items first, resolved at the bottom so we do not
re-litigate settled ground.

Status key: **OPEN** · **IN PROGRESS** · **NEEDS INFO** · **BLOCKED** · **DONE**

---

## Weapons / gameplay

### W1. Shotgun pattern shape is unverified — OPEN
The ring pattern transcribed from the binary has a **hollow centre**: its centroid
is exactly on the aim axis, but the nearest pellet is 1.75° off it and the outer
ring sits at 5.23°. On a wall the marks form a donut with nothing where you aimed.

| range | inner ring | middle ring | outer ring |
|---|---|---|---|
| 500 units | 15 u | 31 u | 46 u |
| 1000 units | 31 u | 61 u | 92 u |
| 2000 units | 61 u | 122 u | 183 u |

Average spread matches Q3 closely (mean radius 8400 vs 8570 units), so the *width*
is plausible; the hole is what is in question. `g_shotgunPattern 1` switches to
Q3's filled cone for comparison — if that suddenly feels right, the ring radii in
the transcription are wrong.

**Next step:** play-test `g_shotgunPattern 0` vs `1` and `g_shotgunSpread 1` vs
`0.7`. Note that a vanilla client will not follow either.

### W2. Middle ring angle: 30 radians or 30 degrees? — OPEN
`g_weapon.c` / `bg_misc.c`, middle ring: `angle = i * 1.0471976f + 30.0f`. The
literal is added as **radians** (≈278.87° once reduced). 30 *degrees* (π/6) would
interleave the middle ring neatly between the inner ring's points, which is what a
designed pattern would do. Affects pellet arrangement only — not cone width and
not hit registration, since both sides compute it identically.

**Next step:** re-check the disassembly at `qagamex86.dll 0x1006d450` for whether
the constant is `30.0` or `0.5235988`.

### W3. Improved shotgun basis is locked behind custom clients — OPEN
`g_shotgunBasis 1` lays the pellets out in the player's own right/up, so the
pattern stops rotating with facing (pellet 0 measured at −60°/−150°/+120° on
screen depending on yaw with the stock frame; a constant −120° with the new one).
It defaults to **0** (stock `PerpendicularVector`) because a vanilla client cannot
be told to follow it and would draw marks nowhere near the damage.

**Decision needed:** is this server meant to serve vanilla clients permanently, or
are custom clients the target? That answer also gates `g_shotgunPattern 1`.

### W4. HMG spread is a fixed constant — OPEN
`g_weapon.c`: the binary computes Heavy Machine Gun spread per shot in `FireWeapon`
(case 0xe) from a per-client field, spin-up/heat style. The formula has not been
recovered; `HMG_SPREAD 350` stands in for it.

---

## Client / UI

The port ships **one** menu file — `content/pak01/ui/main.menu`, containing `main`,
`createserver`, and (as of the server-browser work) `joinserver` and `playersetup`.
Everything else the player sees comes from their Quake Live `pak00.pk3`, parsed by
this port's re-implemented `ui` module. Most UI bugs are therefore our module
mishandling Quake Live's menu scripts, not missing menus — but a few are both.

### U1. Texture filter shows "none" and will not change — NEEDS INFO
`r_textureMode` and its apply path (`GL_TextureMode` on `->modified`, `tr_cmds.c`)
are intact, so this is the menu item. Cannot be diagnosed without seeing the item.

**Next step:** need the relevant `ui/*.menu` out of `pak00.pk3`. With the `???`
change (see U6) the value now painted will name the mismatching cvar directly.

### U2. No player-name prompt while in a match — PARTIAL
A `playersetup` menu now exists and is reachable anywhere via `\menu_open
playersetup`, but nothing in Quake Live's in-game menu opens it — that menu is
theirs and we do not ship a replacement.

**Next step:** either ship an `ingame_options.menu` override (needs QL's original
as a base) or accept the console command.

### U3. `ui/menudef.h` is not packaged — OPEN
`ui/menudef.h` exists at the repo root but is not in `pak01.pk3`, so our
`main.menu` parses against Quake Live's copy. Fine while the two agree — and ours
appears to be QL-derived — but nothing verifies that. Shipping ours would make the
build self-contained *and* would override the header QL's own menus parse against,
so any divergence would break every QL menu at once. Left alone deliberately.

**Next step:** diff our `ui/menudef.h` against the copy in `pak00.pk3` before
deciding.

### U4. `ui/ingame.txt` is orphaned — OPEN
`ui/ingame.txt` at the repo root lists nine `ui/ingame_*.menu` files. None exist in
the repo and the file is not packaged. Either write those menus or delete the file.

### U5. Ambient Light Scale — DONE (verify)
`r_ambientScale` was `CVAR_CHEAT`: the menu could not write it while connected and
the engine reset it to 0.6 on every connect. Now `CVAR_ARCHIVE`. Reported as
"blank", which this may or may not explain — needs a re-test.

### U6. `???` on Advanced settings — DONE (verify)
`Item_Combo_FindCvarByValue` painted `???` whenever a cvar's value was not one of
the item's presets, including when the cvar's own default is absent from the list.
It now shows the raw value; `???` is kept only for cvar-does-not-exist, so the two
faults are distinguishable from a screenshot.

### U7. Control mapping — DONE (verify)
`g_bindings` was a fixed table of Q3's commands; `BindingIDFromName` returned −1 for
anything else, so any command QL's control menus bind that Q3 did not have could
not be bound and painted `???`. The table now grows on demand. Capacity 256.

### U8. Server browser — DONE (verify)
All the machinery was already implemented (`UI_BuildServerDisplayList`,
`UI_StartServerRefresh`, `FEEDER_SERVERS`, sorting, favourites, `UI_NETSOURCE`,
`UI_NETFILTER`). Only the menu was missing.

---

## Renderer

### R1. Only renderergl2 ships — OPEN
`code/renderergl1`, the original Quake 3 renderer, was stripped from this repo.
Only `renderergl2` is built, so there is no way to get Quake 3's actual render
path — only an approximation of it.

The switching machinery is entirely intact: `cl_renderer` exists in `cl_main.c`
(default `"opengl2"`, `CVAR_ARCHIVE | CVAR_LATCH`) and falls back to its reset
string if the named DLL is missing, and `USE_RENDERER_DLOPEN=1`. Dropping
`renderergl1` back in and building `opengl1<arch>.so/.dll` would make
`cl_renderer opengl1` work with no engine changes.

**Cost:** ~15 files from ioquake3 upstream, plus adapting to this port's
`REF_API_VERSION 9` `refexport_t`, which added Quake Live's TrueType text path
(`Font_DrawString`, `TextBounds`, `GetGlyphInfo`, `SetCompositionFont`). The font
core is shared in `renderercommon` (`tr_fontstash.c`, `tr_stbtt.c`), but
`renderergl2/tr_font_gl.c` has no GL1 equivalent — that needs writing against
fixed-function GL.

**Next step:** judge `classic.cfg` first (R2). If it gets close enough, this may
not be worth doing.

### R2. Classic look presets — DONE (verify)
`classic.cfg`, `voodoo.cfg`, `modern.cfg` in `pak01.pk3`. `classic.cfg` disables
what renderergl2 does that Quake 3 did not — `r_hdr`, `r_toneMap`,
`r_postProcess`, normal/specular/deluxe/cube mapping — and restores Quake 3's
texture handling. `r_toneMap` is the important one: environment mapping
(`TCGEN_ENVIRONMENT_MAPPED`) is implemented and working, but tonemapping
compresses exactly the highlights that read as metallic.

### R4. Getting the metallic look back — routes, in cost order — OPEN
Three ways to put gloss on surfaces. A screen-space post-process is **not** one
of them: once the frame is a 2D image the per-surface normals are gone, so a
filter cannot know which pixels are metal or which way they face. Screen space
can do the *Glide* half — dither, soften, bloom — because that genuinely is a
screen-space effect. It cannot do the reflective half.

1. **`r_cubeMapping 1` + `r_specularMapping 1`** — already in the renderer, off
   by default, needs GL 3.0. Falls back to `info_player_deathmatch` spawns as
   probes when a map has no `env.json` and no `misc_cubemap` entities, so it
   works anywhere with no authoring. Shipped as `gloss.cfg`. This is *real*
   reflection and will look glossier than Quake 3 ever did — Quake 3's effect
   was a fixed smear, not a probe.
2. **Shader overrides** — a pk3 of `scripts/*.shader` adding a `tcGen
   environment` stage to the surfaces that should be metal. This is exactly how
   Quake 3 did it: data, not code, per-surface correct, and renderer-agnostic
   (it would survive a move to Vulkan). Best fidelity per unit effort. Needs the
   Quake Live texture names to target.
3. **Screen-space reflections** — needs a normal/depth G-buffer that renderergl2
   does not currently write. Real work, and still approximate at surface edges.

### R5. Vulkan renderer — OPEN
Feasible: Quake3e (`ec-/Quake3e`) has a mature `renderer_vulkan`, and
`cl_renderer` already dispatches by DLL name so a third target needs no engine
change. Two things to be clear about before anyone starts:

- **An API is not an aesthetic.** Quake3e's Vulkan renderer is a
  reimplementation of the *classic* Q3 renderer. It would get the look closer to
  Quake 3 than renderergl2 does — but by being GL1-equivalent, not by being
  Vulkan. It adds no gloss on its own.
- It would need the same `REF_API_VERSION 9` adaptation as R1, including a
  Vulkan backend for the TrueType text path.

Worth doing for performance and driver-stability reasons. Not a route to the
metallic look.

### R6. Voodoo postfilter as a real post-process pass — OPEN, ready to build
`voodoo.cfg` gets the 16-bit dither but not the thing that made it look good:
Voodoo3 and later ran a filter over the dithered output on scanout — the "22-bit"
mode — blending adjacent pixels so the dither pattern read as a smooth gradient
rather than as noise. That is the missing half, and it is squarely reproducible:
unlike reflection, dither-smoothing needs nothing but neighbouring pixels, which
is exactly what a screen-space pass has.

**Plan** (all mechanical, insertion point verified):
- `glsl/voodoo_vp.glsl` + `glsl/voodoo_fp.glsl` — the 3dfx filter kernel.
- Two entries in the Makefile's `renderergl2/glsl` object list; GLSL is
  stringified into `.o` files at build time, not loaded from the pk3.
- A program slot in `GLSL_InitGPUShaders` (`tr_glsl.c`).
- `RB_VoodooFilter()` in `tr_postprocess.c`, called from `RB_PostProcess`
  (`tr_backend.c`) just before the final `FBO_FastBlit` to `dstFbo`.
- Cvar `r_voodooFilter`, folded into `voodoo.cfg`.

Note this is unrelated to API lineage. OpenGL (1992, out of SGI's IRIS GL)
predates Glide (1996), and Glide borrowed conventions from OpenGL rather than
the reverse; Vulkan descends from AMD's Mantle and was a deliberate clean break
from OpenGL's state machine. Glide is a dead branch — nothing of it survives in
Vulkan to be recovered. What is recoverable is the hardware's *output stage*
behaviour, which is what this item is.

### R7. Output dither is one line, and classic.cfg switches it off — OPEN
The entire renderer contains exactly one dither, in `glsl/tonemap_fp.glsl`:

```glsl
// add a bit of dither to reduce banding
color.rgb += vec3(1.0/510.0 * mod(gl_FragCoord.x + gl_FragCoord.y, 2.0) - 1.0/1020.0);
```

That is a 2-phase checkerboard of about half an 8-bit step. Two problems.

1. **It is gated behind tonemapping.** `RB_PostProcess` only runs the tonemap
   pass when `r_hdr && (r_toneMap || r_forceToneMap)`, so `classic.cfg` — which
   sets both `r_hdr 0` and `r_toneMap 0` — removes the only dither in the
   pipeline. Skies and coloured lighting will band *worse* under the classic
   preset than under the defaults. Introduced by R2; needs fixing here rather
   than in the cfg, since the dither belongs at output, not inside tonemapping.
2. **A 2-phase checkerboard is the weakest useful pattern.** An 8x8 Bayer matrix
   or blue noise costs the same per pixel and bands visibly less.

**This is the part of the "22-bit" idea that still pays at 32-bit.** The Voodoo
postfilter recovered information the 16-bit dither had encoded spatially; at
8-bit-per-channel output there is no dither and nothing hidden, so blending
neighbours would only blur. But the renderer computes in float and quantises at
the very end, so the precision genuinely exists right up to that step — dithering
*that* conversion is the same principle applied where it still has something to
work with.

**Plan:** move the dither out of `tonemap_fp.glsl` into the final output blit so
it applies on every path, upgrade it to an 8x8 Bayer or blue-noise pattern, and
gate it on `r_dither` (default on). Shares its insertion point with R6.

**Also worth having:** `r_colorbits 30` for a 10-bit `GL_RGB10_A2` framebuffer,
where the display and driver support it. That is real extra precision rather than
simulated, and would make the dither question mostly moot on that hardware.

### R3. Quake Live art is not Quake 3 art — OPEN
The env-mapped metal shaders live in Quake 3's `pak0.pk3`. Quake Live retextured
everything, so no renderer setting recovers surfaces whose shaders are not
loaded. Note the load order: `paksort` is `FS_PathCmp` ascending and later paks
take precedence, so a Quake 3 `pak0.pk3` dropped into `baseq3` alongside Quake
Live's `pak00.pk3` is **shadowed by it** — it has to sort last (e.g.
`zz_q3pak0.pk3`) for its shaders to win.

**Next step:** test with Quake 3's paks to separate "renderer is wrong" from
"art is different". This decides whether R1 is worth the effort.

---

## Engine / server

### E1. Factory subsystem absent — OPEN
Quake Live's "factory" layer (named rule presets loaded from `scripts/*.factories`,
selecting gametype plus a bundle of cvars) has no implementation. Servers configure
raw cvars instead.

### E2. Teammate weapon icons never draw — OPEN
`clientInfo_t::curWeapon` is never written because the server never sends the
`tinfo` command. The HUD field exists and is read; nothing populates it.

### E3. No master server heartbeat / Valve server query — OPEN
The server does not announce itself, so it cannot appear in any public list. The
client's browser can still reach it by direct connect or LAN.

### E4. ZMQ stats feed absent — OPEN
Quake Live publishes match events over ZeroMQ (`zmq_stats_enable` and friends).
Nothing here implements it. Wanted by most server-stats tooling.

### E5. Steam integration absent — OPEN
Auth, and `FS_CopyFromSteam` is defined but unused (it is one of the two remaining
compiler warnings in engine code).

---

## Resolved

| # | Item |
|---|---|
| R1 | LP64 session-data corruption (`%ld` vs `int` in `g_session.c`) |
| R2 | Out-of-bounds writes from network-supplied weapon indices |
| R3 | Server ban system: `banlist`/`ban`/`unban`/`rehashbans`, in-game `/ban` |
| R4 | Access-level lookup used a broken stub at connect time |
| R5 | Console command auto-completion |
| R6 | Format-string vulnerability in error paths |
| R7 | All 25 project-code compiler warnings cleared (90 → 0) |
| R8 | Hunk exhaustion at high player counts — 88.5 MB of 128 MB at 48 players; `DEF_COMHUNKMEGS` 128 → 256 with a dedicated-server floor derived from `sv_maxclients` |
| R9 | Silent snapshot-entity discards — now counted and warned |
| R10 | Reliable-command buffer overflow dropped clients with no warning |
| R11 | `sv_altEntDir` entity override |
| R12 | Team lock (`G_IsTeamLocked`) |
| R13 | `CG_SpawnParticleEffect` (impact sparks) |
| R14 | Railgun tracer and impacts invisible — shaders never registered |
| R15 | Shotgun server/client pattern divergence — the server ignored the transmitted seed |
| R16 | Shotgun pattern generation duplicated in two files; now shared in `bg_misc.c` |
| R17 | Shotgun smoke puffs spawned at the map origin — the "32 units in front of the muzzle" step had been dropped |
| R18 | Server defaults now trace bit-identically to what an unmodified cgame draws (905760/905760 components, max diff 0) |

---

## Diagnostics available

| Command | Scope | What it reports |
|---|---|---|
| `cg_debugShotgun 1` | client, in-map | Per blast: fire direction vs view axis, muzzle vs camera, resolved pattern parameters, pellet spread off the crosshair |
| `menu_open <name>` | client | Opens any loaded menu by name |
| `menu_close <name>` | client | Closes it |
| `ui_report` | client | Dumps menu/item state |
