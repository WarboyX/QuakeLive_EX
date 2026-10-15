# Bot performance follow-up

Local test build `387d68a6-bot1`, based on uploaded revision
`387d68a61ed3c5d3a36cb5f956e722208354dafc`. Not an upstream release or push.

## Changes

- `com_cpuTimings 1` now measures `BotAIStartFrame` inside the native game
  module. Previously the bots subtotal timed `SV_BotFrame`, an empty hook;
  the actual AI cost was included in the game subtotal. The game subtotal
  now excludes the bot cost. A second line splits bot work into setup,
  world updates, AI and input, averaged per engine frame over two seconds.
- During the scheduled AI loop, client AAS entity snapshots are read once
  per client and shared by value across bots. The cache is cleared each
  server tick, starts after world updates, and ends before input submission.
  Non-client entities and calls outside that interval still go to botlib.
  Visibility traces, tactics, think intervals and scoring rules are unchanged.
- `botscores` in the server console reports time, gametype, warmup and round
  state, score guards, each connected bot's score, and both team scores.
  It works on a dedicated server with no human connected.
- `tools/test-bot-performance.py` runs exact-source C fixtures from
  `tools/validate.sh`, so packaging and CI include the checks.

The internal timing markers use the existing `BotLibVarSet` import and the
reserved name `__ql_botTiming`. Import-table layouts and network fields do
not change. A previous engine ignores the timing feature by treating the
marker as a harmless botlib variable; a previous game module provides no
markers, so its work stays in the game subtotal. Install matching binaries
and `iobin.pk3` from the supplied platform package for the full feature.

## Bot-only scoring remains an open runtime observation

The user's intended test setting is **`g_doWarmup 0`**. Do not attribute a
failure with that setting to a missing human ready-up. The source readiness
check returns true immediately with this setting. Map initialization and
the countdown/restart driver still manage the transition to a live match.

The source fixtures verify that bot scores remain zero during warmup and
that repeated scoring events increase a live bot's score across elapsed
time. They also verify TDM team scoring and training/end-of-match guards.
This tests readiness and `AddScore`; it does not simulate bots fighting,
capturing objectives, or completing rounds. No scoring bug is claimed fixed.

## Check on the real installation

1. Use the same map, mode, population and skill as the run that stalled.
   Set `g_doWarmup 0`, `g_debugWarmup 1` and `com_cpuTimings 1` before loading
   the map. Do not require a human to ready up.
2. Run `botscores` once the match should be live, then again after 30–60
   seconds of fighting or objective play. In a live match, warmup should be
   zero. Compare individual scores and team scores according to the mode.
   Elapsed time alone does not award a frag or capture point.
3. If scores stay flat, retain both reports and the warmup/restart log.
   Check the reported guards as well as whether bots move, fire and kill.
   This separates match transitions, scoring suppression and AI behavior.
4. Compare CPU averages against the baseline with identical settings.
   For the old build, actual AI was in **game**, so compare the sum of
   bots and game across builds. The new AI phase isolates the workload
   for future perception and route optimizations.

## Verification and limits

The synthetic 60-client, 60-bot, five-read-per-client fixture makes 18,000
lookups but only 60 botlib reads, with identical returned snapshots.
Additional cases cover caller mutation, out-of-range IDs, uncached calls,
refresh, disabled profiling and cleanup on an early return. Native timing
fixtures cover four phases, multiple simulation ticks, no double counting,
zero-valued clocks, off/on, missing end markers and duplicate markers.

Linux and Windows x64 builds, source validators and object dependency checks
are recorded with the delivered logs. The Linux binaries pass their version
smoke checks. Windows binaries are cross-compiled, not executed here.

The workspace lacks original Quake Live map and bot assets. The reusable
environment now runs actual bot matches on authored collision/navigation data:
FFA and TDM scores advance, CTF captures score, and the warmup control stays
at zero. CA exposes a spectator-demotion/forfeit failure, retained as a failing
regression. See `docs/test-environment.md` and the runtime logs. A production
30v30 frame-time gain and the user's exact map/mode observation are still
unverified. The broad K18 perception cache remains open.

The two deferred review fixes are in `docs/deferred-review.md`.
