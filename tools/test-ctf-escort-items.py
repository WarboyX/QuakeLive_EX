#!/usr/bin/env python3
"""Compile the actual escort item policy and BotNearbyGoal against controlled inputs."""
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
harness_path = ROOT / 'tools/test-bot-performance.py'
namespace = {'__file__': str(harness_path)}
for node in ast.parse(harness_path.read_text()).body:
    if isinstance(node, (ast.Import, ast.ImportFrom, ast.FunctionDef)) or (
            isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'ROOT' for t in node.targets)):
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(harness_path), 'exec'), namespace)
source = (ROOT / 'code/game/ai_dmnet.c').read_text()
define = next(line for line in (ROOT / 'code/game/ai_main.h').read_text().splitlines()
              if line.startswith('#define LTG_TEAMACCOMPANY '))
namespace['run_fixture']('CTF escort supply ranges and time limits', r'''
#include "game/g_local.h"
#include "botlib/be_ai_goal.h"
#include "botlib/be_aas.h"
#include "game/inv.h"
#include <assert.h>
#include <stdio.h>
''' + define + r'''
typedef struct {
    int ordered, ltgtype, inventory[64], gs, enemy, areanum;
    float lastair_time; vec3_t origin; bot_goal_t teamgoal;
} bot_state_t;
static float now = 10, scale = 3, chosen_range;
static int tactics = 1, gametype = GT_CTF, active = 1, carrying, air;
static int choose_calls, pops, worthy = 1;
#define FloatTime() now
int BotCTFCarryingFlag(bot_state_t* bs) { (void)bs; return carrying; }
int Bot1FCTFCarryingFlag(bot_state_t* bs) { (void)bs; return 0; }
int BotHarvesterCarryingCubes(bot_state_t* bs) { (void)bs; return 0; }
int BotCTFKeepObjective(bot_state_t* bs) { (void)bs; return tactics && gametype == GT_CTF && active; }
int BotGoForAir(bot_state_t* bs, int tfl, bot_goal_t* ltg, float range) {
    (void)bs; (void)tfl; (void)ltg; (void)range; return air;
}
float BotItemSearchRange(bot_state_t* bs, float range) { (void)bs; return range * scale; }
int trap_AAS_AreaTravelTimeToGoalArea(int area, vec3_t origin, int target, int flags) {
    (void)area; (void)origin; (void)target; (void)flags; return 500;
}
int trap_BotChooseNBGItem(int gs, vec3_t origin, int* inventory, int flags, void* ltg, float range) {
    (void)gs; (void)origin; (void)inventory; (void)flags; (void)ltg;
    choose_calls++; chosen_range = range; return 1;
}
int trap_BotGetTopGoal(int gs, void* goal) { (void)gs; memset(goal, 0, sizeof(bot_goal_t)); return 1; }
int BotWantsItemGoal(bot_state_t* bs, bot_goal_t* goal) { (void)bs; (void)goal; return worthy; }
void trap_BotPopGoal(int gs) { (void)gs; pops++; }
''' + ''.join(namespace['function'](source, name) for name in (
    'BotCTFEscortItemRange', 'BotCTFEscortItemDuration', 'BotNearbyGoal')) + r'''
static void legacy(bot_state_t* bs) {
    assert(BotNearbyGoal(bs, 0, NULL, 150)); assert(chosen_range == 450);
    assert(BotCTFEscortItemDuration(bs, 150, 16) == 16);
}
int main(void) {
    bot_state_t bs = {.ltgtype=LTG_TEAMACCOMPANY, .lastair_time=10, .enemy=-1};
    bs.inventory[INVENTORY_HEALTH] = 100;
    assert(BotNearbyGoal(&bs, 0, NULL, 150) && chosen_range == 50); // cap AFTER scaling
    assert(BotCTFEscortItemDuration(&bs, 150, 5.5f) == 1.5f);
    assert(BotCTFEscortItemDuration(&bs, 150, 16) == 1.5f);
    assert(BotCTFEscortItemDuration(&bs, 150, 2.5f) == 1.5f);
    assert(BotCTFEscortItemRange(&bs, 20) == 20);
    assert(BotCTFEscortItemDuration(&bs, 20, 1) == 1); // never lengthen a deadline
    bs.inventory[INVENTORY_HEALTH] = 40;
    assert(BotNearbyGoal(&bs, 0, NULL, 150) && chosen_range == 150);
    assert(BotCTFEscortItemDuration(&bs, 150, 16) == 2.5f);
    bs.inventory[INVENTORY_HEALTH] = 41;
    assert(BotNearbyGoal(&bs, 0, NULL, 150) && chosen_range == 50);
    bs.ordered = 1; legacy(&bs); bs.ordered = 0;
    active = 0; legacy(&bs); active = 1; // dead/former/invalid carrier
    tactics = 0; legacy(&bs); tactics = 1;
    gametype = GT_FFA; legacy(&bs); gametype = GT_CTF;
    carrying = 1; legacy(&bs); carrying = 0;
    bs.ltgtype = 0; legacy(&bs); bs.ltgtype = LTG_TEAMACCOMPANY;
    bs.lastair_time = 3; air = 1; choose_calls = 0;
    assert(BotNearbyGoal(&bs, 0, NULL, 150) && !choose_calls);
    assert(BotCTFEscortItemDuration(&bs, 150, 16) == 16); // air goal keeps ordinary time
    air = 0; legacy(&bs); // unsuccessful air search must keep fallback supplies available
    bs.lastair_time = 4; // equality matches BotGoForAir's strict six-second predicate
    assert(BotNearbyGoal(&bs, 0, NULL, 150) && chosen_range == 50);
    bs.enemy = 1; worthy = 0;
    assert(!BotNearbyGoal(&bs, 0, NULL, 150) && pops == 1);
    puts("Ordinary and critical-health escort budgets, scaled range, orders, air, carrier loss and legacy paths verified");
    return 0;
}
''')
for name in ('AINode_Seek_LTG', 'AINode_Battle_Chase', 'AINode_Battle_Retreat'):
    function = namespace['function'](source, name)
    assert function.count('BotCTFEscortItemDuration(bs, range,') == 1, name
print('Escort deadline policy is wired to all three nearby-item entry paths: PASS')
