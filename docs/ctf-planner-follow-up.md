# CTF planner follow-up: 387d68a6-ctf7

Latest escort-path experiment and fresh normal-speed evidence: [ctf-escort-follow-up.md](ctf-escort-follow-up.md).

Experimental local server AI work over the saved `387d68a6-ctf3` variant of
upstream `387d68a61ed3c5d3a36cb5f956e722208354dafc`. Not pushed upstream.
The earlier source/build kit remains a separate baseline.

## Changes verified in source and compiled fixtures

- One autonomous assignment path runs before combat every bot think. Bots could
  previously switch to combat before reaching team decisions, and the retreat
  path updated only carriers. Bot-leader CTF voice orders, the offense enforcer,
  automatic base defense and the missing-flag goal handler no longer write
  competing autonomous assignments with `bot_tactics 1`.
- Recovery is an explicit role. The census also counts carriers and intentional
  roamers correctly, including a human carrier. Missing flags prioritize
  recovery rather than parking most of the team on an empty stand. Flag-state
  changes invalidate role cooldowns; obsolete recovery ends when the flag returns.
- Travelling escorts are selected from nearby available bots: one on teams up
  to four, two up to eight, four on larger teams. A home carrier waiting for its
  own flag keeps one/two guards. Explicit player orders remain in force and
  occupy slots; completed orders cannot block planning forever.
- Staggered thinks allowed a closer replacement to start following before the
  displaced escort reconsidered. An actual ctf6 diagnostic recorded five
  escorts against a four-slot target. ctf7 makes a replacement wait for a slot.
  Stale/non-carrier follows are not reported as active flag escorts.
- Return-route scoring queries travel in the direction the carrier actually
  walks, rejects unreachable/backward waypoints, limits detours to 1.5 times
  direct travel plus 100 AAS units, and combines total travel with room threat.
  A small preference for the existing route avoids needless switching. Room
  threat uses the existing room census; this is not a new perception system.
- Exact-source fixtures cover both teams, flag transitions, recovery sightings,
  expiration, player orders, escort replacement, role census, route direction,
  reachability and the combat dispatch. The tactical switch remains the control
  for the old assignment behavior. The return-route correction is shared by CTF
  carriers; tactics 0 is not a byte-identical control for this route change.

## Runtime method and limitations

Actual dedicated Linux server, native qagame and botlib, supplied Japanese
Castles BSP/AAS and original bot definitions. No human client. Full teams are
admitted before setting `g_doWarmup 0`; warmup is a separate negative control.
Standard weapons, `sv_fps 40`, unlimited score/time limits, requested skill 10
(the game's 1–10 scale maps this to botlib skill 5). The 15v15 and 30v30 tests
use timescale 4; the 4v4 comparisons use normal speed. Some runs overlap.
These are behavioral tests, not normal-speed CPU benchmarks or evidence of
unchanged accelerated physics. Characters/spawn/combat sampling are stochastic.

Team-score reports are the capture result. The analyzer cuts logs between the
initial and final measured score reports; an additional capture in one baseline
shutdown/polling tail is excluded. Old runner JSON event/kill totals can include
that tail. Position samples are one second apart; reported carrier intervals
measure observed LTG_RUSHBASE time, not exact possession duration, and exclude
unfinished intervals. Earlier role diagnostics omit their representative bot;
the new diagnostics include it.

## Results

The final results table is generated from retained result/launch JSONs. Every
completed and failed run from this follow-up is retained in the evidence kit.
The ctf4 and ctf5 drafts did not yet run the planner before every combat think;
ctf6 includes that integration; ctf7 additionally fixes escort replacement.

| Run | Players | Simulated minutes / speed | Captures red / blue | Sampled carrier median (s) | Escort diagnostic max red / blue |
|---|---:|---|---:|---:|---:|
| baseline-15v15 | 15v15 | 15 / 4x | 2 / 2 | 7.0 | 8 / 10 |
| baseline-15v15-repeat | 15v15 | 15 / 4x | 3 / 1 | 10.5 | 6 / 9 |
| baseline-large | 30v30 | 15 / 4x | 0 / 0 | 9.0 | 6 / 7 |
| baseline-small | 4v4 | 5 / 1x | 1 / 1 | 13.5 | 1 / 2 |
| ctf4-large | 30v30 | 15 / 4x | 0 / 0 | 7.0 | 0 / 3 |
| ctf4-small | 4v4 | 5 / 1x | 3 / 1 | 25.4875 | 1 / 1 |
| ctf5-15v15 | 15v15 | 15 / 4x | 1 / 1 | 9.5 | 1 / 3 |
| ctf5-small | 4v4 | 5 / 1x | 2 / 2 | 18.7 | 1 / 1 |
| ctf6-15v15 | 15v15 | 15 / 4x | 1 / 2 | 10.0 | 5 / 4 |
| ctf6-15v15-repeat | 15v15 | 15 / 4x | 5 / 0 | 9.0 | 4 / 4 |
| ctf6-small | 4v4 | 5 / 1x | 0 / 1 | 12.475 | 1 / 1 |
| ctf6-stock-control | 4v4 | 5 / 4x | 0 / 1 | 19.0 | – / – |
| ctf7-15v15 | 15v15 | 15 / 4x | 3 / 0 | 7.0 | 4 / 4 |
| ctf7-15v15-repeat | 15v15 | 15 / 4x | 0 / 2 | 7.0 | 4 / 4 |

The final ctf7 15v15 trials scored **red 3 / blue 0** and **red 0 / blue 2**,
versus **2 / 2** and **3 / 1** on the ctf3 baseline. That is **five captures
versus eight** across two matches each. The earlier ctf6 pair totalled eight;
its escort replacement overflow was not yet fixed. These results do not show
a dependable capture-rate improvement, and the final trial has a lower total.
Keep ctf3 as the preferred baseline while further work is measured. ctf7 is a
reviewable coordination experiment, not a recommended release replacement.
Both final 15v15 runs passed the objective-progress check. Periodic diagnostics
recorded no active flag-escort count above four on either team; this sampling
is not continuous proof for every frame. Recovery is visible as its own role.

Final ctf7 authored FFA: 25 kills in 60 simulated seconds, movement and score
progress passed. Final Japanese Castles warmup control: four kills in 20
simulated seconds, individual/team scores remained zero. The earlier ctf6
4v4 normal-speed run scored blue 1; the earlier tactics-disabled accelerated
control also scored blue 1. Neither is a final-build 4v4 capture-rate claim.


## Build and validation scope

Linux and Windows x64 dedicated servers and native game modules built with
`BUILD_CLIENT=0 BUILD_SERVER=1 BUILD_GAME_SO=1 BUILD_GAME_QVM=0`. The Windows
build is cross-compiled, not runtime-tested. No new client engine/renderer build
or visual validation is claimed. Delivered server packages contain freshly
packed native modules, with the Linux qagame hash checked against the final
live tests. Source validators and dependency-age checks are recorded separately.
Original Quake Live map/bot assets are private test inputs and are not included.

## Remaining work

Capture rate still needs larger matched samples, normal-speed 15v15 runs,
swapped character/team assignments and other maps. The final 30v30 build has
not been tested; the earlier drafts still made zero captures there. Quotas and
one assignment path are not yet a shared team allocation cache. Escort ranking
uses proximity rather than travel time. Threat should eventually be measured
along the actual route, with documented team knowledge, rather than just at a
waypoint's room. The two saved engine review findings and the separate CA/AD
spectator/forfeit issue remain deferred.
