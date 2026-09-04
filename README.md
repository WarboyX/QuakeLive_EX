# ioquakelive

An implementation of the **Quake Live** game, built on the open-source [ioquake3](https://ioquake3.org/) engine. The goal is to produce a functionally equivalent open-source client/server to the final release of Quake Live (v1069).

#### Why?
It's really important to me that the Quake Live community prosper - it's such a great game; this is why I have put so much time and effort into working on this fork.

## Project Status

**Huge work in progress.** The codebase is being incrementally updated from ioquake3's baseline to match the behaviour/architecture of Quake Live.

- [`IMPROVED.md`](IMPROVED.md) — what this build adds on top of the base fork: subsystems, protections and diagnostics, with verification status.
- [`RELEASE-NOTES.md`](RELEASE-NOTES.md) — the current release body.
- [`TRACKER.md`](TRACKER.md) — every tracked issue, with which binary it lives in and which clients see it.

### What works

- Quake Live clients can connect to the ioquakelive server.
- QL network protocol (custom netchan, entity/player state field tables, usercmd layout)
- QL game module (`qagame`) - 60%-70% complete, with per-gametype source files and lag compensation
- QL UI module - new exports, ~78 new cvars, font/widescreen support
- QL cgame module - updated event system, physics, HUD owner-draws, scoreboard rendering, impact sparks, weapon styles, zoom
- Font rendering via FreeType (though might move to `stb_truetype` in future as this is what Quake Live uses)
- Widescreen coordinate system (4:3 content area with left/center/right bias)
- All base game types: Free for All, Duel, Team Deathmatch, Clan Arena, CTF, 1F CTF, Harvester, Freeze Tag, Domination, A&D, Race, Red Rover (most have basic implementation)

### What's required to run the game
You must have an original `pak00.pk3` file acquired from a legitimate purchased copy of [Quake Live](https://steampowered.com/app/282440) from Steam. ioquakelive will not run without a legitimate `pak00.pk3` file. Quake Live and its assets remain the property of *id Software*.

I do not have any rights to use any Quake Live game assets and cannot/**will not** redistribute these from my own legitimate copy.



### Key differences from upstream ioquake3

| Area | ioquake3 (Q3) | ioquakelive (QL) |
|------|---------------|-------------------|
| Protocol | Q3 1.32 / ioq3 71 | QL build 1069's protocol 91 |
| Base game dir | `baseq3` | `baseq3` filesystem, `baseqz` protocol name |
| Game types | FFA, Tourney, TDM, CTF | + CA, FT, Race, DOM, A&D, Harvester, 1FCTF, RR (and all TA modes) |
| Physics | Q3 air control | + CPM air control, double jump, crouch slide, auto-hop |
| Font system | Scale-based (textFont/bigFont) | fontIndex-based (extraFonts[3]) |
| Widescreen | Stretch only | 4:3 constrained with left/center/right modes for anchoring elements (compatible with QL `.menu` files) |
| Key/door system | N/A | IT_KEY items, key-locked func_door |
| Stats | Basic scoreboard | Detailed per-weapon accuracy, 'premium' scoreboards from QL |

### Implementation progress

#### Network protocol

| Area | Status | Notes |
|------|--------|-------|
| Netchan format | Done | GENCHECKSUM removed to match QL packet header |
| Client message parsing | Done | Extra byte after 3 header longs in `SV_ExecuteClientMessage` |
| entityStateFields | Done | 58 entries, reordered, gravity as int32 |
| playerStateFields | Done | 58 entries, `pm_flags` 24-bit, `weaponPrimary`, 9 new QL fields |
| usercmd_t deltas | Done | Offsets 21-25 reordered, 2 generic byte fields added |
| Configstring flood fix | Done | Track `csUpdated[]` per client during `CS_PRIMED`, only resend changed |
| XOR netchan encoding | Done | `challenge ^ serverId ^ messageAcknowledge` key derivation |
| Huffman on OOB data | Done | `NET_OutOfBandData` compresses connect packets at byte 12 |

#### Client engine

| Area | Status | Notes |
|------|--------|-------|
| CG_REGISTER_CVARS call | Done | `VM_Call` before `CG_INIT` in `cl_cgame.c` |
| Connection protocol | Done | Steam auth in `getchallenge`, `NET_OutOfBandRaw` for binary payloads, `cl_steamId` cvar |
| DLL pak system | Done | Architecture-specific `iobin_x86`/`iobin_x86_64` pk3 with `Make-BinPk3.ps1`, pure server compatible |
| Sound system | Pending | QL sound system changes not yet audited |
| Demo recording/playback | Pending | Not yet tested with QL protocol |
| Download system | Pending | HTTP redirect and pk3 downloads not yet audited |
| VOIP / Steam voice | Pending | QL uses Steam P2P voice, not Q3 VOIP; current code is ioquake3 VOIP |
| Console auto-complete | Partial | Player-name completion wired for `kick`/`banUser`; QL-specific commands/cvars not yet registered for tab-complete |
| Screenshot system | Pending | QL screenshot path/naming conventions not audited |
| Client-side prediction | Pending | Prediction error handling for QL-specific player states (freeze, tutorial) not fully tested |

#### Server engine

| Area | Status | Notes |
|------|--------|-------|
| Snapshot system | Done | Entity/player state serialisation matches QL |
| Bot management | Done | `sv_bot.c` functional |
| Steam auth bypass | Done | `SV_ValidateSteamAuth` returns true unconditionally, so auth always passes with no configuration. (There is no `com_build` cvar in the tree - an earlier note here was wrong.) |
| Game module loading | Done | Native DLL loading with `gameImport_t` function pointer table |
| Ban system | Done | Address/CIDR ban list persisted to `sv_banFile`, loaded at startup. `banUser`, `banClient`, `banaddr`, `exceptaddr`, `bandel`, `exceptdel`, `banlist`, `flushbans`, `rehashbans` |
| ZMQ stats/rcon | Pending | QL uses ZeroMQ for remote console and stats publishing |
| Server browser protocol | Pending | Valve's Server Query Protocol needed for server list |
| Master server heartbeat | Pending | QL uses Steam master servers; needs custom implementation |
| Map download redirect | Pending | HTTP redirect for missing maps not implemented |
| Rate limiting | Pending | QL-specific flood protection tuning not audited |
| High player counts (32-48) | Partial | Hunk auto-sized from `sv_maxclients` (default 256 MB); snapshot-entity and reliable-buffer ceilings now report instead of failing silently. `MAX_SNAPSHOT_ENTITIES` (256) and `MAX_RELIABLE_COMMANDS` (64) are still hard caps - both are client-protocol constants |

#### cgame (client-side game module)

| Area | Status | Notes |
|------|--------|-------|
| Event system | Done | 100 `EV_` event handlers including race, infection, awards |
| Widescreen rendering | Done | `CG_AdjustFrom640` with `STRETCH`/`LEFT`/`CENTER`/`RIGHT` modes |
| Owner-draw text | Done | All ~70 owner-draw functions route through `CG_DrawText` |
| Serverinfo parsing | Done | 29 fields matching binary `CG_ParseServerinfo` |
| Scoreboard owner-draws | Done | Team scores, player counts, match state, round/overtime |
| Duel scoreboard | Done | Per-weapon stats for both players (frags/hits/shots/dmg/acc) |
| Voting display | Done | `ui_voteactive`, `ui_votestring`, end-of-match map voting |
| Weapon rendering | Done | All QL weapons including nailgun, chaingun, HMG, prox launcher |
| Grapple hook chain | Done | `RT_RAIL_CORE` rendering (single tiled quad) |
| Spectator tracking | Done | `cg_spectating` cvar follows `PM_SPECTATOR` transitions |
| Prediction/pmove | Done | 9 binary-verified fixes (freeze, dead float, hookEnemy, etc.) |
| Obituary feed | Done | Attacker/victim name rendering with weapon icons |
| Team overlay | Partial | Scrolling spectator list, team info. `clientInfo_t::curWeapon` is read by the overlay and the selected-player HUD but nothing ever writes it (no `tinfo` server command), so teammate weapon icons never draw |
| Impact sparks | Done | Configurable spark particle system on bullet/rail impacts. `CG_SpawnParticleEffect` was an empty stub until now, so nothing rendered; the spawner and its two shaders are implemented |
| Weapon styles | Done | Muzzle flash control, shotgun smoke, weapon render cvars |
| Vignette overlay | Done | Screen-edge darkening effect |
| Zoom system | Done | Toggle zoom, zoom scaling, zoomOutOnDeath |
| Player model scaling | Done | Bounding box scaling for player models |
| Spectator features | Done | Auto-follow, FOV sync, `cg_followPowerup` |
| Crosshair | Pending | QL crosshair set not fully verified |
| Awards/medals display | Done | `CG_DrawMedal`, dispatched across the medal owner-draw range |
| Damage direction indicator | Pending | Not yet verified against binary |
| Chat beep sounds | Pending | QL-specific chat notification sounds not verified |
| Warmup countdown display | Done | `CG_DrawWarmupCountdown` with per-gametype handling |
| Player clan tags | Pending | Clan tag rendering in scoreboard/nameplate not audited |
| Intermission camera | Pending | QL intermission camera behaviour not verified |

#### UI (user interface module)

| Area | Status | Notes |
|------|--------|-------|
| New exports | Done | `UI_REGISTER_CVARS`, `UI_CHECK_ACTIVE_MENU`, `UI_WALK_MENUS`, `UI_DRAW_ADVERTISEMENT` |
| fontIndex support | Done | All 26+ DC call sites pass `item->fontIndex` |
| Cvars | Done | ~78 new cvars registered in `ui_main.c` |
| Menu file loading | Done | `pak01.pk3` override system for custom menus/other assets |
| Non-team scoreboards | Done | Implemented and fixed up visuals to ensure associated `.menu` files process accurately |
| Server browser | Pending | Not yet adapted for QL master server protocol |
| Team scoreboards | Pending | Still a lot of stuff not correctly drawing here |
| UI script actions | Done | All 13 implemented in `UI_RunMenuScript`; every one now reaches a handler |
| Player profile display | Pending | Steam profile/avatar integration removed; needs replacement |
| Friend/social features | Pending | Friend invite, mute player, lobby system all stubbed |
| Settings menus | Pending | Some QL-specific settings panels may need menu file updates |
| Map voting UI | Pending | End-of-match map voting display driven by server but UI not fully tested |

#### Game module (server-side game logic)

| Area | Status | Notes |
|------|--------|-------|
| Game types | Done | FFA, Duel, TDM, CA, CTF, 1FCTF, Harvester, FT, DOM, A&D, Race, RR - per-gametype `g_gametype_*.c` source files |
| Physics (bg_pmove) | Done | CPM air control, double jump, crouch slide, auto-hop, ladder move |
| Grapple hook | Done | Wall/enemy pull, close-range slowdown, water damping |
| Weapon definitions | Done | All QL weapons with correct reload times |
| Key/door system | Done | `IT_KEY` type, silver/gold/master keys, `func_door` spawnflags |
| Warmup state machine | Done | `g_gameState` cvar, ready percentage, forfeit logic |
| Starting health/armor | Done | `g_startingHealth`, `g_startingHealthBonus`, `g_startingArmor` |
| Voting system | Done | `VF_ENDMAP_VOTING`, `IntermissionVote`, `nextmaps` parsing |
| Serverinfo cvars | Done | 8 new `CVAR_SERVERINFO` cvars |
| Bot loading | Done | `G_LoadBots`, `G_LoadBotsFromFile`, `G_ParseInfos` |
| CVAR table | Done | ~390 cvars with 56 `OnChanged` callbacks |
| Race checkpoints | Done | Race gametype module with init, checkpoint, and timing logic |
| Unlagged (`lagHax`) | Done | Position history recording/rewinding for hitscan accuracy (`g_unlagged.c`) |
| Weapon systems | Done | Shotgun ring pellets, distance falloff, damage-through-surface, player cylinder traces. Server `ShotgunPattern` now applies the same seeded jitter as `CG_ShotgunPattern` - it previously ignored the transmitted seed, so traced pellets did not match drawn ones |
| Tiered armor | Done | `CheckArmor` rewrite, `Pickup_Armor` with armor tiers |
| JSON stats reporting | Partial | Original uses C++ jsoncpp; JSON functions stubbed, non-JSON helpers preserved |
| Loadout system | Done | End to end: `weaponPrimary` validation, `PMF_LOADOUT_FORCED`, default-weapon fallback |
| Factories | Pending | **Not started.** Only `g_factory`/`g_factoryTitle` (read-only cvars) and five console commands that print "not yet implemented"; no factory file is ever opened |
| Premium/subscription | N/A | No premium checks remain in the tree; nothing to remove |
| Access control lists | Done | `g_accessFile` parsed into a Steam-ID table; connect-time ban check and privilege seeding both consult it |
| Admin commands | Done | Privilege gate plus put/mute/lock/ban/promote. `ban` records a real address ban; `/lock` blocks team joins |
| Training mode | Pending | `g_training` cvar checked in multiple places; training mode logic not audited |
| Map entities override | Done | `sv_altEntDir`; spawns from `<dir>/<mapname>.ent` when present, else the .bsp lump. Server-side only |

#### Renderer

| Area | Status | Notes |
|------|--------|-------|
| RT_RAIL_CORE | Done | Rendering case for grapple hook chain |
| QL-specific shaders | Pending | Shader keywords not fully audited |
| Freeze/thaw effects | Done | `CG_FreezeEffect`: LE_FREEZE entity, freeze model, scale-fade, freeze sound |
| Infection visuals | Pending | Red Rover infection visual effects not implemented |
| Post-processing (bloom) | Pending | Bloom is partially implemented in opengl2 but not QL-tuned |
| Advertisement rendering | Pending | `UI_DRAW_ADVERTISEMENT` export exists but ad billboard rendering not implemented |
| Damage plum rendering | Done | Shared floating-effect pool (damage numbers, outlines, freeze/flag glows), screen-projected in one pass |

## Building

### Windows (Visual Studio)

```
cd misc\msvc142
MSBuild ioquakelive.sln -p:Configuration=Debug -p:Platform=Win32
```

Individual projects: `cgame.vcxproj`, `ui.vcxproj`, `qagame.vcxproj`, `quakelive.vcxproj`, `opengl2.vcxproj`

**Toolset:** v145 (VS 2022+), **Platform:** Win32/x64

### Getting the source

**Clone with submodules.** SDL2, libogg, libvorbis, curl, opus and opusfile are
git submodules; a plain `git clone` leaves those directories empty and the build
stops with `No rule to make target '.../bitwise.o'` (that file lives in
`code/libogg`). Nothing is missing from the repository when this happens — the
submodules simply have not been fetched.

```
git clone --recurse-submodules <url>
```

Already cloned without them:

```
git submodule update --init --recursive
```

### Linux / macOS

Standard ioquake3 Makefile build:

```
make release -j$(nproc)
```

Builds clean on Linux x86_64 (needs `libsdl2-dev`). This produces the engine,
the dedicated server, the OpenGL2 renderer and `baseq3/iobin.pk3`.

**The Vulkan renderer is opt-in** — `BUILD_RENDERER_VULKAN` defaults to 0, so an
ordinary `make` does not build it and you get no `vulkanx86_64.so`:

```
make BUILD_RENDERER_VULKAN=1 -j$(nproc)
```

`package-release.sh` always passes it, which is why release archives ship the
Vulkan renderer even though a default build does not produce one.

Running the game still requires a legitimate `pak00.pk3`; the dedicated server
binary starts and accepts console commands without one, but cannot load a map.

## Directory Layout

```
code/
  cgame/          Client-side game module (HUD, scoreboard, effects, prediction)
  game/           Server-side game module (game logic, entities, physics)
  ui/             User interface module (menus, server browser)
  client/         Engine client code
  server/         Engine server code
  qcommon/        Shared engine code (networking, filesystem, commands)
  renderercommon/  Shared renderer code
  renderer_opengl2/ OpenGL 2 renderer
misc/
  msvc142/        Visual Studio solution and project files
pak01_content/    Asset overrides (menus, shaders, models) → pak01.pk3
```

## Ancestry

This project is a fork of [ioquake3](https://github.com/ioquake/ioq3) and retains its GPL v2 license. The original ioquake3 README is preserved below.

---

<details>
<summary>Original ioquake3 README</summary>

![Build](https://github.com/ioquake/ioq3/workflows/Build/badge.svg)

                   ,---------------------------------------.
                   |   _                     _       ____  |
                   |  (_)___  __ _ _  _ __ _| |_____|__ /  |
                   |  | / _ \/ _` | || / _` | / / -_)|_ \  |
                   |  |_\___/\__, |\_,_\__,_|_\_\___|___/  |
                   |            |_|                        |
                   |                                       |
                   `--------- https://ioquake3.org --------'

The intent of this project is to provide a baseline Quake 3 which may be used
for further development and baseq3 fun.
Some of the major features currently implemented are:

  * SDL 2 backend
  * OpenAL sound API support (multiple speaker support and better sound
    quality)
  * Full x86_64 support on Linux
  * VoIP support, both in-game and external support through Mumble.
  * MinGW compilation support on Windows and cross compilation support on Linux
  * AVI video capture of demos
  * Much improved console autocompletion
  * Persistent console history
  * Colorized terminal output
  * Optional Ogg Vorbis support
  * Much improved QVM tools
  * Support for various esoteric operating systems
  * cl_guid support
  * HTTP/FTP download redirection (using cURL)
  * Multiuser support on Windows systems (user specific game data
    is stored in "%APPDATA%\Quake3")
  * PNG support
  * Web support via Emscripten
  * Many, many bug fixes

The map editor and associated compiling tools are not included. We suggest you
use a modern copy from http://icculus.org/gtkradiant/.

The original id software readme that accompanied the Q3 source release has been
renamed to id-readme.txt so as to prevent confusion. Please refer to the
website for updated status.

More documentation including a Player's Guide and Sysadmin Guide are on:
https://ioquake3.org/help/

If you've got issues that you aren't sure are worth filing as bugs, or just
want to chat, please visit our forums:
https://discourse.ioquake.org

# Credits

### Quake III Arena and Quake Live

**id Software** — the game, its engine and its assets.
<https://github.com/id-Software/Quake-III-Arena>

Quake III Arena's engine was released under the GPL (see `COPYING.txt`); the
assets were not, and are never redistributed by this project. Quake Live and Quake III Arena are
trademarks of id Software LLC, a ZeniMax Media company. This project is not
affiliated with or endorsed by them.

### ioquakelive

**tjone270** — the Quake Live protocol, game, cgame and ui work this is built
on. <https://github.com/tjone270/ioquakelive>

### Quake Live Ex

**Jonathan "Warboy" Imler**

### ioquake3

<https://github.com/ioquake/ioq3> · <https://ioquake3.org/>

Maintainers

  * James Canete <use.less01@gmail.com> — [@SmileTheory](https://github.com/SmileTheory)
  * Ludwig Nussel <ludwig.nussel@suse.de> — [@lnussel](https://github.com/lnussel)
  * Thilo Schulz <arny@ats.s.bawue.de>
  * Tim Angus <tim@ngus.net> — [@timangus](https://github.com/timangus)
  * Tony J. White <tjw@tjw.org>
  * Jack Slater <jack@ioquake.org>
  * Zack Middleton <zturtleman@gmail.com> — [@zturtleman](https://github.com/zturtleman)

Handles are given only where corroborated rather than recalled; the
[contributors graph](https://github.com/ioquake/ioq3/graphs/contributors) is the
authority, and the names above without one are not omissions.

Significant contributions from

  * Ryan C. Gordon <icculus@icculus.org> — [@icculus](https://github.com/icculus)
  * Andreas Kohn <andreas@syndrom23.de>
  * Joerg Dietrich <Dietrich_Joerg@t-online.de>
  * Stuart Dalton <badcdev@gmail.com>
  * Vincent S. Cojot <vincent at cojot dot name>
  * optical <alex@rigbo.se>
  * Aaron Gyes <floam@aaron.gy>

</details>
