# CTF navigation-ranked escorts: 387d68a6-ctf9

Experimental server AI follow-up to ctf8, based on upstream
`387d68a61ed3c5d3a36cb5f956e722208354dafc`. Built and played in the isolated
development checkout. No upstream push, main merge or release publication.

## Change and scope

Escort selection previously ranked bots by straight-line distance, allowing a
bot near a carrier through a wall or on another floor to win a slot while a
farther bot with a quicker reachable route was rejected. Selection now ranks
AAS travel time to the teammate flag carrier. Grounded candidates with no
route do not occupy a slot. The shared cache resolves raw costs once per
team/carrier/server frame; player orders, current assignments and vacancies
are checked live. An incumbent gets a 100-unit travel-time preference to
reduce marginal replacements. A replacement still waits for its occupied
slot to become vacant.

Within one navigation area, distance distinguishes candidates because AAS
returns a constant. An airborne/unroutable carrier temporarily uses distance;
a nearby existing escort in an airborne area can retain its slot with a
penalty, while a new or distant candidate there is rejected. Each bot's travel
flags are respected. Escort quotas, player orders, tactics-off and other game
modes retain their existing behavior. This uses known teammate positions; it
adds no new enemy perception or combat aim model.

## Verification

Compiled exact-source fixtures reproduce proximity-through-a-wall selecting
an unreachable bot. They cover reachable route ranking, same-area ties,
per-frame reuse, travel flags, incumbent stability, vacancies, explicit orders,
airborne fallbacks, both teams and disabled/other-mode behavior. All validators
pass, including the new trace-analysis regression covering measured match
boundaries, preceding rather than future samples, stale/dead/opposing-team
follow targets and sample age.

Linux and Windows x64 dedicated servers/native modules build. All 302 objects
passed the header dependency-age check and were nonempty before runtime tests.
The initial Linux build encountered four empty cached objects; after it exited,
those objects were removed and the rebuild succeeded. Both attempts are
retained. Windows was cross-compiled, not run. No new graphical client/renderer
build is claimed. The measured Linux qagame SHA-256 is
`5d5bfb80d888cc0e47da44032d32e565370c8ab893c97c77bc997a2eeaf60c3e`.
Every new runtime launch is checked against that module.

## Actual 15v15 results

Japanese Castles, supplied BSP/AAS and original bot definitions, thirty bots,
no human, `g_doWarmup 0` after full admission. Skill 10 on the server's 1–10
scale, standard weapons, `sv_fps 40`, no score/time limit. Each build got two
900-second simulated matches at 4x plus one 600-second normal-speed match.
Independent matches overlap; these are stochastic behavioral trials, not
identically seeded replays or CPU performance comparisons.

| Build / run | Simulated time / speed | Captures red / blue | Linked escorts within 600 | Linked escorts in item node |
|---|---|---:|---:|---:|
| ctf3-navigation-15v15-1 | 15 min / 4x | 3 / 2 | unavailable | unavailable |
| ctf3-navigation-15v15-2 | 15 min / 4x | 2 / 2 | unavailable | unavailable |
| ctf3-navigation-normal-15v15-1 | 10 min / 1x | 0 / 1 | unavailable | unavailable |
| ctf8-navigation-15v15-1 | 15 min / 4x | 3 / 1 | 41.7% | 29.7% |
| ctf8-navigation-15v15-2 | 15 min / 4x | 1 / 1 | 34.7% | 29.0% |
| ctf8-navigation-normal-15v15-1 | 10 min / 1x | 0 / 1 | 20.9% | 35.8% |
| ctf9-navigation-15v15-1 | 15 min / 4x | 3 / 1 | 48.3% | 27.9% |
| ctf9-navigation-15v15-2 | 15 min / 4x | 2 / 2 | 42.2% | 28.3% |
| ctf9-navigation-normal-15v15-1 | 10 min / 1x | 0 / 1 | 34.2% | 31.8% |

The accelerated pair produced **8 captures on ctf9**, **6 on ctf8**, and **9 on ctf3**. All three normal-speed matches finished **red 0 / blue 1**. The new build improves over its immediate parent in this accelerated pair but trails the saved baseline and ties both at normal speed. This does **not establish a dependable capture-rate gain**. Keep ctf3 as the preferred baseline; ctf9 remains experimental. All nine CTF matches retained full teams, bypassed warmup, recorded captures and passed the runtime checks.

Across the accelerated pair, ctf8 had 3100 linked escort samples: **38.3%** within 600 units of the followed carrier, and **29.4%** in the item node.
Across the accelerated pair, ctf9 had 3690 linked escort samples: **45.4%** within 600 units of the followed carrier, and **28.1%** in the item node.
The nearby fraction increased, but proximity is not proof of visibility,
protection or capture causation. Escort samples are weighted by observed role
time, not independent bots or possessions. Baseline ctf3 lacks the optional
possession/follow-target trace, so this stricter comparison is unavailable for
it. The older geometric accompany proxy remains in measurements for continuity.

Final ctf9 authored FFA control: **21 kills** in 60 simulated seconds at 2x, movement and individual score progress passed. Japanese Castles warmup control: **0 kills** in 20 simulated seconds at 2x; individual and team scores remained zero. Both pass.

## Measurement limits and next target

Analysis cuts the server log chronologically between initial and final measured
score reports, excluding warmup and shutdown/polling play. Linked escorts must
have a living accompany job, no carried flag themselves, and a followed
same-team carrier holding a flag in a preceding sample no older than 1.1 seconds.
Bot thinks are staggered at normal speed; requiring identical timestamps would
bias that analysis. Deaths, flag changes and movement between samples remain
uncertain. Distance is three-dimensional. The item node can represent a useful
health/ammo detour or air survival; the measurement does not mean every such
sample is wasted.

The next concrete candidate is bounding autonomous escort item detours. About
28% of the new build's accelerated linked escort samples were in the item node.
Source inspection also shows a newly assigned escort can finish an existing
nearby-item task because its old nbg_time is retained. Any trial should shorten
optional detours, clear obsolete item tasks on new assignments, and preserve
critical supplies, air escape and explicit player orders. That change has not
been implemented or tested in ctf9.

Carrier route scoring still measures waypoint threat rather than danger along
the whole path; replanning may change the route before an escort arrives.
Assignment remains per-bot quota selection rather than a shared scheduler.
More normal-speed repeats, team/character swaps and other maps are needed.
The two saved engine findings and separate CA/AD spectator/forfeit issue remain
deferred.
