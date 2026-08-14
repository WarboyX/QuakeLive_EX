# v2026.08.14

First merged release. Binaries are build `1a71db4`; main is `82f2c2e`. The only
difference between them is vendored Vulkan headers that nothing includes, so
these archives are what this commit builds.

**Requires Quake Live.** `pak00.pk3` from a Steam install has to be present —
it is not redistributed here and never will be. Drop the archive contents
alongside it, or point `fs_basepath` at your Quake Live directory.

| | |
|---|---|
| `quakelive-windows-x64-1a71db4.zip` | Windows x64 client + dedicated server |
| `quakelive-linux-x86_64-1a71db4.zip` | Linux x86_64 client + dedicated server |

Both contain `baseq3/iobin.pk3` (cgame, qagame, ui for every platform, so one
pak serves a Linux server and its Windows clients under the same `sv_pure`
checksum), `baseq3/pak01.pk3`, ready-to-run server configs, and
`server.cfg.example`.

## Confirmed working in play

- **The client no longer dies when a server terminates.** `VM_Free` was
  unmapping the game module while its own stack frames were still live; on
  Windows x64 `longjmp` then unwound through SEH into an unmapped image, so the
  process died instantly with nothing in the log.
- **The railgun draws one beam.** `CG_RailTrail` was handing the rings half of
  a shot an origin the core half had already moved.
- **Server browser, join server and new game menus.** All three were broken by
  one parser bug: `ItemParse_cvarFloatList` consumed the menu's closing brace on
  any list containing a negative value, merging two menu definitions into one.
  It broke Quake Live's own `ingame_callvote.menu` too.
- **View weapon framing on widescreen.** `cg_fov` is a horizontal field of
  view, so a wider display shows the same width over less height — at 16:9 the
  vertical FOV is a quarter narrower than the 4:3 the viewmodel maths assumed,
  and most weapon tags sit behind the eye, so the first 10–27 units of each
  model were off the bottom edge. `cg_gunAspect` corrects it from the aspect
  ratio; `cg_gunAspect 0` restores the old framing.
- **Build stamps.** The main menu shows which `pak01` and which `iobin` are
  loaded, so a screenshot identifies the build.

## Shipped, not yet confirmed

- LAN server discovery, master server queries, dedicated-server heartbeats.
  `localservers` and `globalservers` previously existed as an unregistered
  function and an undefined one, so no source could ever return anything. Set
  `sv_master1` to reach anything beyond LAN — Quake Live's own master is gone
  and there is no honest default.
- `g_autoJoin` (default on): connecting players enter the match instead of
  spectating. Duel keeps its play queue.
- Instagib single-weapon lockout, and switchable server configs.
- Phantom pickup sound over items already taken.

## Known issues

`TRACKER.md` has all 42 items, each tagged with which of our binaries the fault
lives in and which client actually sees it. Two worth knowing before you run a
server:

- **The shotgun runs in stock-compatible mode on purpose.** The improved
  pattern and basis are written and tested but default to **off**, because they
  are correct on this client and wrong on stock Steam Quake Live — the server
  sends a seed and each client regenerates the pattern locally, and a stock
  client cannot be told to follow. `g_shotgunBasis 1` / `g_shotgunPattern 1`
  enable them if you are not serving stock clients.
- **186 cvars are registered but read by nothing** (`tools/dead-cvars.py`).
  Setting one produces no error and no effect. Includes `g_powerupRespawn`,
  `g_allTalk`, the `g_shuffle_*` group, `cg_itemTimers` and the
  `cg_drawTeamOverlay*` group.

The Vulkan renderer is vendored but not wired up — `BUILD_RENDERER_VULKAN` is
absent from the Makefile, so nothing builds it and it cannot affect this
release.

## Credits

Based on [tjone270/ioquakelive](https://github.com/tjone270/ioquakelive), itself
derived from ioquake3 (GPLv2). `code/renderervk` is vendored from
[ec-/Quake3e](https://github.com/ec-/Quake3e) (GPLv2). Vulkan headers are
Khronos, Apache-2.0. Quake Live is a trademark of id Software LLC.
