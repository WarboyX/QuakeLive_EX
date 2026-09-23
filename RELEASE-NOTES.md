# Quake Live Ex — Alpha 2

The project has a name. Nothing on the wire changed with it: `com_gamename`,
`com_protocol` 91 and `baseqz` are untouched, so stock Quake Live clients still
see these servers and still join them. Only the window title and the version
stamps moved.

**Requires Quake Live.** `pak00.pk3` from a Steam install has to be present — it
is not redistributed here and never will be. Drop the archive contents alongside
it, or point `fs_basepath` at your Quake Live directory.

Both archives contain `baseq3/iobin.pk3` (cgame, qagame and ui for every
platform, so one pak serves a Linux server and its Windows clients under the
same `sv_pure` checksum), `baseq3/pak01.pk3`, ready-to-run server configs and
`server.cfg.example`.

## Lighting and ambient occlusion

A scene target that can hold light brighter than white, and the four bugs turning
it on uncovered.

**`r_rts` — real time shading.** The scene was drawn into an 8-bit target, so
anything above full brightness was discarded at the moment it was written and no
amount of adjustment afterwards could recover it. `r_rts 1` draws into a
floating-point target and applies a response curve at the end of the frame
instead: identity below 0.8, rolling off above it, so turning it on changes
nothing except where the image used to clip. `r_rts 2` uses an unsigned float
format with the same headroom. Off by default — it changes every pixel, and that
is a look decision.

**Dynamic lights no longer subtract light.** The per-pixel light shader computed
`dot(N, L)` and never clamped it, so a rocket or a plasma bolt *darkened* the
geometry it did not reach by as much as it lit what it did — a grenade under a
walkway lit the underside and put a black circle on the top. It had been
invisible for as long as the scene target was fixed-point, because Vulkan clamps
blend inputs to [0,1] for a fixed-point attachment and not at all for a floating-
point one.

**Ambient occlusion now runs before the dynamic lights.** Occlusion estimates how
much *ambient* light reaches a point and has no business attenuating direct
light, which it was doing — a light in a corner had its own contribution
multiplied by that corner's occlusion. `r_rtaoLights` additionally fades
occlusion inside a light, using the light's own falloff so the region cleared is
the region lit.

**Doors cast one shadow now, not two.** Brush models keep their surfaces in the
world's surface array, so every door was baked into the static acceleration
structure at the position it was compiled at *and* given a proxy that followed
it. The static structure is the world's own geometry only.

**Occlusion cost is printed at map load.** The trace is a full-resolution pass and
`r_rtaoSamples` is unrolled into the shader, so the cost is
`width x height x samples` ray queries in a single draw call — 133 million at 4K
with 16 rays, which is past what a driver watchdog permits and shows up as the
display freezing for seconds while sound keeps playing. The log now says the
number and warns past 32M, and the menu labels that setting by cost rather than
calling it "best".

## Sound, the console, and getting back in

**Audio runs at 44kHz.** The mixer was opening the device at 22050 and resampling
Quake Live's 44kHz assets down to meet it. The "not a 22kHz audio file" lines in
every log were the engine saying it was about to throw half of each sound away.

**The console has a backdrop again.** Its shader referenced two Quake 3 image
names that Quake Live's pak does not contain, so both stages were dropped, the
shader ended up with none, and the console drew its text straight over the game.

**Safe video mode says what it took.** Answering yes to the "did not exit
properly" dialog replaces three saved video settings and the originals are gone.
A crash loop left the game at 1024x768 in a window with nothing on screen or in
the log connecting it to a dialog answered before the window opened. It now
prints the previous values and the exact line to restore them.

**`\video` and `\stopvideo` work.** The AVI writer was complete and wired into
the frame loop, the mixer and every shutdown path — the two commands that start
and stop it were the only missing pieces, so none of it had ever run.

**Quake 3 maps load.** BSP version 46 is accepted alongside Quake Live's 47; the
only difference between them is a lump Quake 3 does not have.

## Spawning, which turned out to be the largest fault in the game

On a full server most deaths were not kills. Every spawn selector ended the same
way — when no point was free it fell back to `G_Find(NULL, ...)`, the first spawn
entity in the map, deterministically, every time. So every player who died went
to the same spot, telefragged whoever was standing there, and that player
respawned into it in turn. Self-sustaining, and it accounted for **93% of deaths
on thunderstruck** and **98% on citycrossings**: matches where almost nobody died
to a weapon.

Three things were wrong, and all three are fixed:

- **The fallback is no longer one fixed point.** Points are sorted into clear,
  recently-used, and occupied; when everything is occupied the least recently
  used one is taken, so the next player goes somewhere else and the chain cannot
  form.
- **Maps with few deathmatch spawns now use their team pads.** citycrossings has
  **two** `info_player_deathmatch` entities for sixty-four players, with every
  other pad under `team_CTF_redspawn` / `bluespawn`, and the selector only ever
  looked at the first classname.
- **Gametype filters were deleting spawn points at load.** In free-for-all
  anything marked `notfree`, or listing a gametype that is not ffa, is freed
  during entity spawning — which on a team map is every team pad. They are now
  kept in reserve and used when the map's own points run out.

Measured on trinity, same map before and after: telefrags **159 → 24** against
weapon deaths **492 → 1842**. On thunderstruck, **877 → 259** with weapon deaths
**67 → 825**.

Dead players also stopped occupying spawn points. `GibEntity` left the corpse
linked, and `trap_EntitiesInBox` only returns linked entities, so a body was
reserving a pad it was never going to stand on.

## Snapshots

At 64 players the server was discarding entities silently — 1,350,347 in a
single map. The overflow warning now prints the composition of the snapshot that
filled up, which answered it on the first run: two thirds events, a quarter
invisible gibbed corpses that the client receives and throws away, and **every
single dropped entity was an event**. Players and items were never being lost, so
the failure was cosmetic rather than structural.

Releasing gibbed bodies and fixing the spawn chain took the peak from 256
(overflowing) to 103, with zero drops.

`snapstats` in the console reports it at any time on any map: the fullest
snapshot since the map loaded, what it held by entity type, anything dropped, and
whether the tick rate or the client `rate` limit is what is actually delivering
fewer snapshots than `sv_fps`.

## Scoreboards, HUD and menus

- **The FFA scoreboard's columns line up.** Quake Live heads those boards with
  one space-padded text item spaced for its own font; the headings are now drawn
  at the list's own column positions and cannot drift.
- **"Fragged by" stays still.** Both of its anchors were content-dependent, so it
  moved whenever the text changed.
- **The match summary model has a body.** It was rendering as legs only.
- **The Accuracy field works.** It shared a case label with the follow-target
  name and drew that instead.
- **The end-of-game vote countdown is on screen.** It ignored its item's
  right-alignment and ran off the edge.
- **Map voting no longer crashes**, and neither does a map change, a full bot
  fill, or a client dropping mid-round.
- **A render menu that matches the renderer you are running.** Five of its
  controls were `renderergl2` cvars that Vulkan does not register — switches that
  did nothing — while `r_bloom` and anisotropy had no control at all. Vulkan now
  gets anisotropic filtering (to 16x), MSAA, texture filter, bloom and flares.
- **Credits**, with id Software, ioquake3, ioquakelive and this project.

## Rendering and console

- **Anisotropic filtering defaults to 16x**, and startup reports the level
  actually in use. It had been disabled outright by `classic.cfg`, which turned
  it off deliberately and which no other preset turned back on.
- **Console text scales with the display.** It was a fixed 8x16 *pixels*, correct
  for the 640x480 the charset was drawn for and eight pixels tall on a 1600p
  panel. `con_scale` is now a multiplier on a resolution-derived base rather than
  an absolute, so an existing config gets the fix without being edited.
- **Vulkan layers are listed at startup**, so ReShade, overlays and driver
  injections are visible rather than inferred. ReShade's Vulkan support attaches
  to this client with nothing needed from us; use `r_ext_multisample 0` if you
  want its depth-buffer effects.

## Settings that were only pretending to work

Quake Live's own options menu drives cvars this port registered and then never
read. The result is a setting that takes your value and changes nothing.

- **Smoke trail thickness works.** The menu's *Smoke Radius* controls set
  `cg_smokeRadius_RL` / `_GL` / `_NG`; the trail code read
  `cg_rocketTrailRadius` / `cg_grenadeTrailRadius` / `cg_nailTrailRadius` —
  names Quake Live's UI has never mentioned. The duplicates even had rocket and
  grenade swapped (64/32 against Quake Live's 32/64). Now read the real ones.

  **This one changes how the game looks by default**, unlike the rest of this
  section: rocket trails go from 64 to Quake Live's 32 and grenade trails from
  32 to 64. That is the point — those are Quake Live's numbers — but it is
  visible on first launch and worth knowing before you file it as a regression.
- **"Use item" messages can be turned off or made small.** `cg_useItemMessage`
  and `cg_useItemWarning` are each 0 / 1 large / 2 small, and both prints were
  unconditional and always large.
- **Crosshair brightness works.** `cg_crosshairBrightness` scales the crosshair
  colour — 0 / .3 / .6 / 1 in the menu. It is a brightness multiply and not
  transparency: at 0 you get a *black* crosshair rather than none, which is what
  the shader allows (it discards any alpha passed to it) and what Quake Live's
  own "No" label describes.
- **`fraglimit` was 20, not 50.** The engine registered it at 20 before the game
  module registered it at 50, and the first registration keeps the value — so
  every server shipped at 20 while the game module, the docs and Quake Live all
  said 50. It announced itself on every start (`cvar "fraglimit" given initial
  values: "20" and "50"`) and the line had been scrolling past for the life of
  the tree. The engine no longer registers gamerules it never reads.
- **A few smaller gates**: `cg_waterWarp` (the underwater wobble),
  `cg_crosshairPulse` (the pickup pulse), `cg_lowAmmoWarningSound`, and
  `cg_hitBeep 0` to silence hit beeps.

All of these default to what the code did before, so nothing changes until you
set one.

`docs/cvar-manifest.txt` now carries a verdict for every one of the 234 cvars
that are still registered and read by nothing, and the build fails if a new one
appears without a verdict. 75 of them turned out to be read by Quake Live's own
menus rather than dead at all.

## Natural textures in Quake Live's own maps

`baseq3/pak01.pk3` now carries `scripts/ql_enhanced.shader`, which adds
`qlNaturalTexture` to `textures/stone/rockcliff_01` and `_02` — Quake Live's own
cliff materials. No map is modified and no art is shipped; the shader overrides
Quake Live's definition and asks the renderer to break up the repeat.

Worth knowing what does and does not reach a stock map:

- **Natural textures: anywhere.** Hex-tiling is a texture-space operation and
  needs no map data.
- **Derived normal maps: under a dynamic light.** The normal map is derived from
  the diffuse at load, so a rocket or lightning beam lights it. The baked
  lightmap cannot.
- **Static bump mapping: not at all.** It reads a deluxemap, and only
  `q3map2 -deluxe` writes one. Quake Live's maps carry none.

## Natural textures — no more hard cuts

A texture tiled thirty times across a floor reads as a grid, not as a floor.
`r_qlNaturalTextures` breaks that up.

The request was for Minecraft's "natural textures", and that mechanism does not
port: OptiFine rotates a *block's* texture from the block's coordinates, which
works because the seam it creates lands on a block boundary you were going to see
anyway. A Quake Live wall is one polygon with continuous UVs and has no such
boundary. So this uses the continuous-surface form of the same idea — stochastic
hex-tiling (Heitz & Neyret, HPG 2018; Mikkelsen, JCGT 2022): a triangle lattice
over UV space, a random offset and rotation per hexagon, the three nearest
blended. Randomised per cell exactly as OptiFine does it, with no edge between
cells.

Measured on the test corridor's cobble floor, luminance autocorrelation at a lag
of exactly one texture repeat drops from **+0.88 to +0.035** — the repeat is gone
— and contrast is not lost doing it (stddev 26.2 → 30.7).

- `r_qlNaturalTextures 1` (default) applies it to shaders that ask, with the
  `qlNaturalTexture` stage keyword. `2` forces it on every world diffuse stage,
  which is for looking at a whole map at once: it costs three texture fetches per
  surface and it **destroys any texture with structure in it**, because three
  randomly offset copies of a brick wall are three brick walls. OptiFine ships a
  per-texture allow-list for this exact reason, and mode 1 is that allow-list.
- `r_qlNaturalRotate` (0–1, default 0.5) and `r_qlNaturalContrast` tune it.
  All three are latched — the pipelines are built once at map load — so they take
  effect on `vid_restart`, which the Render menu's APPLY button runs.
- The normal map is hex-tiled with the albedo, in derivative space rather than as
  a colour blend: averaging three unit normals gives a *flatter* surface, not the
  slope the three describe.

Separately, the noise underneath the test textures now wraps. It never had, so
every generated material carried one vertical and one horizontal discontinuity
regardless of how neatly its cells divided 256 — visible as a line break repeating
down a corridor wall. `tools/check-tiling.py` measures this rather than leaving it
to the eye, and the test-map build runs it.

## Bots

Stopped hunting their own team in instagib, stopped dancing around each other,
stopped touching their own flag before attacking in CTF, and no longer put a
whole team on defence because of a default voice order. `bot_minplayers` fills to
the real slot count, and a connecting player takes a bot's slot instead of being
refused.

## Diagnostics

Faults that used to be silent now say something: Windows crashes report what
address was touched and from where, the log can keep a timestamped file per run
(`logfile_keep`), the map's spawn point count is printed at load, the console
reports its own scale, and an out-of-range model frame names the model instead of
faulting.

**Test maps now ship with the build.** `baseq3/qltest_maps.pk3` is in both
archives — no moving anything, just:

- `/devmap qltest_stone` — the stone corridor. Parallax at grazing angles, and
  the floor is split at x = −128 into plain and hex-tiled cobble so the natural
  textures work can be judged against itself.
- `/devmap qltest_bump` — authored normal maps under static light (deluxemaps).
- `/devmap qltest_light` — derived normal maps under a dynamic light. Fire at the
  panels.

`qltest_maps.pk3` is the *pak* name, not a map name; the three maps above are
what `/devmap` takes. Everything in it is namespaced under `maps/qltest_*` and
`textures/qltest/`, so it collides with nothing in `pak00.pk3` and a normal game
neither loads nor checksums it.

## Known issues

**Normal map green-channel convention.** A normal map is baked against one of
two conventions and the wrong one turns bumps into dents. Our own generated maps
share one convention; a third-party map can ship either.

- Per material: **`qlNormalFlipG`** on the stage, which says "this material is
  baked the other way round". It XORs against the global, so a map that declares
  it stays correct whatever the global is set to.
- Globally: **`r_qlNormalFlipG`** (latched, needs `vid_restart`).

If bumps on a custom map read as dents, either will fix it; the keyword is the
one that does not break every other material at the same time.


`TRACKER.md` is the full list, each item tagged with which binary the fault lives
in and which client sees it. Worth knowing before you run a server:

- **The shotgun runs in stock-compatible mode on purpose.** The improved pattern
  and basis are written and tested but default to off, because they are correct
  on this client and wrong on stock Quake Live — the server sends a seed and each
  client regenerates the pattern locally. `g_shotgunBasis 1` /
  `g_shotgunPattern 1` if you are not serving stock clients.
- **Some maps cannot hold 64 players.** thunderstruck has five spawn points and
  no team pads to fall back on. The spawn work does everything it can; the rest
  is a player cap or a different map, and the console says which maps are in that
  position.
- **The client `rate` clamp, not `sv_fps`, is the delivery limit.** A 1308-byte
  snapshot lets a default-rate client sustain 18 a second against a tick of 40.
  Raising `sv_fps` would not help; raising the clamp would.
- **186 cvars are registered but read by nothing** (`tools/dead-cvars.py`).
  Setting one produces no error and no effect.
- Console text is still the Quake 3 bitmap charset, so scaling it magnifies it.
  Moving it onto the TrueType renderer already used for the HUD is scoped in
  `TRACKER.md` (R16).
- **Ambient occlusion is expensive at high settings.** The trace is a
  full-resolution pass and the sample count is unrolled into the shader, so cost
  is `width x height x samples`. At 4K, `r_rtaoSamples 16` is 133 million ray
  queries in one draw call and will trip a driver watchdog — the display freezes
  for seconds at a time while sound and input keep working. The default of 4 is
  the default for that reason; the log prints the number and warns.
- **`r_rts` is off by default and changes every pixel.** It is also the setting
  that has repeatedly exposed older faults rather than caused them, because a
  floating-point target removes clamping the fixed-point one applied silently.
  If something looks wrong only with `r_rts 1`, the first question is what the
  old target was quietly correcting.
- **Water reflections, `r_ssr`.** New, off by default. Quake Live's water is a
  scrolling texture; this gives it back what it reflects, by marching the depth
  buffer from the map's water planes. Screen space, so what is not on screen
  cannot be reflected — the edge fade is where that is hidden rather than
  pretended away. Tunables: `r_ssr` (strength), `r_ssrDistance`, `r_ssrSteps`,
  `r_ssrThickness`, and `r_ssrDebug` to see what the pass is doing.

  It spent six rounds painting the wall, the decking and the view weapon before
  the cause turned out to be in the **occlusion** pass, not this one: that pass
  declared the depth buffer as `STORE_OP_DONT_CARE`, and since it is the pass
  the rest of the frame is drawn in, depth became undefined the moment it ended
  — which is exactly when the reflection pass reads it. Harmless for years,
  because occlusion reads depth during that pass and nothing else ever read it
  at all. `r_rtao 0` was the only thing that ever fixed the reflection, and that
  is what located it.

- **Waves, `r_waterWaves`.** On by default wherever `r_ssr` is, and free when it
  is not. The surface stays geometrically flat — Quake Live's water is
  subdivided at map compile time and cannot be retessellated at runtime — but
  the normal travels in a wave pattern, so the reflection ripples, and the
  surface is displaced in Z as well, so the waterline against a pool wall rises
  and falls and the reflection stretches over a trough.

  `r_waterWaveSteepness` (default `0.08`) is the strongest control by a distance
  — it is the maximum slope, so `0.08` is about five degrees, and past `0.2` the
  reflection reads as noise. `r_waterWaveHeight` (`2`) is the Z variance in
  world units. `r_waterWaveScale` (`96`) is units per wavelength; a player is
  about 56 units wide. `r_waterWaveSpeed 0` freezes them, which is the easiest
  way to see the wave shape itself.

  What is still missing is refraction: what is *under* the water is drawn
  undistorted and has no depth-based colour, so the surface reflects properly
  but does not yet read as having anything behind it.

- **Surface detail under dynamic lights, `r_qlNormalMaps`.** New, on by default,
  and **not yet tested in play** — it compiles and its maths is verified, which
  is not the same thing. Quake Live's rock and brick read as flat planes with a
  photograph on them; this derives a normal map from each lit surface's own
  texture at load and perturbs the normal the dynamic light pass shades with, so
  a rocket flying past a wall lights the bumps that are painted on it. No new
  assets and no geometry change.

  **A still scene is unchanged.** The sunlight and shadow on those surfaces is a
  baked lightmap and this does not touch it; it is only what rockets, impacts,
  muzzle flashes and plasma do. `r_qlNormalScale` (default `0.5`) sets the
  strength — `1.0` means a luminance slope of 32 per texel tilts the normal 45
  degrees. Both cvars are latched, because the strength is baked into the image
  at load, so a change needs a `vid_restart` and the engine says so rather than
  appearing to take and doing nothing.

  Luminance-as-height is honest for rock, brick and gravel and wrong wherever a
  texture is light and dark for some other reason — signs, posters, lettering
  will grow bumps that track the artwork. A shader declines with `qlNoPerturb`,
  and `r_qlNormalMaps 0` plus `vid_restart` turns the whole thing off.

## Before cutting the release

`developer` still defaults to `1` in `common.c`, and that is a decision rather
than an oversight: **it stays on for the Alpha 2 release candidates and goes to
`0` when a candidate becomes the actual Alpha 2.** Every diagnostic in the tree
is gated on it, so a tester on a candidate build who hits something odd already
has the evidence in the console instead of being asked to reproduce it — which
is how a one-off report turns into no report. When the candidate is promoted it
is a one-line change, and that default is the only switch (`CVAR_TEMP`, so it
never sticks in a config and there is nothing for a tester to clear).

`cg_scoreboardDebug` and `ui_inputDebug` both default to `0` and are no longer a
release concern; the scaffolding behind them is inert until set.

The replacement scoreboard is switched on by one line in the shipped
`autoexec.cfg` (`set cg_ioScoreboard 1`). The cvar defaults to `0` in code and is
not archived, so **removing that line is the whole switch** - a release build
then shows Quake Live's own scoreboard, and no player's config holds a value that
would override it.

## Credits

id Software for Quake III Arena and Quake Live; the engine was released under the
GPL, the assets were not and are never redistributed here.
[ioquake3](https://github.com/ioquake/ioq3) for the engine, and
[tjone270/ioquakelive](https://github.com/tjone270/ioquakelive) for the Quake
Live work this is built on. `code/renderervk` is vendored from
[ec-/Quake3e](https://github.com/ec-/Quake3e) (GPLv2); Vulkan headers are
Khronos, Apache-2.0.

Quake Live and Quake III Arena are trademarks of id Software LLC, a ZeniMax Media
company. This project is not affiliated with or endorsed by them.
