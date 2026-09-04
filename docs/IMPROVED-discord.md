Discord-ready version of IMPROVED.md.

Each block below is one Discord message, every one under the 2000-character
limit for a normal (non-Nitro) account. The ═══ divider lines are not part of
the content - copy what is between them. Post them in order.

Discord does not render markdown tables, so the tables in IMPROVED.md are
lists here. Headers (##), bold, inline code and code fences all render.

═══════════ MESSAGE 1 of 10 — 793 characters ═══════════

## Quake Live Ex — what this build adds

Everything below is work on top of **tjone270/ioquakelive**, from commit `1487e89` onward — 273 commits. It is deliberately not the bug list; `TRACKER.md` has every tracked item with its cause and evidence.

**Not ours, so not listed:** hitscan lag compensation, enemy position obfuscation, the loadout system, Red Rover, race checkpoints, Domination medals, spectator item timers, the `alias` system, and the Quake Live protocol and netchan work all came from the base fork.

Freeze Tag is the awkward case — it existed in name, with a gametype, cvars and menus, but nothing in the tree ever froze a player. That one is ours.

**Requires Quake Live.** `pak00.pk3` from a Steam install has to be present. It is not redistributed here and never will be.

═══════════ MESSAGE 2 of 10 — 1360 characters ═══════════

## Renderer

**A Vulkan backend that ships.** Vendored from ec-/Quake3e, then made to run against this tree — which was most of the work. Twenty-eight units brought up against a drifted `refexport_t`; the **SDL windowing layer written from scratch** (Quake3e implements it against its own platform layer, so there was nothing to port); text on fontstash/stb_truetype; a font-atlas resize that left the shader pointing at a freed image; `vid_restart` and map changes made survivable.

It also **falls back properly** — when the selected renderer will not load, the client reverts to `opengl2` and says so instead of exiting. That path was written and never worked.

- **Anisotropic filtering to 16x** by default, with the level actually negotiated reported at startup. It had been switched off outright by our own `classic.cfg`, which no other preset turned back on, so setting it in the menu did nothing.
- **Look presets** — `classic`, `voodoo`, `gloss`, `modern`.
- **Output dither**, scaled to the framebuffer actually obtained.
- **`r_shaderTimeSource`** — animated textures stop running at a framerate-dependent speed.
- **Dynamic lights reach world surfaces.** They never did; the lookup's name field was wrong, so every dlight lit entities and nothing else.
- **Vulkan layers named at startup**, so ReShade and overlays are visible rather than inferred.

═══════════ MESSAGE 3 of 10 — 1438 characters ═══════════

## Spawning — the largest gameplay change

On a full server most deaths were not kills. **93% of deaths on thunderstruck and 98% on citycrossings were telefrags.**

Every spawn selector ended the same way: when no point was free it fell back to `G_Find(NULL, ...)` — the first spawn entity in the map, deterministically, forever. Everyone who died went to the same spot, telefragged whoever was standing there, and that player respawned into it in turn.

- **A real selector** — points sorted into clear / recently used / occupied, least-recently-used when everything is taken, so the chain cannot form.
- **Fallback to team pads** when a map has too few deathmatch spawns. citycrossings has **two** `info_player_deathmatch` for sixty-four players.
- **Gametype filters no longer delete spawn points at load** — they are kept in reserve.
- **Step aside instead of telefragging** — 8 directions × 3 radii, each floor- and solid-checked, before falling back to the kill box.
- **A spawn telefrag is not a death** for the victim, and scores nothing for the killer.
- **Spawn protection** (1500 ms) **with a firing lockout** — shooting forfeits it, so it cannot be used to spawn-camp — shown as a translucent silhouette.
- Gibbed bodies get unlinked, so a corpse stops reserving a pad.

Measured, same map before and after: trinity telefrags **159 → 24** against weapon deaths **492 → 1842**. thunderstruck **877 → 259** against **67 → 825**.

═══════════ MESSAGE 4 of 10 — 1530 characters ═══════════

## Server operation and protections

**A persistent ban system** — `banaddr`, `bandel`, `listbans`, `flushbans`, `exceptaddr`, `exceptdel`, `rehashbans`, saved to disk, reloaded at start, checked at connect. The admin commands existed as stubs that parsed their arguments and did nothing; nothing enforced a ban.

**Integrity.** Extracted game modules are verified against the pak they came from, with shipped checksums. The filesystem says when more than one `iobin.pk3` is loaded — otherwise you get a build that is half one release and half another.

**Five separate overflows fixed:** serverinfo past 1024 bytes silently dropped keys; scoreboard messages lost their tail; HUD score configstrings overran the reliable command queue; the configstring backlog dropped clients; the snapshot ring wrapped. Plus `target_location` writes bounded, and network-driven weapon indices bounds-checked.

**Crash survival.** `sv_maxclients` clamped to `MAX_CLIENTS` — setting it higher crashed on map load. A dropped client no longer takes the server down. `map_restart` stopped initialising the game twice. A VM is no longer unmapped while its stack frames are live. More than ~25 bots stopped crashing the server. The hunk is sized for 64 players. The locale is forced to C, so a comma decimal separator stops corrupting every float parsed.

Also: a connecting player takes a bot's slot instead of being refused, `sv_altEntDir` overrides a map's entities without repacking the bsp, and `com_maxfps` lands on rates that do not divide 1000.

═══════════ MESSAGE 5 of 10 — 1311 characters ═══════════

## Diagnostics

This codebase's characteristic failure is silence: a registered cvar nothing reads, a shader name the pak does not contain, a menu that fails to parse, an entity discarded from a snapshot. All four produce no error and no effect. Most of what follows exists to make one of those audible.

- **`snapstats`** — the fullest snapshot since map load, its composition by entity type, anything dropped, and whether `sv_fps` or the client `rate` clamp is the real delivery limit. This found **1,350,347 silently discarded entities in a single map**, then proved every one was an event rather than a player or an item.
- **Windows crash reporting** — `crashlog.txt`, the address touched as well as where it happened, and a null call that names its own site.
- **`logfile_keep`** — a timestamped log per run, so a log stops overwriting the one that recorded the crash you are chasing.
- **Failed asset registrations are named.** `RE_RegisterShader` returns 0 for a name the paks do not contain and every caller treats 0 as "draw nothing".
- **Spawn point counts printed at map load.**
- **`bots`** — what every bot is doing right now, in any gametype.
- **Build stamps in three places** (`pak01`, `iobin`, module init), because replacing one pak and not the other repeatedly made a fixed bug look unfixed.

═══════════ MESSAGE 6 of 10 — 1302 characters ═══════════

## Menus and HUD

- **A server browser**, with the queries implemented and join-on-connect. The menu existed and listed nothing.
- **A player setup menu**, and blank, stuck and `???` rows fixed across the advanced settings.
- **A render options menu gated on the renderer you are running.** Five of its controls were `renderergl2` cvars Vulkan never registers — switches that did nothing — while bloom and anisotropy had no control at all.
- **A credits screen**, cross-referencing the projects this is built on.
- **Team select from the in-game menu**, its layout derived from Quake Live's own menu at runtime rather than hardcoded against it.
- **A scrollable scoreboard** — per-list scrolling, position bars, sticky selection, mouse support. It could not be scrolled at all, so on a full server most of it was unreachable.
- **Career stats on the scoreboard.** K/D, damage and accuracy only ever showed the current life.
- **Text fields accept input.** No field anywhere in the UI could be typed into.
- **Any bindable command is bindable** from Controls, and cvar values stopped being hidden.
- **The FFA scoreboard's columns line up.**
- **Console text scales with the display.** It was a fixed 8x16 *pixels* — correct for the 640x480 the charset was drawn for, eight pixels tall on a 1600p panel.

═══════════ MESSAGE 7 of 10 — 718 characters ═══════════

## Freeze Tag

Listed separately because it shipped as a supported gametype and did not function.

Nothing in the tree ever froze a player. The first frag took the server down by freeing the player entity. The auto-thaw test was inverted, so every freeze that did happen died on the frame it happened. The round state machine never started, because serverinfo had overflowed. The entry parser read 18 fields where 17 were written. And there was no ice, because five media handles were never assigned.

It now freezes, thaws at chest height on a timer with a radius, holds the frozen pose, shows an ice shell that shrinks with thaw progress, puts FROZEN in the team overlay's location column, and colours rails by team.

═══════════ MESSAGE 8 of 10 — 1489 characters ═══════════

## Bots

**Behaviour fixes.** Stopped hunting their own team in instagib; stopped dancing around each other; stopped touching their own flag before attacking in CTF; stopped putting a whole human team on defence because of a default voice order. `bot_minplayers` fills to the real slot count, and they parse Quake Live's chat format.

**A tactical layer**, because the Quake 3 bot decides everything from its own inventory and knows nothing about the room it is in. `bot_tactics 0` restores stock behaviour exactly.

- **Engagement range by weapon.** `IDEAL_ATTACKDIST` is **140 for every weapon**, so a railgun bot walks to shotgun range and a lightning gun bot backs out of the gun's own 768-unit reach.
- **Side-stepping.** Reactive: incoming missiles, *in line of sight* — without that test a bot dodges a rocket fired through a wall. Anticipatory: an enemy holding a hitscan weapon on the bot breaks its strafe rhythm before the shot. The stock rhythm flips only on a timer **and** `random() > 0.935`.
- **Group pushes**, from counting and weighing the room — a bot at 25 health is not a whole body.
- **Retreating** considers being outnumbered and is held for 1.5s, so bots stop flickering in and out of the retreat node. It heads towards the nearest team mate.
- **Item awareness** — search range scales with need, and junk is refused mid-fight.
- **Auto-defence**, capped at a quarter of the team.

Cvars: `bot_tactics` 1, `bot_dodge` 1, `bot_squadRange` 800, `bot_debugTactics` 0.

═══════════ MESSAGE 9 of 10 — 1376 characters ═══════════

## Build and project protections

- **The repo builds from a plain clone** — SDL2, libogg and libvorbis vendored.
- **macOS arm64 and x86_64**, folded into the shared universal pak.
- **`package-release.sh`** builds both platforms and names every archive by commit, appending `-dirty` when the tree is not clean, because a build from an uncommitted tree cannot be reproduced from its hash.
- **Three build gates**, each guarding a failure this project has actually had:
 - `check-configs.py` — a mode config leaking a cvar the reset block does not cover
 - `stub-report.py` — a new empty function joining the seventy already there without a verdict
 - `check-menus.py` — a `.menu` parse failure, which looks like a working menu with the wrong items in it
- **`dead-cvars.py`** — 186 cvars are registered and read by nothing. Setting one produces no error and no effect; this has caused real bugs twice.
- **`docs/pak-manifest.txt`** — names only, no asset content, so asset names are verified rather than guessed. Added after cgame registered three Freeze Tag ice models under invented names; a scan showed the pak has no ice meshes at all.
- **Product name split from protocol name.** "Quake Live Ex" is the window title and version stamps; `com_gamename`, `com_protocol` 91 and `baseqz` are untouched, so stock Quake Live clients still see these servers and still join them.

═══════════ MESSAGE 10 of 10 — 1524 characters ═══════════

## Verification status

Worth stating plainly, because it varies a lot by area.

**Measured from server logs:** the spawn work, the snapshot work, the telefrag accounting, anisotropic filtering, console scale. These have before-and-after numbers on the same maps.

**Confirmed in play:** the Freeze Tag chain, the bot pool and fill fixes, the door travel fix, the score tracker, `com_maxfps`, and the module mismatch check — which caught a real mismatch on its first run.

**Read-correct but not yet exercised in a live match:** the ban system, spawn protection, the reserve spawn pads, the credits screen, the render menu's Vulkan block, and the whole bot tactical layer. Most tracker entries are marked `DONE (verify)` for exactly this reason.

## Still open

- **The shotgun runs in stock-compatible mode on purpose.** The improved pattern is written and tested but defaults off: the server sends a seed and each client regenerates the pattern locally, so the change is correct here and wrong on a stock client.
- **Some maps cannot hold 64 players.** thunderstruck has five spawn points and no team pads. The console says which maps are in that position.
- **The client `rate` clamp, not `sv_fps`, is the delivery limit.** A 1308-byte snapshot lets a default-rate client sustain 18 a second against a tick of 40.
- **Dynamic light glow is still weak** — the largest visual improvement still available.
- **RT reflections and ambient occlusion** are scoped and not started.
- **186 registered cvars are read by nothing.**
