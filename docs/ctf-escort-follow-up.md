# CTF escort follow-up: 387d68a6-ctf8

Experimental server AI follow-up to `387d68a6-ctf7`, based on upstream
`387d68a61ed3c5d3a36cb5f956e722208354dafc`. Built and played in this isolated
development checkout. No upstream push, merge or release publication.

## Why this change

The previous final 15v15 matches had 38/44 flag grabs, 35/40 carrier kills,
and only 3/2 captures. Carriers' observed rush-base intervals had a median of
seven seconds. Their tracks mostly show movement, not prolonged stationary
behavior. Existing escort quotas alone did not establish a capture improvement.

Two concrete route problems remained in `BotCarrierRoute` / `BotEscortGoal`:

1. Escort prediction went straight home while bot carriers could instead be
   walking to an alternate waypoint. The screen could run to another corridor.
2. The interception search first required beating the carrier to the final
   route point, then used a binary search. A reachable early interception can
   exist even when that final test fails, particularly with a detour or different
   directional travel permissions. The new fixture reproduces that rejection.

The shared prediction now walks to a pending carrier waypoint and then home,
using the carrier's travel flags. Its cache also keys on waypoint/home area.
A reached waypoint is excluded; a human carrier uses the direct path. Each
escort scans the bounded route for the first reachable point where it arrives
before the carrier, at least 80 AAS time units ahead of the current position.
If none exists it keeps the existing follow behavior. Shared route prediction
and escort decisions remain cached for half a second; they are approximations,
not an exact future trajectory. The scan is capped at 256 route points.

Optional `bot_debugTrack` output now includes separate `ctftrack` records:
health, actual flag possession, followed client, pending waypoint, direct home
travel time and combat node. Existing `bottrack` records remain unchanged.
The analyzer adds nearby-escort and return-progress summaries. These diagnostics
only run when position tracking is enabled and add routing work to those runs.

## Verification

Exact-source compiled fixtures cover the chosen detour, both route legs,
carrier travel flags, cache reuse/replanning, an early reachable interception
with an unreachable endpoint, equal arrival times, human carriers, home,
prediction failure, tactics off and other game modes. All source validators
pass. Linux and Windows x64 dedicated servers/native game modules build;
all 302 objects pass the dependency-age check and are nonempty. Windows is
cross-compiled, not run. No new graphical engine or renderer build is claimed.

## Runtime comparison

Actual Japanese Castles BSP/AAS and original bot definitions, 15 players on
each team, no human. Full admission precedes `g_doWarmup 0`. Skill 10 on the
server's 1–10 scale, standard weapons, `sv_fps 40`, no score/time limit.
Each build gets two fresh 15-minute simulated matches at timescale 4 and one
10-minute match at normal speed. Runs overlap and are behavioral trials, not
CPU benchmarks. Characters, spawns and combat are stochastic: the same setup
is not a replay with identical decisions or random seeds.

| Build / run | Simulated time / speed | Captures red / blue | Carrier median (s) | Nearby accompany samples |
|---|---|---:|---:|---:|
| ctf3-followup-15v15-1 | 15 min / 4x | 2 / 2 | 13.0 | 42.3% |
| ctf3-followup-15v15-2 | 15 min / 4x | 1 / 3 | 13.0 | 47.9% |
| ctf3-normal-15v15-1 | 10 min / 1x | 1 / 1 | 10.25 | 28.9% |
| ctf7-followup-15v15-1 | 15 min / 4x | 2 / 2 | 9.0 | 61.5% |
| ctf7-followup-15v15-2 | 15 min / 4x | 3 / 1 | 10.0 | 60.0% |
| ctf7-normal-15v15-1 | 10 min / 1x | 2 / 2 | 7.2875 | 59.8% |
| ctf8-followup-15v15-1 | 15 min / 4x | 3 / 2 | 10.0 | 62.4% |
| ctf8-followup-15v15-2 | 15 min / 4x | 2 / 0 | 7.0 | 57.1% |
| ctf8-normal-15v15-1 | 10 min / 1x | 2 / 2 | 4.65 | 63.8% |

The new ctf8 accelerated pair made **7 captures**, versus **8**
on the immediate ctf7 parent and **8** on the preferred ctf3 baseline.
At normal speed, ctf8 scored **red 2 / blue 2**, ctf7
**red 2 / blue 2**, and ctf3 **red 1 / blue 1**.
The corrected escort functions pass their reproduction fixtures, but these
small live samples do **not establish a dependable capture-rate gain**. The
accelerated total is lower, and the normal-speed comparison is only one match
per build. Keep ctf3 as the preferred baseline; ctf8 remains experimental.
All nine CTF matches retained full teams, bypassed warmup, recorded captures
and passed the runner checks with no runtime errors.

Final ctf8 authored FFA control: 23 kills in 60 simulated seconds at 2x,
movement and score progress passed. Japanese Castles warmup control: one kill
in 20 simulated seconds at 2x, individual/team scores stayed zero. Both pass.


Scores are measured team-score reports. Analysis cuts the log chronologically
between the initial and final reports to exclude warmup and shutdown/polling
play. Escort coverage uses preceding position samples no older than 1.1 seconds
and a 600-unit three-dimensional distance. Nearby accompany jobs are an escort
proximity proxy: stale follows of a former carrier can contribute to the count.
Distance is not proof of visibility, protection or continuous coverage.
Carrier durations measure sampled LTG_RUSHBASE intervals and exclude open
intervals; they are not exact possession durations. Health/node/progress traces
are only available on ctf8, so they are not a before/after metric.

## Remaining work

Escort selection still uses straight-line proximity, which can pick a bot on
the wrong side of a wall or floor. Routing still scores threat at a waypoint,
not along the whole path, and periodic carrier replanning can change that path
before a screen arrives. The room threat census remains the existing mechanism;
this change adds no new enemy perception model. The team planner is still
per-bot quota allocation, not a shared assignment scheduler. More normal-speed
matches, character/team swaps and other maps are needed before claiming a
reliable improvement. The two saved engine findings and the separate CA/AD
spectator/forfeit issue remain deferred.
