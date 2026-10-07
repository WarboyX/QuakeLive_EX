#!/usr/bin/env python3
"""Compile exact-source fixtures for bot snapshots, timing, and scoring gates.

Requires a host C compiler, no proprietary game assets. These fixtures do not
simulate bot navigation or establish a live-match frame-rate improvement.
"""
import os
import platform
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    """Extract a definition, ignoring braces in strings and comments."""
    match = re.search(r"^[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError(f"definition not found: {name}")
    start = source.index("{", match.start())
    token = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for part in token.finditer(source, start):
        if part.group() == "{":
            depth += 1
        elif part.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():part.end()]
    raise RuntimeError(f"unterminated definition: {name}")


def run_fixture(name, code):
    with tempfile.TemporaryDirectory(prefix="ql-bot-test-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(code)
        arch = platform.machine().lower()
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                        '-DARCH_STRING="' + arch + '"',
                        "-I", str(ROOT / "code"), str(path / "test.c"), "-o", str(path / "test"), "-lm"], check=True)
        subprocess.run([str(path / "test")], check=True)
    print(f"{name}: PASS", flush=True)


ai = (ROOT / "code/game/ai_main.c").read_text()
engine = (ROOT / "code/server/sv_game.c").read_text()
main = (ROOT / "code/server/sv_main.c").read_text()
game = (ROOT / "code/game/g_main.c").read_text()
combat = (ROOT / "code/game/g_combat.c").read_text()

run_fixture("client snapshots and game wrapper", r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include <assert.h>
#include <stdio.h>
static int reads, revision = 1, enabled, markers, result = qtrue;
static char sequence[128];
void trap_AAS_EntityInfo(int number, void *output) {
    aas_entityinfo_t *info = output;
    memset(info, 0, sizeof(*info));
    info->number = number;
    info->origin[0] = revision;
    info->valid = number >= 0 && number < MAX_CLIENTS;
    reads++;
}
int trap_Cvar_VariableIntegerValue(const char *name) {
    assert(!strcmp(name, "com_cpuTimings"));
    return enabled;
}
int trap_BotLibVarSet(char *name, char *value) {
    assert(!strcmp(name, BOT_TIMING_MARKER));
    strcat(sequence, value); strcat(sequence, " "); markers++;
    return 0;
}
''' + ai[ai.index("static qboolean botEntityCacheActive;"):ai.index("/*\n==============\nNumBots")] + r'''
static qboolean botTimingEnabled;
''' + function(ai, "BotTimingStage") + r'''
static int BotAIStartFrameRun(int time) {
    (void)time;
    assert(!botEntityCacheActive);
    botEntityCacheActive = qtrue; // simulate an AAS failure inside the AI loop
    return result;
}
''' + function(ai, "BotAIStartFrame") + r'''
int main(void) {
    aas_entityinfo_t a, b;
    int i, bot, pass;
    botEntityCacheActive = qtrue;
    for (bot = 0; bot < 60; bot++) for (pass = 0; pass < 5; pass++) for (i = 0; i < 60; i++) {
        BotEntityInfo(i, &a);
        assert(a.valid && a.number == i && a.origin[0] == 1);
    }
    assert(reads == 60);
    BotEntityInfo(0, &a); a.origin[0] = 99;
    BotEntityInfo(0, &b); assert(b.origin[0] == 1);
    BotEntityInfo(-1, &a); BotEntityInfo(MAX_CLIENTS, &a);
    assert(reads == 62);
    botEntityCacheActive = qfalse; revision = 2;
    BotEntityInfo(0, &a); assert(a.origin[0] == 2 && reads == 63);
    memset(botEntityCacheValid, 0, sizeof(botEntityCacheValid));
    botEntityCacheActive = qtrue;
    BotEntityInfo(0, &a); assert(a.origin[0] == 2 && reads == 64);
    enabled = 1; result = qfalse;
    assert(BotAIStartFrame(100) == qfalse);
    assert(!botEntityCacheActive && !botTimingEnabled && markers == 2);
    assert(!strcmp(sequence, "begin end "));
    enabled = 0; result = qtrue;
    assert(BotAIStartFrame(200) == qtrue);
    assert(!botEntityCacheActive && markers == 2);
    puts("60 clients x 60 bots x 5 reads: 18000 lookups -> 60 botlib reads; copies, bypass, refresh and early-return cleanup pass");
    return 0;
}
''')

simulation_start = main.index("    {", main.index("// run the game simulation in chunks"))
simulation_end = main.index("\n\n    if (com_speeds", simulation_start)
run_fixture("native timing and server accounting", r'''
#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "botlib/botlib.h"
#include <assert.h>
#include <stdio.h>
cvar_t timing;
cvar_t *com_cpuTimings = &timing;
int64_t com_usBots, com_usGame, com_usScene, com_usSubmit, com_usBotStages[BOT_TIMING_STAGES];
static int64_t now;
int64_t Sys_Microseconds(void) { return now; }
static void *sv_vmMainTable[1];
#define GAME_RUN_FRAME 0
static botlib_export_t *botlib_export;
''' + engine[engine.index("static struct {\n    qboolean allowed, active;"):engine.index("static int SV_GI_BotLibDefine")] + function(engine, "SV_GameRunFrame") + r'''
static struct { int timeResidual, time; } sv;
static struct { int time; } svs;
static int omitEnd;
static void FakeGame(int time) {
    (void)time;
    now += 10;
    SV_GI_BotLibVarSet(BOT_TIMING_MARKER, "begin");
    now += 20;
    SV_GI_BotLibVarSet(BOT_TIMING_MARKER, "world");
    now += 30;
    SV_GI_BotLibVarSet(BOT_TIMING_MARKER, "ai");
    now += 40;
    SV_GI_BotLibVarSet(BOT_TIMING_MARKER, "input");
    now += 50;
    if (!omitEnd) SV_GI_BotLibVarSet(BOT_TIMING_MARKER, "end");
}
static void Simulate(void) {
    int frameMsec = 50;
''' + main[simulation_start:simulation_end] + r'''
}
int main(void) {
    int stage;
    sv_vmMainTable[0] = (void *)FakeGame;
    timing.integer = 1; sv.timeResidual = 100;
    Simulate();
    assert(com_usBots == 280 && com_usGame == 20 && now == 300);
    for (stage = 0; stage < BOT_TIMING_STAGES; stage++) assert(com_usBotStages[stage] == (20 + 10 * stage) * 2);
    assert(!sv_botTiming.active && !sv_botTiming.allowed);
    SV_GI_BotLibVarSet(BOT_TIMING_MARKER, "begin"); // outside GAME_RUN_FRAME
    assert(!sv_botTiming.active);
    omitEnd = 1; sv.timeResidual = 50; Simulate();
    assert(com_usBots == 420 && com_usGame == 30);
    timing.integer = 0; sv.timeResidual = 50; Simulate();
    assert(com_usBots == 420 && com_usGame == 30);
    timing.integer = 1; omitEnd = 0; sv.timeResidual = 50; Simulate();
    assert(com_usBots == 560 && com_usGame == 40);
    sv_botTiming.allowed = qtrue; now = 0;
    SV_BotTimingMark("begin"); SV_BotTimingMark("begin");
    now = 7; SV_BotTimingMark("unknown"); SV_BotTimingMark("end"); SV_BotTimingMark("end");
    assert(com_usBots == 567 && !sv_botTiming.active);
    puts("Actual bot phases sum correctly; game excludes bots; multiple ticks, off/on, zero clock, missing end and duplicate markers pass");
    return 0;
}
''')

run_fixture("bot-only readiness and scoring guards", r'''
#include "game/g_local.h"
#include <assert.h>
#include <stdio.h>
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
vmCvar_t g_training, g_gametype, g_rrAllowNegativeScores, g_doWarmup, g_warmupDelay,
    bot_autoReady, g_teamForcePresent, g_forfeit, g_teamSizeMin, sv_warmupReadyPercentage;
static int plums, rankUpdates;
static const char *blocked;
static void WarmupBlocked(const char *why) { blocked = why; }
qboolean G_CheckTeamBalance(void) { return qtrue; }
int TeamCount(int ignored, team_t team) {
    int i, count = 0;
    for (i = 0; i < level.maxclients; i++) if (i != ignored && clients[i].pers.connected == CON_CONNECTED && clients[i].sess.sessionTeam == team) count++;
    return count;
}
void ScorePlum(gentity_t *ent, vec3_t origin, int score) { (void)ent; (void)origin; (void)score; plums++; }
void AddTeamScore(vec3_t origin, int team, int score) { (void)origin; level.teamScores[team] += score; }
void CalculateRanks(void) { rankUpdates++; }
''' + function(game, "CheckWarmupConditions") + function(combat, "AddScore") + r'''
static void Reset(void) {
    int i;
    memset(&level, 0, sizeof(level)); memset(clients, 0, sizeof(clients));
    memset(g_entities, 0, sizeof(g_entities));
    level.clients = clients; level.maxclients = 2; level.numPlayingClients = 2;
    for (i = 0; i < 2; i++) {
        clients[i].pers.connected = CON_CONNECTED;
        clients[i].sess.sessionTeam = TEAM_FREE;
        g_entities[i].client = &clients[i]; g_entities[i].r.svFlags = SVF_BOT;
    }
    level.warmupTime = -1; g_doWarmup.integer = 1; bot_autoReady.integer = 1;
    sv_warmupReadyPercentage.value = 0.5f; g_gametype.integer = GT_FFA;
    blocked = NULL;
}
int main(void) {
    vec3_t origin = {0}; int i;
    Reset();
    assert(!CheckWarmupConditions() && blocked && level.numReadyClients == 2 && level.numReadyHumans == 0);
    for (i = 0; i < 100; i++) { level.time += 1000; AddScore(&g_entities[0], origin, 1); }
    assert(clients[0].ps.persistant[PERS_SCORE] == 0 && plums == 0);
    g_doWarmup.integer = 0; assert(CheckWarmupConditions());
    level.warmupTime = 0; // match transition is managed separately by the game
    for (i = 0; i < 100; i++) { level.time += 1000; AddScore(&g_entities[0], origin, 1); }
    assert(clients[0].ps.persistant[PERS_SCORE] == 100 && plums == 100 && rankUpdates == 100);
    level.intermissionQueued = 1; AddScore(&g_entities[0], origin, 1); level.intermissionQueued = 0;
    level.gameStatsReported = qtrue; AddScore(&g_entities[0], origin, 1); level.gameStatsReported = qfalse;
    level.scoringDisabled = qtrue; AddScore(&g_entities[0], origin, 1); level.scoringDisabled = qfalse;
    g_gametype.integer = GT_RACE; AddScore(&g_entities[0], origin, 1); g_gametype.integer = GT_TEAM;
    assert(clients[0].ps.persistant[PERS_SCORE] == 100);
    clients[0].ps.persistant[PERS_TEAM] = TEAM_RED;
    AddScore(&g_entities[0], origin, 3); assert(level.teamScores[TEAM_RED] == 3);
    g_training.integer = 1; g_entities[1].r.svFlags = 0;
    AddScore(&g_entities[1], origin, 3); assert(clients[1].ps.persistant[PERS_SCORE] == 0);
    AddScore(&g_entities[0], origin, 3); assert(clients[0].ps.persistant[PERS_SCORE] == 106);
    Reset(); g_gametype.integer = GT_DUEL; assert(CheckWarmupConditions());
    Reset(); sv_warmupReadyPercentage.value = 0; assert(CheckWarmupConditions());
    puts("Bot-only FFA warmup blocks scoring; live bots accrue event scores over time; TDM, training and end-of-match guards pass; readiness bypasses verified");
    return 0;
}
''')
