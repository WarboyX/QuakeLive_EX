#!/usr/bin/env python3
"""Exercise real interception-aware escort selection and cache behavior."""
import ast
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
namespace={'__file__':str(ROOT/'tools/test-bot-performance.py')}
for node in ast.parse((ROOT/'tools/test-bot-performance.py').read_text()).body:
    if isinstance(node,(ast.Import,ast.ImportFrom,ast.FunctionDef)) or (isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='ROOT' for t in node.targets)):
        exec(compile(ast.Module(body=[node],type_ignores=[]),'<harness>','exec'),namespace)
s=(ROOT/'code/game/ai_tactics.c').read_text()
defines='\n'.join(line for f in ('ai_main.h','ai_tactics.h') for line in (ROOT/'code/game'/f).read_text().splitlines() if line.startswith(('#define LTG_','#define CTFROLE_')))
cache=s[s.index('typedef struct {\n    int frame, carrier;'):s.index('/*\n==================\nBotTacticsEnabled')]
route=s[s.index('#define ESCORT_POINTS'):s.index('static escortroute_t escortroutes')]
body=r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
'''+defines+r'''
typedef struct { int client,inuse,areanum,tfl,ordered,ltgtype,teammate; vec3_t origin; } bot_state_t;
bot_state_t *botstates[MAX_CLIENTS];
static bot_state_t states[MAX_CLIENTS];
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
level_locals_t level;
vmCvar_t bot_tactics, bot_ctfIntercept;
int gametype=GT_CTF;
bot_goal_t ctf_redflag,ctf_blueflag;
static int direct[MAX_CLIENTS],meeting[MAX_CLIENTS],directcalls,interceptcalls;
int BotTeam(bot_state_t *bs) { return clients[bs->client].sess.sessionTeam; }
int BotPointAreaNum(vec3_t point) { (void)point; return 100; }
int trap_AAS_AreaReachability(int area) { return area>0; }
int trap_AAS_AreaTravelTimeToGoalArea(int area,vec3_t origin,int target,int flags) {
    (void)origin; assert(flags==(TFL_DEFAULT)); assert(area>0 && area<=4);
    if (target==100) { directcalls++; return direct[area-1]; }
    assert(target==101); interceptcalls++; return meeting[area-1];
}
flagStatus_t Team_GetFlagStatus(int team) { (void)team; return FLAG_ATBASE; }
'''+cache+route+r'''
static escortroute_t route;
static escortroute_t* BotCarrierRoute(int carrier,int team) {
    assert(carrier==3 && (team==TEAM_RED || team==TEAM_BLUE)); return &route;
}
'''+''.join(namespace['function'](s,name) for name in ('BotCTFInterceptPoint','BotCTFEscortInterceptTime','BotCTFEscortLimit','BotCTFEscortTravelTime','BotCTFEscortEligible','BotCTFReleaseEscort'))+r'''
static void reset(int team) {
    int i;
    memset(states,0,sizeof(states)); memset(clients,0,sizeof(clients));
    memset(g_entities,0,sizeof(g_entities)); memset(botstates,0,sizeof(botstates));
    memset(ctfEscortCosts,0,sizeof(ctfEscortCosts)); memset(&route,0,sizeof(route));
    ctfEscortCosts[0].frame=ctfEscortCosts[1].frame=-1;
    level.time++; level.maxclients=4; bot_tactics.integer=1; bot_ctfIntercept.integer=3;
    directcalls=interceptcalls=0; route.time=10; route.num=1;
    route.area[0]=101; route.eta[0]=100; route.pos[0][0]=160;
    for (i=0;i<4;i++) {
        g_entities[i].inuse=1; g_entities[i].client=&clients[i];
        clients[i].pers.connected=CON_CONNECTED; clients[i].sess.sessionTeam=team;
        clients[i].ps.stats[STAT_HEALTH]=100;
        states[i].client=i; states[i].inuse=1; states[i].areanum=i+1;
        states[i].tfl=TFL_DEFAULT; states[i].teammate=-1; botstates[i]=&states[i];
        direct[i]=600; meeting[i]=0;
    }
    clients[3].ps.powerups[team==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG]=1;
}
int main(void) {
    int team,i,before;
    for (team=TEAM_RED;team<=TEAM_BLUE;team++) {
        reset(team); direct[0]=200; direct[1]=900; meeting[1]=50;
        assert(BotCTFEscortTravelTime(&states[0],3)==200);
        assert(BotCTFEscortTravelTime(&states[1],3)==100);
        assert(!BotCTFEscortEligible(&states[0],3));
        assert(BotCTFEscortEligible(&states[1],3)); // forward meeting beats walk back
        /* E224: interception off for this team only is ctf17's choice - the walk
           back - and the other team's setting does not leak into it */
        bot_ctfIntercept.integer = team==TEAM_RED ? 2 : 1; level.time++;
        before=interceptcalls;
        assert(BotCTFEscortTravelTime(&states[1],3)==900);
        assert(BotCTFEscortEligible(&states[0],3) && !BotCTFEscortEligible(&states[1],3));
        assert(interceptcalls==before);
        bot_ctfIntercept.integer = 3; level.time++;
        assert(BotCTFEscortTravelTime(&states[1],3)==100);
        before=directcalls+interceptcalls;
        for(i=0;i<30;i++) BotCTFEscortEligible(&states[i%3],3);
        assert(directcalls+interceptcalls==before);
        before=interceptcalls; level.time++; BotCTFEscortEligible(&states[1],3);
        assert(interceptcalls==before); // expensive scan retained across frames
        route.time+=.5f; route.eta[0]=400; level.time++;
        assert(BotCTFEscortEligible(&states[0],3)); // waiting far ahead isn't instant help
        assert(!BotCTFEscortEligible(&states[1],3));
        route.time+=.5f; route.eta[0]=100; level.time++;
        states[0].ltgtype=LTG_TEAMACCOMPANY; states[0].teammate=3;
        assert(BotCTFEscortEligible(&states[0],3)); // incumbent margin prevents churn
        route.time+=.5f; route.eta[0]=80; level.time++;
        assert(BotCTFReleaseEscort(&states[0]));
        assert(!BotCTFEscortEligible(&states[1],3)); // wait for occupied slot
        states[0].ltgtype=0; assert(BotCTFEscortEligible(&states[1],3));
        clients[1].ps.stats[STAT_HEALTH]=0;
        assert(!BotCTFEscortEligible(&states[1],3)); // death is checked live
        clients[1].ps.stats[STAT_HEALTH]=100;
        states[2].ordered=1; states[2].ltgtype=LTG_TEAMACCOMPANY; states[2].teammate=3;
        assert(!BotCTFEscortEligible(&states[1],3)); // preserve player-order slot
        states[2].ltgtype=LTG_PATROL;
        direct[1]=0; route.time+=.5f; level.time++;
        assert(BotCTFEscortTravelTime(&states[1],3)==80); // carrier now unreachable, meeting reachable
        states[1].areanum=0; route.time+=.5f; level.time++;
        assert(BotCTFEscortTravelTime(&states[1],3)<0); // no invented airborne meeting
        reset(team); states[0].areanum=101; route.pos[0][0]=640;
        assert(BotCTFInterceptPoint(&states[0],&route)<0); // same-area travel is not 1
        route.pos[0][0]=160;
        assert(BotCTFInterceptPoint(&states[0],&route)==0);
        route.num=0; assert(BotCTFInterceptPoint(&states[0],&route)<0);
    }
    puts("Per-team interception switch (E224), forward meetings, waiting ETA, shared cache, replacement margin, live death/orders, unreachable and same-area cases pass for both teams");
    return 0;
}
'''
namespace['run_fixture']('CTF interception-aware escort selection',body)
assert 'BotCTFInterceptPoint(bs, r)' in namespace['function'](s,'BotEscortGoal')
assert 'BotCTFEscortInterceptTime(other, carrier)' in namespace['function'](s,'BotCTFEscortTravelTime')
print('Assignment and movement share the actual interception rule: PASS')
