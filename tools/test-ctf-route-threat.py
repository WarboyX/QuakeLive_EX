#!/usr/bin/env python3
"""Compile the actual route-risk function against controlled route/room inputs."""
import ast
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ns={'__file__':str(ROOT/'tools/test-bot-performance.py')}
for n in ast.parse((ROOT/'tools/test-bot-performance.py').read_text()).body:
 if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef)) or (isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='ROOT' for t in n.targets)):
  exec(compile(ast.Module(body=[n],type_ignores=[]),'<harness>','exec'),ns)
ns['run_fixture']('route-threat',r'''
#include "qcommon/q_shared.h"
#include "botlib/be_aas.h"
#include <assert.h>
#include <stdio.h>
typedef struct {int unused;} bot_state_t;
static int enabled=1, calls=0, census=0, blocked=0;
static int foes[300];
static int BotTacticsEnabled(void){return enabled;}
static void BotRoomCensus(void){census++;}
static int BotRoomAt(vec3_t p){return (int)p[0];}
static int BotRoomEnemies(bot_state_t* b,vec3_t p){(void)b;return foes[(int)p[0]];}
int trap_AAS_PredictRoute(aas_predictroute_t* r,int a,vec3_t p,int t,int flags,int max,int time,int stop,int contents,int travel,int stoparea){
 (void)p;(void)t;(void)flags;(void)time;(void)stop;(void)contents;(void)travel;(void)stoparea;
 assert(max==1);calls++;r->endarea=blocked?a:a+1;r->time=1;VectorSet(r->endpos,r->endarea,0,0);return 1;
}
'''+ns['function']((ROOT/'code/game/ai_tactics.c').read_text(),'BotCTFRouteThreat')+r'''
int main(void){bot_state_t b;vec3_t from={1,0,0},to={5,0,0};
 foes[1]=99;foes[2]=3;foes[3]=3;foes[5]=2;
 assert(BotCTFRouteThreat(&b,1,from,5,to,37)==3 && census==1 && calls==4);
 foes[5]=8;assert(BotCTFRouteThreat(&b,1,from,5,to,37)==8);
 blocked=1;calls=0;assert(BotCTFRouteThreat(&b,1,from,5,to,37)==8 && calls==1);
 blocked=0;calls=0;to[0]=250;foes[250]=8;
 assert(BotCTFRouteThreat(&b,1,from,250,to,37)==8 && calls==128);
 enabled=0;calls=0;assert(!BotCTFRouteThreat(&b,1,from,250,to,37) && !calls);
 enabled=1;assert(!BotCTFRouteThreat(&b,0,from,250,to,37));
 puts("Actual route risk: census initialization, starting-room exclusion, peak not sum, endpoint inclusion, no progress, 128-step bound and disabled gating verified");return 0;}
''')
