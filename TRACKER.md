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

### U14. Console is unreadable at high resolution — DONE (verify)
At 3840x2160 the console font is tiny and the text cramped, which makes the
console painful to use for exactly the diagnostic work it is needed for. Q3 draws
console text with `SCR_DrawSmallChar` at fixed 640x480 virtual metrics, so it
does not scale with resolution.

`con_scale` existed, defaulted to **0.5**, and was clamped to `[0.5, 1.0]` —
half-size glyphs with no way to make them bigger. Default is now **1.0**, range
opened to `[0.25, 4]`.

**Not a resolution problem.** Console text goes through `re.DrawStretchPic` and
the renderer's 2D pass runs in a 640x480 ortho projection, so glyphs already
scale with the display: 1.0 looks the same at 720p and 2160p. I first "fixed"
this with a `vidHeight / 480` auto factor, which gives 4.0 at 2160p and made the
text four times too large. Confirmed by testing: `con_scale 1` is right at
3840x2160, which is exactly what resolution independence predicts. This is a
taste setting, not a per-display one — do not reintroduce a resolution-derived
scale here.
Also visible in the same capture and worth chasing separately:
- `WARNING: CM_SrfXPlane unreachable` repeated dozens of times on load
- `WARNING: Failed to load sound mus_high_score.ogg` — falls back, harmless but noisy
- red `ERROR:` lines about unknown mesa keywords in `scripts/*.menu` parsing

The port ships **one** menu file — `content/pak01/ui/main.menu`, containing `main`,
`createserver`, and (as of the server-browser work) `joinserver` and `playersetup`.
Everything else the player sees comes from their Quake Live `pak00.pk3`, parsed by
this port's re-implemented `ui` module. Most UI bugs are therefore our module
mishandling Quake Live's menu scripts, not missing menus — but a few are both.

### U1. Advanced settings: blanks, stuck values and `???` — DONE (verify)
Three separate faults, all found from the Advanced screenshots.

**Blank rows and stuck values** — `ItemParse_cvarStrList` set `strDef = qfalse`.
That sent every `cvarStrList` item down the *numeric* branch of
`Item_Multi_Setting`, comparing against a `cvarValue[]` array this parser never
populates. Every entry therefore compared equal to 0, so:
- a cvar whose numeric value is 0 (or which is a non-numeric string, `atof` → 0)
  always matched entry 0 and displayed the **first** item in the list —
  "Texture Filter: None" was `r_textureMode` reading `GL_LINEAR_MIPMAP_LINEAR`
  and landing on entry 0, which is why it would not change;
- any other value matched nothing and displayed **blank** — Ambient Light Scale,
  Crosshair Style, Gun Position, Damage Num Style, Sparks Velocity.

**`???` on preset rows** — two causes. `Item_Combo_FindCvarByValue` returned
`cvarStr[i]`, the name of the linked `ITEM_TYPE_PRESET` item, where it should
return `cvarList[i]`, the preset name that `Item_Combo_HandleKey` actually stores
in the cvar. And `Menu_CheckPresetCvars` could only ever *demote* a named preset
to "Custom": if the cvar named no known preset — the state a config that has
never been through the menu starts in — nothing matched, nothing was written,
and the row painted `???` forever. It now resolves an unnamed or drifted cvar by
scanning for the preset that matches the current settings, falling back to
"Custom". It also no longer `return`s on the first mismatch, which was
abandoning every remaining preset row in the menu.

Affected rows in the screenshots: Impact Sparks, Teammate Indicators, Low Ammo
Warning, Draw Rewards, Force Team/Enemy Model and Skin, Damage Indicator, Impact
Marks, Lighting Model.

### U9. Server browser painted over createserver — OPEN, both diagnoses were wrong
**Correction, from the actual menu files.** I claimed Quake Live's paks define
`createserver`, `joinserver` and `playersetup`, so ours collided with theirs.
**They do not.** Grepping every file QL loads: no menu anywhere in its `ui/` is
named any of those. Its `menus.txt` has `joinserver.menu`, `createserver.menu`
and `player.menu` **commented out** — the only menus it loads are `default`,
`main`, `main_options`, `connect`, `quit_popmenu`, `demo`, `navframeBL`, `error`,
plus the `ingame_*` set. There was never a collision.

The `io_` prefix is harmless and worth keeping as insurance, but it was not a
fix, and it changed the console commands for no reason. Both menus in the
overlap were **ours**, both `fullScreen`, both visible at once.

**Untested since:** the overlap has not been re-checked since the prefix landed.
Confirm it still happens before digging further.

**Worth copying from QL's own main menu:** it never closes `main` to show a
sub-menu. It does `hide mainnav ; open main_options`, leaving `main` open as the
backdrop with a `fullScreen 0` overlay on top. Ours does `close main ; open
io_createserver` with both `fullScreen MENU_TRUE`. Matching QL's pattern — hide
the nav group, make the sub-menus non-fullscreen overlays — would make the whole
class of overlap impossible rather than relying on paired open/close.

#### Earlier, also wrong Both being `fullScreen`, and Q3 painting every visible menu rather than
only the topmost, they drew on top of each other: doubled headers, overlapping
labels, two sets of BACK buttons, the map preview sitting where the server list
should be. One bug, many symptoms.

The menus this build defines now carry an `io_` prefix so they cannot collide.
`main` is deliberately left unprefixed: this file replaces `ui/main.menu` in the
search path, so Quake Live's own `main` never loads.

**Console commands changed:** `menu_open io_playersetup`,
`menu_open io_renderoptions`.

The first attempt — closing the sibling from the menu script — was treating a
symptom and could not have worked, since the `close` was landing on a different
menu than the `open`.

#### First attempt (did not work)
Both are `fullScreen`, and Q3's menu system paints every visible menu rather than
only the topmost, so one left open behind the other showed through it — doubled
headers, overlapping labels, two sets of BACK buttons. The navigation now closes
the sibling before opening, and `main`'s `onOpen` closes both, so returning to
main always clears whatever was underneath. My bug, introduced with U8.

### U10. Controls menu is empty — NEEDS INFO
Not `???` rows: **no rows at all**. The tabs draw, so the menu loaded and it is
the item list inside that is missing. That is a different fault from U7 (commands
that could not be bound), so U7 is not the explanation here.

**Menu file now read.** No feeder and no exotic owner draw — the bind rows are
plain `ITEM_TYPE_BIND` items (`+forward`, `+back`, …) each carrying
`group grpControls` and **`visible 0`**. They are revealed by the menu's own
script:

```
onOpen { hide grpControls ; show move ; show header1 ; uiScript loadControls ... }
```

so the panel starts empty by design and `show move` is what fills it. The
show/hide machinery checks out — `Menu_ItemsMatchingGroup` matches on name *or*
group, and `Menu_ShowItemByName` sets `WINDOW_VISIBLE` correctly — so the fault is
further along.

**Ruled out by inspection — do not re-check these:**
- *The route.* QL's own `main.menu` uses the identical action:
  `hide mainnav ; open main_options ; open ingame_controls`. Ours is faithful.
- *`;` handling.* `Item_RunScript` skips a bare `;` and resets `bRan` per
  statement, so the chained `onOpen` runs to completion.
- *Name-vs-group matching.* 18 items are named `move`; `Menu_ItemsMatchingGroup`
  and `Menu_GetMatchingItemByNumber` both match name **or** group, identically.
- *`Menus_Activate`.* Builds the stack `itemDef_t` with `parent` set before
  running `onOpen`, matching Q3.

**Remaining suspects, in order:**
1. `uiScript loadControls` erroring or long-jumping before the `setitemcolor`
   run, though the shows precede it so this would not explain an empty panel.
2. `ingame_controls` not being *loaded* at the moment it is opened —
   `ui/ingame.txt` is loaded separately from `ui/menus.txt`, so opening it from
   the main menu may target a menu that does not exist yet. `Menus_ActivateByName`
   on a missing name fails silently.
3. The six `grpControlbutton` tab buttons that switch groups.

**Next step:** print the menu list at the moment OPTIONS is pressed and confirm
`ingame_controls` is present. Suspect 2 is cheap to test and fits the evidence:
the tab bar drew because that is `main_options`, and the panel below was empty
because the menu meant to fill it was never loaded.

### U11. Cosmetic layout faults — OPEN
Visible in the Advanced screenshots, none investigated yet:
- "Default" painting over the "Game Settings" title on the Weapons page — looks
  like a preset name drawn at the title position.
- "Zoom Sens" slider labels read `0.01 | 1 | 1`; the maximum label appears wrong.

### U12. Render options have no home in Quake Live's menus — DONE (verify)
`r_dither` and the rest of the renderer work has nowhere to live: the Video
Options and Post Process tabs are in Quake Live's `pak00.pk3`, which this build
neither ships nor can edit. A self-contained `renderoptions` menu carries them
instead — reachable from the main menu's RENDER entry or `\menu_open
renderoptions` anywhere, including mid-match.

Covers: `r_dither` (off / ordered / temporal), `r_colorbits` (driver default /
16-bit / 10-bit), `r_hdr`, `r_toneMap`, `r_postProcess`, `r_cubeMapping`,
`r_cubemapSize`, `r_specularMapping`, `r_normalMapping`, `r_ambientScale`, plus
one-click buttons for the classic / gloss / voodoo / defaults presets and an
APPLY that runs `vid_restart` for the latched ones.

Every value row needs `text ""` even when the label is a separate decoration
item: `Item_Multi_Paint`, `Item_YesNo_Paint` and `Item_Slider_Paint` all paint the
value at `item->textRect`, and `textRect` is only computed inside
`Item_Text_Paint`, which is skipped when `item->text` is NULL. Omitting it draws
every value at 0,0 — off the panel entirely, which is what the first cut of this
menu did. The `createserver` rows had it right; these did not.

**Open:** the proper home is still Quake Live's own tabs. If its
`ui/*.menu` files turn up (U1, U10 need them too) these rows should move there.

### U2. No player-name prompt while in a match — PARTIAL
A `playersetup` menu now exists and is reachable anywhere via `\menu_open
playersetup`, but nothing in Quake Live's in-game menu opens it — that menu is
theirs and we do not ship a replacement.

**Next step:** either ship an `ingame_options.menu` override (needs QL's original
as a base) or accept the console command.

### U3. `ui/menudef.h` — DONE
Diffed against Quake Live's copy: **identical apart from trailing whitespace on
two lines.** The concern that shipping ours could break every QL menu at once was
unfounded, so it is now in `pak01.pk3` and the build no longer depends on finding
their header. Nothing else changes, since the two agree.

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

### C4. Console commands sent as chat in-game — DONE (verify), with a caveat
`cl_keys.c` only prepends the implicit `\\` when `clc.state != CA_ACTIVE`. Once
connected, any console line not starting with `\\` or `/` falls through to
`if (con_autochat->integer) Cbuf_AddText("cmd say ")` and is chatted instead of
run. That is stock ioquake3 behaviour with `con_autochat` defaulting to 1, not
something this port broke — but it is a bad default for a build used to
administer servers. Default is now 0.

**Caveat, same trap as R8:** `con_autochat` is `CVAR_ARCHIVE`, so a config that
already holds a value keeps it and the new default does nothing. Existing
installs need `/con_autochat 0` once. Prefixing with `/` or `\\` always works
regardless.

**Menu row resolved.** From `ingame_options_advanced.menu:1757`, the row is
`ITEM_TYPE_YESNO` bound to **`cl_allowConsoleChat`** — Quake Live's name for the
setting. This build only ever read ioquake3's `con_autochat`, so the toggle wrote
a cvar nothing consulted. `cl_allowConsoleChat` is now registered (default 0) and
both must agree before a console line is chatted, so either turns it off and the
menu row works.

The row also carries `cvarTest "com_allowConsole"` with `disableCvar { "0" }`, so
it greys out when the console itself is disabled.

### C3. Weapon viewmodel sits bigger and lower — OPEN, cg_fov ruled out
`cg_fov` reads 100, the default, and was set to 100 and other values deliberately
without fixing the framing. **My earlier read of the console capture was wrong** —
38.400208 was a transient from an experiment, not the resting value. Fov is not
the cause.

Remaining candidates, none checked:
- `cg_gunX` / `cg_gunY` / `cg_gunZ` — viewmodel offsets, all default 0.
- `cg_drawGun` — 1 normal, 2 and 3 select different positions.
- `CG_AddPlayerWeapon`'s placement, and the `MatrixMultiply` that was moved
  before the `VectorMA` chain during the earlier weapon work. That change is in
  the same function that positions the viewmodel and is the most likely thing
  this port altered.
- `CG_CalcFov`'s widescreen aspect handling (U13).

**Next step:** the `MatrixMultiply` reorder is the one thing known to have been
changed near this code. Diff `CG_AddPlayerWeapon` against upstream `1487e89`
before looking anywhere else.
`/cg_fov` reads **38.400208**, against a default of 100. The viewmodel shares the
world refdef, so a fov that low enlarges the gun and crops it lower — exactly the
reported symptom, and nothing to do with the render presets.

**Immediate fix:** `/cg_fov 100`.

**The real question is how it got there.** 38.400208 is not a value anyone types.
The Game Options page has an FOV *slider* (`10 | 100 | 130`), and sliders were
among the rows painting wrongly before U1/U12 — `Item_Slider_Paint` positions
from `item->textRect` like the others. A slider whose hit region and drawn
position disagree will write a value from wherever the click landed, and 38.4 is
roughly where a click near the left of that track maps to. Suspect that opening
or brushing the FOV row wrote it.

**Next step:** confirm by resetting `cg_fov` to 100, opening Game Options, and
checking whether it changes without a deliberate drag. If it does, the slider's
hit region is wrong — check `Rect_ContainsWidescreenPoint` against where the
slider actually paints, which ties directly to U13.
Reported after the render preset work: the first-person weapon is larger and
further down the screen, so less of it is visible. Most obvious on the lightning
gun. Explicitly *not* a brightness issue — the framing changed.

**Nothing in the render work should be able to cause this.** `r_dither`,
`r_dlightMode`, `r_ambientScale`, tonemapping and cubemapping do not move or
scale the viewmodel, and neither `classic.cfg` nor `gloss.cfg` touches a gun or
fov cvar. So either something else changed it, or it predates the preset work and
was noticed alongside it. Do not assume the presets are the cause.

**The cvars that actually control this:**
- `cg_fov` — default here is **100**. Quake 3 shipped 90. The viewmodel is drawn
  in the same refdef as the world, so a *lower* fov makes the gun look bigger and
  crop lower, which is the direction reported. Worth checking whether the config
  in use is overriding it, and what Quake Live's own default is.
- `cg_gunX` / `cg_gunY` / `cg_gunZ` — all default 0, viewmodel offsets.
- `cg_drawGun` — 1 normal; 2 and 3 select different positions, and note the
  `EV_RAILTRAIL` handler already special-cases 2 and 3 by nudging `origin2`
  along `cg.refdef.viewaxis[1]`, so those modes do shift things.

Also still worth checking `CG_CalcFov`'s widescreen aspect handling (U13).

### C1. Railgun draws two beams — DONE (verify, 2nd fix)
**The one that mattered.** There are *three* rail trail sources, not two:
1. `cg_predict.c:537` — predicted, local player only, from the barrel.
2. `cg_event.c:1141` — the server's `EV_RAILTRAIL`, from the transmitted muzzle.
3. `cg_weapons.c:1648` → `CG_SpawnRailTrail` — QL's own path, which is supposed
   to re-spawn the beam from the **rendered weapon flash tag** so it leaves the
   drawn barrel rather than the server's muzzle point.

`CG_SpawnRailTrail` reads `cent->pe.railgunTrailStart` as its start. That field
is declared (`cg_local.h:140`) and read (`cg_weapons.c:523`) and **written
nowhere in the codebase** — so every rail shot drew a beam from a zero vector on
top of the correct one. That is the second beam, and it happened for every
player, which is why gating only the local predicted shot did not fix it.

Made inert until the capture exists rather than drawing from the world origin;
`EV_RAILTRAIL` covers the trail meanwhile.

**Proper follow-up:** implement the capture — write `pe.railgunTrailStart` from
the weapon flash tag origin in `CG_AddPlayerWeapon` when the rail muzzle flash is
placed, then let `CG_SpawnRailTrail` draw and drop the trail from the
`EV_RAILTRAIL` handler. That restores QL's intended behaviour, where the beam
tracks the drawn weapon rather than the server's muzzle.

#### First fix (correct, but not the cause of what was visible)
Firing the rail draws one trail from the barrel and a second along the view
axis, both ending at the same impact point.

Two separate call sites draw a trail for the same shot:
- `cg_predict.c:537` — a **predicted** trail, drawn from `muzzle + right*4 - up`
  the instant you fire, so the shot feels immediate rather than waiting a round
  trip. This is the barrel one.
- `cg_event.c:1141` — the server's `EV_RAILTRAIL`, `CG_RailTrail(ci,
  es->origin2, es->pos.trBase)`, arriving a moment later. This is the view one.

Nothing suppresses either for your own shots, so both draw.

**Fixed.** `cg.predictedRailTime` records when the predicted trail was drawn, and
the `EV_RAILTRAIL` handler skips both the trail and the impact when the event is
our own shot within 1000ms of it. A dedicated field rather than reusing
`cg.lastAutoFireTime`, which doubles as the fire-rate gate. The window is
deliberately short: every other player's rail arrives through this event, and a
shot we did not predict — paused, in a timeout, spectating — still has to be
drawn here. Rail refire is 1500ms so 1000 cannot swallow a genuine second shot.

Note the predicted path drew the *impact* too, so `CG_MissileHitWall` was being
called twice for the same hit as well — the fix covers both.

### C2. Quad pickup now lights the room — DONE (verify)
`CG_Item` adds a dlight for a powerup resting on the ground, matching the
colours `CG_PlayerPowerups` uses for a carried one — quad blue, battlesuit
green, regen red, invis white, radius 200 + rand()&31. Quake 3 only ever lit the
powered-up *player*, never the pickup. Suppressed while an item is scaling up on
respawn (`frac != 1.0`).

Whether this fully answers the reported missing glow is unverified: R8 is still
open on why explosions light so weakly, and if that turns out to be a renderer
path fault it will affect this light too.

### R8. Dynamic light glow is weak or absent — OPEN, my earlier diagnosis was wrong
**Correction.** I claimed `r_dlightMode 0` keeps dynamic lights off world surfaces
entirely. That is not what it does. Reading its actual uses:

- `tr_shade.c:1509` — with `r_dlightMode` non-zero, a single-pass lightall shader
  takes a *fast* dlight path folded into the main shader.
- `>= 2` gates shadowed dlights (`tr_glsl.c:1032`, `tr_image.c:2650`,
  `tr_scene.c:452`, `tr_shade.c:804`).

So `0` still lights the world, via the legacy `ProjectDlightTexture` additive
pass. Changing it alters *which* path runs, not whether lights exist. That is why
setting it to 1 by hand changed little.

**Also:** `r_dlightMode` is `CVAR_ARCHIVE | CVAR_LATCH`. Changing the default in
`tr_init.c` cannot affect anyone whose config already contains a value — the
archived one wins. The build did not "force enable" it and could not have. Any
setting we actually need on must be applied through a cfg or checked at runtime,
not just given a new default.

**Still unexplained:** rocket blasts light far too weakly, and the quad pickup
casts no glow at all.

**Leads worth taking next:**
1. The quad item may never have had a dlight. In Quake 3 it is the *powered-up
   player* that emits light (`cg_players.c:1930`), not the item on the floor.
   `CG_AddItem` adds no light for powerups. If Quake Live's pickup glows, that is
   a dlight this port has not implemented — cgame work, not a renderer setting.
2. For rockets, `CG_MakeExplosion` sets `le->light = 300` with an orange
   `lightColor`. Check `CG_AddLocalEntities` actually forwards that to
   `trap_R_AddLightToScene`, and whether the legacy dlight path scales it down.
3. Compare both dlight paths directly: `r_dlightMode 0` vs `1` vs `2` on the same
   explosion, and check `ProjectDlightTexture` is even reached.


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

`classic.cfg` also sets `r_dither 2` explicitly, because before R7 the dither
lived inside the tonemap pass and turning tonemapping off silently removed it.

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

### R5. Vulkan renderer — IN PROGRESS
**Started.** `code/renderervk` is vendored from ec-/Quake3e (GPLv2, 29 files),
with `code/renderervk/README.ioquakelive.md` carrying the plan. **Not wired up:**
`BUILD_RENDERER_VULKAN` defaults to 0, no Makefile rule references the directory,
so the OpenGL build cannot be affected.

`cl_renderer` is exposed in the render options menu (OpenGL 2 / Vulkan). Picking
Vulkan before the renderer exists falls back to `opengl2` through the existing
reset-string path rather than failing to start, so the row is safe to ship now.

**Next, in order:**
1. `Makefile` target producing `vulkan<arch>.so` / `.dll`, behind
   `BUILD_RENDERER_VULKAN=1` so it stays opt-in until it works.
2. A shim satisfying this tree's `refexport_t` (v9) from Quake3e's (v8),
   stubbing `Font_DrawString`, `TextBounds`, `GetGlyphInfo`,
   `SetCompositionFont` and `Get_Advertisements`.
3. `sdl_glimp.c` split so it can request `SDL_WINDOW_VULKAN` and a surface
   rather than a GL context unconditionally.
4. Milestone 1: builds, links, clears the screen.

**Read `renderergl2/tr_font_gl.c` before estimating anything past milestone 1** —
it is the GL half of the QL TrueType path and has no Vulkan counterpart. It is
the largest unknown in the whole port.

#### Original scoping
Wanted. Feasible. Not small — this is a multi-session project, and worth being
honest about that up front rather than starting it and stalling.

**What makes it tractable:** `cl_renderer` already dispatches by DLL name
(`cl_main.c`, default `"opengl2"`, `CVAR_ARCHIVE | CVAR_LATCH`, falls back to its
reset string when the named library is missing) and `USE_RENDERER_DLOPEN=1`. A
third renderer needs no engine change at all — it needs to build as
`vulkan<arch>.so/.dll` and export `GetRefAPI`. `renderercommon` is already shared,
so image loading, the shader-script parser, skins, models and the font core come
along for free.

**Base to port from:** Quake3e (`ec-/Quake3e`), `code/renderervk`. Mature,
maintained, and a faithful reimplementation of the *classic* renderer — which
suits the goal here, since the classic look is what we are chasing anyway.

**Known work beyond a straight copy:**
1. `REF_API_VERSION 9`. This port's `refexport_t` adds Quake Live's TrueType text
   path — `Font_DrawString`, `TextBounds`, `GetGlyphInfo`, `SetCompositionFont` —
   and `Get_Advertisements`. The font core is shared in `renderercommon`
   (`tr_fontstash.c`, `tr_stbtt.c`) but the GL-specific half,
   `renderergl2/tr_font_gl.c`, has no Vulkan equivalent. That file is the single
   biggest unknown; it wants reading before committing to an estimate.
2. Quake3e's renderer expects Quake3e's engine-side helpers in places. Each
   needs checking against this tree rather than assumed.
3. The widescreen bias handling and anything else this port changed in the
   refexport surface.
4. `R_ExportCubemaps_f` and the cubemap path (R4) have no classic-renderer
   equivalent — a Vulkan build would simply not have `r_cubeMapping`.
5. SDL window creation currently requests a GL context unconditionally
   (`sdl_glimp.c`); Vulkan needs `SDL_WINDOW_VULKAN` and a surface instead.

**Suggested first milestone:** get it building and clearing the screen, with text
rendering stubbed, before touching anything else. That answers the two real
questions — whether the `refexport_t` surface can be satisfied and whether the
SDL path can be split cleanly — and everything after it is incremental.

**What it will and will not give you.** Expect better frame pacing, far better
driver behaviour on Intel Arc and on Linux/Mesa, and a renderer that is
GL1-equivalent in output — so closer to Quake 3 than renderergl2 is. Expect it to
add no gloss and no cubemaps: an API is not an aesthetic. If the goal is the
metallic look, R4 is the route, not this.

### R6. Voodoo postfilter as a real post-process pass — OPEN
`voodoo.cfg` gets the 16-bit dither but not the thing that made it look good:
Voodoo3 and later ran a filter over the dithered output on scanout — the "22-bit"
mode — blending adjacent pixels so the dither read as a smooth gradient rather
than as noise. Unlike reflection, this genuinely is reproducible in screen space:
dither-smoothing needs nothing but neighbouring pixels.

R7 landed first and shares the insertion point. The postfilter would be a second
pass in `RB_DitherToScreen`, reading neighbours instead of adding noise, and is
only worth running under `r_colorbits 16` — at 8 bits and above there is no
dither pattern coarse enough to be worth smoothing.

Note this has nothing to do with API lineage. OpenGL (1992, out of SGI's IRIS GL)
predates Glide (1996), and Glide borrowed conventions from OpenGL rather than the
reverse; Vulkan descends from AMD's Mantle and was a deliberate break from
OpenGL's state machine. Glide is a dead branch — nothing of it survives in Vulkan
to be recovered. What is recoverable is the hardware's *output stage* behaviour,
which is what this item and R7 are.

### R7. Output dither — DONE (verify)
The renderer had exactly one dither: a 2-phase checkerboard hardcoded to half an
8-bit step, inside `tonemap_fp.glsl`. It only ran when `r_hdr` and `r_toneMap`
were both on, so `classic.cfg` — which turns both off — removed the only dither in
the pipeline and banded *worse* than the defaults. A fixed 8-bit amplitude is also
wrong on any other framebuffer: four times more noise than a 10-bit output needs.

Now `RB_DitherToScreen` (`tr_postprocess.c`), replacing the `FBO_FastBlit` calls
at the end of `RB_SwapBuffers`, so it applies on every path. Amplitude derives
from `glConfig.colorBits` — what the driver actually handed back, not what was
requested — so a 10-bit framebuffer gets a quarter of the noise an 8-bit one does.

| value | pattern |
|---|---|
| `r_dither 0` | plain hardware blit |
| `r_dither 1` | ordered 8x8 Bayer, stable frame to frame |
| `r_dither 2` | *(default)* interleaved gradient noise, golden-ratio phase per frame off a frame counter |

`r_dither 2` puts its error in high spatial frequencies where the eye discards
it, and decorrelating successive frames lets it average out over time instead of
sitting still on a surface — worth roughly another bit at these frame rates.

`r_colorbits 30` asks for a real 10-bit framebuffer; the amplitude follows it
automatically. That is actual precision rather than simulated, and makes the
dither mostly moot on hardware and displays that support it.

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
