**Later follow-up:** explicit recovery roles, one autonomous decision path and the 15v15 trials are in [ctf-planner-follow-up.md](ctf-planner-follow-up.md). This document retains the ctf3 baseline evidence.

# Japanese Castles CTF AI review

Local experimental build `387d68a6-ctf3`, based on the uploaded
`387d68a61ed3c5d3a36cb5f956e722208354dafc` source and the prior `bot1`
instrumentation/cache work. Not committed or pushed upstream.

**Lives in:** server qagame (`ai_dmq3.c`, `ai_dmnet.c`, `ai_tactics.c`).
**Seen by:** every client through the server-controlled bots.

## Findings and implemented changes

1. **Recovery was cancelled while an enemy carried our flag.**
   `BotCTFEnforceOffense` accepted `LTG_RETURNFLAG` only for `FLAG_DROPPED`.
   It therefore undid recovery decisions while the flag was `FLAG_TAKEN`.
   With tactics enabled, recovery now survives either state, and a returned
   flag clears the job so the bot can choose again.
2. **Recovery went to an empty enemy flag stand.** The CTF return-flag
   long-term-goal handler copied the enemy base goal. It now targets the actual
   dropped flag, or a carrier position observed by a living teammate. Team
   sightings expire after 15 seconds; an unseen carrier's current position is
   never the destination. Without a recent sighting it searches the enemy
   stand. Old alternate-route waypoints cannot redirect this moving target.
3. **Combat could divert an objective run.** Recovery could chase ordinary
   opponents, and squad fallback could replace the objective destination with
   an ally. Recovery and live escorts now fight while moving toward their
   objective. Carriers and attackers retain that destination during fallback.
   A visible enemy flag carrier still takes priority for pursuit.
4. **A home carrier could keep a whole escort group while nobody recovered
   our flag.** Autonomous escorts now keep the nearest guard on teams of up to
   four, or the nearest two on larger teams. Surplus escorts switch to recovery.
   Explicit orders remain in force and occupy guard slots; travelling carriers
   keep their existing escort policy.

Entity discovery and sight scans are shared once per team per server frame.
This bounds repeated work by pursuers, but does **not** establish a net CPU
improvement: the patch also adds perception work. Normal-speed performance
needs separate, repeated, uncontended measurements.

The new behavior is gated by `bot_tactics`; disabled tactics retains the old
recovery and movement policy. No scoring bonus, weapon accuracy, damage,
navigation data, player speed, or warmup rule was changed.

## Actual runtime results

The tests launch the real Linux dedicated server and native qagame/botlib with
the supplied Japanese Castles BSP/AAS and original bot files. Live matches use
`g_doWarmup 0`, skill 10, `sv_fps 40`, standard weapons, unlimited match/score
limits, no human clients, and full population admission before timing begins.
Each run copies its own executable, module and assets; launch hashes and every
console command are recorded.

| Build/test | Population | Measured game time | Captures, red / blue | Kills | Grabs / returns / carrier kills |
|---|---:|---:|---:|---:|---:|
| Original bot1 test | 4v4 | 5 min, normal speed | 0 / 0 | 39 | 11 / 9 / 8 |
| bot1 repeat | 4v4 | 5 min, normal speed | 2 / 0 | 33 | 9 / 4 / 5 |
| Recovery destinations only, ctf1 | 4v4 | 5 min, normal speed | 2 / 0 | 41 | 9 / 7 / 7 |
| Recovery + combat policy, ctf2 | 4v4 | 5 min, normal speed | 1 / 0 | 30 | 10 / 8 / 8 |
| Recovery + combat + home escorts, ctf3 | 4v4 | 5 min, normal speed | 3 / 1 | 30 | 12 / 7 / 7 |
| ctf3 repeat | 4v4 | 5 min, normal speed | 2 / 0 | 43 | 9 / 6 / 7 |
| ctf3 with bot_tactics 0 | 4v4 | 5 min, normal speed | 2 / 0 | 42 | 13 / 7 / 9 |
| Original bot1 test | 30v30 | 15 min, timescale 4 | 1 / 0 | 1,917 | 31 / 24 / 29 |
| ctf2 | 30v30 | 15 min, timescale 4 | 0 / 0 | 1,930 | 30 / 27 / 29 |
| ctf3 | 30v30 | 15 min, timescale 4 | 0 / 0 | 1,898 | 31 / 29 / 30 |

The final ctf3 repeat made two captures, and the tactics-disabled control made
two. Its diagnostic stillness timer is inactive with tactics disabled, so that
timer must not be used to classify movement in the control; position tracks
confirm actual movement. The original and repeated baseline differ, so the
four-capture result does not
prove a reliable increase in capture rate. The crowded-map tests **fail** the
objective-progress check; kills and individual score growth are not substitutes
for captures. Accelerated tests establish behavior in that setup, not normal
speed CPU performance or unchanged physics.

An exploratory ctf1 4v4 run scored red 2 / blue 1 at timescale 4 with eight
different characters; it is not a matched comparison. Its 30v30 run stopped
advancing at 441.5 measured game seconds and timed out with no captures.
The later ctf2 and ctf3 runs completed. The stall's cause was not established;
the failed run is retained rather than omitted. Periodic debug backtraces were
enabled in ctf3's large run only, using a private preload library which is not
part of the shipped game.

## Verification

- Linux client, dedicated server, OpenGL/Vulkan renderers and all native game
  modules compile. Windows x64 equivalents cross-compile; Windows was not run.
- All `tools/validate.sh` checks pass, including an exact-source C fixture for
  recovery targets, frame sharing, PVS rejection, dead/spectator observers,
  sight expiry, returned/re-taken flags, both teams, objective movement gates,
  guard ranking, explicit orders, small/large-team guard limits and the offense
  enforcer with tactics enabled/disabled.
- All 872 objects in the final platform trees pass the dependency-age check;
  no object is empty. The delivered Linux dedicated/qagame hashes match the
  binaries used for ctf3's real-map tests.
- Actual authored-fixture FFA: 22 kills in 60 simulated seconds, scores grew
  and bots moved. Japanese Castles warmup negative control: two kills in 20
  simulated seconds, individual/team scores remained zero.
- No new renderer behavior was tested or claimed. The two deferred engine
  review findings and the separately documented round-mode bug remain open.

## Next changes, in priority order

1. **One CTF team planner with an explicit recovery role.** Current bot-leader
   orders, self decisions, the offense enforcer and long-term-goal substitutions
   all assign jobs independently. Flag-state branches return before quota
   reconsideration. `BotCTFRoleCounts` has four roles and no recovery role;
   recovery and carrying fall through to geographic classification. Assign
   bounded recovery squads using AAS travel time, preserve a guard for our
   carrier, and count jobs consistently. Respect human orders as explicit
   overrides. Test state transitions with one/both flags out and dropped.
2. **Bound travelling escorts and spread routes.** Diagnostic snapshots of
   the latest 30v30 run still show nine red escorts against a quota of four.
   The near-carrier exceptions permit that and early returns prevent repair.
   Keep the closest useful escorts, release distant surplus, and reserve
   separate approach routes rather than sending everyone to one moving point.
3. **Measure and improve carrier survival.** In the latest large run, 30 of 31
   flag grabs ended in carrier kills. Room-level enemy counts are a coarse route
   heuristic. Evaluate threats along reachable route segments, estimate travel
   time and exposure, coordinate cover before entry, and reconsider nearby-item
   detours. Compare carrier life duration, progress toward home, captures per
   grab and crowd blockage, alongside kills. This is a design candidate, not a
   verified explanation for every carrier death.
4. **Profile the planner before expanding caches.** Keep client entity snapshots
   per AI tick, share team perception and role counts, and avoid repeating path
   queries for the same assignments. Cache keys must include flag transitions,
   roster/death changes and map resets. Budget sensing separately from movement
   and shooting. Run matched normal-speed CPU tests; do not infer a speedup from
   the accelerated-match timings or the number of eliminated calls alone.

## Reproduction

Build with the README prerequisites and `BUILD_RENDERER_VULKAN=1`. Set
`PRODUCT_GIT_HASH=387d68a6-ctf3` for this source snapshot. Generate private map
packs from the user's original `maps.zip` and `botfiles.zip`:

```sh
python3 tools/test-environment/prepare-map-assets.py \
  --maps /path/to/maps.zip --botfiles /path/to/botfiles.zip --output private-assets
python3 tools/test-environment/run.py \
  --build build/release-linux-x86_64 --map japanesecastles \
  --asset-pk3 /path/to/botfiles.zip \
  --asset-pk3 private-assets/bot-definitions.pk3 \
  --asset-pk3 private-assets/japanesecastles.pk3 \
  --characters anarki,bitterman,visor,sarge --skill 10 \
  --mode ctf --bots 8 --seconds 300 --timescale 1 \
  --report-interval 5 --track-interval 1000 --diagnostic-interval 30 \
  --require-score --root private-runs/4v4 --port 27991
```

For 30v30 use 60 bots, 900 seconds, timescale 4, and the eight-character list
`anarki,bitterman,visor,sarge,major,hunter,keel,slash`. For a tactical-layer
control add `--tactics 0`. `--warmup` deliberately keeps the match unready and
checks that scores stay zero. Original map/bot assets are not distributed.

The review kit includes source, a patch relative to the prior bot1 work,
both platform packages, exact commands, hashes, result JSONs and logs. Treat
the packages as experimental; the 30v30 capture failure remains open.
