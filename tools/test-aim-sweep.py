#!/usr/bin/env python3
"""Compile the real controller against deterministic sampled aim trajectories.
Optional --baseline FILE prints the same metrics for a previous ai_main.c.
This tests controller mechanics, not human likeness or match strength.
"""
import ast
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
ns = {'__file__': str(ROOT / 'tools/test-bot-performance.py')}
for n in ast.parse((ROOT/'tools/test-bot-performance.py').read_text()).body:
    if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef)) or (isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id=='ROOT' for t in n.targets)):
        exec(compile(ast.Module(body=[n], type_ignores=[]), '<helpers>', 'exec'), ns)
source = Path(sys.argv[2]).read_text() if len(sys.argv)>2 else (ROOT/'code/game/ai_main.c').read_text()
old = len(sys.argv)>2
constants = '\n'.join(s for s in source.splitlines() if s.startswith('#define AIM_'))
body = r'''
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int qboolean;
#define qfalse 0
#define qtrue 1
#define PITCH 0
#define YAW 1
#define CHARACTERISTIC_AIM_SKILL 0
static struct {int time;} level;
static struct {int integer;} bot_tactics={1},bot_aimSweep={1};
static float sampleclock, skill=1;
#define FloatTime() sampleclock
static float AngleMod(float a) { return a - floorf(a/360)*360; }
static float AngleDifference(float a,float b) {float d=AngleMod(a-b);return d>180?d-360:d;}
static float trap_Characteristic_BFloat(int c,int n,float l,float h) {(void)c;(void)n;(void)l;(void)h;return skill;}
static int submitted;
static void trap_EA_View(int c,float *v) {(void)c;assert(isfinite(v[0])&&isfinite(v[1]));submitted++;}
typedef struct {int enemy,character,client;float ideal_viewangles[3],viewangles[3];struct {
float aimsweep_len,aimsweep_start,aimfilter_time,aimsample_time;
int aimenemy;
float aimfrom[2],aimto[2],aimprev[2],aim1st[2],aim2nd[2],aimoffset[2],aimoffsetgoal[2];
} tac;} bot_state_t;
'''+constants+'\n'+ns['function'](source,'BotAimSweep')+r'''
static void frame(bot_state_t *b,int ms,int dt,float angle,int fresh) {
 level.time=ms;
 sampleclock=(ms/100)*.1f;
 if(fresh) {b->ideal_viewangles[1]=AngleMod(angle);b->tac.aimsample_time=ms*.001f;}
 BotAimSweep(b,dt*.001f);
}
int main(void) {
 int dt,j,mode;
 for(dt=10;dt<=50;dt+=15) {
  for(mode=0;mode<3;mode++) {
   bot_state_t b={0};float total=0,max=0;int count=0,lastsample=-1;
   b.enemy=1;
   for(j=0;j<=4000;j+=dt) {
    int sample=j/100;float target=mode==0?(j<500?0:.2f):mode==1?30*j*.001f:20*sinf(j*.001f*3);
    float err;
    frame(&b,j,dt,target,sample!=lastsample);lastsample=sample;
    err=fabsf(AngleDifference(target,b.viewangles[1]));
    if(j>1000) {total+=err;count++;if(err>max)max=err;}
   }
   printf("dt=%dms trajectory=%d mean_error=%.4f max_error=%.4f\n",dt,mode,total/count,max);
#if !BASELINE
   if(mode==0) assert(max<.04f); // includes engine angle quantization
   if(mode==1) assert(total/count<3.0f);
   if(mode==2) assert(total/count<5.0f);
#endif
  }
 }
#if !BASELINE
 { bot_state_t b={0};float previous; b.enemy=1;
   frame(&b,0,25,90,1);frame(&b,25,25,90,0);previous=b.viewangles[1];
   frame(&b,50,25,90,0);assert(b.viewangles[1]>previous); // repeated botlib time
   frame(&b,100,25,93,1);previous=b.tac.aim1st[1];assert(previous>0);
   frame(&b,125,25,93,0);assert(b.tac.aim1st[1]==previous); // preserve sampled velocity
   b.enemy=2;frame(&b,150,25,100,1);assert(b.tac.aim1st[1]==0 && b.tac.aimenemy==2);
   b.enemy=-1;frame(&b,175,25,100,0);assert(b.tac.aimsweep_len==0);
   b.enemy=1;frame(&b,200,25,359,1);frame(&b,300,25,1,1);assert(fabsf(b.tac.aim1st[1])<25);
   frame(&b,1000,25,1,0);assert(fabsf(AngleDifference(b.tac.aimto[1],1))<.006f); // stale lead expires
   frame(&b,0,25,1,1);assert(b.tac.aimsweep_start==0); // clock reset
   bot_aimSweep.integer=0;frame(&b,25,25,1,0);assert(b.tac.aimsweep_len==0);
 }
#endif
 assert(submitted>0);return 0;
}
'''
body = body.replace('static float AngleMod(float a) { return a - floorf(a/360)*360; }', ns['function']((ROOT/'code/qcommon/q_math.c').read_text(), 'AngleMod'))
body = body.replace('static float AngleDifference(float a,float b) {float d=AngleMod(a-b);return d>180?d-360:d;}', ns['function'](source, 'AngleDifference'))
ns['run_fixture']('aim sweep '+('baseline metrics' if old else 'regressions'), '#define BASELINE '+str(int(old))+'\n'+body)

if not old:
    dm = (ROOT/'code/game/ai_dmq3.c').read_text()
    start = dm.index('    if (bot_tactics.integer && bot_aimDrift.integer)', dm.index('void BotAimAtEnemy'))
    end = dm.index('    // if the bots should be really challenging', start)
    ns['run_fixture']('aim drift remains effective with sweep off', r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#define PITCH 0
#define YAW 1
static float crandom(void) { return .5f; }
static float AngleMod(float a) { return a-floorf(a/360)*360; }
static struct {int integer;} bot_tactics={1},bot_aimSweep,bot_aimDrift={1};
static struct {int time;} level={100};
static struct {float vspread,hspread;} wi={1,1};
typedef struct {float ideal_viewangles[3];struct {float aimdrift[2],aimoffsetgoal[2],aimoffset[2],aimsample_time;} tac;} bot_state_t;
static void aim(bot_state_t *bs) {
float aim_accuracy=.5f;
''' + dm[start:end] + r'''
}
int main(void) {
 bot_state_t b={0};
 aim(&b);assert(fabsf(b.ideal_viewangles[1]-.45f)<.0001f);
 assert(b.tac.aimsample_time==.1f);
 memset(&b,0,sizeof(b));bot_aimSweep.integer=1;
 aim(&b);assert(b.ideal_viewangles[1]==0 && b.tac.aimoffsetgoal[1]>.4f);
 memset(&b,0,sizeof(b));bot_tactics.integer=0;
 aim(&b);assert(b.tac.aimoffsetgoal[1]==0 && b.ideal_viewangles[1]==1.5f);
 return 0;
}
''')
