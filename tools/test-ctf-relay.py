#!/usr/bin/env python3
"""Compile actual enemy-drop perception/goal functions against controlled world state."""
import ast
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];h=ROOT/'tools/test-bot-performance.py';ns={'__file__':str(h)}
for node in ast.parse(h.read_text()).body:
 if isinstance(node,(ast.Import,ast.ImportFrom,ast.FunctionDef)) or (isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='ROOT' for t in node.targets)):
  exec(compile(ast.Module(body=[node],type_ignores=[]),str(h),'exec'),ns)
s=(ROOT/'code/game/ai_tactics.c').read_text();start=s.index('typedef struct {',s.index('/* [CTF17]'));end=s.index('static void BotCTFRelayReset',start)
code=r'''
#include "game/g_local.h"
#include "botlib/be_ai_goal.h"
#include <assert.h>
#include <stdio.h>
typedef struct bot_state_s {int client;} bot_state_t;
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
static gitem_t item;
static int flags[TEAM_NUM_TEAMS], visible[MAX_CLIENTS], pvs=1, traces;
int gametype=GT_CTF;
bot_goal_t ctf_redflag={.areanum=10},ctf_blueflag={.areanum=20};
int BotTeam(bot_state_t* b){return clients[b->client].sess.sessionTeam;}
flagStatus_t Team_GetFlagStatus(int team){return flags[team];}
int BotPointAreaNum(vec3_t p){return (int)p[0];}
qboolean trap_InPVS(const vec3_t a,const vec3_t b){(void)a;(void)b;return pvs;}
float BotEntityVisible(int who,vec3_t eye,vec3_t angles,float fov,int entity){
 (void)eye;(void)angles;assert(fov==360);assert(entity==4);traces++;return visible[who];
}
'''+s[start:end]+''.join(ns['function'](s,n) for n in ('BotCTFRelayReset','BotCTFEnemyFlagGoal','BotCTFEnemyFlagAvailable'))+r'''
int main(void){int team,i;bot_state_t bs={.client=0};bot_goal_t goal;
 for(team=TEAM_RED;team<=TEAM_BLUE;team++){
  int enemy=team==TEAM_RED?TEAM_BLUE:TEAM_RED;
  memset(g_entities,0,sizeof(g_entities));memset(clients,0,sizeof(clients));memset(flags,0,sizeof(flags));memset(visible,0,sizeof(visible));
  BotCTFRelayReset();level.time=100;level.maxclients=4;level.num_entities=5;traces=0;pvs=1;
  for(i=0;i<4;i++){g_entities[i].inuse=qtrue;g_entities[i].client=&clients[i];clients[i].pers.connected=CON_CONNECTED;clients[i].ps.stats[STAT_HEALTH]=100;clients[i].sess.sessionTeam=(team_t)team;}
  assert(BotCTFEnemyFlagGoal(&bs,&goal)&&goal.areanum==(enemy==TEAM_RED?10:20));
  flags[enemy]=FLAG_TAKEN;assert(!BotCTFEnemyFlagAvailable(&bs));
  flags[enemy]=FLAG_DROPPED;item.giType=IT_TEAM;item.giTag=enemy==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG;
  g_entities[4].inuse=qtrue;g_entities[4].flags=FL_DROPPED_ITEM;g_entities[4].item=&item;g_entities[4].nextthink=30100;g_entities[4].r.currentOrigin[0]=55;
  assert(!BotCTFEnemyFlagGoal(&bs,&goal)); // unseen drop has no target
  visible[1]=1;level.time++;assert(BotCTFEnemyFlagGoal(&bs,&goal));assert(goal.areanum==55 && goal.entitynum==4 && (goal.flags&GFL_DROPPED));
  {int before=traces;for(i=0;i<30;i++)assert(BotCTFEnemyFlagAvailable(&bs));assert(traces==before);}
  visible[1]=0;g_entities[4].r.currentOrigin[0]=66;level.time++;assert(BotCTFEnemyFlagGoal(&bs,&goal)&&goal.origin[0]==55); // no hidden tracking
  g_entities[4].nextthink++;level.time++;assert(!BotCTFEnemyFlagAvailable(&bs)); // reused entity/new drop
  visible[1]=1;clients[1].ps.stats[STAT_HEALTH]=0;level.time++;assert(!BotCTFEnemyFlagAvailable(&bs));
  clients[1].ps.stats[STAT_HEALTH]=100;clients[1].sess.sessionTeam=(team_t)enemy;level.time++;assert(!BotCTFEnemyFlagAvailable(&bs));
  clients[1].sess.sessionTeam=TEAM_SPECTATOR;level.time++;assert(!BotCTFEnemyFlagAvailable(&bs));
  clients[1].sess.sessionTeam=(team_t)team;clients[1].pers.connected=CON_DISCONNECTED;level.time++;assert(!BotCTFEnemyFlagAvailable(&bs));
  clients[1].pers.connected=CON_CONNECTED;pvs=0;level.time++;{int before=traces;assert(!BotCTFEnemyFlagAvailable(&bs));assert(traces==before);}
  pvs=1;level.time++;assert(BotCTFEnemyFlagAvailable(&bs));
  flags[enemy]=FLAG_TAKEN;assert(!BotCTFEnemyFlagAvailable(&bs));flags[enemy]=FLAG_DROPPED;visible[1]=0;assert(!BotCTFEnemyFlagAvailable(&bs)); // same-frame status invalidation
  visible[1]=1;level.time++;assert(BotCTFEnemyFlagAvailable(&bs));g_entities[4].inuse=qfalse;level.time++;assert(!BotCTFEnemyFlagAvailable(&bs));
  g_entities[4].inuse=qtrue;level.time++;assert(BotCTFEnemyFlagAvailable(&bs));BotCTFRelayReset();visible[1]=0;assert(!BotCTFEnemyFlagAvailable(&bs));
  flags[enemy]=FLAG_ATBASE;assert(BotCTFEnemyFlagAvailable(&bs));
 }
 gametype=GT_FFA;assert(!BotCTFEnemyFlagAvailable(&bs));gametype=GT_CTF;clients[0].sess.sessionTeam=TEAM_SPECTATOR;assert(!BotCTFEnemyFlagAvailable(&bs));
 puts("Relay: both teams, seen/unseen, dead/enemy/spectator/disconnected viewers, PVS, shared cache, hidden movement, slot reuse, pickup/return/disappearance/reset and mode guards verified");return 0;}
'''
ns['run_fixture']('CTF spotted enemy-flag relay',code)
for name in ('BotCTFRoleMix','BotCTFPickRole'):
 assert 'BotCTFEnemyFlagAvailable(bs)' in ns['function'](s,name)
assert 'BotCTFRelayReset();' in ns['function'](s,'BotRoomsReset')
assert 'BotCTFEnemyFlagAvailable(bs)' in ns['function']((ROOT/'code/game/ai_dmq3.c').read_text(),'BotCTFPlanGoals')
assert 'if (BotCTFEnemyFlagGoal(bs, goal)) return qtrue;' in (ROOT/'code/game/ai_dmnet.c').read_text()
print('Planner, quota, movement and map-reset wiring: PASS')
