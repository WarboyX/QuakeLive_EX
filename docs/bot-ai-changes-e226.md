# Bot AI iteration E226: isolating why the tactical layer loses to "layer off"

Handoff document, as CLAUDE.md requires for every bot AI iteration.

**Behaviour change in this iteration: none by default.** Every new switch
defaults to the current behaviour. This iteration adds measurement and
isolation, and records what was measured.

## Baselines

| | |
|---|---|
| Original uploaded build | `QuakeLive_EX-development(2).zip` (SHA256 `fc98ee0f…c6f69`) = this repo's `387d68a6` |
| Preceding iteration | `46544c03` (E224, CTF18 merge) and `dcaa64e5` (E225 heat maps) |
| This iteration | `d0356dd7` (switches, possession analysis), plus this document's commit |
| qagame played (current) | `ee694e84e04413e43183af1f630c8f48908c0f321c8f3e95dcd150814aac68d6` |
| qagame played (pre-merge) | `6d4f4f8512db3f3f8a8209b722227707b63f6e259b1af8926894c4e6037ef99d`: `387d68a6` + per-team switch + `botscores` only (no AI change) |
| Bot data | the test harness pack `zz_botharness.pk3` (SHA256 `fa189cb8…bded`): botfiles, japanesecastles .bsp/.aas. Test-only, never committed. **No botfiles, character, weight, chat or navigation asset was changed in this iteration.** |

## Code changes versus the preceding iteration

All changes are server AI code (qagame) and test tools.

| File / function | Before | After |
|---|---|---|
| `g_main.c`, `g_local.h` | — | `bot_ctfDetours`, `bot_ctfObjectiveMove` (1 red, 2 blue, 3 both; default 3) |
| `ai_dmq3.c` `BotGetAlternateRouteGoal` | carrier always picks a route | with `bot_ctfDetours` off for its team, the carrier clears its waypoint and goes straight home |
| `ai_tactics.c` `BotCTFObjectiveMove` (new) | — | `BotCTFKeepObjective` gated by `bot_ctfObjectiveMove` |
| `ai_dmq3.c` `BotWantsToRetreat`, `BotWantsToChase`; `ai_tactics.c` `BotRegroupGoal` | used `BotCTFKeepObjective` | use `BotCTFObjectiveMove` (identical at default) |
| `tools/test-ctf-carrier-selection.py` | — | per-team detour switch case |
| `tools/test-environment/ctf-possessions.py` (new) | — | per-possession trajectories and metrics |
| `tools/test-environment/heatmap.py` | "Where the fighting was" | "Bots in a battle node": the `f` state includes retreat-following |
| `docs/bot-tactics-switch-audit.md` (new) | — | what `bot_tactics` does and does not switch |

Rollback: revert `d0356dd7` and this document's commit. Defaults make that a
no-op for play.

## Runtime evidence

All matches: japanesecastles, 15v15 bots, skill 5, 600 game seconds,
**timescale 1**, `g_doWarmup 0`, `tools/test-environment/run.py`, every
match PASS with no errors. "On" is the tactical layer, "off" is
`bot_tacticsTeams` excluding that team. Each pair swaps sides.

### Batch 1 (20:41–20:52 UTC)

| pair | question | captures | deaths |
|---|---|---|---|
| A | layer on vs off | on 1, off 10 | on 492, off 289 (1.70×) |
| B | carrier detours off vs full layer | no-detours 5, full 6 | mixed (180/151, 147/173) |
| C | objective movement off vs full layer | no-obj-move 10, full 7 | 289 vs 303 |
| D | **pre-merge** layer on vs off | on 0, off 3 | on 453, off 266 (1.70×) |

- **Step 1 (normal speed):** the deficit holds at timescale 1.
- **Step 5 (pre-merge):** the same 1.70× death ratio before the CTF merge. It is
  inherited, not an integration regression.
- **Step 3 (detours):** no measurable effect.
- **Step 4a (objective movement):** small at most.

### Possessions (pair A)

From `ctf-possessions.py`, pickup to capture or death:

| | possessions | reached home | captured | died at home | reversals per possession |
|---|---:|---:|---:|---:|---:|
| layer on | 28 | 5 | 1 | 4 | 0.25 / 0.08 |
| layer off | 50 | 12 | 10 | 2 | 1.10 / 0.76 |

The carrier trajectories do **not** show looping. Layer-on carriers reverse
less than layer-off ones. The deficit is fewer pickups (attack) and home
carriers dying while their own flag is away (defence and recovery).

### Batch 2 (20:53–21:04 UTC): one aim or view piece off for the layer-on team

These cvars only act when `bot_tactics` is on, so setting one to 0 removes it
from the layer-on team alone.

| feature off | deaths on / off | ratio | captures on / off |
|---|---|---:|---|
| none (batch 1 A) | 492 / 289 | 1.70 | 1 / 10 |
| `bot_aimDrift` | 496 / 275 | 1.80 | 1 / 7 |
| **`bot_aimSweep`** | 473 / 389 | **1.22** | **4 / 2** |
| `bot_viewSmooth` | 506 / 300 | 1.69 | 1 / 4 |
| `bot_dodge` | 561 / 308 | 1.82 | 1 / 3 |
| `bot_targetCommit` | 466 / 329 | 1.42 | 2 / 3 |

`BotAimSweep` (ai_main.c) is deliberately human-like: eased sweeps, a
correction that slows as it shrinks, and a deadzone below which it does not
correct. The layer-off team aims with the stock servo every frame.

### Batch 3 (21:04 UTC): replication

16 matches, 21:04–21:15 UTC. Four more side-swapped baseline pairs and four
pairs with `bot_aimSweep 0`, run in the same batch.

| group (4 pairs) | deaths on / off | ratio | captures on / off | per pair (on–off) |
|---|---|---:|---|---|
| baseline | 2000 / 1206 | 1.66 | **3 / 22** | 1–8, 1–5, 1–4, 0–5 |
| `bot_aimSweep 0` | 1968 / 1558 | 1.26 | **12 / 8** | 3–4, 0–2, 4–1, 5–1 |

Batch 2 replicates. Over all five aim-sweep-off pairs: captures **16–10** for
the layer, against **4–32** in the five baseline pairs. The death ratio falls
from about 1.68 to about 1.25.

**Conclusion.** `BotAimSweep` accounts for most of the capture deficit. A death
gap of about 1.25× remains with it off. The candidates for that remainder are:
think rate (untested), `bot_targetCommit` (1.42 in a single pair), and the job
logic (attack: fewer pickups).

**No default was changed.** Whether to drop, tune or keep the human-like aim
is a design decision. It was written to make bots read as people, and it costs
them fights against servo aim.

## Limits

- One map and one population, skill 5.
- Matches are stochastic, not seeded pairs. Compare per side-swapped pair, not
  per sample.
- Deaths come from `bottrack` state transitions sampled every 250 ms.
- Possession ends are classified from `ctftrack` and broadcasts.
- The pre-merge build logs no `ctftrack`, so it has deaths and captures but no
  possession metrics.
- Think rate has no switch yet, so its share is untested.
- The remaining gap with aim sweep off (1.22×) is not explained.
