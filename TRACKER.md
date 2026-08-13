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
