#!/usr/bin/env python3
"""Exercise the actual CTF recovery function without proprietary map assets."""
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Reuse the exact-source fixture harness without executing its other fixtures.
harness = (ROOT / "tools/test-bot-performance.py").read_text()
namespace = {"__file__": str(ROOT / "tools/test-bot-performance.py")}
tree = ast.parse(harness)
for node in tree.body:
    if isinstance(node, (ast.Import, ast.ImportFrom, ast.FunctionDef)) or (
            isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "ROOT" for t in node.targets)):
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(ROOT / "tools/test-bot-performance.py"), "exec"), namespace)
source = (ROOT / "code/game/ai_tactics.c").read_text()
decisions = (ROOT / "code/game/ai_dmq3.c").read_text()
defines = "\n".join(line for filename in ("ai_main.h", "ai_tactics.h")
                    for line in (ROOT / "code/game" / filename).read_text().splitlines()
                    if line.startswith(("#define LTG_", "#define CTFROLE_", "#define CTF_GETFLAG_TIME")))
escort_cache = source[source.index("typedef struct {\n    int frame, carrier;"):source.index("/*\n==================\nBotTacticsEnabled")]
cache = source[source.index("typedef struct {"):source.index("/*\n==================\nBotTacticsEnabled")]
namespace["run_fixture"]("CTF recovery destinations and shared sight cache", r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
''' + defines + r'''
typedef struct bot_state_s {
    int client, ltgtype, teammate, ordered, inuse, decisionmaker, areanum, tfl;
    vec3_t origin; bot_goal_t teamgoal;
    float owndecision_time, teamgoal_time;
    struct { int assignedrole; } tac;
} bot_state_t;
bot_state_t *botstates[MAX_CLIENTS];
vmCvar_t bot_tactics, bot_ctfIntercept = {.integer = 3};
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
int gametype = GT_CTF;
static float floattime;
#define FloatTime() floattime
bot_goal_t ctf_redflag, ctf_blueflag;
static flagStatus_t flagstatus[4];
static int visible[MAX_CLIENTS], reads, traces, pvs = qtrue;
static vec3_t carrierOrigin;
int BotTeam(bot_state_t *bs) { return clients[bs->client].sess.sessionTeam; }
int BotCTFCarryingFlag(bot_state_t *bs) {
    return clients[bs->client].ps.powerups[BotTeam(bs) == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] != 0;
}
flagStatus_t Team_GetFlagStatus(int team) { return flagstatus[team]; }
int BotPointAreaNum(vec3_t point) { return point[0] ? (int)point[0] : 0; }
int trap_AAS_AreaReachability(int area) { return area > 0; }
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t origin, int target, int flags) {
    (void)area; (void)flags; return (int)(fabs(origin[0] - target) * (100.0f / 320.0f)) + 1;
}
void BotEntityInfo(int entity, aas_entityinfo_t *info) {
    reads++; memset(info, 0, sizeof(*info));
    info->valid = qtrue; info->number = entity;
    VectorCopy(carrierOrigin, info->origin);
}
qboolean trap_InPVS(const vec3_t a, const vec3_t b) { (void)a; (void)b; return pvs; }
float BotEntityVisible(int viewer, vec3_t eye, vec3_t angles, float fov, int entity) {
    (void)eye; (void)angles; (void)fov; (void)entity; traces++;
    return visible[viewer];
}
static int routes, statuses;
int BotCTFRoleCrowded(bot_state_t *bs, int role) { (void)bs; (void)role; return qfalse; }
int BotOppositeTeam(bot_state_t *bs) { return BotTeam(bs) == TEAM_RED ? TEAM_BLUE : TEAM_RED; }
int BotGetAlternateRouteGoal(bot_state_t *bs, int team) { (void)bs; (void)team; routes++; return qtrue; }
void BotSetTeamStatus(bot_state_t *bs) { (void)bs; statuses++; }
static int BotCTFEscortInterceptTime(bot_state_t *bs, int carrier) { (void)bs; (void)carrier; return -1; }
''' + cache + namespace["function"](source, "BotCTFRecoveryGoal") + namespace["function"](source, "BotCTFKeepObjective") + namespace["function"](source, "BotCTFEscortLimit") + namespace["function"](source, "BotCTFEscortTravelTime") + namespace["function"](source, "BotCTFEscortEligible") + namespace["function"](source, "BotCTFReleaseEscort") + namespace["function"](decisions, "BotCTFEnforceOffense") + r'''
static void advance(void) { level.time += 100; floattime += .1f; }
int main(void) {
    bot_state_t red = {.client=0, .teammate=-1, .areanum=1}, blue = {.client=1, .teammate=-1};
    bot_state_t escort = {.client=4, .areanum=1, .ltgtype=LTG_TEAMACCOMPANY, .teammate=2, .inuse=1};
    bot_goal_t goal; gitem_t item;
    int i, before;
    level.maxclients = 4; level.num_entities = 5;
    ctf_redflag.areanum = 10; ctf_redflag.origin[0] = 10;
    ctf_blueflag.areanum = 20; ctf_blueflag.origin[0] = 20;
    for (i = 0; i < 4; i++) {
        g_entities[i].inuse = qtrue; g_entities[i].client = &clients[i];
        clients[i].pers.connected = CON_CONNECTED;
        clients[i].sess.sessionTeam = i % 2 ? TEAM_BLUE : TEAM_RED;
        clients[i].ps.stats[STAT_HEALTH] = 100;
    }
    ctfRecovery[0].frame = ctfRecovery[1].frame = -1;
    assert(!BotCTFRecoveryGoal(&red, &goal)); // home flag: no recovery job
    flagstatus[TEAM_RED] = FLAG_TAKEN;
    clients[1].ps.powerups[PW_REDFLAG] = 1;
    carrierOrigin[0] = 40; visible[2] = 1; // an ally sees the carrier
    assert(BotCTFRecoveryGoal(&red, &goal));
    assert(goal.areanum == 40 && goal.entitynum == 1 && goal.flags == 0);
    before = reads;
    for (i = 0; i < 30; i++) assert(BotCTFRecoveryGoal(&red, &goal));
    assert(reads == before); // once per frame, not once per pursuer
    advance(); visible[2] = 0; carrierOrigin[0] = 80;
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 40); // no hidden position leak
    advance(); visible[2] = 1; clients[2].ps.stats[STAT_HEALTH] = 0;
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 40); // dead ally cannot spot
    clients[2].ps.stats[STAT_HEALTH] = 100;
    clients[2].sess.sessionTeam = TEAM_SPECTATOR; advance();
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 40);
    clients[2].sess.sessionTeam = TEAM_RED; pvs = qfalse; advance(); before = traces;
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 40);
    assert(traces == before); // PVS rejects without visibility traces
    floattime = 16; level.time = 16000;
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 20); // expired sight: search stand
    pvs = qtrue; advance();
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 80);
    flagstatus[TEAM_RED] = FLAG_ATBASE;
    assert(!BotCTFRecoveryGoal(&red, &goal));
    flagstatus[TEAM_RED] = FLAG_TAKEN; visible[2] = 0;
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 20); // retake clears old sight
    memset(&item, 0, sizeof(item)); item.giType = IT_TEAM; item.giTag = PW_REDFLAG;
    g_entities[4].inuse = qtrue; g_entities[4].flags = FL_DROPPED_ITEM;
    g_entities[4].item = &item; g_entities[4].r.currentOrigin[0] = 60;
    flagstatus[TEAM_RED] = FLAG_DROPPED;
    assert(BotCTFRecoveryGoal(&red, &goal)); // same-frame status transition refreshes
    assert(goal.areanum == 60 && goal.entitynum == 4 && goal.flags == (GFL_ITEM | GFL_DROPPED));
    flagstatus[TEAM_BLUE] = FLAG_TAKEN; clients[0].ps.powerups[PW_BLUEFLAG] = 1;
    carrierOrigin[0] = 90; visible[1] = 1;
    assert(BotCTFRecoveryGoal(&blue, &goal) && goal.areanum == 90); // independent team cache
    advance(); g_entities[4].inuse = qfalse;
    assert(BotCTFRecoveryGoal(&red, &goal) && goal.areanum == 20); // drop vanished: safe search
    gametype = GT_FFA; assert(!BotCTFRecoveryGoal(&red, &goal));
    gametype = GT_CTF; clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    assert(!BotCTFRecoveryGoal(&red, &goal));
    clients[0].sess.sessionTeam = TEAM_RED; bot_tactics.integer = 1;
    assert(BotCTFKeepObjective(&red)); // carrying the enemy flag
    clients[0].ps.powerups[PW_BLUEFLAG] = 0;
    assert(!BotCTFKeepObjective(&red));
    red.ltgtype = LTG_RETURNFLAG; flagstatus[TEAM_RED] = FLAG_TAKEN;
    assert(BotCTFKeepObjective(&red));
    flagstatus[TEAM_RED] = FLAG_ATBASE; assert(!BotCTFKeepObjective(&red));
    red.ltgtype = LTG_GETFLAG; assert(BotCTFKeepObjective(&red));
    red.ltgtype = LTG_TEAMACCOMPANY; red.teammate = 2;
    clients[2].ps.powerups[PW_BLUEFLAG] = 1; assert(BotCTFKeepObjective(&red));
    clients[2].ps.stats[STAT_HEALTH] = 0; assert(!BotCTFKeepObjective(&red));
    clients[2].ps.stats[STAT_HEALTH] = 100;
    clients[2].pers.connected = CON_DISCONNECTED; assert(!BotCTFKeepObjective(&red));
    clients[2].pers.connected = CON_CONNECTED;
    red.teammate = level.maxclients; assert(!BotCTFKeepObjective(&red));
    red.ltgtype = LTG_GETFLAG; bot_tactics.integer = 0; assert(!BotCTFKeepObjective(&red));
    bot_tactics.integer = 1; gametype = GT_1FCTF; assert(!BotCTFKeepObjective(&red));
    gametype = GT_CTF; level.maxclients = 6;
    g_entities[4].inuse = qtrue; g_entities[4].client = &clients[4];
    clients[4].pers.connected = CON_CONNECTED; clients[4].sess.sessionTeam = TEAM_RED;
    clients[4].ps.stats[STAT_HEALTH] = 100;
    botstates[0] = &red; botstates[4] = &escort; red.inuse = 1;
    red.ltgtype = LTG_TEAMACCOMPANY; red.teammate = 2;
    flagstatus[TEAM_RED] = FLAG_TAKEN;
    g_entities[2].r.currentOrigin[0] = ctf_redflag.origin[0];
    red.origin[0] = 11; escort.origin[0] = 40;
    assert(!BotCTFReleaseEscort(&red)); // nearest guard remains
    assert(BotCTFReleaseEscort(&escort)); // surplus escort goes to recovery
    escort.ordered = 1; assert(!BotCTFReleaseEscort(&escort));
    escort.ordered = 0; flagstatus[TEAM_RED] = FLAG_ATBASE;
    assert(BotCTFReleaseEscort(&escort)); // travelling quota still applies
    flagstatus[TEAM_RED] = FLAG_TAKEN; g_entities[2].r.currentOrigin[0] = 1000; advance();
    assert(!BotCTFReleaseEscort(&escort)); // travelling carrier keeps escorts
    g_entities[2].r.currentOrigin[0] = 10; clients[0].ps.stats[STAT_HEALTH] = 0; advance();
    assert(!BotCTFReleaseEscort(&escort)); // a dead escort cannot occupy the guard slot
    {
        bot_state_t third = {.client=6, .areanum=1, .ltgtype=LTG_TEAMACCOMPANY, .teammate=2, .inuse=1};
        level.maxclients = 10; clients[0].ps.stats[STAT_HEALTH] = 100;
        for (i = 6; i <= 8; i += 2) {
            g_entities[i].inuse = qtrue; g_entities[i].client = &clients[i];
            clients[i].pers.connected = CON_CONNECTED; clients[i].sess.sessionTeam = TEAM_RED;
            clients[i].ps.stats[STAT_HEALTH] = 100;
        }
        third.origin[0] = 60; botstates[6] = &third; advance();
        assert(!BotCTFReleaseEscort(&red) && !BotCTFReleaseEscort(&escort));
        assert(BotCTFReleaseEscort(&third)); // larger team keeps two guards
    }
    red.ltgtype = LTG_RETURNFLAG; red.tac.assignedrole = CTFROLE_ATTACK;
    flagstatus[TEAM_RED] = FLAG_TAKEN; bot_tactics.integer = 1;
    BotCTFEnforceOffense(&red); assert(red.ltgtype == LTG_RETURNFLAG && routes == 0);
    flagstatus[TEAM_RED] = FLAG_DROPPED;
    BotCTFEnforceOffense(&red); assert(red.ltgtype == LTG_RETURNFLAG && routes == 0);
    flagstatus[TEAM_RED] = FLAG_ATBASE; red.owndecision_time = 99;
    BotCTFEnforceOffense(&red);
    assert(red.ltgtype == LTG_RETURNFLAG && red.owndecision_time == 99 && routes == 0 && statuses == 0); // planner owns reassignment
    red.ltgtype = LTG_RETURNFLAG; flagstatus[TEAM_RED] = FLAG_TAKEN;
    bot_tactics.integer = 0; red.owndecision_time = 99;
    BotCTFEnforceOffense(&red);
    assert(red.ltgtype == LTG_GETFLAG && red.owndecision_time == 99); // old tactics-disabled policy
    red.ltgtype = LTG_RETURNFLAG; flagstatus[TEAM_RED] = FLAG_DROPPED;
    BotCTFEnforceOffense(&red); assert(red.ltgtype == LTG_RETURNFLAG);
    puts("CTF destinations, sight expiry, status transitions and per-frame sharing verified");
    return 0;
}
''')

namespace["run_fixture"]("CTF planner roles, transition handling and bounded escorts", r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
''' + defines + r'''
#define CTF_MAX_DEFENDERS 8
#define INVENTORY_HEALTH 29
typedef struct bot_state_s {
    int client, entitynum, ltgtype, teammate, ordered, inuse, decisionmaker, lastgoal_ltgtype;
    int areanum, arrive_time, tfl;
    vec3_t origin;
    bot_goal_t teamgoal, altroutegoal;
    float owndecision_time, teamgoal_time, teammessage_time, defendaway_time;
    float ctfroam_time, teammatevisible_time, formation_dist, nbg_time, lastair_time;
    int inventory[64];
    struct { int assignedrole, ctfphase; float roleredecide_time; } tac;
} bot_state_t;
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
static bot_state_t states[MAX_CLIENTS];
bot_state_t *botstates[MAX_CLIENTS];
vmCvar_t bot_tactics, bot_debugTactics, bot_ctfIntercept = {.integer = 3};
int gametype = GT_CTF;
static float floattime = 10;
#define FloatTime() floattime
bot_goal_t ctf_redflag, ctf_blueflag;
static flagStatus_t flags[4];
static int carrier = -1, routes, statuses;
int BotTeam(bot_state_t* bs) { return clients[bs->client].sess.sessionTeam; }
int BotTacticsEnabled(void) { return bot_tactics.integer; }
int BotTeamFlagCarrier(bot_state_t* bs) { (void)bs; return carrier; }
flagStatus_t Team_GetFlagStatus(int team) { return flags[team]; }
static int droppedavailable;
int BotCTFEnemyFlagAvailable(bot_state_t* bs) {
 int status=flags[BotTeam(bs)==TEAM_RED?TEAM_BLUE:TEAM_RED];
 return status==FLAG_ATBASE || (status==FLAG_DROPPED && droppedavailable);
}
int BotEnemyFlagAtBase(bot_state_t* bs) { return flags[BotTeam(bs) == TEAM_RED ? TEAM_BLUE : TEAM_RED] == FLAG_ATBASE; }
int BotOppositeTeam(bot_state_t* bs) { return BotTeam(bs) == TEAM_RED ? TEAM_BLUE : TEAM_RED; }
int BotGetAlternateRouteGoal(bot_state_t* bs, int team) { (void)bs; (void)team; routes++; return qtrue; }
void BotSetTeamStatus(bot_state_t* bs) { (void)bs; statuses++; }
void QDECL G_Printf(const char *fmt, ...) { (void)fmt; }
int BotPointAreaNum(vec3_t point) { (void)point; return 1; }
int trap_AAS_AreaReachability(int area) { return area > 0; }
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t origin, int target, int flags) {
    (void)area; (void)origin; (void)target; (void)flags; return 100;
}
static int BotCTFEscortInterceptTime(bot_state_t *bs, int carrier) { (void)bs; (void)carrier; return -1; }
''' + escort_cache + ''.join(namespace["function"](source, name) for name in (
    "BotCTFEscortLimit", "BotCTFEscortTravelTime", "BotCTFEscortEligible", "BotCTFReleaseEscort",
    "BotCTFRoleMix", "BotCTFRoleWanted", "BotCTFClientRole", "BotCTFRoleCounts", "BotCTFRoleCrowded", "BotCTFPickRole"))
    + namespace["function"](decisions, "BotCTFPlanGoals") + r'''
static void reset(int size, int team) {
    int i;
    memset(&level, 0, sizeof(level)); memset(g_entities, 0, sizeof(g_entities));
    memset(clients, 0, sizeof(clients)); memset(states, 0, sizeof(states));
    memset(botstates, 0, sizeof(botstates)); memset(flags, 0, sizeof(flags));
    memset(ctfEscortCosts, 0, sizeof(ctfEscortCosts));
    ctfEscortCosts[0].frame = ctfEscortCosts[1].frame = -1;
    level.maxclients = size; carrier = -1; bot_tactics.integer = 1; droppedavailable=0;
    ctf_redflag.origin[0] = -2000; ctf_redflag.areanum = 1;
    ctf_blueflag.origin[0] = 2000; ctf_blueflag.areanum = 2;
    for (i = 0; i < size; i++) {
        g_entities[i].inuse = 1; g_entities[i].client = &clients[i];
        clients[i].pers.connected = CON_CONNECTED;
        clients[i].sess.sessionTeam = team; clients[i].ps.stats[STAT_HEALTH] = 100;
        states[i].inuse = 1; states[i].client = i; states[i].areanum = 1; states[i].teammate = -1;
        states[i].origin[0] = 100 + i * 100; states[i].tac.assignedrole = -1; states[i].tac.ctfphase = -1;
        botstates[i] = &states[i];
        states[i].inventory[INVENTORY_HEALTH] = 100; states[i].lastair_time = floattime;
    }
}
int main(void) {
    int team, i, slots, have[CTFROLE_COUNT], size;
    float want[CTFROLE_COUNT];
    for (team = TEAM_RED; team <= TEAM_BLUE; team++) {
        reset(30, team);
        flags[team] = FLAG_TAKEN;
        states[1].ltgtype = LTG_RETURNFLAG;
        states[2].ltgtype = LTG_RUSHBASE;
        states[3].ltgtype = LTG_GETFLAG;
        states[4].ltgtype = LTG_DEFENDKEYAREA;
        BotCTFRoleCounts(&states[0], have, &size);
        assert(size == 30 && have[CTFROLE_RECOVER] == 1 && have[CTFROLE_CARRIER] == 1);
        assert(have[CTFROLE_ATTACK] == 1 && have[CTFROLE_DEFEND] == 1);
        states[5].tac.assignedrole = CTFROLE_ROAM;
        VectorCopy((team == TEAM_RED ? ctf_redflag : ctf_blueflag).origin, g_entities[5].r.currentOrigin);
        assert(BotCTFClientRole(5, team) == CTFROLE_ROAM); // spawn location cannot disguise an assigned roam
        botstates[6] = NULL;
        clients[6].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
        assert(BotCTFClientRole(6, team) == CTFROLE_CARRIER); // human carrier
        BotCTFRoleWanted(&states[0], size, want);
        assert(want[CTFROLE_RECOVER] > want[CTFROLE_DEFEND]);
        assert(want[CTFROLE_DEFEND] <= 8);
        states[0].ltgtype = LTG_RETURNFLAG; states[0].teamgoal_time = floattime + 30;
        states[0].tac.roleredecide_time = floattime + 5;
        BotCTFPlanGoals(&states[0]); assert(states[0].ltgtype == LTG_RETURNFLAG);
        flags[team] = FLAG_ATBASE;
        BotCTFPlanGoals(&states[0]); assert(states[0].ltgtype != LTG_RETURNFLAG);
        states[0].ordered = 1; states[0].ltgtype = LTG_PATROL;
        states[0].teamgoal_time = floattime + 60;
        states[0].decisionmaker = 17; states[0].lastgoal_ltgtype = LTG_PATROL;
        flags[team] = FLAG_TAKEN;
        BotCTFPlanGoals(&states[0]);
        assert(states[0].ltgtype == LTG_PATROL && states[0].decisionmaker == 17 &&
               states[0].ordered && states[0].lastgoal_ltgtype == LTG_PATROL);
        states[0].teamgoal_time = floattime - 1;
        BotCTFPlanGoals(&states[0]);
        assert(!states[0].ordered && !states[0].lastgoal_ltgtype); // completed orders cannot block planning forever
        reset(30, team);
        for (i = 0; i < 20; i++) {
            states[i].ltgtype = LTG_GETFLAG; states[i].teamgoal_time = floattime + 60;
            states[i].tac.roleredecide_time = floattime + 30; states[i].tac.ctfphase = 0;
        }
        flags[team] = FLAG_TAKEN;
        BotCTFPlanGoals(&states[0]);
        assert(states[0].ltgtype == LTG_RETURNFLAG); // grab invalidates the role cooldown
        reset(30, team); carrier = 29;
        clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
        states[0].nbg_time = floattime + 20;
        BotCTFPlanGoals(&states[0]);
        assert(states[0].ltgtype == LTG_TEAMACCOMPANY && states[0].nbg_time == 0);
        states[0].nbg_time = floattime + 20;
        BotCTFPlanGoals(&states[0]);
        assert(states[0].nbg_time == floattime + 20); // unchanged assignment does not repeatedly cancel tasks
        reset(30, team); carrier = 29;
        clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
        states[0].inventory[INVENTORY_HEALTH] = 40; states[0].nbg_time = floattime + 20;
        BotCTFPlanGoals(&states[0]); assert(states[0].nbg_time == floattime + 2.5f);
        reset(30, team); carrier = 29;
        clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
        states[0].lastair_time = floattime - 7; states[0].nbg_time = floattime + 20;
        BotCTFPlanGoals(&states[0]); assert(states[0].nbg_time == floattime + 20);
        reset(30, team); carrier = 29;
        clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
        slots = 0;
        for (i = 0; i < 29; i++) slots += BotCTFEscortEligible(&states[i], carrier);
        assert(slots == 4); // nearest travelling screen
        for (i = 0; i < 4; i++) {
            states[i].ltgtype = LTG_TEAMACCOMPANY; states[i].teammate = carrier;
            states[i].origin[0] = 1000 + i * 100;
        }
        states[4].origin[0] = 1; level.time++; // new nearest candidate
        assert(!BotCTFEscortEligible(&states[4], carrier)); // wait for a vacancy, even before old bots think
        assert(BotCTFReleaseEscort(&states[3]));
        states[3].ltgtype = 0;
        assert(BotCTFEscortEligible(&states[4], carrier));
        reset(30, team); carrier = 29;
        clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
        states[10].ordered = 1; states[10].ltgtype = LTG_TEAMACCOMPANY; states[10].teammate = carrier;
        slots = 0;
        for (i = 0; i < 29; i++) slots += BotCTFEscortEligible(&states[i], carrier);
        assert(slots == 4); // the ordered escort occupies one slot
        flags[team] = FLAG_TAKEN;
        VectorCopy((team == TEAM_RED ? ctf_redflag : ctf_blueflag).origin, g_entities[carrier].r.currentOrigin); level.time++;
        slots = 0;
        for (i = 0; i < 29; i++) slots += BotCTFEscortEligible(&states[i], carrier);
        assert(slots == 2); // waiting home guard
        clients[0].pers.connected = CON_DISCONNECTED;
        clients[1].ps.stats[STAT_HEALTH] = 0;
        VectorCopy(g_entities[carrier].r.currentOrigin, states[2].origin); level.time++;
        assert(BotCTFEscortEligible(&states[2], carrier));
        states[10].ordered = 0;
        states[0].ordered = 0; states[0].ltgtype = LTG_TEAMACCOMPANY; states[0].teammate = carrier;
        clients[0].pers.connected = CON_CONNECTED; clients[0].ps.stats[STAT_HEALTH] = 100;
        states[0].origin[0] = 9999; level.time++;
        BotCTFPlanGoals(&states[0]); assert(states[0].ltgtype != LTG_TEAMACCOMPANY);
        clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 0;
        assert(!BotCTFEscortEligible(&states[2], carrier));
        assert(!BotCTFEscortEligible(&states[2], -1));
        assert(!BotCTFEscortEligible(&states[2], 30));
        reset(15, team); flags[team==TEAM_RED?TEAM_BLUE:TEAM_RED]=FLAG_DROPPED;
        BotCTFPlanGoals(&states[2]); // hidden drop cannot create an attack
        assert(states[2].ltgtype!=LTG_GETFLAG);
        states[2].ltgtype=0;states[2].tac.assignedrole=CTFROLE_ROAM;
        states[2].teamgoal_time=floattime+100;states[2].tac.roleredecide_time=floattime+100;
        droppedavailable=1;
        BotCTFPlanGoals(&states[2]);
        assert(states[2].ltgtype==LTG_GETFLAG); // sight change bypasses stale role timer
        bot_tactics.integer = 0; assert(BotCTFPickRole(&states[2]) == -1);
    }
    puts("Recovery census, flag transitions, player orders and escort caps verified for both teams");
    return 0;
}
''')

namespace["run_fixture"]("CTF carrier route direction and detour budget", r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
typedef struct { int areanum, tfl; vec3_t origin; bot_goal_t altroutegoal; } bot_state_t;
static int leg, remaining, enemies;
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t origin, int target, int flags) {
    (void)origin; (void)target; (void)flags; return area == 1 ? leg : remaining;
}
int BotCTFRouteThreat(bot_state_t* bs,int area,vec3_t origin,int target,vec3_t dest,int flags) {
 (void)bs;(void)area;(void)origin;(void)target;(void)dest;(void)flags;return enemies;
}
int BotRoomEnemies(bot_state_t* bs, vec3_t origin) { (void)bs; (void)origin; return enemies; }
''' + namespace["function"](decisions, "BotCTFCarrierRouteCost") + r'''
int main(void) {
    bot_state_t bs = {.areanum=1}; bot_goal_t home = {.areanum=3};
    aas_altroutegoal_t waypoint = {.areanum=2};
    leg = 100; remaining = 500;
    assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) == 600);
    enemies = 2; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) == 1200);
    bs.altroutegoal.areanum = 2;
    assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) == 1150);
    remaining = 600; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) < 0); // no progress
    remaining = 0; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) < 0); // reverse path unavailable
    remaining = 500; leg = 0; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) < 0);
    leg = 19; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) < 0); // movement already considers it reached
    leg = 20; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) == 1070); // strict arrival boundary
    leg = 501; assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 600) < 0); // >1.5x+100 detour
    assert(BotCTFCarrierRouteCost(&bs, &waypoint, &home, 0) < 0);
    puts("Actual return direction, reachable legs, progress, threat cost and route hysteresis verified");
    return 0;
}
''')

namespace["run_fixture"]("CTF planning before combat and once per think", r'''
#include "game/g_local.h"
#include <assert.h>
#include <stdio.h>
typedef struct { float order_time; } bot_state_t;
int gametype = GT_CTF, dead, observer, intermission, seek, retreat, enforce, other;
vmCvar_t bot_tactics, bot_ctfIntercept = {.integer = 3};
int BotIntermission(bot_state_t* bs) { (void)bs; return intermission; }
int BotIsObserver(bot_state_t* bs) { (void)bs; return observer; }
int BotIsDead(bot_state_t* bs) { (void)bs; return dead; }
void BotCTFSeekGoals(bot_state_t* bs) { (void)bs; seek++; }
void BotCTFRetreatGoals(bot_state_t* bs) { (void)bs; retreat++; }
void BotCTFEnforceOffense(bot_state_t* bs) { (void)bs; enforce++; }
void Bot1FCTFSeekGoals(bot_state_t* bs) { (void)bs; other++; }
void Bot1FCTFRetreatGoals(bot_state_t* bs) { (void)bs; other++; }
void BotObeliskSeekGoals(bot_state_t* bs) { (void)bs; other++; }
void BotObeliskRetreatGoals(bot_state_t* bs) { (void)bs; other++; }
void BotHarvesterSeekGoals(bot_state_t* bs) { (void)bs; other++; }
void BotHarvesterRetreatGoals(bot_state_t* bs) { (void)bs; other++; }
''' + namespace["function"](decisions, "BotCTFThinkGoals") + namespace["function"](decisions, "BotTeamGoals") + r'''
int main(void) {
    bot_state_t bs = {.order_time=10};
    bot_tactics.integer = 1;
    BotCTFThinkGoals(&bs); assert(seek == 1);
    BotTeamGoals(&bs, 0); BotTeamGoals(&bs, 1);
    assert(seek == 1 && retreat == 0 && enforce == 0 && bs.order_time == 0);
    dead = 1; BotCTFThinkGoals(&bs); assert(seek == 1); dead = 0;
    observer = 1; BotCTFThinkGoals(&bs); assert(seek == 1); observer = 0;
    intermission = 1; BotCTFThinkGoals(&bs); assert(seek == 1); intermission = 0;
    bot_tactics.integer = 0; BotCTFThinkGoals(&bs); assert(seek == 1);
    BotTeamGoals(&bs, 0); BotTeamGoals(&bs, 1);
    assert(seek == 2 && retreat == 1 && enforce == 2);
    bot_tactics.integer = 1; gametype = GT_1FCTF;
    BotCTFThinkGoals(&bs); BotTeamGoals(&bs, 0); BotTeamGoals(&bs, 1);
    assert(seek == 2 && other == 2);
    puts("Combat nodes cannot bypass or duplicate tactical CTF planning; other modes and tactics 0 keep their dispatch");
    return 0;
}
''')
assert "BotCTFThinkGoals(bs);" in namespace["function"](decisions, "BotDeathmatchAI")

route_types = source[source.index('#define ESCORT_POINTS'):source.index('static escortroute_t* BotCarrierRoute')]
namespace['run_fixture']('CTF escorts follow detours and reachable intercepts', r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
typedef struct {
    int inuse, tfl, areanum, teammate;
    float reachedaltroutegoal_time;
    vec3_t origin; bot_goal_t altroutegoal;
    struct { float escort_time; bot_goal_t escortgoal; } tac;
} bot_state_t;
bot_state_t *botstates[MAX_CLIENTS];
gentity_t g_entities[MAX_GENTITIES];
static gclient_t client;
bot_goal_t ctf_redflag, ctf_blueflag;
vmCvar_t bot_tactics, bot_ctfIntercept = {.integer = 3};
int gametype = GT_CTF;
static float now = 10;
#define FloatTime() now
static int queries, lastflags, times[16], broken;
int BotTeam(bot_state_t *bs) { (void)bs; return TEAM_RED; }
int BotPointAreaNum(vec3_t pos) { return (int)pos[0]; }
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t pos, int target, int flags) {
    (void)area; (void)pos; (void)flags; return times[target];
}
int trap_AAS_PredictRoute(void *data, int area, vec3_t pos, int target,
                        int flags, int maxareas, int maxtime, int stop, int contents,
                        int travel, int stoparea) {
    (void)pos; (void)maxareas; (void)maxtime; (void)stop; (void)contents; (void)travel; (void)stoparea;
    aas_predictroute_t *route = data;
    queries++; lastflags = flags; memset(route, 0, sizeof(*route));
    if (broken) return 0;
    // Detour: 1 -> 2 -> 3 -> 4 -> 5. Direct: 1 -> 6 -> 5.
    // Replacement detour: 1 -> 7 -> 8 -> 4 -> 5.
    route->endarea = target == 3 ? area + 1 : target == 8 ? (area == 1 ? 7 : 8) :
                     area == 1 ? 6 : area == 3 || area == 8 ? 4 : 5;
    route->time = 100; route->endpos[0] = route->endarea;
    return 1;
}
''' + route_types + namespace['function'](source, 'BotCarrierRoute') + namespace['function'](source, 'BotCTFInterceptPoint') + namespace['function'](source, 'BotEscortGoal') + r'''
int main(void) {
    bot_state_t runner = {.inuse=1, .tfl=TFL_DEFAULT | TFL_SLIME};
    bot_state_t escort = {.areanum=9, .teammate=0};
    escortroute_t *route; bot_goal_t goal; int before;
    botstates[0] = &runner; g_entities[0].client = &client;
    g_entities[0].r.currentOrigin[0] = 1; ctf_redflag.areanum = 5;
    runner.altroutegoal.areanum = 3; bot_tactics.integer = 1;
    route = BotCarrierRoute(0, TEAM_RED);
    assert(route->num == 4 && route->area[0] == 2 && route->area[1] == 3 && route->area[3] == 5);
    assert(route->eta[3] == 400 && lastflags == runner.tfl);
    before = queries; BotCarrierRoute(0, TEAM_RED); assert(queries == before);
    // Can meet at the waypoint even though home is unreachable. The old
    // endpoint test and binary search rejected this reachable interception.
    times[2] = 110; times[3] = 150; times[4] = 500; times[5] = 0;
    assert(BotEscortGoal(&escort, &goal) && goal.areanum == 3);
    now += .1f; runner.altroutegoal.areanum = 8;
    times[7] = 50; assert(BotEscortGoal(&escort, &goal) && goal.areanum == 7);
    assert(queries > before); // route change invalidates shared and escort caches
    now += .6f; runner.reachedaltroutegoal_time = now;
    route = BotCarrierRoute(0, TEAM_RED); assert(route->num == 2 && route->area[0] == 6);
    times[6] = 100; times[5] = 200; assert(!BotEscortGoal(&escort, &goal)); // a tie is not a lead
    times[6] = 50; now += .6f; assert(BotEscortGoal(&escort, &goal) && goal.areanum == 6);
    botstates[0] = NULL; now += .6f; route = BotCarrierRoute(0, TEAM_RED);
    assert(route->num == 2 && lastflags == (TFL_DEFAULT)); // human carrier
    now += .6f; g_entities[0].r.currentOrigin[0] = 5;
    assert(!BotEscortGoal(&escort, &goal)); // already home
    now += .6f; g_entities[0].r.currentOrigin[0] = 1; broken = 1;
    assert(!BotEscortGoal(&escort, &goal)); // failed prediction never invents a goal
    bot_tactics.integer = 0; assert(!BotEscortGoal(&escort, &goal));
    bot_tactics.integer = 1; gametype = GT_FFA; assert(!BotEscortGoal(&escort, &goal));
    puts("Chosen detours, directional flags, non-monotonic intercepts, cache changes and human/home/failure fallbacks verified");
    return 0;
}
''')

namespace['run_fixture']('CTF navigation-ranked escorts and incumbent stability', r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
''' + defines + r'''
typedef struct {
    int client, inuse, areanum, tfl, ordered, ltgtype, teammate;
    vec3_t origin;
} bot_state_t;
bot_state_t *botstates[MAX_CLIENTS];
static bot_state_t states[MAX_CLIENTS];
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
level_locals_t level;
vmCvar_t bot_tactics, bot_ctfIntercept = {.integer = 3};
int gametype = GT_CTF;
bot_goal_t ctf_redflag, ctf_blueflag;
static int carrier, carrierarea, calls, times[MAX_CLIENTS], lastflags, ours;
int BotTeam(bot_state_t *bs) { return clients[bs->client].sess.sessionTeam; }
int BotPointAreaNum(vec3_t point) { (void)point; return carrierarea; }
int trap_AAS_AreaReachability(int area) { return area > 0; }
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t origin, int target, int flags) {
    (void)origin; assert(target == carrierarea); calls++; lastflags = flags;
    return times[area-1];
}
flagStatus_t Team_GetFlagStatus(int team) { (void)team; return ours ? FLAG_TAKEN : FLAG_ATBASE; }
static int BotCTFEscortInterceptTime(bot_state_t *bs, int carrier) { (void)bs; (void)carrier; return -1; }
''' + escort_cache + ''.join(namespace['function'](source, name) for name in (
    'BotCTFEscortLimit','BotCTFEscortTravelTime','BotCTFEscortEligible','BotCTFReleaseEscort')) + r'''
static void reset(int size, int team) {
    int i;
    memset(states, 0, sizeof(states)); memset(clients, 0, sizeof(clients));
    memset(g_entities, 0, sizeof(g_entities)); memset(botstates, 0, sizeof(botstates));
    memset(ctfEscortCosts, 0, sizeof(ctfEscortCosts));
    ctfEscortCosts[0].frame = ctfEscortCosts[1].frame = -1;
    level.time++; level.maxclients = size; carrier = size-1; carrierarea = 100;
    calls = ours = 0; bot_tactics.integer = 1;
    for (i = 0; i < size; i++) {
        g_entities[i].inuse = 1; g_entities[i].client = &clients[i];
        clients[i].pers.connected = CON_CONNECTED; clients[i].sess.sessionTeam = team;
        clients[i].ps.stats[STAT_HEALTH] = 100;
        states[i].client = i; states[i].inuse = 1; states[i].areanum = i+1;
        states[i].origin[0] = i * 100; states[i].teammate = -1; states[i].tfl = TFL_DEFAULT;
        botstates[i] = &states[i]; times[i] = 600;
    }
    clients[carrier].ps.powerups[team == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG] = 1;
}
int main(void) {
    int team, before, i;
    for (team = TEAM_RED; team <= TEAM_BLUE; team++) {
        reset(6, team);
        times[0] = 0; times[1] = 300; times[2] = 100; // closest bot blocked; farther bot has short route
        assert(!BotCTFEscortEligible(&states[0], carrier));
        assert(BotCTFEscortEligible(&states[1], carrier));
        assert(BotCTFEscortEligible(&states[2], carrier));
        assert(!BotCTFEscortEligible(&states[3], carrier));
        before = calls;
        for (i = 0; i < 20; i++) BotCTFEscortEligible(&states[i%5], carrier);
        assert(calls == before && calls == 5); // one raw route query per candidate per frame
        clients[2].ps.stats[STAT_HEALTH] = 0; assert(BotCTFEscortEligible(&states[3], carrier));
        reset(4, team); times[0] = 300; times[1] = 250;
        states[0].ltgtype = LTG_TEAMACCOMPANY; states[0].teammate = carrier;
        assert(BotCTFEscortEligible(&states[0], carrier)); // small gain cannot break the screen
        assert(!BotCTFEscortEligible(&states[1], carrier));
        times[1] = 100; level.time++;
        assert(!BotCTFEscortEligible(&states[1], carrier)); // still wait for the occupied slot
        assert(BotCTFReleaseEscort(&states[0])); states[0].ltgtype = 0;
        assert(BotCTFEscortEligible(&states[1], carrier)); // worthwhile replacement can join after release
        states[2].ordered = 1; states[2].ltgtype = LTG_TEAMACCOMPANY; states[2].teammate = carrier;
        assert(BotCTFEscortEligible(&states[2], carrier));
        assert(!BotCTFEscortEligible(&states[1], carrier)); // ordered follower owns the slot
        states[2].ltgtype = LTG_PATROL; assert(BotCTFEscortEligible(&states[1], carrier));
        states[1].areanum = carrierarea; states[1].origin[0] = 320; level.time++;
        assert(BotCTFEscortTravelTime(&states[1], carrier) == 100); // same-area AAS 1 is not distance
        states[0].tfl |= TFL_LAVA; states[1].areanum = 0; states[2].areanum = 0; level.time++;
        BotCTFEscortTravelTime(&states[0], carrier); assert(lastflags == states[0].tfl);
        reset(4, team); states[0].areanum = 0;
        assert(BotCTFEscortTravelTime(&states[0], carrier) < 0); // no route for new airborne candidate
        states[0].ltgtype = LTG_TEAMACCOMPANY; states[0].teammate = carrier; level.time++;
        assert(BotCTFEscortTravelTime(&states[0], carrier) == 101); // nearby incumbent may land shortly
        states[0].origin[0] = 1000; level.time++;
        assert(BotCTFEscortTravelTime(&states[0], carrier) < 0); // distant unreachable follow released
        carrierarea = 0; level.time++;
        assert(BotCTFEscortTravelTime(&states[1], carrier) == 31); // temporary carrier-air fallback
        before = calls; carrierarea = 100; level.time++;
        BotCTFEscortTravelTime(&states[2], carrier); assert(calls > before);
        bot_tactics.integer = 0; assert(!BotCTFEscortEligible(&states[2], carrier));
        bot_tactics.integer = 1; gametype = GT_FFA; assert(!BotCTFEscortEligible(&states[2], carrier));
        gametype = GT_CTF;
    }
    puts("Reachable travel beats wall proximity, cached routing, stable incumbents, vacancies, orders and airborne/same-area fallbacks verified for both teams");
    return 0;
}
''')
