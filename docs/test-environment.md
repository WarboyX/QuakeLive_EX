# Reusable test environment

The isolated workspace can now run the actual Linux dedicated server,
native qagame, botlib, physics, damage and scoring. No human client is needed.
An authored collision arena, three-cell navigation file and minimal bot
definitions replace the missing original data for these tests. This is a
headless integration fixture, not a recreation of a production map.

## Run it

From the project root, after building Linux as described in README.md:

```sh
python3 tools/test-environment/verify.py
python3 tools/test-environment/run.py --mode ffa --seconds 120 --timescale 1
python3 tools/test-environment/run.py --mode tdm --bots 4 --seconds 120
python3 tools/test-environment/run.py --warmup --seconds 45
```

The runner requires Python 3, a host C compiler and the Linux dedicated server
and qagame in `build/release-linux-x86_64`. Use `--build /absolute/build/path`
to select another matching build. It needs the `botscores` diagnostic supplied
by the bot-performance follow-up. The checksum helper compiles the project's
MD4 implementation so the generated BSP and AAS checksums agree.

`--root /absolute/test/path` chooses where runs live. Each run has a unique
directory and private engine, paks, home and extracted game module. The server
binds to loopback, receives commands through its stdin, and quits cleanly.
Concurrent runs require separate `--port` values. The runner never deletes an
earlier run. Default output `test-environment/` is gitignored.

Bots are admitted during warmup before the measured test starts. The runner
then sets **`g_doWarmup 0`** and waits for a live match with the requested
playing population. This avoids starting a round with only the first bot.
The warmup control leaves it at 1 and never readies a human. Every control and
runtime command is retained in `launch.json` and `commands.json`.

`--timescale 4` accelerates functional testing. Use **1**, identical populations,
settings and maps for CPU comparisons; accelerated runs are not performance
benchmarks. The authored bots only know the gauntlet and machinegun and the map
only has three navigation cells. A 30v30 production performance claim needs
representative maps and normal time.

## Results saved per run

- `server.log`: engine, game, bot tracks, scores and CPU phase timings.
- `home/baseq3/fixture-game.log`: game events, kills and match transitions.
- `launch.json`: settings and hashes of the selected engine and qagame.
- `commands.json`: exact console commands, including the warmup bypass.
- `result.json`: initial/final scores, population, movements, kills, captures,
  observed round states, status and failures.
- `server/baseq3/fixture-manifest.json`: generated map checksum and fixture scope.

The FFA/TDM check requires real kills and increasing scores over elapsed game
time. Warmup requires zero scores despite combat. Objective modes use
`--require-score` to require team-score progress. Spectator demotions,
forfeits, lost population and failed prerequisites fail the run and return a
nonzero exit code. `verify.py` combines FFA, warmup, TDM and CTF and saves a
suite report.

The per-run file named `pak00.pk3` contains only authored startup defaults; it
is clearly marked as a fixture. It contains no original Quake Live data.
Do not copy it into a real installation. Generated BSP/AAS files exercise
collision and navigation; they contain no render surfaces, player models,
sounds or production materials.

## Heat maps

```sh
python3 tools/test-environment/heatmap.py <map>.bsp <run dir> <out prefix> "title"
```

Four panels over the map's floors, from a run's `bottrack`/`ctftrack` lines:
team presence, combat, deaths and flag carriers' paths. With
`--set bot_tacticsTeams=1` (or 2) on the run, one team is our tactical layer
and the other stock, so the panels compare them in the same match.

## Shadow checks

```sh
python3 tools/test-environment/run-shadow.py --software
python3 tools/test-environment/verify.py --graphics --software
```

The shadow runner wraps the **current tree's complete shadow shader** as
compute, recompiles it, validates SPIR-V and runs synthetic geometry/light
cases through Vulkan with Khronos validation enabled. It records the shader
hash, build commands, outputs and errors in a new graphics run directory.
It requires a C++17 compiler, GLM headers, Vulkan headers/loader, validation
layers, `glslangValidator` and `spirv-val`. It detects the already-installed
workspace SDK, or uses normal host tools. `--software` chooses Mesa lavapipe;
without it, the host driver is used.

These shader checks do not test the game's world/mesh submission, ownership,
overflow preflight, the GPU's fast-update path or production screenshots.
This workspace uses software Vulkan. Real-map and NVIDIA checks remain separate.

## Observed results on 387d68a6-bot1

| Test | Result |
|---|---|
| FFA, two bots, 120 simulated seconds | Pass: 13 kills, final scores 6 and 7, both bots moved |
| FFA warmup, two bots, 45 simulated seconds | Pass: five kills, both scores stayed zero |
| TDM, four bots, 120 simulated seconds | Pass: 31 kills and increasing individual/team scores |
| CTF, four bots, 120 simulated seconds | Pass: 33 kills, team capture scores reached red 8 / blue 9 |
| Full shadow shader, software Vulkan | Pass: 13 fixture views, zero validation errors |
| CA, four bots, staged admission, normal/zero round delay | Fail: bots become permanent spectators and match forfeits |

These are actual game runs on authored data, not mocked calls to AddScore.
Raw logs and JSON reports retain the precise outcomes. They do not establish
whether the user's production-map scoring stall has the same cause.

## Reproduced round failure

```sh
python3 tools/test-environment/run.py --mode ca --bots 4 --round-delay 0 --require-score --seconds 90
python3 tools/test-environment/run.py --mode ca --bots 4 --round-delay 1000 --require-score --seconds 90
python3 tools/test-environment/verify.py --rounds
```

These tests currently fail; do not turn their exit codes into passes.
The staged logs show four bots playing on red/blue in warmup, followed by bots
moving to team 3 (spectator), an empty team and automatic forfeit after the
`g_doWarmup 0` transition. With zero round delay this happens during the initial
restart/begin sequence. With a nonzero delay it also happens during round play.

The shared CA/AD functions `ClientBegin_RoundBased` and
`ClientBegin_RoundBased_impl` in `g_gametype_common.c` use `Cmd_FollowCycle_f`.
That calls `G_FollowCycle(..., qfalse)`, which calls `SetTeam(..., "spectator")`.
`G_FollowCycleKeepTeam` already exists and is used by Freeze Tag. This is a
concrete candidate fix, followed by verifying restart ordering and respawn
state. No game-rule fix is included in the environment setup.


## Japanese Castles: 15v15 and carrier analysis

Prepare the private map/definitions with `prepare-map-assets.py` as described
in `ctf-ai-review.md`. Use `run.py --mode ctf --map japanesecastles --bots 30
--seconds 900 --timescale 4 --require-score --diagnostic-interval 15`, passing
the original botfiles ZIP and the two prepared PK3s with `--asset-pk3`, and
`--characters anarki,bitterman,visor,sarge,major,hunter,keel,slash`. The runner
sets `g_doWarmup 0` after admission. Choose `--timescale 1` for normal speed;
accelerated trials are not CPU comparisons. Each run keeps its own binaries.

`python3 tools/test-environment/analyze-ctf.py <run-folder> --output summary.json`
records authoritative team scores, live-window event counts, sampled carrier
intervals and diagnostic escort maxima. It cuts by the initial/final score
reports' positions in the log so warmup restarts and shutdown/polling tails do
not inflate the measured window. The legacy runner event fields in old results
can include a shutdown tail; the retained analyzer output supplies corrected
window counts. Full planner results: `ctf-planner-follow-up.md`. Latest escort-routing trials
and normal-speed results: `ctf-escort-follow-up.md`. Navigation-ranked selection
and the latest comparisons: `ctf-navigation-follow-up.md`.

In ctf8, enabled `bot_debugTrack` additionally emits `ctftrack <ms> <slot>
hp<health> flag<carrying> mate<followed-client> via<pending-waypoint-area>
home<direct-home-travel-time> node<retreat/item/fight/other>`. These records
distinguish actual possession from a stale job. The analyzer reports nearby
accompany-job samples as a geometric escort proxy (stale follows can count),
and summarizes ctf8 carrier node/route progress. Tracking adds routing/log work;
these traced overlapping matches are not performance benchmarks.

In ctf9, `linked_escort_trace` restricts node/proximity counts to the actual
followed same-team flag carrier, using preceding samples no older than 1.1s.
Future samples and known dead or former carriers cannot prove an escort link.
Between-sample changes remain uncertain; this is not a protection metric.

Latest escort supply limits and detailed original-build handoff: `bot-ai-changes-ctf10.md`.
Linked escort analysis also separates health above 40 from at most 40.
The retained nodeitem field labels Battle_NBG only; Seek_NBG is inside nodeother.

Latest carrier route-selection fixes and detailed original-build handoff: `bot-ai-changes-ctf11.md`.
Linked escort analysis also separates health above 40 from at most 40.
The retained nodeitem field labels Battle_NBG only; Seek_NBG is inside nodeother.

## ctf16 route-risk candidate

See `bot-ai-changes-ctf16.md` for exact original-relative source/asset comparison.
Actual normal-speed 15v15 Japanese Castles matches compare the candidate with
recovered ctf11; the final evidence records every match, including no-score
failures. The ctf12–15 scratch checkout disappeared during tests; those interrupted
results are not available as reproduced evidence. The saved ctf11 kit is the
recovered baseline. Before copying a native qagame, the runner now rejects
empty object files and resolves its imports and dllEntry export. Run
`python3 tools/check-native-game-modules.py build/release-linux-x86_64` to
check all three game modules after building.
