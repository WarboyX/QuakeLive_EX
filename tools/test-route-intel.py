#!/usr/bin/env python3
"""Actual C radio memory: isolation, deduplication, expiry and route influence."""
import ast
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ns={'__file__':str(ROOT/'tools/test-bot-performance.py')}
for n in ast.parse((ROOT/'tools/test-bot-performance.py').read_text()).body:
 if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef)) or (isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='ROOT' for t in n.targets)):
  exec(compile(ast.Module(body=[n],type_ignores=[]),'<harness>','exec'),ns)
s=(ROOT/'code/game/ai_tactics.c').read_text()
a=s.index('#define ROUTE_INTEL_TTL');b=s.index('static void BotRouteIntelReport(void) {',a)
ns['run_fixture']('shared route intelligence',r'''
#include "game/g_local.h"
#include "botlib/be_aas.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
#define MAX_ROOMS 128
static int numRooms=8;
typedef struct {int client,inuse,areanum,tfl;vec3_t origin;bot_goal_t altroutegoal;} bot_state_t;
static bot_state_t states[MAX_CLIENTS],*botstates[MAX_CLIENTS];
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
level_locals_t level;
vmCvar_t bot_tactics={.integer=1},bot_tacticsTeams={.integer=3};
int gametype=GT_CTF;
static void BotRoomCensus(void){}
static int BotRoomAt(vec3_t p){return (int)p[0];}
static int BotTacticsEnabled(void){return bot_tactics.integer;}
int trap_AAS_PredictRoute(void* output,int a,vec3_t p,int t,int flags,int max,int time,int stop,int contents,int travel,int stoparea){
 (void)p;(void)t;(void)flags;(void)max;(void)time;(void)stop;(void)contents;(void)travel;(void)stoparea;
 aas_predictroute_t* r=output;
 r->endarea=t==6?6:(a==1?3:(a==6?5:a+1));r->time=1;VectorSet(r->endpos,r->endarea,0,0);return 1;
}
int trap_AAS_AreaTravelTimeToGoalArea(int area,vec3_t origin,int goal,int flags) {
 (void)origin;(void)flags;return area==1 && goal==6?200:300;
}
'''+s[a:b]+ns['function'](s,'BotCTFRouteThreat')+ns['function']((ROOT/'code/game/ai_dmq3.c').read_text(),'BotCTFCarrierRouteCost')+r'''
int main(void) {
 aas_altroutegoal_t alt={0};bot_goal_t home={0};
 int i; vec3_t room={3,0,0},other={4,0,0},from={1,0,0},end={5,0,0};
 level.maxclients=4;level.time=1000;
 for(i=0;i<4;i++) {states[i].client=i;states[i].inuse=1;botstates[i]=&states[i];g_entities[i].inuse=1;g_entities[i].client=&clients[i];clients[i].sess.sessionTeam=i<2?TEAM_RED:TEAM_BLUE;}
 BotRouteIntelReset();
 assert(BotRouteKnownDanger(&states[1],room)==0);
 BotRouteReportSight(&states[0],2,room);
 assert(BotRouteKnownDanger(&states[1],room)==1); // unseen teammate consumes sighting
 assert(BotRouteKnownDanger(&states[3],room)==0); // no enemy-team leakage
 for(i=0;i<20;i++) BotRouteReportSight(&states[1],2,room);
 assert(BotRouteKnownDanger(&states[0],room)==1); // repeated reports count once
 BotRouteReportSight(&states[0],2,other);
 assert(BotRouteKnownDanger(&states[1],room)==0 && BotRouteKnownDanger(&states[1],other)==1);
 BotRouteReportSight(&states[0],3,other);
 assert(BotRouteKnownDanger(&states[1],other)==2);
 assert(BotCTFRouteThreat(&states[1],1,from,5,end,0)==2);
 // With travel 400 direct / 500 alternative, two risk units reverse the choice.
 states[1].areanum=1;VectorCopy(from,states[1].origin);alt.areanum=6;alt.origin[0]=6;home.areanum=5;home.origin[0]=5;
 assert(BotCTFCarrierRouteCost(&states[1],&alt,&home,400)==500);
 assert(400+300*BotCTFRouteThreat(&states[1],1,from,5,end,0)>BotCTFCarrierRouteCost(&states[1],&alt,&home,400));
 level.time=6000;assert(BotRouteKnownDanger(&states[1],other)==1);
 level.time=9000;assert(BotRouteKnownDanger(&states[1],other)==0);
 assert(400+300*BotCTFRouteThreat(&states[1],1,from,5,end,0)<BotCTFCarrierRouteCost(&states[1],&alt,&home,400));
 BotRouteReportDamage(0,room,qtrue);assert(BotRouteKnownDanger(&states[1],room)==2);
 for(i=0;i<20;i++) BotRouteReportDamage(1,room,qfalse);
 assert(BotRouteKnownDanger(&states[1],room)==2); // incidents cannot stack without bound
 BotRouteReportSight(&states[0],2,room);assert(BotRouteKnownDanger(&states[1],room)==2); // no double count
 for(i=0;i<MAX_CLIENTS;i++) BotRouteReportSight(&states[0],i,room);
 assert(BotRouteKnownDanger(&states[1],room)==ROUTE_INTEL_CAP);
 BotRouteIntelReset();assert(BotRouteKnownDanger(&states[1],room)==0);
 bot_tacticsTeams.integer=2;BotRouteReportSight(&states[0],2,room);BotRouteReportDamage(0,room,qtrue);
 bot_tacticsTeams.integer=3;assert(BotRouteKnownDanger(&states[1],room)==0);
 bot_tactics.integer=0;BotRouteReportSight(&states[0],2,room);bot_tactics.integer=1;assert(BotRouteKnownDanger(&states[1],room)==0);
 BotRouteReportSight(&states[0],2,room);level.time=8000;assert(BotRouteKnownDanger(&states[1],room)==0); // reject future evidence
 BotRouteIntelReset();level.time=10000;BotRouteReportDamage(-1,room,qtrue);assert(BotRouteKnownDanger(&states[1],room)==0);
 puts("Team isolation, shared sighting, enemy dedup/movement, decay/expiry, bounded incidents, map reset, backward time and route-cost influence pass");return 0;
}
''')
# Guard the production damage hook's eligibility and the post-visibility call site.
damage=(ROOT/'code/game/g_combat.c').read_text()
assert 'targ->health > 0 &&\n        (targ->r.svFlags & SVF_BOT) && !OnSameTeam(targ, attacker)' in damage
assert 'BotRouteReportDamage(targ->s.number, targ->r.currentOrigin' in damage
near=ns['function'](s,'BotCountNearby')
assert near.index('!BotEntityVisible(')<near.index('BotRouteReportSight(')
print('Production hooks: sight report follows visibility test; damage passes victim location only')
