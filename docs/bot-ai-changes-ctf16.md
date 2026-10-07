# Bot AI change handoff — 387d68a6-ctf16

## Outcome and exact baseline

This is a measured Japanese Castles capture trial against saved ctf11. It is
an isolated route-risk trial based on recovered ctf11. It does not include the
lost ctf12 relay, ctf13 larger screen, or rejected ctf14 movement changes.
The ctf15 matches were interrupted by a workspace reset and have no retained
complete evidence. No interrupted match is counted here.

Original uploaded source: `QuakeLive_EX-development(2).zip`, SHA256
`fc98ee0f673227de3e75c40709ccdae550f7d4ae740f0c5d077b67db21cc6f69`.
Original botfiles: `botfiles.zip`, SHA256
`613475b275f112501c3de1cef562ae3d4d114a414b7e23a2a1aa96a7d0210a67`.
All 151 non-directory botfile entries remain unchanged. No character,
item/weapon weights, chat, or supplied AAS/BSP data is edited. Changes are
server AI C code and reproducibility tools. Both teams use the same policy.

The recovered ctf11 kit SHA256 is
`71adf7c92fea62936c751a0191a6efbe783918718c5c162f346ac8218a0bffda`.
Its tested qagame hash is
`8d2de3889608f6c1eb3a228835f4627f5e944055b3e01a13d1b0104f716961a8`.
The complete inherited original-relative changes are documented in the ctf11
report, reproduced below. The cumulative patch applies directly to the exact
uploaded original; the incremental patch applies to recovered ctf11. Apply
one, not both.

## Measured result

Across six 600-second normal-speed15v15 Japanese Castles matches per build,
ctf16 captured **16 flags** versus ctf11's **10** (+60%). First cohort10–4;
second cohort6–6. All six candidate games scored2–4 captures, while baseline
scored0–3 and had one no-progress failure. The second cohort tied, so the first
cohort accounts for the entire aggregate gain. Further repetitions are needed
before calling the capture advantage reliable. This is a notable observed gain on this
map, not statistical proof of a general improvement on other maps or seeds.
The test kit retains all twelve live matches, including the baseline failure.
The candidate runs reported no errors. Original151 botfile entries unchanged.
The sampled pending-waypoint switch rate also fell from3.97% (51/1284 adjacent
carrier pairs) to1.18% (21/1778), a70.3% decrease. Those are sampled transitions
with the limitations explained below, not exact events or proven wasted travel.

## New code versus recovered ctf11

| File / function | Before | Candidate behavior / reason |
|---|---|---|
| `ai_tactics.c`, new `BotCTFRouteThreat` | Carrier risk reads only a waypoint's room | Predict each AAS route one area at a time, taking the peak enemy count in subsequent named rooms. Initialize the room census first, exclude starting room, include destination, stop after 128 transitions or no progress. Detect contested rooms traversed before an apparently empty waypoint. |
| `ai_dmq3.c`, `BotCTFCarrierRouteCost` | First-leg plus return travel, then 100 units per enemy at waypoint | Add 300 units per peak enemy along either leg. Keep reachability, first-leg >=20, remaining < direct, total <=1.5*direct+100, and 50-unit incumbent preference. Weight is an experimental heuristic, not a damage prediction. |
| `ai_dmq3.c`, `BotGetAlternateRouteGoal` | Picks only among alternate waypoints, without giving direct home a score | Score direct home with the same 300-unit route risk; clear the alternate when its score is greater than or equal to direct. Direct wins ties, so a detour needs a reason. When hysteresis retains an incumbent, update its score before this comparison. |
| `ai_tactics.h` | No route-risk API | Declare the helper. No bot state layout or wire protocol changes. |
| `tools/test-ctf-route-threat.py` | No route-sampling fixture | Compile the actual function with controlled AAS and room counts; check initialization, peak-not-sum, starting room exclusion, destination, no progress, cap and disabled behavior. |
| `tools/test-ctf-carrier-selection.py` | Tests existing selector invariants | Add direct-safe versus risky detour, risky direct versus safer detour, and retained-score tie cases using the complete actual selector. |
| `tools/test-ctf-recovery.py` | Cost assertions use waypoint risk weight100 | Stub route risk and update expectations for weight300; preserve every prior recovery/planning check. |
| `tools/validate.sh` | Existing checks | Run new route fixture alongside all existing validators. |
| `tools/check-native-game-modules.py` | Link success could hide unresolved shared-library imports | Reject zero-size objects; ctypes-load all Linux modules and check required exports without calling their entry points. qagame exports dllEntry; cgame/ui also vmMain. |
| `tools/test-environment/run.py`, `setup` | Copies selected native module | Reject empty objects and load qagame/check dllEntry before copying it into a private match. |

The runner clarifies that warmup suppresses individual scores while CTF
practice team captures remain recorded. A stricter exploratory criterion was
rejected after reading unchanged scoring code. This changes test reporting,
not warmup or game scoring.

## Limits and rollout

The room census already knows global current enemy counts and associates positions
with nearest location names. This is coarse tactical knowledge, not visibility
or an inference that every enemy is behind cover. It refreshes twice a second.
Peak room population is not cumulative exposure, damage, line of sight, or
path safety. Excluding the start room discounts danger already around the
carrier; the second leg also excludes its own start room. The first leg includes
that waypoint room. The cap can miss intermediate rooms late in a long route,
although the endpoint is still considered. This introduces bounded extra AAS
work at replans; no CPU improvement is claimed without profiling.

The existing additional 100-unit replacement margin and 50-unit incumbent
preference remain. A kept route must now beat direct home as well. The existing
zero-home/no-valid-waypoint branch still clears instead of reviving a random
waypoint. Setting bot_tactics=0 makes room risk zero but does not disable the
new direct-versus-detour comparison; exact rollback uses the incremental patch.
No changes to speed, movement physics, aim, health, damage, warmup, capture rules,
weapons or network messages. Human and stock clients see the server bots.

## Build and fixtures

Native Linux dedicated engine and qagame/cgame/ui compiled. The first build
failed with four zero-byte objects, including ai_team.o. After deleting those
objects/dependency files and rebuilding, all three modules resolved imports.
The initial failed build log is retained. This is why the new native guard exists.
Dependency check (151 objects current), 151 nonempty objects, and complete
validator pass are retained. The played qagame SHA256 is
`acc5f105496aff3f3c34949f3eeda65b425b7c7ad59ada7243b5809fb7d5e067`. Windows has not been
built in this recovered trial; no client renderer or GPU test is claimed.

Commands: stamp with PRODUCT_GIT_HASH=387d68a6-ctf16, then
`make release BUILD_CLIENT=0 BUILD_SERVER=1 BUILD_GAME_SO=1 BUILD_GAME_QVM=0 -j2`.
Run `bash tools/validate.sh`, `python3 tools/check-stale-objects.py`, and
`python3 tools/check-native-game-modules.py build/release-linux-x86_64`.
The experiment drivers and per-run commands/launch metadata are in the kit.
To reconstruct their layout, extract the source zip under `source/`, compile
Linux there, extract the previous saved ctf11 server under `baseline/`, and
put supplied `botfiles.zip`/`maps.zip` under `inputs/`. If that baseline's native
module is only inside iobin.pk3, extract qagamex86_64.so beside it in baseq3.
Run `prepare-japanese-assets.py` to recreate private containers, then the two
normal drivers. The recorded drivers accept the current ctf16 source folder
or the recovery folder name ctf11. The exact executed commands remain in every
launch.json and cohort command/results JSON. Original assets are not supplied.
The played qagame SHA256 is `acc5f105496aff3f3c34949f3eeda65b425b7c7ad59ada7243b5809fb7d5e067`.

## Runtime protocol and results

Twelve actual Japanese Castles matches are planned: three games per build per
cohort, two cohorts, 15v15, original bot definitions, skill10 (botlib5), sv_fps40,
standard weapons, 600 live seconds, timescale1, g_doWarmup0, no human or limits.
Each run owns its assets, engine and module copy. Supplied map and bot contents
remain private and are omitted from this kit. Only the authored catalog/arena
metadata makes the supplied files discoverable. Missing optional arenas.txt
is logged but the .arena file and map load. Repeats are stochastic, not
identical seeds, and concurrency does not establish CPU improvements.

| Trial | Status | Red | Blue | Captures |
|---|---|---:|---:|---:|
| ctf11-risk-recovered-15v15-1 | PASS | 2 | 1 | 3 |
| ctf11-risk-recovered-15v15-2 | PASS | 0 | 1 | 1 |
| ctf11-risk-recovered-15v15-3 | FAIL | 0 | 0 | 0 |
| ctf11-risk-recovered-15v15-4 | PASS | 1 | 2 | 3 |
| ctf11-risk-recovered-15v15-5 | PASS | 2 | 0 | 2 |
| ctf11-risk-recovered-15v15-6 | PASS | 1 | 0 | 1 |
| ctf16-risk-recovered-15v15-1 | PASS | 1 | 2 | 3 |
| ctf16-risk-recovered-15v15-2 | PASS | 1 | 2 | 3 |
| ctf16-risk-recovered-15v15-3 | PASS | 1 | 3 | 4 |
| ctf16-risk-recovered-15v15-4 | PASS | 0 | 2 | 2 |
| ctf16-risk-recovered-15v15-5 | PASS | 1 | 1 | 2 |
| ctf16-risk-recovered-15v15-6 | PASS | 2 | 0 | 2 |

Completed totals: ctf16 **16 captures in 6 games**; ctf11 **10 in 6 games**.
Both six-game cohorts completed. This is an observed map-specific gain, not a guarantee or a controlled identical-seed causal estimate. All no-score failures remain in the table and evidence.

| Cohort | ctf16 captures | ctf11 captures |
|---|---:|---:|
| First | 10 | 4 |
| Second | 6 | 6 |

### Sampled carrier-route behavior

| Build | Adjacent carrier sample pairs | Pending waypoint switches | Switches / pairs |
|---|---:|---:|---:|
| ctf16 | 1778 | 21 | 1.18% |
| ctf11 | 1284 | 51 | 3.97% |

These are one-second sampled transitions, not exact possession or damaging replans. The analyzer uses previous waypoint travel >=20 to distinguish pending switches from ordinary waypoint completion; it excludes gaps >1.1 seconds. This is supporting route-behavior evidence, not proof that a particular swap caused a death or capture. Raw measurements and classifier fixtures are retained.

### Non-objective controls and rejected warmup criterion

- ctf11-warmup-long: recorded PASS, team scores {'red': 0, 'blue': 0}, errors [].
- ctf11-warmup-strict: recorded PASS, team scores {'red': 0, 'blue': 0}, errors [].
- ctf16-ffa-control: recorded PASS, team scores {'red': 0, 'blue': 0}, errors [].
- ctf16-warmup-control: recorded PASS, team scores {'red': 1, 'blue': 2}, errors [].
- ctf16-warmup-practice-control: recorded PASS, team scores {'red': 1, 'blue': 2}, errors [].
- ctf16-warmup-strict: recorded FAIL, team scores {'red': 1, 'blue': 3}, errors ['warmup negative control awarded team scores'].

The exploratory strict team-score criterion rejected candidate practice
captures; those raw FAIL results are retained as produced. Inspection confirms
AddScore suppresses individual points during warmup, while AddTeamScore and
Team_TouchOurFlag permit CTF practice team captures. The entire g_team.c is
byte-identical to the original upload. The strict zero-team criterion was
therefore rejected; the final runner retains practice capture timelines and
requires non-live warmup plus zero individual scores. Baseline short/long
controls happened to stay scoreless; that does not prove a warmup scoring
policy difference. The corrected candidate control checks the actual policy.
No game scoring or warmup logic changed. None of these warmup captures is
included in the normal-speed live comparison. FFA is an authored two-bot smoke
control, not Japanese Castles or objective proof.

## Exact original-relative source inventory

### code/game/ai_dmnet.c

{
  "file": "code/game/ai_dmnet.c",
  "original_sha256": "8f57b17d817cc9e5b7ccfe4f8b87372cf2b2f500fbf158ca55aeb611ad3623e9",
  "current_sha256": "1dfb4996dfc1c9ed764e8aadbb32bc31c463e344af5717dcc517ee3791f53753",
  "original_bytes": 122369,
  "current_bytes": 121197,
  "added_lines": 37,
  "removed_lines": 51,
  "functions_added": [
    "BotCTFEscortItemDuration",
    "BotCTFEscortItemRange"
  ],
  "functions_removed": [],
  "functions_changed": [
    "AINode_Battle_Chase",
    "AINode_Battle_Retreat",
    "AINode_Seek_LTG",
    "BotGetLongTermGoal",
    "BotNearbyGoal"
  ]
}

### code/game/ai_dmq3.c

{
  "file": "code/game/ai_dmq3.c",
  "original_sha256": "8499f0aaebdc7dc331dabbff73baee7d4214a904d7ab0836ce59c7dabf49c9ea",
  "current_sha256": "a79cfde1290645a523e0a5b6f2be13eb0e9a725d4b32552b7353c4728404d100",
  "original_bytes": 258131,
  "current_bytes": 266367,
  "added_lines": 197,
  "removed_lines": 15,
  "functions_added": [
    "BotCTFCarrierRouteCost",
    "BotCTFKeepCarrierRoute",
    "BotCTFPlanGoals",
    "BotCTFThinkGoals"
  ],
  "functions_removed": [],
  "functions_changed": [
    "BotCTFEnforceOffense",
    "BotCTFSeekGoals",
    "BotDeathmatchAI",
    "BotGetAlternateRouteGoal",
    "BotTeamGoals",
    "BotWantsToChase",
    "BotWantsToRetreatRaw"
  ]
}

### code/game/ai_main.c

{
  "file": "code/game/ai_main.c",
  "original_sha256": "b2c9063094c4ba70af2766db1375b772aa0871bb89be4bc31dfda52de13da2c8",
  "current_sha256": "f82ef18633872a19a758d1c357388fcb826b4a12fb8ecf7f06c36c1180264a0e",
  "original_bytes": 76681,
  "current_bytes": 79140,
  "added_lines": 59,
  "removed_lines": 2,
  "functions_added": [
    "BotAIStartFrameRun",
    "BotTimingStage"
  ],
  "functions_removed": [],
  "functions_changed": [
    "BotAIStartFrame",
    "BotEntityInfo",
    "BotTrackSample"
  ]
}

### code/game/ai_main.h

{
  "file": "code/game/ai_main.h",
  "original_sha256": "6a6b8156132d99b36962d314bda52258a6bc69f1f16b0aeb27d32cfee271059d",
  "current_sha256": "855ba601e2614c3701063942fe26a5e9ce766748f98f20376bf466980db4857c",
  "original_bytes": 21491,
  "current_bytes": 21580,
  "added_lines": 1,
  "removed_lines": 0,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": []
}

### code/game/ai_tactics.c

{
  "file": "code/game/ai_tactics.c",
  "original_sha256": "975a59af22ca63f45d4df896bc46f94ae518eb50d6b4267e53e57e6246056538",
  "current_sha256": "aea1d3007f9ef00ea36a145e76fad0841c80cb2b483db14bbea302bd376ab4ad",
  "original_bytes": 83137,
  "current_bytes": 93475,
  "added_lines": 439,
  "removed_lines": 228,
  "functions_added": [
    "BotCTFClientRole",
    "BotCTFEscortEligible",
    "BotCTFEscortLimit",
    "BotCTFEscortTravelTime",
    "BotCTFKeepObjective",
    "BotCTFRecoveryGoal",
    "BotCTFReleaseEscort",
    "BotCTFRouteThreat"
  ],
  "functions_removed": [],
  "functions_changed": [
    "BotAutoDefendGoal",
    "BotCTFPickRole",
    "BotCTFRoleCounts",
    "BotCTFRoleMix",
    "BotCTFRoleWanted",
    "BotCarrierRoute",
    "BotEscortGoal",
    "BotRegroupGoal",
    "BotRoomsReset",
    "BotTacticsReport",
    "BotTacticsReset"
  ]
}

### code/game/ai_tactics.h

{
  "file": "code/game/ai_tactics.h",
  "original_sha256": "4ea06ada9d45fa6f92befc33244c0028c853800b474c6c28fc8a587374dc17f5",
  "current_sha256": "f7b802933af730261918885559c715520852fd1838cdc18f9e1a11500a57b6d5",
  "original_bytes": 5089,
  "current_bytes": 5838,
  "added_lines": 13,
  "removed_lines": 1,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": []
}

### code/game/ai_team.c

{
  "file": "code/game/ai_team.c",
  "original_sha256": "ef503d9ce12c4451d39d1c9f36484b6b9c0d0759598f3ae7528f5e0583ff3002",
  "current_sha256": "4eed5763fd7b4fce9b9696f440dad212001775ffddd49bfe1f84c2854bdbc596",
  "original_bytes": 87133,
  "current_bytes": 87220,
  "added_lines": 1,
  "removed_lines": 0,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": [
    "BotCTFOrders"
  ]
}

### code/game/g_svcmds.c

{
  "file": "code/game/g_svcmds.c",
  "original_sha256": "be22322ad311ee39c8ef58b5b90ae93c617757baacd39a3d6cd1b29bb5784562",
  "current_sha256": "2a14a850744393ca652e8e0e47074b10a68ac2b882e81d34a0a1e470dd8039b6",
  "original_bytes": 23825,
  "current_bytes": 25106,
  "added_lines": 26,
  "removed_lines": 0,
  "functions_added": [
    "Svcmd_BotScores_f"
  ],
  "functions_removed": [],
  "functions_changed": [
    "ConsoleCommand"
  ]
}

### code/qcommon/common.c

{
  "file": "code/qcommon/common.c",
  "original_sha256": "a5a4e34de2d9c56ad4f96a967eed40ea21d519592f81ed93f2223f4b4d6c7e8e",
  "current_sha256": "2edd1a7a9e667f116b95fc88cf3e0805c377ddd90319ce47304a16d1439a44bc",
  "original_bytes": 115021,
  "current_bytes": 115609,
  "added_lines": 10,
  "removed_lines": 0,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": [
    "Com_CpuTimingsReport",
    "Com_Frame"
  ]
}

### code/qcommon/q_shared.h

{
  "file": "code/qcommon/q_shared.h",
  "original_sha256": "eecfae7d95989b94579f2b74d6ded9c7f9dda6cf47e73e091c9f9d4f766cb26c",
  "current_sha256": "5ae2f3e1876f65a7f0100ba8cac2a163caad27f3d1a72ee8c8601d898a459e08",
  "original_bytes": 46139,
  "current_bytes": 46474,
  "added_lines": 8,
  "removed_lines": 0,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": []
}

### code/qcommon/qcommon.h

{
  "file": "code/qcommon/qcommon.h",
  "original_sha256": "39d0b161ad3f660a2c4c96e95973e80d884936d38664a44e97c39724e809ac9a",
  "current_sha256": "742a552930225b1e6a414238271ffbb369965bf0ba7ff86debb4b9e27d1dce31",
  "original_bytes": 39210,
  "current_bytes": 39261,
  "added_lines": 1,
  "removed_lines": 0,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": []
}

### code/server/sv_game.c

{
  "file": "code/server/sv_game.c",
  "original_sha256": "5d5452874b79ab2e0802a5b5116073866ec82bdf05ea8660abda3996046cf9cd",
  "current_sha256": "af968fd2ced4f278c28c7cb64e3e809e74118df627372dfcbe70e6dd0dad7388",
  "original_bytes": 52470,
  "current_bytes": 53856,
  "added_lines": 42,
  "removed_lines": 0,
  "functions_added": [
    "SV_BotTimingMark"
  ],
  "functions_removed": [],
  "functions_changed": [
    "SV_GI_BotLibVarSet",
    "SV_GameRunFrame"
  ]
}

### code/server/sv_main.c

{
  "file": "code/server/sv_main.c",
  "original_sha256": "54169688ea1ad7302b4ad2f4aee071fdbfa94329c68c61f77bf2a2e6bd4436c7",
  "current_sha256": "5812fd6dc0c9b1b45ffbd92442c91b91559c535c0af1944ebdd0079f6ea3ee02",
  "original_bytes": 41125,
  "current_bytes": 41013,
  "added_lines": 5,
  "removed_lines": 6,
  "functions_added": [],
  "functions_removed": [],
  "functions_changed": [
    "SV_Frame"
  ]
}

## Integration and rollback

Use the cumulative original patch with the recorded original archive, or the
incremental ctf11 patch with the restored ctf11 tree. Source manifest supplies
per-file original/current hashes and function changes. Source and patches are
local development work; nothing was pushed or merged to main. Use the exact
native module hash from launch.json to reproduce runtime tests. Original assets
are required separately. Do not redistribute original paks or map/bot contents.
Reverse the incremental patch to restore ctf11 AI; retained tests/build guard
can be kept independently. Do not confuse source-only fixture success with
improved actual objective scores.

## Inherited ctf11 original-build comparison (historical evidence)

# Bot AI code-change handoff — 387d68a6-ctf11

This document compares the current experimental AI with the original uploaded
build and separately describes the changes made in this iteration. It is meant
to travel with the source, patches and actual test evidence so another developer
can review, reproduce or continue the work.

**Lives in:** server qagame and server timing support. **Seen by:** every client
through server-controlled bot behavior. Development checkout only; no upstream
push, main merge or release publication. Character assets are separate from the
server AI that reads them.

## 1. Exact comparison targets

| Target | Identity and meaning |
|---|---|
| Original uploaded source | `QuakeLive_EX-development(2).zip`, SHA-256 `fc98ee0f673227de3e75c40709ccdae550f7d4ae740f0c5d077b67db21cc6f69`; extracted `QuakeLive_EX-development/` |
| Upstream label retained from earlier work | `387d68a61ed3c5d3a36cb5f956e722208354dafc`; the original archive bytes and per-file hashes, rather than an unavailable original Git object, are the authoritative comparison |
| Saved recovery baseline | Local `cd140e0`, variant `387d68a6-ctf3`; already contains earlier recovery/combat/cache/timing changes and is **not** the untouched original |
| Immediate previous iteration | Local `fcbfbee`, variant `387d68a6-ctf10` |
| This iteration | `387d68a6-ctf11`; final source commit is recorded in the accompanying kit; Linux module SHA-256 `8d2de3889608f6c1eb3a228835f4627f5e944055b3e01a13d1b0104f716961a8` |
| Original character/weight assets | Supplied `botfiles.zip`, SHA-256 `613475b275f112501c3de1cef562ae3d4d114a414b7e23a2a1aa96a7d0210a67`, 151 non-directory entries |

The cumulative patch is generated by comparing the actual original archive's
files with current source. It is not just a diff against ctf3 or ctf9. Applying
it to a temporary copy of the original files reproduced all 13 current file
hashes. `bot-source-file-manifest.json` contains complete original/current
SHA-256 values, line counts and added/changed/removed function names, plus the
151 original bot-asset entry hashes. Global fields/macros are covered by the
file hashes and complete patch even when they are outside a function.

## 2. What happened to the original botfiles

**No original bot character, item/goal/weapon weight, chat or navigation file was
edited.** This iteration changes C AI source compiled into qagame. The original
`botfiles.zip` is copied byte-for-byte into each private real-map runtime; the
runtime copies' hashes were verified. `code/botlib/` itself has no cumulative
source change in this comparison.

The test environment makes a private `scripts/bots.txt` catalog naming the
original `bots/*_c.c` AI files, using `visor/default` as its model entry, plus an
arena declaration. It also packages the original Japanese Castles BSP/AAS into
a private map pack without modifying their bytes. Those registration/packaging
steps are harness inputs, not edits to the original bot characteristics or
navigation. All variants in a comparison use the same inputs. Private original
Quake Live assets are omitted from the handoff kit.

Handing someone replacement `botfiles` alone will not install these AI changes.
They need the rebuilt native qagame. Timing accounting additionally needs the
matching dedicated engine/common code. Both platform packages include native
qagame/cgame/ui modules; no new graphical engine or renderer build is claimed.

## 3. New carrier-selection changes in ctf11 versus ctf10

All three runtime changes are in `code/game/ai_dmq3.c`. The preceding ctf10
accelerated matches recorded 29 and 27 pending-waypoint changes, plus 38 and
29 direct-to-waypoint transitions. Some transitions represent legitimate route
progress. The source inconsistencies below are independently reproduced by
controlled route inputs; those counts alone do not prove oscillation or explain
every carrier death.

### 3.1 Match route selection to the arrival threshold

**Function:** `BotCTFCarrierRouteCost`.

Before, any nonzero first-leg travel was eligible. `BotAlternateRoute` itself
marks a waypoint reached when its travel is below 20. Periodic selection could
therefore re-arm a waypoint movement already considers reached, clearing its
reached time and briefly pointing an otherwise home-bound carrier at it again.
Now a first leg below 20 is rejected. Exactly 20 remains eligible. This uses
the existing arrival criterion; it does not introduce a world-distance cutoff
or change movement speed. Zero/unreachable legs remain invalid.

### 3.2 Retain a useful pending route unless the replacement is meaningful

**Functions:** new `BotCTFKeepCarrierRoute`; modified `BotGetAlternateRouteGoal`.

The selector still computes the best candidate normally, including travel,
room enemy cost and the existing 50-unit incumbent preference. If that winner
is different from a valid **pending** incumbent, the new helper retains the
incumbent unless the winner saves at least another 100 score units:

```c
keep = current_cost >= 0 && best_cost + 100 > current_cost;
```

Equality permits switching. These are combined travel/threat score units, not
an exact one-second promise: one room enemy also contributes 100. The margin
is additional to the previous 50-unit incumbent preference. A reached waypoint,
a reported stuck bot, an unreachable/backward/excessive incumbent, or an
incumbent no longer in the candidate list does not receive the extra protection.
It does not force switching when the incumbent remains the ordinary best choice.
The aim is to avoid marginal corridor changes while allowing useful escapes.
A valid route can still become dangerous between the existing three-second
carrier replans, and the margin may postpone a worthwhile smaller improvement.

### 3.3 Do not revive a rejected random carrier waypoint

**Function:** `BotGetAlternateRouteGoal`.

Before, all costs could reject when direct home travel was zero, but the failure
branch checked `togo || tohome`. Both were then zero, so execution reached the
legacy random fallback and installed a waypoint that had never passed the
carrier's reachability/progress/detour tests. Now a CTF carrier with no accepted
candidate clears `altroutegoal.areanum` and returns false, even with zero direct
home travel. The long-term goal continues toward home. This does not magically
make an unavailable AAS route reachable; it prevents manufacturing an accepted
detour out of the rejected list. The random fallback for other modes is retained.

### 3.4 Complete-selector fixtures and clearer route evidence

`tools/test-ctf-carrier-selection.py` compiles the **entire actual**
`BotGetAlternateRouteGoal` together with its actual cost/retention helpers.
Controlled AAS input covers a 99-unit saving versus an exact 100-unit saving,
arrival at 19/20, valid incumbents, reached/stuck/invalid/incumbent-not-listed
release, all candidates already arrived, zero direct travel, and other-mode
legacy dispatch. The older recovery fixture also checks the arrival boundary.
The common validator runs the new fixture in packaging and CI.
A separate reproduction compiles the complete ctf10 selector against the same
controlled setup and confirms its marginal switch, near-arrived re-selection
and zero-home-travel random fallback. Its source and output are in the kit.

The analyzer now separates pending-waypoint swaps, waypoint-to-direct transitions
and direct-to-waypoint transitions. Reaching a waypoint is ordinary progress,
so the old aggregate waypoint_changes count must not be described as a count
of harmful replans. The classification is checked with controlled trace records
and uses the same prior-sample/interval limitations as the existing metrics.
No diagnostics in the runtime module changed between ctf10 and ctf11.

## 4. Escort-item changes inherited from ctf10

The ctf9 pair preceding ctf10 had approximately 28% of linked escort samples in the
`AINode_Battle_NBG` item node. Source inspection showed ordinary item search
scaling and deadlines remained in effect for escorts, and a new escort
assignment did not expire its preceding nearby-item task. The new trial targets
those mechanisms without changing aim, damage or scoring.

### 4.1 Bound the scaled item-search range

**File:** `code/game/ai_dmnet.c`. **Functions:** new
`BotCTFEscortItemRange`; modified `BotNearbyGoal`.

Before, an ordinary range such as 150 AAS time units was passed through
`BotItemSearchRange`, which can expand it for health shortages and quieter
situations. Escorts used that general policy. Now the escort cap is applied
**after** that expansion, so the global multiplier cannot undo it.

| Situation | New maximum item-search range | Reason |
|---|---:|---|
| Autonomous escort, live followed flag carrier, health above 40 | 50 AAS travel-time units | Allow a short pickup without an ordinary roaming detour |
| Same job, health 40 or below | 150 units | Give a wounded escort more room to obtain supplies |
| Already smaller caller range | Keep the smaller range | The cap never expands a request |
| Explicit order, actual flag carrier, non-escort, expired/invalid followed carrier, tactics disabled or another game mode | Existing policy | The new rule is scoped to autonomous live CTF escorts |
| Air needed: `lastair_time < FloatTime() - 6` | Existing policy | Preserve both direct air goals and item-based escape when the air search falls back |

AAS units approximate hundredths of a second of travel; they are not world
units or an exact physical arrival prediction. Low ammunition still participates
in the existing item weights, but this trial adds no separate low-ammo exception.
It is therefore possible to restrict a useful ammunition detour. That tradeoff
must be evaluated in live play.

### 4.2 Bound all three item-entry deadlines

**File:** `code/game/ai_dmnet.c`. **Functions:** new
`BotCTFEscortItemDuration`; modified `AINode_Seek_LTG`,
`AINode_Battle_Chase`, `AINode_Battle_Retreat`.

Capping `BotNearbyGoal`'s local range would not change the caller's original
range used to set `nbg_time`. The new helper explicitly caps the caller's
duration as well. For an ordinary caller range of 150:

| Entry path | Previous deadline budget | New healthy escort budget | New critical-health budget |
|---|---:|---:|---:|
| Seek long-term goal | `4 + range * 0.01` = 5.5 s | At most 1.5 s | At most 2.5 s |
| Battle chase | `0.1 * range + 1` = 16 s | At most 1.5 s | At most 2.5 s |
| Battle retreat | `range / 100 + 1` = 2.5 s | At most 1.5 s | At most 2.5 s |

The cap is `min(existing_duration, capped_range / 100 + 1)` and cannot lengthen
an existing duration. The chase comment says five seconds although its formula
produces sixteen at range 150; this trial does not assert that every legacy
chase budget was a bug and does not change that formula for other jobs.
Air-seeking and explicit orders retain their ordinary budgets. `BotGoForAir`
still executes before the optional item selection.

### 4.3 Expire an old item task on a new escort assignment

**File:** `code/game/ai_dmq3.c`. **Function:** `BotCTFPlanGoals`, new
logic in its `CTFROLE_ESCORT` assignment branch.

Before, a bot selected to escort could keep its old `nbg_time` and finish that
item task before following the new carrier. On a **new** escort assignment:

- Above 40 health and without an air need, set `nbg_time = 0`; the active NBG
  node's existing expiry path pops the goal and returns to objective movement.
- At 40 health or below, shorten a longer existing task to at most 2.5 seconds
  from now; preserve an already shorter deadline.
- If the six-second air predicate is active, leave the existing deadline alone.
- An unchanged escort assignment returns before this branch, so it does not
  repeatedly reset the deadline on each think. Explicit live orders also return
  before autonomous assignment.

`BotDeathmatchAI` refreshes inventory and calls `BotCheckAir` before tactical
planning, so the decision uses the current think's health/air state. The code
does not manually remove a goal-stack entry during assignment; its owning node
performs the existing expiry/pop operation.

### 4.4 Diagnostics, reproducibility and handoff policy

`tools/test-environment/analyze-ctf.py` now splits linked escort node samples by
health above 40 versus at most 40. `tools/test-ctf-analysis.py` checks that boundary.
`tools/test-ctf-escort-items.py` compiles the real helper and `BotNearbyGoal`
against controlled inputs and checks wiring into all three entry paths.
`tools/test-ctf-recovery.py` checks new assignment expiry, critical-health
shortening, air preservation and unchanged-assignment stability for both teams.
The common validator runs the new fixture in packaging and CI.

`tools/bot-change-handoff.py` generates the exact original comparison and
inventories for future iterations. `CLAUDE.md` now records Jonathan's requirement
for a detailed cumulative and incremental bot-AI handoff every iteration.

## 5. Cumulative behavior versus the original uploaded build

The changes below include work preceding ctf10. They are already in this source
and are included in the cumulative patch; they should not be mistaken for all
having been introduced by the latest route-selection change.

### 5.1 Shared recovery destinations and objective movement

**Files:** `ai_tactics.c`, `ai_tactics.h`, `ai_dmnet.c`, `ai_dmq3.c`.

The original return-flag long-term handler copied an enemy-base goal, and the
old offense enforcer retained return-flag jobs only when our flag was dropped.
Recovery could therefore be cancelled while an enemy carried it, or head to an
empty stand. `BotCTFRecoveryGoal` now finds the actual dropped own flag, or a
carrier position seen by a living teammate. A team/server-frame cache shares
entity discovery and visibility work. PVS rejects impossible sightings before
a visibility trace; dead/spectating teammates cannot contribute. Sightings expire
after 15 seconds and are invalidated when the carrier changes or our flag
returns. With no recent sighting, the bot searches the enemy stand rather than
using the unseen carrier's current location. Old alternate waypoints are cleared
for the moving recovery target.

`BotCTFKeepObjective` causes valid recovery and flag escorts to fight while
moving toward that objective, rather than chasing incidental opponents or
regrouping toward another ally. Attackers and actual carriers also retain their
objective during regroup fallback. A visible enemy flag carrier still has the
existing pursuit priority. That is a policy choice, not a guarantee that every
escort always stays with its teammate.

### 5.2 One autonomous CTF assignment path

**Files:** `ai_dmq3.c`, `ai_main.h`, `ai_team.c`, `ai_tactics.c`.

Originally bot-leader orders, per-bot team decisions, the offense enforcer,
auto-defense and missing-flag long-term-goal substitution could all assign jobs.
Combat could begin before the seek path reached a team decision.
`BotCTFThinkGoals` now invokes CTF planning before combat-node dispatch on each
eligible think, and `BotCTFPlanGoals` is the autonomous writer with tactics on.
`BotCTFOrders`, `BotTeamGoals`, `BotCTFEnforceOffense`, `BotAutoDefendGoal` and the
missing-flag fallback stop creating competing autonomous CTF assignments.

The tactical state gains `ctfphase`, reset to -1, encoding both flags' statuses.
Flag transitions invalidate the role cooldown. Useful unchanged roles refresh
their goal deadline; attack changes get a 30-second reconsideration delay,
others five seconds. A live explicit order outranks the planner; expired orders
clear their stale last-goal state so they cannot block planning indefinitely.
This is per-bot quota selection, not a global optimal-assignment solver.

### 5.3 Recovery/carrier roles and quotas

**Files:** `ai_tactics.c`, `ai_tactics.h`, `ai_main.h`.

Recovery and carrier become explicit roles, expanding `CTFROLE_COUNT` from four
to six. The census honors declared jobs and intentional roam, counts a human
flag carrier, and no longer mistakes a stale/non-carrier follow for active flag
escort work. Diagnostics include the representative bot itself. These base
weights are before reserving a carrier and its escort screen:

| Flag state | Original attack / defend / escort / roam | Current attack / defend / escort / roam / recover |
|---|---|---|
| Both home | 45 / 35 / 0 / 20% | 55 / 25 / 0 / 20 / 0% |
| Only ours out | 25 / 60 / 0 / 15% | 20 / 15 / 0 / 10 / 55% |
| Only theirs out | 25 / 30 / 35 / 10% | 0 / 25 / 0 / 75 / 0% |
| Both out | 15 / 45 / 30 / 10% | 0 / 15 / 0 / 20 / 65% |

The original slow sine jitter shifting attack/defense by up to ten percentage
points was removed. A present teammate carrier reserves one carrier slot and a
bounded screen, deducted from roam then recovery as needed. Defender demand is
capped at eight; overflow goes to attack when our flag is home and recovery
when it is missing. These are desired counts subject to existing jobs, orders
and reassignment, not an instantaneous exact composition.

### 5.4 Bounded, reachable escorts

**Files:** `ai_tactics.c`, `ai_tactics.h`, `ai_dmnet.c`.

The original near-carrier exception could admit escorts beyond the nominal
quota. Current travelling caps are one for team size up to four, two up to
eight, four above eight. A carrier waiting within 600 units of home while our
flag is missing retains one guard on teams up to four or two on larger teams.
Player orders occupy slots and are never revoked by the autonomous selector.
A closer replacement waits for an occupied slot to become vacant, avoiding the
staggered-think overflow found during the earlier trials.

`BotCTFEscortTravelTime` ranks reachable AAS travel instead of proximity through
a wall, with per-frame shared raw costs and a 100-unit incumbent preference.
Same-area candidates use distance because AAS returns a constant there. An
unroutable/airborne carrier temporarily uses distance; an airborne candidate is
retained only as a nearby existing escort, with a penalty. Grounded candidates
with no route are rejected. Orders, assignments and vacancies are checked live
rather than frozen in the cache. Item-detour limits from section 4 then
control optional pickups while that assignment is active.

### 5.5 Carrier routes and escort interception

**Files:** `ai_dmq3.c`, `ai_tactics.c`.

`BotCTFCarrierRouteCost` queries carrier-to-waypoint and waypoint-to-home in the
actual return direction, using the carrier's flags. It rejects unreachable legs
and waypoints whose remaining home travel is no better than direct travel.
Arrival and pending-route replacement additionally follow section 3.
Total detour travel is bounded by `direct * 1.5 + 100`; room enemy count adds
100 units per enemy, and an existing waypoint gets a 50-unit stability
preference. Previously reverse/static route data and room crowding could favor
a backward or excessive detour. Threat is still room/waypoint-based, not an
exposure integral over the route.

`BotCarrierRoute` predicts the pending alternate waypoint and then home with
the carrier's travel flags; human carriers have no bot waypoint and use the
existing direct-home prediction. Cache keys include home and waypoint.
`BotEscortGoal` scans the bounded route for the earliest reachable lead, at least
80 AAS units ahead. The former endpoint check and binary search could reject a
reachable earlier meeting when travel permissions or detours made the timing
non-monotonic. The scan is bounded at 256 points and cached for half a second;
route changes invalidate an earlier intercept. This may add path-query work and
does not establish a CPU improvement or predict the carrier perfectly.

### 5.6 Client entity-read cache and accurate CPU accounting

**Files:** `ai_main.c`, `q_shared.h`, `qcommon.h`, `common.c`, `sv_game.c`,
`sv_main.c`.

`BotEntityInfo` caches client AAS snapshots only inside the stable update/input
window and returns copies. Non-client indices and outside-window reads bypass
it; the cache is cleared each tick and closed on early return. The fixture's
18,000 repeated lookups reduce to 60 botlib reads, but that synthetic result is
not a measured end-to-end speedup.

`BotAIStartFrame` wraps its worker and emits optional setup/world/AI/input markers
through the existing `BotLibVarSet` import. `SV_BotTimingMark` records actual
native elapsed phases, closes a missing end marker and handles disabled timing.
The game subtotal excludes bot time because bot AI runs inside `G_RunFrame`.
Common CPU reporting adds the four phases. Shared marker constants are added
without changing the import table or network protocol; an older engine treats
the marker as an ordinary libvar and lacks the new accounting. Pair the engine
and game module to obtain correct diagnostics. No uncontended CPU benchmark was
performed in the latest overlapping traced matches.

### 5.7 Objective-progress evidence

**Files:** `ai_main.c`, `g_svcmds.c`; tools in the source package.

The new `botscores` console command reports actual team and individual scores,
warmup/round state, playing clients and scoring guards. It does not grant scores
or change readiness. `bot_debugTrack` adds optional possession/follow-target,
health, route and node records beside the existing position records. The analyzer
cuts between measured score reports and checks preceding carrier samples within
1.1 seconds. It distinguishes an actual followed carrier from a known stale
follow, although deaths or flag changes between samples remain uncertain.
`nodeitem` means **Battle_NBG only** in the retained instrumentation; Seek_NBG is
inside `nodeother`, so item-node fractions are not a complete census of all
supply detours. Tracking itself adds queries/log work.

## 6. Exact changed source-file/function inventory

| File | Added / removed lines | Added functions | Changed functions or globals |
|---|---:|---|---|
| `code/game/ai_dmnet.c` | +37 / −51 | `BotCTFEscortItemDuration`, `BotCTFEscortItemRange` | `AINode_Battle_Chase`, `AINode_Battle_Retreat`, `AINode_Seek_LTG`, `BotGetLongTermGoal`, `BotNearbyGoal` |
| `code/game/ai_dmq3.c` | +189 / −15 | `BotCTFCarrierRouteCost`, `BotCTFKeepCarrierRoute`, `BotCTFPlanGoals`, `BotCTFThinkGoals` | `BotCTFEnforceOffense`, `BotCTFSeekGoals`, `BotDeathmatchAI`, `BotGetAlternateRouteGoal`, `BotTeamGoals`, `BotWantsToChase`, `BotWantsToRetreatRaw` |
| `code/game/ai_main.c` | +59 / −2 | `BotAIStartFrameRun`, `BotTimingStage` | `BotAIStartFrame`, `BotEntityInfo`, `BotTrackSample` |
| `code/game/ai_main.h` | +1 / −0 | — | bot_tactical_t.ctfphase |
| `code/game/ai_tactics.c` | +408 / −228 | `BotCTFClientRole`, `BotCTFEscortEligible`, `BotCTFEscortLimit`, `BotCTFEscortTravelTime`, `BotCTFKeepObjective`, `BotCTFRecoveryGoal`, `BotCTFReleaseEscort` | `BotAutoDefendGoal`, `BotCTFPickRole`, `BotCTFRoleCounts`, `BotCTFRoleMix`, `BotCTFRoleWanted`, `BotCarrierRoute`, `BotEscortGoal`, `BotRegroupGoal`, `BotRoomsReset`, `BotTacticsReport`, `BotTacticsReset` |
| `code/game/ai_tactics.h` | +11 / −1 | — | Recovery/carrier role enums and new helper declarations |
| `code/game/ai_team.c` | +1 / −0 | — | `BotCTFOrders` |
| `code/game/g_svcmds.c` | +26 / −0 | `Svcmd_BotScores_f` | `ConsoleCommand` |
| `code/qcommon/common.c` | +10 / −0 | — | `Com_CpuTimingsReport`, `Com_Frame` |
| `code/qcommon/q_shared.h` | +8 / −0 | — | BOT_TIMING_MARKER and botTimingStage_t |
| `code/qcommon/qcommon.h` | +1 / −0 | — | com_usBotStages extern declaration |
| `code/server/sv_game.c` | +42 / −0 | `SV_BotTimingMark` | `SV_GI_BotLibVarSet`, `SV_GameRunFrame` |
| `code/server/sv_main.c` | +5 / −6 | — | `SV_Frame` |

`bot-source-file-manifest.json` gives full byte counts and hashes for each row.
There are no removed functions in this cumulative code comparison. Struct,
enum and declaration changes in headers are separately visible in the patch.
Testing/documentation additions are in the full source and incremental patch;
they are not character assets consumed by botlib.

## 7. Verification and live results for this iteration

The new complete-selector fixture and arrival-boundary regressions pass.
Inherited exact-source fixtures check scaling-before-cap, health 40/41, smaller ranges,
unchanged deadlines, all three item-entry paths, oxygen success/fallback,
orders, flag carriers, former/invalid carriers, other game modes and tactics
0. Planner fixtures cover new healthy/critical/air assignments and unchanged
assignments for both teams. Analysis checks measured boundaries, prior/future
samples, sample age, health groups and stale/dead/opposing-team follow targets.
All common validators pass.

Linux and Windows x64 dedicated servers/native modules compile. The initial
Linux link exposed two empty restored objects, `cm_test.o` and `g_gametype_rr.o`; after make exited, those
cached objects/dependencies were removed and the rebuild succeeded. The initial
and final logs are retained. All 302 dependency-checked objects pass, and all
303 objects including the Windows resource are nonempty. Windows is
cross-compiled only. Graphical client, Vulkan, NVIDIA shadows and human round
starts were not tested by this bot iteration.

Actual dedicated Linux server, Japanese Castles, 15v15 bots, no human,
`g_doWarmup 0` after admission. Server skill 10 (botlib 5), standard weapons,
`sv_fps 40`, no capture/time limit. Two independent 900-second simulated matches
at 4x and one 600-second normal-speed match per build. Matches overlap and are
stochastic rather than identically seeded replays or CPU benchmarks. The saved
ctf3 baseline already contains earlier AI changes; this runtime comparison is
not against an untouched original binary.

| Build / run | Time / speed | Captures red / blue | Runner status | Linked escort Battle_NBG samples |
|---|---|---:|---|---:|
| ctf10-route-15v15-1 | 15 min / 4x | 1 / 1 | PASS | 7.7% (113/1466) |
| ctf10-route-15v15-2 | 15 min / 4x | 2 / 2 | PASS | 6.4% (108/1699) |
| ctf10-route-normal-15v15-1 | 10 min / 1x | 1 / 0 | PASS | 8.5% (53/621) |
| ctf11-route-15v15-1 | 15 min / 4x | 4 / 0 | PASS | 6.5% (109/1689) |
| ctf11-route-15v15-2 | 15 min / 4x | 0 / 1 | PASS | 7.7% (103/1344) |
| ctf11-route-normal-15v15-1 | 10 min / 1x | 1 / 2 | PASS | 6.5% (58/898) |
| ctf3-route-15v15-1 | 15 min / 4x | 3 / 0 | PASS | unavailable |
| ctf3-route-15v15-2 | 15 min / 4x | 3 / 1 | PASS | unavailable |
| ctf3-route-normal-15v15-1 | 10 min / 1x | 0 / 0 | FAIL | unavailable |

Accelerated captures: **ctf11 5**, **ctf10 6**, **ctf3 7**. Normal speed: **ctf11 red 1 / blue 2**, **ctf10 red 1 / blue 0**, **ctf3 red 0 / blue 0**.

These samples do **not establish a dependable capture-rate gain**. The carrier selector changes are verified in controlled fixtures, but a small stochastic comparison
cannot show that they reliably improve captures. Keep ctf3 as the preferred
baseline; ctf11 remains experimental.

| Sample group | Paired carrier samples | Pending waypoint switches | Switches per 100 pairs | Direct-to-waypoint | Waypoint-to-direct |
|---|---:|---:|---:|---:|---:|
| ctf10, 4x | 717 | 46 | 6.4 | 71 | 79 |
| ctf11, 4x | 694 | 28 | 4.0 | 64 | 71 |
| ctf10, 1x | 143 | 7 | 4.9 | 23 | 25 |
| ctf11, 1x | 214 | 13 | 6.1 | 22 | 26 |

These are one-second sampled carrier traces, not a count of all replans or
proof that a change was harmful. A waypoint-to-direct change normally means
progress past a waypoint. A nonzero-to-nonzero change can be a justified
threat response. Short gaps and hidden within-sample transitions remain
uncertain. ctf3 lacks this optional possession/route trace. The same runtime
instrumentation is used by ctf10 and ctf11.

Retained objective-progress failures:

- ctf3-route-normal-15v15-1: no team-score progress in objective/round test

A zero-capture objective check is a failure even if bots move, fight and gain
individual points. It is included in the kit rather than removed.

ctf11 authored FFA control: **23 kills** in 60 simulated seconds at 2x; movement and individual score progress pass. Japanese Castles warmup control: **0 kills** in 20 simulated seconds at 2x; individual/team scores remain zero. Both pass.


## 8. Integration, reproduction and rollback

The kit contains the full source, both dedicated-server packages, this document,
`bot-ai-cumulative-from-original.patch`,
`carrier-route-changes-from-ctf10.patch`, `all-ctf-changes-from-ctf3.patch`,
`bot-source-file-manifest.json`, launch/command/result JSONs, server/event logs,
measurements, build/validator logs and checksums. The bot-only cumulative patch
covers the 13 runtime source files. The incremental patch covers this iteration's
code, tests and documentation. Use the full source package for all test tools.

For the original build's source checkout, first verify its archive/file hashes,
then review and check the cumulative patch before applying it:

```sh
git apply --check /path/to/bot-ai-cumulative-from-original.patch
git apply /path/to/bot-ai-cumulative-from-original.patch
```

If integrating into a later writer's branch, review conflicts function by
function rather than assuming those files still match. Apply the original
cumulative patch only once; applying it on ctf10 would duplicate earlier work.
For an exact ctf10 checkout, use the incremental patch instead. Any structure or
header change requires rebuilding all dependent game objects; never install a
new header alongside stale objects. The source includes the dependency checker.

Linux dedicated/native-module reproduction from the full source:

```sh
PRODUCT_GIT_HASH=387d68a6-ctf11 python3 code/tools/make_git_version_header.py code/qcommon/git_version.h
bash tools/validate.sh
PRODUCT_GIT_HASH=387d68a6-ctf11 make release BUILD_CLIENT=0 BUILD_SERVER=1 BUILD_GAME_SO=1 BUILD_GAME_QVM=0 -j2
python3 tools/check-stale-objects.py build/release-linux-x86_64
```

Use README prerequisites and the release packager/toolchain for Windows or a
full graphical build. The delivered Windows binaries were cross-compiled with
MinGW-w64. Server packages require legitimate Quake Live assets; the native
module pak is the same six-module Linux/Windows pak in both platform archives.
Avoid reusing a home directory with an older extracted module when comparing
builds; the test runner creates an isolated runtime for every trial.

Prepare the private input packs with `tools/test-environment/prepare-map-assets.py`
and run the real match using the supplied, unchanged bot archive:

```sh
python3 tools/test-environment/prepare-map-assets.py --maps /path/to/maps.zip --botfiles /path/to/botfiles.zip --output private-assets
python3 tools/test-environment/run.py --build build/release-linux-x86_64 --root private-runs/ctf11-normal --mode ctf --map japanesecastles --asset-pk3 /path/to/botfiles.zip --asset-pk3 private-assets/bot-definitions.pk3 --asset-pk3 private-assets/japanesecastles.pk3 --characters anarki,bitterman,visor,sarge,major,hunter,keel,slash --bots 30 --skill 10 --seconds 600 --timescale 1 --report-interval 5 --track-interval 1000 --diagnostic-interval 15 --require-score --port 28340
```

The runner admits full teams before `g_doWarmup 0`; **no human is required**
for these CTF matches. Read team-score reports as captures, not kills or
individual score growth. Commands copied from the actual trials are included.
To regenerate the original comparison, extract the recorded original source
archive and run `tools/bot-change-handoff.py` with its original/current/archive/
botfiles/output arguments.

Rollback can restore the saved ctf10 engine/module package or revert the isolated
ctf11 source commit and rebuild. `bot_tactics 0` disables the new escort-detour
policy and autonomous CTF planner, but is **not** an exact rollback of timing,
entity caching or the shared carrier-return route corrections. The original
botfiles need no restoration because they were never edited.

## 9. Current limits and work remaining

This is a measured experimental AI trial, not a claim of a finished rewrite.
The new retention margin trades fewer marginal switches for potentially slower
response to small threat changes. Carrier route selection is shared even with
bot_tactics 0, so that setting is not a rollback of these three route fixes.
Health alone controls the larger supply budget; low ammo is not given a separate
exception. Fixed budgets can miss worthwhile detours and carrier death/route
changes remain stochastic. Room-level threat and per-bot quota allocation still
need better path-risk scoring and shared assignment. More normal-speed repeats,
team/character swaps and other maps are required to establish capture-rate
improvement. The latest 30v30 build has not been tested; older crowded trials
with no captures remain in the earlier reports.

The two previously saved engine findings and the separate CA/AD
spectator/forfeit issue remain deferred. No shadow-renderer or round-readiness
fix is hidden inside this AI patch.
