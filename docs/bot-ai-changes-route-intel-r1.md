# Route Intel R1 — team reports for carrier path choices

## Scope and baseline

**Lives in:** server qagame. **Seen by:** every client observing bots.
This is the first shared-report routing implementation, on AimSweep A1; aiming stays unchanged.
Local preceding source commit: `c8abbb847fd47c9acda6ac2f4f97521261442ed4`.
Latest supplied source archive is `QuakeLive_EX-development(3).zip`, SHA256 `755b2471f298f72d70373ddd7c30f3a6cf5fe2cf0f807b950e67d1e95535f4aa` (contains E226).
Original cumulative baseline: `QuakeLive_EX-development(2).zip`, SHA256 `fc98ee0f673227de3e75c40709ccdae550f7d4ae740f0c5d077b67db21cc6f69`.
Original botfiles SHA256 `613475b275f112501c3de1cef562ae3d4d114a414b7e23a2a1aa96a7d0210a67`: all 151 entries unchanged. No character, weight, chat, BSP or AAS edits. No remote branch or main change.

A1 qagame tested: `2994a6a57632e6b85908a7ad6671fb0af5c81b1979ff3cccf1d66c4d49da3c2e`.
R1 qagame tested: `cd7b9e6e69a0e872991c0f81d212e5de84a233f498b79230889cbc8741cc2711`.
Both groups use the same previously tested A1 dedicated executable: `31b4664622951d39eabca98ce83d0e3060927197304497ea35e10d6d098f1a86`.

## Incremental changes from A1

| File / function | Before | After |
|---|---|---|
| `ai_tactics.c`, `BotCountNearby` | Visible foes affected local combat posture only. | Publishes their observed positions after the existing range/visibility checks. No additional visibility trace is performed. Existing 360-degree awareness semantics are preserved. |
| `ai_tactics.c`, `BotRouteReportSight` (new) | No shared sighting memory for routes. | Separate red/blue arrays keyed by enemy slot, storing last-reported room, time and one unit of evidence. Multiple observers refresh one enemy, rather than multiplying it. New observed positions replace old ones. |
| `g_combat.c`, `G_Damage`; `g_local.h` declaration | No route warning from confirmed damage. | Before applying health damage, report a living bot's position for actual enemy damage >=5, or any positive lethal hit. Reject self/friendly/world damage. The API receives no attacker location or identity. Armour-only hits do not produce this incident report. |
| `ai_tactics.c`, `BotRouteReportDamage` (new) | No incident memory. | Latest room per victim; damage weight 1, lethal weight 2. Refreshes rather than accumulates. Multiple incidents in a room use the strongest remaining one. Bots-only automatic reports; no simulated messages attributed to human teammates. |
| `BotRouteRadioTeam` (new) | — | Validate CTF, live client identity and existing tactical/per-team enable mask. Collection and consumption both honor team policy. |
| `BotRouteReportWeight`, `BotRouteKnownDanger` (new) | Current global enemy census. | Eight-second linear evidence decay. Sum distinct sighted enemies in a room, take the maximum of that and incident evidence, cap at four risk units, round to integer units for existing cost function. Unknown space has no evidence penalty; zero does not mean verified safe. |
| `BotCTFRouteThreat` | Maximum current enemy count along predicted route. | Maximum shared-report risk along the same route. Existing start-room exclusion, endpoint check, 128-transition bound and no-progress termination remain. |
| `BotRoomsReset`, `BotRouteIntelReset` | Reset existing room state only. | Also clears all team evidence, risk caches and counters on map setup. Negative report age is ignored. |
| `BotTacticsReport`, `BotRouteIntelReport` | Existing bot diagnostics. | `bots` reports cumulative per-team sightings, incidents and risk lookups. Lookups are not distinct path decisions; route sampling may perform many. |
| `tools/test-route-intel.py` (new), existing route fixture, `tools/validate.sh` | Old global-census route fixture. | Actual-C memory and carrier-cost regressions; common validator entry point includes new test. |

Risk is recomputed per team at most once per game frame unless a new report invalidates it. The arrays are bounded by MAX_CLIENTS and MAX_ROOMS. Room assignment still uses the existing nearest named-location mapping. This is not an AAS-edge or visibility-volume threat map.

No new cvars/default changes. The existing 300 travel-time-unit penalty per risk unit, 1.5× direct-travel detour limit plus 100 units, progress constraints and route-switch hysteresis remain. The new four-unit risk cap bounds the evidence penalty to 1200 travel-time units; it does not override the existing physical detour limit.

## What remains outside this pass

Only CTF carrier route threat consumes this memory. Other uses of `BotRoomEnemies`, defender decisions, attack coordination and the global room census remain unchanged. Clear-space reports do not actively erase sightings yet: evidence expires. No new reconnaissance policy, confidence negotiation, directional corridor hazard or explicit radio delay is implemented. Reports are shared immediately in the server AI, without team-chat spam. Strength currently means distinct sightings/incident severity, not observed weapon/armour classification.

Garden candidate coverage is not established. The Japanese Castles BSP has 85 named locations, including both gardens, garden halls and back halls. Runtime generates 15 alternate places toward red and 18 toward blue. These counts do not prove a useful garden candidate is available from each carrier position. No candidate generation was changed, so a missing garden route cannot be fixed by this scorer alone.

## Verification performed

- Linux dedicated/native modules built with `make release -j2 BUILD_CLIENT=0 BUILD_RENDERER_OPENGL2=0`. Native entry points/imports resolved; 151 objects passed the header freshness check.
- Full `tools/validate.sh` passed. Updated real carrier-cost fixture also passed after the full suite completed.
- Actual C tests cover teammate sharing, enemy-team isolation, enemy deduplication/relocation, decay/expiry, map reset, future-timestamp rejection, disabled policy, bounded repeated damage/deaths, and avoiding sighting/incident double-counting.
- Actual `BotCTFCarrierRouteCost` plus route threat are exercised: a 400-unit direct route through reported enemies loses to a safe 500-unit detour; after expiry, the direct route becomes cheaper again. This proves the controlled cost comparison, not a live match path change.
- Production hook placement/eligibility is inspected by source assertions; the fixture does not simulate the entire G_Damage function or real rendering visibility.
- First fixture compile failed because its stub used the typed AAS pointer rather than the public void-pointer signature; corrected fixture and failed log retained.
- First build failed with three zero-byte botlib objects (`l_precomp.o`, `l_struct.o`, `be_aas_route.o`). After make exited, those outputs were deleted and rebuilt. Failed log retained; no failed binary was tested.
- No Windows, graphical client, per-frame spectator or Vulkan testing claimed.

## Every match

Japanese Castles, 15v15, skill 5, 180 requested live seconds, `timescale 1`; `g_doWarmup 0` bypass; `bot_aimSweep=1`, `bot_aimDrift=1`, `bot_challenge=0`.
Baseline here means **A1**, not E226. Candidate means **R1 on A1**. Each version runs tactical red and tactical blue against the common layer-off opponent. These four short unseeded runs are integration checks, not statistical evidence of better capture rate.

| Build / tactical side | Live seconds | Red–blue captures | Status |
|---|---:|---:|---|
| baseline-blue | 180.28 | 0–1 | PASS; no reported errors |
| baseline-red | 180.28 | 1–0 | PASS; no reported errors |
| candidate-blue | 180.28 | 1–0 | PASS; no reported errors |
| candidate-red | 180.28 | 0–1 | PASS; no reported errors |

The A1 tactical teams won both short games (2–0 combined captures); R1 tactical teams lost both (0–2). This is adverse preliminary evidence, not proof of a regression from two games. Do not promote R1 as a strength improvement or merge it by default on this evidence.

Raw commands, launch hashes, snapshots and every game log are included. `radio-counters.json` records the final diagnostic counters: enabled teams both published evidence and queried it; disabled teams have no reports/queries. These counts prove the memory path runs in real gameplay, but do not establish that a particular live route was selected because of a report. Death-versus-nonfatal incident counters are not separated. Matches are too short to infer capture improvement.

## Original-upload comparison

The cumulative runtime patch includes inherited A1 and earlier CTF changes. It is not a second patch to apply on A1. The manifest records original/current per-file hashes and function changes; the detailed A1 report accompanies this handoff.

| Runtime file | Cumulative lines from original upload |
|---|---:|
| `code/game/ai_dmnet.c` | +45/−51 |
| `code/game/ai_dmq3.c` | +215/−19 |
| `code/game/ai_main.c` | +190/−264 |
| `code/game/ai_main.h` | +4/−5 |
| `code/game/ai_tactics.c` | +738/−228 |
| `code/game/ai_tactics.h` | +18/−1 |
| `code/game/ai_team.c` | +1/−0 |
| `code/game/g_combat.c` | +6/−0 |
| `code/game/g_gametype_ca.c` | +8/−2 |
| `code/game/g_gametype_common.c` | +8/−2 |
| `code/game/g_local.h` | +7/−0 |
| `code/game/g_main.c` | +22/−0 |
| `code/game/g_svcmds.c` | +26/−0 |
| `code/qcommon/common.c` | +10/−0 |
| `code/qcommon/q_shared.h` | +8/−0 |
| `code/qcommon/qcommon.h` | +1/−0 |
| `code/server/sv_game.c` | +42/−0 |
| `code/server/sv_main.c` | +5/−6 |

## Integrate / roll back

Apply `route-intel-r1-from-a1.patch` after `git apply --check` on an A1-equivalent development tree. For an E226-only tree, apply/review A1 first. Use the original-relative patch only against the recorded original archive. Resolve coworker changes by function rather than replacing newer files wholesale.

Rebuild every qagame dependency after the public header edit; run native/stale guards and validators. Test per-team policy and map restart. Reverse the incremental patch and rebuild to recover exact A1 routing. Disabling bot_tactics also removes other tactical behaviors and is not a precise rollback.

This is a reviewable first implementation. The next justified check is live route-decision telemetry and garden candidate coverage, followed by longer side-swapped tests before drawing strength conclusions.
