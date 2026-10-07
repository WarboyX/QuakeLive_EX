#!/usr/bin/env python3
"""Compile the complete carrier alternate selector with controlled AAS routes."""
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
harness_path = ROOT / 'tools/test-bot-performance.py'
namespace = {'__file__': str(harness_path)}
for node in ast.parse(harness_path.read_text()).body:
    if isinstance(node, (ast.Import, ast.ImportFrom, ast.FunctionDef)) or (
            isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'ROOT' for t in node.targets)):
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(harness_path), 'exec'), namespace)
source = (ROOT / 'code/game/ai_dmq3.c').read_text()
define = next(line for line in (ROOT / 'code/game/ai_main.h').read_text().splitlines()
              if line.startswith('#define LTG_GETFLAG '))
namespace['run_fixture']('CTF carrier complete route selection', r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
''' + define + r'''
typedef struct {
    int areanum, tfl, inuse, ltgtype, altrouteside;
    vec3_t origin; bot_goal_t altroutegoal;
    float reachedaltroutegoal_time;
    struct { int reportedstuck; } tac;
} bot_state_t;
static bot_state_t *botstates[MAX_CLIENTS];
static aas_altroutegoal_t red_altroutegoals[2], blue_altroutegoals[2];
static int red_altroutesides[2], blue_altroutesides[2];
static int red_numaltroutegoals = 2, blue_numaltroutegoals = 2;
static bot_goal_t ctf_redflag = {.areanum=99}, ctf_blueflag = {.areanum=88};
static int gametype = GT_CTF, carrying = 1, direct = 500, queries;
vmCvar_t bot_ctfDetours = {.integer = 3};
static int legs[2] = {100,50}, remaining[2] = {400,350};
int BotTeam(bot_state_t* bs) { (void)bs; return TEAM_RED; }
int BotCTFCarryingFlag(bot_state_t* bs) { (void)bs; return carrying; }
int BotRoomEnemies(bot_state_t* bs, vec3_t origin) { (void)bs; (void)origin; return 0; }
static int directdanger, detourdanger;
int BotCTFRouteThreat(bot_state_t* bs, int area, vec3_t origin, int target, vec3_t destination, int flags) {
    (void)bs;(void)origin;(void)destination;(void)flags;
    return area == 1 && target == 99 ? directdanger : detourdanger;
}
int BotRoomCrowding(bot_state_t* bs, vec3_t origin) { (void)bs; (void)origin; return 0; }
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t origin, int target, int flags) {
    (void)origin; queries++; assert(flags == 37);
    if (area == 1 && target == 99) return direct;
    if (area == 1 && target == 10) return legs[0];
    if (area == 1 && target == 20) return legs[1];
    if (target == 99 && area == 10) return remaining[0];
    if (target == 99 && area == 20) return remaining[1];
    assert(0); return 0;
}
''' + ''.join(namespace['function'](source, name) for name in (
    'BotCTFCarrierRouteCost', 'BotCTFKeepCarrierRoute', 'BotGetAlternateRouteGoal')) + r'''
static bot_state_t reset(void) {
    bot_state_t bs = {.areanum=1, .tfl=37, .altroutegoal={.areanum=10}};
    blue_altroutegoals[0].areanum = 10; blue_altroutegoals[1].areanum = 20;
    legs[0] = 100; legs[1] = 50; remaining[0] = 400; remaining[1] = 350;
    directdanger = detourdanger = 0; direct = 500; gametype = GT_CTF; carrying = 1; queries = 0;
    return bs;
}
int main(void) {
    bot_state_t bs = reset();
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 10);
    /* E226: detours off for the other team leave this one alone; off for its
       own team (red) the carrier goes straight home and asks nothing */
    bot_ctfDetours.integer = 1; bs = reset();
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 10);
    bot_ctfDetours.integer = 2; bs = reset(); queries = 0;
    assert(!BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 0 && queries == 0);
    bot_ctfDetours.integer = 3;
    bs = reset(); remaining[1] = 300; // replacement saves exactly 100 beyond incumbent preference
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); remaining[1] = 301; // 99 is insufficient
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 10);
    bs = reset(); bs.tac.reportedstuck = 1;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); bs.reachedaltroutegoal_time = 1;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); legs[0] = 19;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); legs[0] = 20; // boundary remains a valid new candidate
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 10);
    bs = reset(); remaining[0] = 500; // old route no longer progresses
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); legs[0] = 0;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); bs.altroutegoal.areanum = 777;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 20);
    bs = reset(); legs[0] = legs[1] = 19;
    assert(!BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && !bs.altroutegoal.areanum);
    bs = reset(); direct = 0; // all costs reject; never revive a random unscored waypoint
    assert(!BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && !bs.altroutegoal.areanum);
    bs = reset(); gametype = GT_FFA; carrying = 0;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && !queries);
    bs = reset(); directdanger=0; detourdanger=2;
    assert(!BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && !bs.altroutegoal.areanum);
    bs = reset(); directdanger=5; detourdanger=2;
    assert(BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && bs.altroutegoal.areanum == 10);
    bs = reset(); legs[0]=150; remaining[0]=400; remaining[1]=500;
    assert(!BotGetAlternateRouteGoal(&bs, TEAM_BLUE) && !bs.altroutegoal.areanum);
    puts("Per-team detour switch (E226), complete selector: arrival boundary, meaningful savings, stuck/reached/invalid release, no-route fallback and legacy mode verified");
    return 0;
}
''')
