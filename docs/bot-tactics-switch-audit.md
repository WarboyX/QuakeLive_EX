# What `bot_tactics` / `bot_tacticsTeams` actually switch (E226)

`bot_tacticsTeams` (E224) makes `bot_tactics` read as a team's own setting while
that team's bot thinks and takes input. This records what that turns off and
what it does not, so a result is named for what was compared.

**Name the baseline "tactical layer off", not "stock Quake 3".** Several of our
changes are not behind `bot_tactics` and run for both teams.

## How the switch is applied

`BotAIStartFrameRun` (ai_main.c) sets `bot_tactics.integer` to the bot's team
setting around `BotAI` (the think) and `BotUpdateInput` (aim/view/movement
input), then restores it. Every reader below sits under one of those two calls.
The only reader outside them is `BotTacticsReport`, a console report.

The `bot_pause` path calls `BotUpdateInput` unwrapped. Paused bots do not move,
so it does not matter for matches.

Per-team caches (`ctfEscortCosts[2]`, `ctfRecovery[2]`, `ctfRelay[2]`) are
indexed by team, so a team's caches are only filled by its own policy.

## Behind `bot_tactics` (switched per team)

Readers, by function (`grep -n "bot_tactics.integer\|BotTacticsEnabled()"`):

| Area | Functions |
|---|---|
| CTF jobs | `BotCTFSeekGoals` (6), `BotCTFThinkGoals`, `BotCTFPlanGoals` via `BotCTFSeekGoals`, `BotCTFEnforceOffense` (4), `BotTeamGoals`, `BotCTFOrders` (stock team orders are *skipped* when on), `BotGetLongTermGoal` (6: get-flag / return-flag / escort handling), `BotCTFKeepObjective`, `BotCTFEscortEligible`, `BotCTFReleaseEscort`, `BotEscortGoal`, `BotCTFPickRole`, `BotCTFRoleCrowded`, `BotCTFRouteThreat` (route enemy count), `BotRoomCrowding`, `BotRoomEnemies` |
| Other modes' jobs | `Bot1FCTFSeekGoals`, `BotObeliskSeekGoals`, `BotHarvesterSeekGoals` |
| Defence | `BotAutoDefendGoal`, `BotDefendPostGoal`, `BotRoamWaypoint`, `BotRegroupGoal` (squad fallback) |
| Combat choice | `BotAggression`, `BotWantsToChase`, `BotWantsToRetreat`, `BotFindEnemy` (target commitment, `bot_targetCommit`), `BotCheckAttack`, `BotCloseRangeWeapon`, `BotAttackMove` (6), `BotPosture`, `BotTacticsUpdate` (3), `BotDodgeDirection` (`bot_dodge`), `BotEnemyTrackingMe` |
| Aim / view | `BotAimAtEnemy` (`bot_aimDrift`), `BotAimSweep` (`bot_aimSweep`), `BotChangeViewAngles` (`bot_viewSmooth`) |
| Movement | `BotSetupForMovement`, `BotTeamSpacing`, `BotItemSearchRange`, `BotWantsItemGoal` |
| **Think rate** | `BotAIStartFrameRun`: with the layer on, a bot thinks every `thinktime * (1 - 0.35 * aim skill)` ms (min 40), up to 35% more often |

So "layer on vs layer off" changes jobs, combat choice, aim, view, dodging,
movement and think rate all at once.

## Not behind `bot_tactics` (both teams, always)

- **Carrier route selection** (`BotGetAlternateRouteGoal` carrier branch:
  `BotCTFCarrierRouteCost`, `BotCTFKeepCarrierRoute`, direct-vs-detour). Only
  the enemy-count term (`BotCTFRouteThreat`) is zero for a layer-off team, so a
  layer-off carrier still uses the travel-cost part of the CTF16–18 detour
  selection. `bot_ctfDetours` (E226) turns the whole selection off per team.
- **Attacker waypoint choice by map side** (E136) and the emptiest-room pick
  that replaced stock's random roll, in `BotGetAlternateRouteGoal`.
- **botlib**: crowd steering (`bot_crowdsteer`, E133) and the routing fixes at
  cluster portals (E135).
- Skill mapping (`bot_startingSkill`, E139), chat budget, defender cap and the
  other engine/game changes since the original upload.

## Switches for isolating one piece (per team: 1 red, 2 blue, 3 both)

| cvar | Off means | Default |
|---|---|---|
| `bot_tacticsTeams` | whole layer off for that team | 3 |
| `bot_ctfIntercept` | escorts ranked by distance to the carrier now (ctf17), not meeting point (ctf18) | 3 |
| `bot_ctfDetours` | carrier goes straight home, no alternate waypoint | 3 |
| `bot_ctfObjectiveMove` | objective bots fight, chase and fall back like any other (item ranges unchanged) | 3 |

The aim and view features (`bot_aimDrift`, `bot_aimSweep`, `bot_viewSmooth`,
`bot_dodge`, `bot_targetCommit`) are global, but each only acts when
`bot_tactics` is on. Setting one to 0 in a layer-on vs layer-off match therefore
removes it from the layer-on team alone. Think rate has no switch yet.
