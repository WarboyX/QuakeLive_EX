# AimSweep A1 — source changes and test handoff

2026-10-07. Experimental refinement on the latest uploaded development source.
**Lives in:** server qagame. **Seen by:** every client observing server-controlled bots.
No upstream push or main branch change. This is a candidate for review, not a proven CTF-strength release.

## Exact baselines and build identity

- Incremental baseline: `QuakeLive_EX-development(3).zip`, SHA256 `755b2471f298f72d70373ddd7c30f3a6cf5fe2cf0f807b950e67d1e95535f4aa`. Contains E226 docs and switches. The archive has no upstream git history; correspondence to upstream `e4036f1e` is supplied context, not independently verified. Local pristine import commit: `3499a24f0e54979f43b95a14ae9dd91f022f9773`.
- Original cumulative baseline: `QuakeLive_EX-development(2).zip`, SHA256 `fc98ee0f673227de3e75c40709ccdae550f7d4ae740f0c5d077b67db21cc6f69` (original 387d68a6-era upload).
- Original botfiles archive: SHA256 `613475b275f112501c3de1cef562ae3d4d114a414b7e23a2a1aa96a7d0210a67`; **all 151 original entries unchanged**. No character, weapon/goal weights, chat or navigation asset edits. Test input manifest records every entry hash.
- Baseline qagame: `eee23f22f9057b4a84a4b758d924c6397471893bc1d314b58ba2bd2080dc53d6`.
- Candidate qagame: `2994a6a57632e6b85908a7ad6671fb0af5c81b1979ff3cccf1d66c4d49da3c2e`.
- Engine baseline/candidate: `6f070c9e51da2345ea388f2b032116fd2eeb3fa4b6452edf3da27a488c0c589e` / `31b4664622951d39eabca98ce83d0e3060927197304497ea35e10d6d098f1a86`.
- Binaries were built in this isolated tree before the final documentation commit. Use hashes, not a presumed upstream release stamp, to identify these test binaries.

## Incremental code changes from E226 upload

| Location | Before | A1 change and reason |
|---|---|---|
| `ai_main.c`, `BotAimSweep` clock | Sweep and filters use stepped botlib `FloatTime`; repeated timestamps reset the filters. | Flick uses `level.time`; filters update only on a new aim sample and retain their state between samples. |
| `ai_dmq3.c`, `BotAimAtEnemy` sample stamp | No independent timestamp on produced aim. | Records game time after producing ideal aim. Allows distinguishing a fresh observation from another input frame. |
| `BotAimSweep` acquisition | New sweep after an 8° destination step; no explicit enemy identity check. | Reset for new enemy, loss/disabled state, backward clock, long sample gap, or >45° aim discontinuity. Normal movement can continue tracking without repeatedly restarting its flick. |
| `BotAimSweep` prediction | Two cascaded 0.2-per-update filters measure ideal aim plus drift; lead mixes 0.01 + 0.075 seconds, despite an under-one-degree comment. | One elapsed-time filter, 80ms time constant; raw rate bounded ±180°/s; 30ms skill-scaled lead bounded ±2°; expires after 200ms without a sample. The separately generated drift offset is excluded. Existing upstream low-accuracy directional noise can still be in ideal aim. |
| `BotAimSweep` acquisition motion | Cubic eased movement; duration depends on distance and skill. | Preserved: `(0.05 + 0.004 * angular distance) * (1.5 - skill)`, clamped 0.05–1s. No instant snap introduced for acquisition. |
| `BotAimSweep` tracking | Slow gain and fixed 0.35° deadzone; small misses persist. | Exponential correction at 12–20/s by skill, capped at 180–720°/s during tracking; no intentional deadzone. Engine angle quantization still limits final precision. |
| `BotAimSweep` drift smoothing | Per-frame `dt * 4`, clamped. | Exponential `1-exp(-4*dt)` retains smooth offset motion across input rates. |
| `ai_dmq3.c`, drift/sweep switch interaction | Drift produces offsets that sweep-off never consumes. | Sweep-off adds the correlated error to ideal aim on the think. This restores the error term; its temporal response differs from sweep-on smoothing. It does **not** make sweep-on/off an identical-motion/noise comparison. |
| `ai_main.h`, `bot_tactics_t` | Two velocity-filter vectors. | Replaces second-filter storage with enemy identity and fresh-aim timestamp. Rebuild every qagame object using this header. No protocol or save-file format change. |
| `tools/test-aim-sweep.py`, `tools/validate.sh` | No isolated controller regressions. | Tests extracted real controller and engine angle helpers; registered in common validator entry point. Separate extracted drift block fixture checks switches. |

No changes to weapon damage, physics, character skill/accuracy values, CTF routing, think-rate policy or cvar defaults. AimSweep remains controlled by existing `bot_aimSweep`, drift by `bot_aimDrift`, and E224's per-team tactical gating remains intact. `bot_challenge` can still bypass normal aiming upstream; matches explicitly use 0.

The new constants are engineering choices, **not a fit to human aiming data**. Preserving eased acquisition and correlated error is a design intent. Human appearance needs spectator review. Lead operates on the already-selected ideal aim; projectile flight prediction is unchanged, but weapons still need individual evaluation for excessive controller compensation.

## Tests actually performed

Linux dedicated server and all native game modules built with:

```sh
make release -j2 BUILD_CLIENT=0 BUILD_RENDERER_OPENGL2=0
python3 tools/check-native-game-modules.py build/release-linux-x86_64
python3 tools/check-stale-objects.py
sh tools/validate.sh
python3 tools/test-aim-sweep.py
python3 tools/test-aim-sweep.py --baseline /path/to/pristine/ai_main.c
```

Full validator suite passed; latest focused fixture passed after adding real angle quantization and drift switch coverage. Native imports/entry points resolved and all 151 checked objects were current. No Windows, graphical client or Vulkan build is claimed.

Failed build attempts are retained in `logs/`: baseline first attempt produced five zero-byte objects, and candidate first attempt produced four. After each make exited, only those zero-byte objects were removed and make was rerun. An additional stale-object check caught a comment-only header edit during a build; a final rebuild cleared it. No failed binary was used in a match.

Controller fixtures exercise stationary small-error correction, 30°/s constant motion, sinusoidal reversing motion, 10/25/40ms input intervals with ~100ms aim sampling, repeated botlib timestamps, preserved velocity between samples, target identity changes, loss, yaw wrap, stale lead expiry, backwards time and sweep disable. Controller skill in trajectory comparisons is 1 (maximum aim characteristic), independent of game bot skill numbering. Drift test uses deterministic nonzero error.

At 25ms input intervals, using actual engine angle wrapping/quantization:

| Trajectory | Baseline mean error | A1 mean error |
|---|---:|---:|
| 0.2° stationary correction | 0.2000° | 0.0022° |
| 30°/s motion | 4.0735° | 1.3942° |
| Reversing sine motion | 4.1776° | 1.8128° |

These are synthetic controller metrics, not measured hit rate, damage, or human-likeness scores. Full multi-rate output is retained; results still vary with sampling cadence.

## Every match

Four real dedicated matches, Japanese Castles, 15v15, skill 5, requested 180 live seconds, normal `timescale 1`. Each version has its tactical team on red then blue against the common tactical-layer-off opponent. Warmup is bypassed with `g_doWarmup 0`; no human ready-up required. Layer-off is **not stock Quake 3**: shared routing remains active. Separate binaries per run, same character rotation and settings, no fixed RNG seed. Small side-swapped smoke sample, not statistical confirmation.

| Match (tactical side in name) | Live seconds | Red–blue captures | Red / blue deaths | Status |
|---|---:|---:|---:|---|
| baseline-red | 180.2 | 0–1 | 67 / 41 | PASS; no reported errors |
| baseline-blue | 180.2 | 1–0 | 28 / 58 | PASS; no reported errors |
| candidate-red | 180.2 | 1–0 | 67 / 60 | PASS; no reported errors |
| candidate-blue | 180.2 | 0–0 | 44 / 71 | PASS; no reported errors |

Aggregate tactical-versus-opponent captures were 0–2 in the baseline pair and 1–0 in the candidate pair. Tactical enemy kills were 65 versus 97; tactical deaths were 125 versus 138, with opposing deaths 69 versus 104. The death ratio improved from 1.81 to 1.33, but absolute tactical deaths increased. This small sample supports no general survival or win-rate claim.

Death counts use `botkill` victim team strictly after the initial live snapshot through the final snapshot; include world/suicide deaths. Warmup events are excluded. Raw initial/final score snapshots and commands are retained, so captures can be independently checked. Match journal timestamps every start, 30-second progress sample and completion. No interrupted runs are counted as finished.

The controlled tracking improvement is established for these fixtures. Four short matches cannot establish general CTF strength or explain all earlier death-rate differences. No claim that gardens/routing, carrier survival or team defence is fixed by this change. More maps, repeated side-swapped matches, per-weapon damage/accuracy and spectator footage remain needed.

## Cumulative comparison against the original upload

The cumulative runtime patch includes **inherited E224–E226 work**, not just this iteration. Apply the incremental patch to current development; do not apply the cumulative patch over E226. The manifest contains per-file old/new SHA256, exact line counts, and added/removed/changed function lists. Earlier detailed iteration reports are retained in the full source `docs/`.

| Runtime file | Cumulative lines | Ownership |
|---|---:|---|
| `code/game/ai_dmnet.c` | +45/−51 | Inherited from latest upload; unchanged by A1 |
| `code/game/ai_dmq3.c` | +215/−19 | Changed in A1 as well as inherited work |
| `code/game/ai_main.c` | +190/−264 | Changed in A1 as well as inherited work |
| `code/game/ai_main.h` | +4/−5 | Changed in A1 as well as inherited work |
| `code/game/ai_tactics.c` | +610/−228 | Inherited from latest upload; unchanged by A1 |
| `code/game/ai_tactics.h` | +18/−1 | Inherited from latest upload; unchanged by A1 |
| `code/game/ai_team.c` | +1/−0 | Inherited from latest upload; unchanged by A1 |
| `code/game/g_gametype_ca.c` | +8/−2 | Inherited from latest upload; unchanged by A1 |
| `code/game/g_gametype_common.c` | +8/−2 | Inherited from latest upload; unchanged by A1 |
| `code/game/g_local.h` | +4/−0 | Inherited from latest upload; unchanged by A1 |
| `code/game/g_main.c` | +22/−0 | Inherited from latest upload; unchanged by A1 |
| `code/game/g_svcmds.c` | +26/−0 | Inherited from latest upload; unchanged by A1 |
| `code/qcommon/common.c` | +10/−0 | Inherited from latest upload; unchanged by A1 |
| `code/qcommon/q_shared.h` | +8/−0 | Inherited from latest upload; unchanged by A1 |
| `code/qcommon/qcommon.h` | +1/−0 | Inherited from latest upload; unchanged by A1 |
| `code/server/sv_game.c` | +42/−0 | Inherited from latest upload; unchanged by A1 |
| `code/server/sv_main.c` | +5/−6 | Inherited from latest upload; unchanged by A1 |

## Integration and rollback

1. Start from the latest-upload-equivalent development tree. Inspect `aimsweep-a1-from-latest.patch`; `git apply --check` before applying. If coworker edits overlap, merge the three runtime files by function, retaining their newer unrelated changes.
2. Apply the patch, rebuild qagame and every dependent object, run common validators and native/stale checks. Deploy only with the normal project's packaging procedure. These Linux modules are test artifacts, not Windows release packages.
3. Compare existing cvar defaults first; test `bot_aimSweep 1`, `bot_aimDrift 1`, `bot_challenge 0`. The corrected sweep-off drift behavior means historical sweep-off numbers are not directly comparable to future ones without recording drift settings.
4. To restore exact E226 aiming, reverse the incremental patch (check first) and rebuild. Simply setting sweep to 0 does not restore the old error-dropping bug.
5. Full source and both baseline/candidate Linux test binaries accompany this report. Proprietary map/bot asset bytes are excluded; reproduce with the original private inputs whose hashes are in launch records.
