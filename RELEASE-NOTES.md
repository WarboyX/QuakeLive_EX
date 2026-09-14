# Quake Live Ex — beta

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

## Known issues

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

  There are still no waves; `TRACKER.md` (R19) records why deforming the
  geometry cannot give them without retessellating the map.

## Before cutting the release

`developer` still defaults to `1` (`common.c`), and `cg_scoreboardDebug` /
`ui_inputDebug` scaffolding is still present. Both are wanted while testing and
neither belongs in a release build.

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
