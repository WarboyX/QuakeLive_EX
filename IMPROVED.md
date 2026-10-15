# What this build adds

Everything here is work done on top of
[tjone270/ioquakelive](https://github.com/tjone270/ioquakelive). The baseline is
commit `1487e89` — the last one authored there — and this file covers the 273
commits since it.

It is deliberately **not** the bug list. `TRACKER.md` has every tracked item with
its cause and evidence; this is the additive side: subsystems that did
not exist, protections that were not there, and things that worked so badly that
replacing them counts as an addition. Where an entry has a `TRACKER.md` id it is
given, so the reasoning behind it can be read in full.

A note on attribution before anything else: hitscan lag compensation (`HAX_*`),
enemy position obfuscation, the loadout system, Red Rover, race checkpoints,
Domination medals, spectator item timers, the `alias` command system and the
Quake Live protocol and netchan work all came from the base fork. They are not
listed below. Freeze Tag is the awkward case — it existed in name, with a
gametype, cvars and menus, but nothing in the tree ever froze a player. That one
is here.

---

## Renderer

### A Vulkan backend that ships

`code/renderervk` is vendored from [ec-/Quake3e](https://github.com/ec-/Quake3e)
and then made to run against this tree, which was most of the work rather than
the vendoring. Twenty-eight units brought up against a `refexport_t` that had
drifted; **the SDL windowing layer written from scratch** (`vk_window.c`, which
Quake3e implements against its own platform layer and which therefore had no
counterpart to port); text brought up on fontstash/stb_truetype; the font atlas
resize fixed, where a resize left the shader pointing at the freed image; the
`Shutdown` argument translated so `vid_restart` closes its window; and a map
change stopped unloading the renderer.

It also **falls back properly**: when the selected renderer will not load, the
client reverts to `opengl2` and says so, rather than exiting. That path was
written and never worked. → `R5`, `R12`

### Everything else in the renderer

- **Anisotropic filtering to 16x** by default (`r_ext_max_anisotropy`), with the
  level actually negotiated reported at startup. It had been switched off
  outright by our own `classic.cfg`, which no other preset turned back on, so no
  amount of setting it in the menu did anything. → `R10`, `E54`
- **Look presets** — `classic.cfg`, `voodoo.cfg`, `gloss.cfg`, `modern.cfg`. → `R2`
- **Output dither**, scaled to the framebuffer actually obtained rather than the
  one requested. → `R7`
- **`r_shaderTimeSource`**, which decouples shader animation from `cg.time`, so
  animated textures stop running at a speed that varies with framerate.
- **Dynamic lights reach world surfaces.** They never did — the name field the
  lookup used was wrong, so every dlight lit entities and nothing else. A powerup
  on the ground now lights the room around it. How bright that glow ends up is
  still open. → `C2`, `R9`, and `R8` for what is left
- **Vulkan instance layers are named at startup**, so ReShade, overlays and
  driver injections are visible rather than inferred. ReShade's Vulkan support
  attaches to this client with nothing needed from us. → `R11`
- **An out-of-range model frame names the model** instead of faulting inside the
  mesh path.

---

## Spawning

The largest gameplay change in the project, and the one with the most measurement
behind it. On a full server most deaths were not kills: **93% of deaths on
thunderstruck and 98% on citycrossings were telefrags.**

Every spawn selector ended the same way — when no point was free it fell back to
`G_Find(NULL, ...)`, which returns the first spawn entity in the map,
deterministically, forever. Every player who died went to the same spot,
telefragged whoever was standing there, and that player respawned into it in
turn.

- **A real selector.** Points are sorted into clear, recently used and occupied;
  when everything is occupied the least recently used one is taken, so the next
  player goes somewhere else and the chain cannot form. → `E45`
- **Maps with too few deathmatch spawns fall back to their team pads.**
  citycrossings has **two** `info_player_deathmatch` entities for sixty-four
  players, with everything else under `team_CTF_redspawn` / `bluespawn`, and the
  selector only ever looked at one classname. → `E50`
- **Gametype filters no longer delete spawn points at load.** In free-for-all,
  anything marked `notfree` or listing a non-ffa gametype was freed during entity
  spawning — on a team map, that is every pad. They are kept in reserve
  (`FL_SPAWN_RESERVE`) and used when the map's own points run out. → `E56`
- **`G_NudgeSpawnClear` — step aside instead of telefragging.** Eight directions
  by three radii, each candidate checked for startsolid/allsolid, a box trace
  from the pad, a 128-unit floor trace and an occupancy test, before falling back
  to the kill box. → `E58`
- **A spawn telefrag is not a death.** It stopped counting against the victim's
  K/D and stopped scoring for the player who spawned on them. → `E58`
- **Spawn protection** (`g_spawnProtectionTime`, 1500 ms) **with a firing
  lockout** — shooting forfeits the protection on the same frame, so it cannot be
  used to spawn-camp. → `E59`
- **Protected players are visibly protected**, as a translucent silhouette
  (`cg_spawnProtectAlpha`). Protocol 91 had no free bit for this; the reasoning
  behind reusing missile-only `EF_BOUNCE_HALF`, and the grenade regression that
  cost, is written out in full in the tracker. → `C38`
- **Dead players stopped occupying spawn points.** `GibEntity` left the corpse
  linked and `trap_EntitiesInBox` only returns linked entities, so a body was
  reserving a pad it was never going to stand on.

Measured on the same map before and after: trinity telefrags **159 → 24** against
weapon deaths **492 → 1842**; thunderstruck **877 → 259** against **67 → 825**.
→ `E53`, `E55`

---

## Server operation and protections

### Bans

A **persistent address ban system**: `banaddr`, `bandel`, `listbans`,
`flushbans`, `exceptaddr`, `exceptdel`, `rehashbans`, saved to `sv_banFile`,
reloaded at server start, and checked at connect time. The admin commands existed
as stubs that parsed their arguments and did nothing; nothing enforced a ban.

### Integrity

- Extracted game modules are **verified against the pak they came from**, with
  shipped checksums, so a module that does not match what was packaged is caught
  rather than run. → `E14`
- The filesystem **says when more than one `iobin.pk3` is loaded**, which is
  otherwise silent and produces a build that is half one release and half
  another.
- Windows **reports why it refused a game module** instead of failing quietly.

### Overflow, five separate ones

| what overflowed | what happened before |
|---|---|
| serverinfo past 1024 bytes | keys silently dropped |
| scoreboard messages | the tail was lost, so the board was short |
| HUD score configstrings | overran the reliable command queue |
| configstring backlog | clients dropped for "server command overflow" |
| the snapshot ring | wrapped |

`target_location` writes are bounded to `MAX_LOCATIONS`, and network-driven
weapon indices are bounds-checked before they index anything. → `E16`, `E23`

### Crash survival

- `sv_maxclients` **clamped to `MAX_CLIENTS`** — setting it higher crashed on map
  load.
- A **dropped client no longer takes the server down**.
- `map_restart` **stopped initialising the game twice**.
- A **VM is no longer unmapped while its stack frames are live**.
- **More than ~25 bots stopped crashing the server** — bot states moved out of
  the 256 KB pool and the pool raised. → `E13`
- The **hunk is sized for 64 players**.
- The **locale is forced to C**, so a machine with a comma decimal separator does
  not corrupt every float it parses. → `E18`

### Other server work

- A **connecting player takes a bot's slot** instead of being refused on a full
  server.
- **`sv_altEntDir`** — override a map's entities from disk without repacking the
  bsp.
- **`com_maxfps` lands on rates that do not divide 1000**, via microsecond frame
  pacing (`com_framePacing`). → `E9`, `E43`

---

## Diagnostics

The theme is that this codebase's characteristic failure is silence: a registered
cvar nothing reads, a shader name the pak does not contain, a menu that fails to
parse, an entity discarded from a snapshot. All four produce no error and no
effect. Most of what follows exists to make one of those audible.

- **`snapstats`** — the fullest snapshot since the map loaded, its composition by
  entity type, anything dropped, and whether `sv_fps` or the client `rate` clamp
  is what is actually limiting delivery. This is what found **1,350,347 silently
  discarded entities in a single map**, and then proved that every one of them
  was an event rather than a player or an item. → `E48`, `E49`, `E52`
- **Windows crash reporting** — `crashlog.txt` on fault, the address that was
  touched as well as where it happened, and a null call that names its own call
  site. → `E12`
- **`logfile_keep`** — a timestamped log per run, so a log stops overwriting the
  one that recorded the crash you are chasing.
- **Failed asset registrations are named.** `RE_RegisterShader` returns 0 for a
  name the paks do not contain and every caller treats 0 as "draw nothing"; it
  now prints the name that failed.
- **Spawn point counts are printed at map load**, and CTF spawn selection warns
  when it falls back to team-agnostic pads.
- **Build stamps in three places** — `pak01`, `iobin`, and the cgame/qagame init
  lines — because menu fixes ship in `pak01.pk3` and module fixes in `iobin.pk3`,
  and replacing one and not the other repeatedly made a fixed bug look unfixed.
  → `U17`

---

## Menus and HUD

- **A server browser**, with the server queries implemented and join-on-connect.
  The menu existed and listed nothing. → `E3`
- **A player setup menu**, and blank, stuck and `???` rows fixed across the
  advanced settings. → `U1`, `U6`
- **A render options menu**, gated on the renderer actually running: five of its
  controls were `renderergl2` cvars that Vulkan never registers — switches that
  did nothing — while bloom and anisotropy had no control at all. → `U12`, `R12`
- **A credits screen**, categorised, cross-referencing the projects this is built
  on.
- **Team select from the in-game menu**, with its layout derived from Quake
  Live's own menu at runtime rather than hardcoded against it. → `E44`
- **A scrollable scoreboard** — per-list scrolling, position bars, sticky
  selection and mouse support. It could not be scrolled at all, so on a full
  server most of it was unreachable. → `C13`, `C19`, `C25`
- **Career stats on the scoreboard.** K/D, damage and accuracy only ever showed
  the current life. → `C18`
- **Text fields accept input.** No field anywhere in the UI could be typed into.
- **Any bindable command is bindable** from the Controls menu, and cvar values
  stopped being hidden. → `U10`, `U7`
- **The FFA scoreboard's columns line up**, drawn at the list's own column
  positions so they cannot drift from it. → `C35`
- **Console text scales with the display.** It was a fixed 8×16 *pixels* —
  correct for the 640×480 the charset was drawn for, eight pixels tall on a 1600p
  panel. `con_scale` is a multiplier on a resolution-derived base rather than an
  absolute, so an existing config gets the fix without being edited.
  → `U14`, `R14`, `R15`

---

## Freeze Tag

Listed separately because it was shipped as a supported gametype and did not
function. Nothing in the tree ever froze a player (`E22`); the first frag took
the server down by freeing the player entity (`E20`); the auto-thaw test was
inverted, so every freeze that did happen died on the frame it happened (`E24`);
the round state machine never started because serverinfo had overflowed; the
entry parser read 18 fields where 17 were written; and there was no ice, because
five media handles were never assigned (`C23`).

It now freezes, thaws at chest height on a timer with a radius, holds the frozen
pose, shows an ice shell that shrinks with thaw progress, puts FROZEN in the team
overlay's location column, and colours rails by team.

---

## Bots

**The behaviour fixes.** Stopped hunting their own team in instagib and stopped
the hunt outranking team orders; stopped dancing around each other; stopped
touching their own flag before attacking in CTF; and stopped putting a whole
human team on defence because of a default voice order. `bot_minplayers` fills to
the real slot count, bots understand Quake Live's chat format, a bot no longer
inherits `g_teamSpawnAsSpec` and fills the server with spectators, and pending
bots no longer eat every slot. → `E10`, `E26`

**A tactical layer** (`code/game/ai_tactics.c`), because the Quake 3 bot decides
everything from its own inventory and knows nothing about the room it is standing
in. `bot_tactics 0` returns the stock answer from every entry point, which is how
it gets tested — two matches on one map, one cvar apart, no restart.

- **Engagement range by weapon.** `IDEAL_ATTACKDIST` is **140 for everything**, so
  a railgun bot walks to shotgun range and a lightning gun bot backs out of the
  gun's own 768-unit reach. Below 0.5 attack skill the old single distance stays.
- **Side-stepping, two mechanisms.** Reactive: missiles that are moving, heading
  at the bot, arriving within a second *and in line of sight* — without that last
  test a bot dodges a rocket fired through a wall. Anticipatory: an enemy holding
  a hitscan weapon on the bot breaks its strafe rhythm before the shot. The stock
  rhythm flips only on a timer **and** `random() > 0.935`, holding one direction
  for seconds at a time.
- **Group pushes**, from counting and weighing the room — a bot at 25 health is
  not a whole body. The posture feeds `BotAggression`, which four gametype paths
  already test, so a group turns aggressive together with nothing coordinating it.
- **Retreating** now considers being outnumbered, and is **held for 1.5 s**;
  `BotAggression` sits either side of 50 as health ticks while the fight node asks
  every think, so a bot on the boundary oscillated several times a second. Retreat
  heads towards the nearest team mate.
- **Item awareness.** The search range — an AAS travel time, not a distance —
  scales with what the bot is short of, and with an enemy about the goals that are
  not worth it are refused, read from `g_entities[].item` rather than by name.
- **Auto-defence**, capped at a quarter of the team, gametypes with a place to
  lose, 60 seconds.
- **`bots`** console command: posture, ally/foe counts, AI node, goal and totals,
  in every gametype.

Cvars: `bot_tactics` 1, `bot_dodge` 1, `bot_squadRange` 800, `bot_debugTactics` 0.
→ `E60`

---

## Build and project protections

- **The repo builds from a plain clone** — SDL2, libogg and libvorbis vendored.
- **macOS arm64 and x86_64**, folded into the shared universal pak.
- **`package-release.sh`** builds both platforms and names every archive by
  commit, appending `-dirty` when the tree is not clean, because a build from an
  uncommitted tree cannot be reproduced from its hash.
- **Three build gates**, each guarding a failure mode that has actually cost a
  round here:

  | tool | what it stops |
  |---|---|
  | `content/serverconfigs/check-configs.py` | a mode config leaking a cvar the reset block does not cover |
  | `tools/stub-report.py` | a new empty function joining the seventy already there without a verdict |
  | `tools/check-menus.py` | a `.menu` parse failure, which looks like a working menu with the wrong items in it |

- **`tools/dead-cvars.py`** — 186 cvars are registered and read by nothing.
  Setting one produces no error and no effect; this has caused real bugs twice.
  → `E8`
- **`docs/pak-manifest.txt`** and **`docs/ql-menu-names.txt`** — names only, no
  asset content, so asset and menu names are verified rather than guessed. Added
  after cgame registered three Freeze Tag ice models under invented names; a scan
  showed the pak has no ice meshes at all.
- **Product name split from protocol name.** "Quake Live Ex" is the window title
  and the version stamps; `com_gamename`, `com_protocol` 91 and `baseqz` are
  untouched, so stock Quake Live clients still see these servers and still join
  them. → `E57`

---

## Verification status

Worth stating plainly, because it varies a lot by area.

**Measured from server logs:** the spawn work, the snapshot work, the telefrag
accounting, anisotropic filtering, and the console scale reports. These have
before-and-after numbers on the same maps.

**Confirmed in play:** the Freeze Tag chain, the bot pool and fill fixes, the
door travel fix, the score tracker, `com_maxfps`, and the module mismatch check —
which caught a real mismatch on its first run.

**Read-correct but not yet exercised in a live match:** the ban system, spawn
protection and its firing lockout, the reserve spawn pads, the credits screen,
and the render menu's Vulkan block. Most tracker entries are marked
`DONE (verify)` for exactly this reason.

---

## Still open

`TRACKER.md` is the full list. The ones worth knowing about:

- **The shotgun runs in stock-compatible mode on purpose.** The improved pattern
  and basis are written and tested but default off, because the server sends a
  seed and each client regenerates the pattern locally — so the change is correct
  on this client and wrong on a stock one. `g_shotgunBasis 1` /
  `g_shotgunPattern 1` if you are not serving stock clients. → `W1`–`W3`
- **Some maps cannot hold 64 players.** thunderstruck has five spawn points and
  no team pads to fall back on. The spawn work does everything it can; the rest
  is a player cap or a different map, and the console now says which maps are in
  that position.
- **The client `rate` clamp, not `sv_fps`, is the delivery limit.** A 1308-byte
  snapshot lets a default-rate client sustain 18 a second against a tick of 40.
  → `E52`
- **Dynamic light glow is still weak**, and is the largest visual improvement
  still available. → `R8`
- **RT reflections and ambient occlusion** are scoped against `VK_KHR_ray_query`
  and not started. → `R13`
- **The console is still the Quake 3 bitmap charset**, so scaling it magnifies
  it. Moving it onto the TrueType renderer already used for the HUD is scoped.
  → `R16`
- **186 registered cvars are read by nothing.** → `E8`
