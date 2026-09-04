/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*****************************************************************************
 * name:		ai_tactics.c
 *
 * desc:		[QL] tactical layer over the Quake 3 bot AI
 *
 *****************************************************************************/

#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_char.h"
#include "../botlib/be_ai_chat.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/be_ai_weap.h"
//
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_dmnet.h"
#include "ai_tactics.h"
//
#include "chars.h"  // characteristics
#include "inv.h"    // indexes into the inventory

/*
==================
BotTacticsEnabled
==================
*/
static int BotTacticsEnabled(void) {
    return bot_tactics.integer != 0;
}

/*
==================
BotTacticsReset
==================
*/
void BotTacticsReset(bot_state_t* bs) {
    memset(&bs->tac, 0, sizeof(bot_tactics_t));
    bs->tac.nearestally = -1;
    bs->tac.nearestallydist = 99999;
    bs->tac.posture = TACTIC_EVEN;
    bs->tac.lasthealth = bs->inventory[INVENTORY_HEALTH];
}

/*
==================
BotEffectiveStrength

How much of a body a player is worth to a fight right now. A bot at 25 health
with no armour is standing in the room but is not two hundred points of trouble,
and counting it as a whole one is how a pair of nearly dead bots talk each other
into a push they lose.
==================
*/
static float BotEffectiveStrength(int clientnum) {
    gclient_t* cl;
    int total;

    if (clientnum < 0 || clientnum >= level.maxclients) {
        return 0.0f;
    }
    if (!g_entities[clientnum].inuse || !g_entities[clientnum].client) {
        return 0.0f;
    }
    cl = g_entities[clientnum].client;
    if (cl->ps.stats[STAT_HEALTH] <= 0) {
        return 0.0f;
    }
    // a quad or a battle suit is worth more than the health behind it
    if (cl->ps.powerups[PW_QUAD] || cl->ps.powerups[PW_BATTLESUIT]) {
        return 1.5f;
    }
    total = cl->ps.stats[STAT_HEALTH] + cl->ps.stats[STAT_ARMOR];
    if (total >= 120) {
        return 1.0f;
    }
    if (total >= 60) {
        return 0.75f;
    }
    return 0.4f;
}

/*
==================
BotCountNearby

Players of each side within range, and how much of a fight each side is. The
visibility traces are the expensive half, which is why the whole picture is
refreshed on a timer rather than every frame.
==================
*/
static void BotCountNearby(bot_state_t* bs, float range) {
    int i, sameteam;
    float dist, strength, own, other, fallbackmargin;
    vec3_t dir;
    aas_entityinfo_t entinfo;

    bs->tac.allies = 0;
    bs->tac.foes = 0;
    bs->tac.nearestally = -1;
    bs->tac.nearestallydist = 99999;
    own = BotEffectiveStrength(bs->client);
    other = 0.0f;
    fallbackmargin = TeamPlayIsOn() ? 0.01f : 1.5f;

    for (i = 0; i < level.maxclients; i++) {
        if (i == bs->client) {
            continue;
        }
        if (!g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        if (g_entities[i].client->sess.sessionTeam == TEAM_SPECTATOR) {
            continue;
        }
        BotEntityInfo(i, &entinfo);
        if (!entinfo.valid || EntityIsDead(&entinfo)) {
            continue;
        }
        VectorSubtract(entinfo.origin, bs->origin, dir);
        dist = VectorLength(dir);
        if (dist > range) {
            continue;
        }
        /*
        Enemies have to be seen; team mates do not. A team knows roughly where
        its own people are without looking at them, and skipping the trace for
        them halves the cost of this loop - which every bot pays, several times a
        second, against every other player on the server.

        360 degrees for the ones that are traced: this is awareness of the room,
        not of the crosshair, and a bot that only counted what was in front of it
        would push into a pair it had already walked past.
        */
        sameteam = BotSameTeam(bs, i);
        if (!sameteam &&
            !BotEntityVisible(bs->entitynum, bs->eye, bs->viewangles, 360, i)) {
            continue;
        }
        strength = BotEffectiveStrength(i);
        if (sameteam) {
            bs->tac.allies++;
            own += strength;
            if (dist < bs->tac.nearestallydist) {
                bs->tac.nearestallydist = dist;
                bs->tac.nearestally = i;
                VectorCopy(entinfo.origin, bs->tac.nearestallyorigin);
            }
        } else {
            bs->tac.foes++;
            other += strength;
        }
    }

    /*
    Posture, with hysteresis in one direction only.

    Deciding to push is held for a moment before it takes effect, because the
    counts flicker as people move behind each other and a bot that re-decided
    every refresh would walk forward and back on the spot. Deciding to fall back
    is not held: by the time a bot is outnumbered, waiting to be sure is how it
    dies mid-deliberation.
    */
    if (other <= 0.01f) {
        /*
        Nobody in sight. Standing in the open taking hits from something the bot
        cannot see is the one case where that is still not a reason to hold
        ground - it is the shape of being sniped, and the stock AI's answer to it
        is to keep standing there.
        */
        if (bs->tac.hurt_time > FloatTime() - 1.5f) {
            bs->tac.posture = TACTIC_FALLBACK;
            bs->tac.posture_time = FloatTime() + 1.0f;
            return;
        }
        // otherwise do not carry a stale posture into the next fight
        bs->tac.posture = TACTIC_EVEN;
        return;
    }
    /*
    In free-for-all everyone in the room counts against the bot, so "outnumbered"
    is the normal state of a busy map rather than a warning, and a bot that fell
    back whenever a second player was visible would spend a sixty-four player
    match walking backwards. Falling back there needs a real margin - roughly two
    healthy opponents against one healthy bot - where in a team game being down by
    one body is already the moment to give ground.
    */
    if (other > own + fallbackmargin) {
        /*
        And in free-for-all, being outnumbered has to come with being in trouble.

        A margin of 1.5 was not enough: a sixty player match on longestyard put
        four to seven visible enemies in front of every bot at all times, so
        every bot was permanently in FALLBACK - a field report showed 42 of 60 -
        which took 30 off their aggression, sent them into Battle_Retreat, and
        from there into the suicidal-fight loop. "Outnumbered" carries no
        information in a gametype where everyone always is.

        A healthy bot in a crowd is just playing free-for-all. One at half health
        in the same crowd is genuinely losing, and that is worth acting on.
        */
        if (!TeamPlayIsOn() && own >= 1.0f) {
            bs->tac.posture = TACTIC_EVEN;
            return;
        }
        bs->tac.posture = TACTIC_FALLBACK;
        bs->tac.posture_time = FloatTime() + 1.0f;
        return;
    }
    if (bs->tac.posture_time > FloatTime()) {
        return;
    }
    if (own >= other + 1.0f) {
        bs->tac.posture = TACTIC_PUSH;
    } else {
        bs->tac.posture = TACTIC_EVEN;
    }
    bs->tac.posture_time = FloatTime() + 0.6f;
}

#define MAX_TRACKED_MISSILES 64

static int tacMissiles[MAX_TRACKED_MISSILES];
static int tacNumMissiles;
static int tacMissileTime = -1;

/*
==================
BotRefreshMissileList

Walking every entity looking for missiles is cheap once and ruinous sixty-four
times: at forty ticks that is two and a half million entity tests a second for
nothing. The list is built once per server frame and every bot reads it.
==================
*/
static void BotRefreshMissileList(void) {
    int i;
    gentity_t* ent;

    if (tacMissileTime == level.time) {
        return;
    }
    tacMissileTime = level.time;
    tacNumMissiles = 0;
    for (i = level.maxclients; i < level.num_entities; i++) {
        ent = &g_entities[i];
        if (!ent->inuse || ent->s.eType != ET_MISSILE) {
            continue;
        }
        // a grenade at rest is a mine, not something to sidestep
        if (VectorLengthSquared(ent->s.pos.trDelta) < Square(100)) {
            continue;
        }
        tacMissiles[tacNumMissiles++] = i;
        if (tacNumMissiles >= MAX_TRACKED_MISSILES) {
            return;
        }
    }
}

/*
==================
BotScanForMissiles

Missiles in flight that are heading at this bot, close enough to matter and
actually in sight. The sight test is the point: without it a bot sidesteps a
rocket fired through a wall two rooms away, which reads as a bot that knows
things it should not.
==================
*/
static void BotScanForMissiles(bot_state_t* bs) {
    int i;
    gentity_t* ent;
    vec3_t veldir, tobot, side, up = {0, 0, 1};
    float speed, dist, closing, missdist, impact;
    bsp_trace_t trace;

    if (!bot_dodge.integer) {
        return;
    }
    BotRefreshMissileList();
    for (i = 0; i < tacNumMissiles; i++) {
        ent = &g_entities[tacMissiles[i]];
        // the list is a frame old at worst, and an entity can be freed and
        // reused inside that frame
        if (!ent->inuse || ent->s.eType != ET_MISSILE) {
            continue;
        }
        // the bot's own shots, which it is not going to be hit by
        if (ent->parent == &g_entities[bs->entitynum]) {
            continue;
        }
        VectorCopy(ent->s.pos.trDelta, veldir);
        speed = VectorNormalize(veldir);
        if (speed < 100) {
            continue;
        }
        VectorSubtract(bs->origin, ent->r.currentOrigin, tobot);
        dist = VectorNormalize(tobot);
        if (dist > 1500 || dist < 48) {
            continue;
        }
        closing = DotProduct(veldir, tobot);
        // not coming this way
        if (closing < 0.85f) {
            continue;
        }
        // how far off the bot it passes, if nothing moves
        missdist = dist * sqrt(1.0f - closing * closing);
        if (missdist > 140) {
            continue;
        }
        impact = dist / speed;
        // already too late to move, or far enough out that the bot has not
        // reacted to it yet
        if (impact < 0.08f || impact > 1.2f) {
            continue;
        }
        BotAI_Trace(&trace, bs->eye, NULL, NULL, ent->r.currentOrigin, bs->entitynum, MASK_SOLID);
        if (trace.fraction < 1.0f) {
            continue;
        }
        // step across the missile's path, on the side the bot is already nearer
        CrossProduct(veldir, up, side);
        side[2] = 0;
        if (VectorNormalize(side) < 0.1f) {
            continue;
        }
        if (DotProduct(side, tobot) < 0) {
            VectorNegate(side, side);
        }
        VectorCopy(side, bs->tac.threatdir);
        bs->tac.threat_time = FloatTime() + 0.4f;
        return;
    }
}

/*
==================
BotTacticsUpdate
==================
*/
void BotTacticsUpdate(bot_state_t* bs) {
    float range;

    if (!BotTacticsEnabled() || BotIsDead(bs)) {
        bs->tac.posture = TACTIC_EVEN;
        bs->tac.threat_time = 0;
        return;
    }
    /*
    Notice being shot. This is the one piece of information a bot gets for free
    and the stock AI only uses it to decide whether to say something.

    Five points is the floor because a health drop is not proof of an attacker -
    lava, slime and falling all take health too, and a bot that treated a bad
    landing as incoming fire would fall back from nothing. The lava test covers
    the case that ticks steadily; the threshold covers the rest.
    */
    if (bs->tac.lasthealth - bs->inventory[INVENTORY_HEALTH] >= 5 && !BotInLavaOrSlime(bs)) {
        bs->tac.hurt_time = FloatTime();
    }
    bs->tac.lasthealth = bs->inventory[INVENTORY_HEALTH];

    /*
    How long the bot has been going nowhere, which is the one question the AI
    could not previously be asked. A bot standing still is normal for a second -
    it is camping, or waiting on a lift, or dead - and pathological for thirty,
    and there was no way to tell those apart short of watching it.

    Speed rather than distance, because a bot walking into a wall has velocity
    and gets nowhere; 48 units is a little over a second of walking, and the
    origin test is what catches the wall.
    */
    if (VectorLengthSquared(bs->cur_ps.velocity) > Square(40) &&
        DistanceSquared(bs->origin, bs->tac.lastorigin) > Square(48)) {
        bs->tac.moved_time = FloatTime();
        VectorCopy(bs->origin, bs->tac.lastorigin);
    } else if (bs->tac.moved_time <= 0.0f) {
        bs->tac.moved_time = FloatTime();
        VectorCopy(bs->origin, bs->tac.lastorigin);
    }
    //
    BotScanForMissiles(bs);
    //
    if (bs->tac.update_time > FloatTime()) {
        return;
    }
    // spread the refreshes out so sixty-four bots do not all count the room on
    // the same server frame
    bs->tac.update_time = FloatTime() + 0.2f + random() * 0.1f;
    range = bot_squadRange.value;
    if (range < 100) {
        range = 100;
    }
    BotCountNearby(bs, range);

    if (bot_debugTactics.integer && bs->tac.foes) {
        BotAI_Print(PRT_MESSAGE, "%s: %d allies, %d foes, posture %s\n",
                    g_entities[bs->entitynum].client->pers.netname,
                    bs->tac.allies, bs->tac.foes,
                    bs->tac.posture == TACTIC_PUSH     ? "push"
                    : bs->tac.posture == TACTIC_FALLBACK ? "fall back"
                                                         : "even");
    }
}

/*
==================
BotPosture
==================
*/
int BotPosture(bot_state_t* bs) {
    if (!BotTacticsEnabled()) {
        return TACTIC_EVEN;
    }
    return bs->tac.posture;
}

/*
==================
BotIdealAttackRange

The stock AI fights everything at 140 units with a 40 unit tolerance, so a bot
holding a railgun walks into shotgun range to use it and a bot holding a
lightning gun backs out of the gun's own 768 unit reach. These are the bands the
weapons are actually good in.
==================
*/
void BotIdealAttackRange(bot_state_t* bs, float* dist, float* range) {
    switch (bs->cur_ps.weapon) {
        case WP_GAUNTLET:
            *dist = 0;
            *range = 0;
            return;
        case WP_SHOTGUN:
            *dist = 220;
            *range = 130;
            return;
        case WP_LIGHTNING:
            // the gun stops at 768; being outside that is the same as having no
            // weapon at all
            *dist = 380;
            *range = 200;
            return;
        case WP_RAILGUN:
            *dist = 900;
            *range = 450;
            return;
        case WP_ROCKET_LAUNCHER:
            // close enough to lead the splash, far enough not to eat it
            *dist = 400;
            *range = 220;
            return;
        case WP_GRENADE_LAUNCHER:
            *dist = 450;
            *range = 250;
            return;
        case WP_PLASMAGUN:
            *dist = 450;
            *range = 250;
            return;
        case WP_BFG:
            *dist = 600;
            *range = 300;
            return;
        case WP_MACHINEGUN:
        case WP_CHAINGUN:
        case WP_HMG:
        case WP_NAILGUN:
            *dist = 500;
            *range = 300;
            return;
        default:
            *dist = 300;
            *range = 200;
            return;
    }
}

/*
==================
BotDodgeDirection
==================
*/
int BotDodgeDirection(bot_state_t* bs, vec3_t dir) {
    if (!BotTacticsEnabled() || !bot_dodge.integer) {
        return qfalse;
    }
    if (bs->tac.threat_time < FloatTime()) {
        return qfalse;
    }
    VectorCopy(bs->tac.threatdir, dir);
    return qtrue;
}

/*
==================
BotEnemyTrackingMe

True when the enemy is holding a hitscan weapon and has the bot near the middle
of their view. There is nothing to dodge yet - the shot has not happened - so
this is what makes a bot break its strafe rhythm rather than hold one direction
long enough to be an easy rail target.
==================
*/
int BotEnemyTrackingMe(bot_state_t* bs) {
    gclient_t* cl;
    vec3_t forward, tobot;

    if (!BotTacticsEnabled() || !bot_dodge.integer) {
        return qfalse;
    }
    if (bs->enemy < 0 || bs->enemy >= level.maxclients) {
        return qfalse;
    }
    if (!g_entities[bs->enemy].inuse || !g_entities[bs->enemy].client) {
        return qfalse;
    }
    cl = g_entities[bs->enemy].client;
    switch (cl->ps.weapon) {
        case WP_RAILGUN:
        case WP_LIGHTNING:
        case WP_MACHINEGUN:
        case WP_SHOTGUN:
        case WP_CHAINGUN:
        case WP_HMG:
            break;
        default:
            return qfalse;
    }
    AngleVectors(cl->ps.viewangles, forward, NULL, NULL);
    VectorSubtract(bs->origin, cl->ps.origin, tobot);
    if (VectorNormalize(tobot) < 1.0f) {
        return qfalse;
    }
    return DotProduct(forward, tobot) > 0.97f;
}

/*
==================
BotGoalItem

The gitem_t behind an item goal, or NULL. Goals also cover flags, obelisks and
map locations, so this is allowed to fail and every caller has to cope.
==================
*/
static gitem_t* BotGoalItem(bot_goal_t* goal) {
    gentity_t* ent;

    if (!(goal->flags & GFL_ITEM)) {
        return NULL;
    }
    if (goal->entitynum <= 0 || goal->entitynum >= level.num_entities) {
        return NULL;
    }
    ent = &g_entities[goal->entitynum];
    if (!ent->inuse) {
        return NULL;
    }
    return ent->item;
}

/*
==================
BotWantsItemGoal

Whether an item is worth breaking off what the bot is doing for. Only consulted
when there is an enemy about: with the map to itself a bot should keep hoovering
up everything it walks past, which is what the stock weights already do well.
==================
*/
int BotWantsItemGoal(bot_state_t* bs, bot_goal_t* goal) {
    gitem_t* item;
    int have, ammo;

    if (!BotTacticsEnabled()) {
        return qtrue;
    }
    item = BotGoalItem(goal);
    // not an item, or an item this frame cannot resolve: leave the stock
    // decision alone rather than guess
    if (!item) {
        return qtrue;
    }
    switch (item->giType) {
        case IT_POWERUP:
        case IT_PERSISTANT_POWERUP:
            // always. A quad on the floor is worth losing a fight over
            return qtrue;
        case IT_HOLDABLE:
            return qtrue;
        case IT_TEAM:
            return qtrue;
        case IT_HEALTH:
            // mega health is worth it even at full health; the rest is not
            if (item->quantity >= 100) {
                return qtrue;
            }
            return bs->inventory[INVENTORY_HEALTH] < 100;
        case IT_ARMOR:
            // a five point shard is not worth crossing a room under fire for
            if (item->quantity <= 5) {
                return bs->inventory[INVENTORY_ARMOR] < 50;
            }
            return bs->inventory[INVENTORY_ARMOR] < 150;
        case IT_WEAPON:
            if (item->giTag <= WP_NONE || item->giTag >= WP_NUM_WEAPONS) {
                return qtrue;
            }
            have = (bs->cur_ps.stats[STAT_WEAPONS] & (1 << item->giTag)) != 0;
            if (!have) {
                return qtrue;
            }
            // already carrying it: only worth it for the ammo that comes with it
            return bs->cur_ps.ammo[item->giTag] < 20;
        case IT_AMMO:
            if (item->giTag <= WP_NONE || item->giTag >= WP_NUM_WEAPONS) {
                return qtrue;
            }
            // ammo for a weapon the bot is not carrying can wait
            if (!(bs->cur_ps.stats[STAT_WEAPONS] & (1 << item->giTag))) {
                return qfalse;
            }
            ammo = bs->cur_ps.ammo[item->giTag];
            return ammo < 40;
        default:
            return qtrue;
    }
}

/*
==================
BotItemSearchRange

How far off its path a bot will go for something, scaled by what it is short of.
A bot at 30 health should be hunting a medkit two rooms away; the same bot at
full health should not.
==================
*/
float BotItemSearchRange(bot_state_t* bs, float range) {
    float scale = 1.0f;

    if (!BotTacticsEnabled()) {
        return range;
    }
    if (bs->inventory[INVENTORY_HEALTH] < 50) {
        scale = 2.0f;
    } else if (bs->inventory[INVENTORY_HEALTH] < 80) {
        scale = 1.5f;
    } else if (bs->inventory[INVENTORY_ARMOR] < 30) {
        scale = 1.3f;
    }
    // with nobody in sight there is no cost to looking further
    if (bs->enemy < 0 && !bs->tac.foes) {
        scale += 0.5f;
    }
    range *= scale;
    if (range > 900) {
        range = 900;
    }
    return range;
}

/*
==================
BotRegroupGoal

A goal standing on the nearest team mate. Retreating away from an enemy takes a
bot into empty map on its own; retreating towards the person already shooting at
that enemy is how a group falls back together.
==================
*/
int BotRegroupGoal(bot_state_t* bs, bot_goal_t* goal) {
    int areanum;

    if (!BotTacticsEnabled()) {
        return qfalse;
    }
    if (!TeamPlayIsOn()) {
        return qfalse;
    }
    if (bs->tac.nearestally < 0) {
        return qfalse;
    }
    // already there
    if (bs->tac.nearestallydist < 200) {
        return qfalse;
    }
    areanum = BotPointAreaNum(bs->tac.nearestallyorigin);
    if (!areanum || !trap_AAS_AreaReachability(areanum)) {
        return qfalse;
    }
    memset(goal, 0, sizeof(bot_goal_t));
    goal->entitynum = bs->tac.nearestally;
    goal->areanum = areanum;
    VectorCopy(bs->tac.nearestallyorigin, goal->origin);
    VectorSet(goal->mins, -8, -8, -8);
    VectorSet(goal->maxs, 8, 8, 8);
    return qtrue;
}

/*
==================
BotTeamKeyArea

The thing this bot's team loses the match by losing. NULL in the gametypes that
have no such place, which is most of them.
==================
*/
static bot_goal_t* BotTeamKeyArea(bot_state_t* bs) {
    switch (gametype) {
        case GT_CTF:
        case GT_1FCTF:
            return BotTeamFlag(bs);
        case GT_OBELISK:
        case GT_HARVESTER:
            return (BotTeam(bs) == TEAM_RED) ? &redobelisk : &blueobelisk;
        default:
            return NULL;
    }
}

/*
==================
BotAutoDefendGoal

Take a defensive posting when nobody on the team has one.

This is capped hard and deliberately. A default voice order once put an entire
human team on defence for a whole match (TRACKER E31), and the fix for that is
not worth undoing here: at most a quarter of the team defends, and only bots
that have nothing else to do at all are eligible.
==================
*/
int BotAutoDefendGoal(bot_state_t* bs) {
    int i, teammates, defenders, wanted;
    bot_goal_t* keyarea;

    if (!BotTacticsEnabled()) {
        return qfalse;
    }
    if (!TeamPlayIsOn()) {
        return qfalse;
    }
    // an order outranks this, always
    if (bs->ordered || bs->ltgtype) {
        return qfalse;
    }
    keyarea = BotTeamKeyArea(bs);
    // team deathmatch and clan arena have nothing to stand on; the areanum test
    // also covers a gametype whose goals have not been resolved on this map
    if (!keyarea || !keyarea->areanum) {
        return qfalse;
    }
    teammates = 0;
    defenders = 0;
    for (i = 0; i < level.maxclients; i++) {
        if (!g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        if (g_entities[i].client->sess.sessionTeam != g_entities[bs->client].client->sess.sessionTeam) {
            continue;
        }
        teammates++;
        if (i == bs->client) {
            continue;
        }
        if (botstates[i] && botstates[i]->inuse &&
            botstates[i]->ltgtype == LTG_DEFENDKEYAREA) {
            defenders++;
        }
    }
    wanted = teammates / 4;
    if (wanted < 1) {
        wanted = 1;
    }
    if (defenders >= wanted) {
        return qfalse;
    }
    //
    bs->ltgtype = LTG_DEFENDKEYAREA;
    memcpy(&bs->teamgoal, keyarea, sizeof(bot_goal_t));
    // not TEAM_DEFENDKEYAREA_TIME: that is ten minutes, which is right for an
    // order from a team leader and wrong for a job a bot gave itself because the
    // base happened to be empty when it looked. A minute, then reconsider.
    bs->teamgoal_time = FloatTime() + 60;
    bs->decisionmaker = bs->client;
    bs->ordered = qfalse;
    bs->defendaway_time = 0;
    if (bot_debugTactics.integer) {
        BotAI_Print(PRT_MESSAGE, "%s: no one defending (%d of %d), taking it\n",
                    g_entities[bs->entitynum].client->pers.netname, defenders, teammates);
    }
    return qtrue;
}

/*
==================
BotNodeName

Which AI node a bot is sitting in, by pointer. There is no name stored anywhere -
BotRecordNodeSwitch takes one as a string argument and throws it away after
printing - so this is the only way to ask a running bot what it is doing.
==================
*/
static const char* BotNodeName(bot_state_t* bs) {
    if (bs->ainode == AINode_Battle_Fight) {
        return (bs->flags & BFL_FIGHTSUICIDAL) ? "fight (suicidal)" : "fight";
    }
    if (bs->ainode == AINode_Battle_Chase) {
        return "chase";
    }
    if (bs->ainode == AINode_Battle_Retreat) {
        return "retreat";
    }
    if (bs->ainode == AINode_Battle_NBG) {
        return "grab (in fight)";
    }
    if (bs->ainode == AINode_Seek_NBG) {
        return "grab";
    }
    if (bs->ainode == AINode_Seek_LTG) {
        return "seek";
    }
    if (bs->ainode == AINode_Seek_ActivateEntity) {
        return "activate";
    }
    if (bs->ainode == AINode_Stand) {
        return "stand";
    }
    if (bs->ainode == AINode_Respawn) {
        return "respawn";
    }
    if (bs->ainode == AINode_Observer) {
        return "observer";
    }
    if (bs->ainode == AINode_Intermission) {
        return "intermission";
    }
    if (bs->ainode == AINode_InstaGib) {
        return "instagib hunt";
    }
    return "?";
}

/*
==================
BotTacticsReport

The "bots" console command.

BotTeamplayReport already existed and prints one line per bot - but it walks the
red team and then the blue team, so in a free-for-all it prints two headings and
nothing else, which is exactly the gametype the tactical layer was first tested
in. This reports every bot in every gametype, and says what the layer decided
rather than only what the long term goal is.
==================
*/
void BotTacticsReport(void) {
    int i, bots, posture[3], dodging, fighting, stuck, idle;
    float still;
    bot_state_t* bs;
    char netname[MAX_NETNAME];
    gclient_t* cl;

    if (!bot_tactics.integer) {
        G_Printf("bot_tactics is 0 - stock Quake 3 behaviour, nothing below is in use\n");
    }
    G_Printf("squad range %g, dodge %s\n", bot_squadRange.value,
             bot_dodge.integer ? "on" : "off");
    G_Printf("%-20s %7s %-10s %-9s %5s %-16s %-10s %s\n",
             "name", "hp/ar", "posture", "near", "enemy", "node", "goal", "still for");

    bots = 0;
    dodging = 0;
    fighting = 0;
    stuck = 0;
    posture[0] = posture[1] = posture[2] = 0;

    for (i = 0; i < level.maxclients; i++) {
        bs = botstates[i];
        if (!bs || !bs->inuse) {
            continue;
        }
        if (!g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        cl = g_entities[i].client;
        if (cl->sess.sessionTeam == TEAM_SPECTATOR) {
            continue;
        }
        bots++;
        if (bs->tac.posture >= 0 && bs->tac.posture <= 2) {
            posture[bs->tac.posture]++;
        }
        if (bs->tac.threat_time > FloatTime()) {
            dodging++;
        }
        if (bs->enemy >= 0) {
            fighting++;
        }
        /*
        [QL] Standing still is correct at intermission, and BotTacticsUpdate is
        not called there - BotDeathmatchAI gates it on !BotIntermission - so
        moved_time simply stops advancing and every bot reads as stuck. The map
        report runs at ShutdownGame, which is usually *after* an intermission, so
        the first version of this reported "59 of them have not moved in over
        three seconds" for a server that was working perfectly.
        */
        idle = (bs->ainode == AINode_Intermission || bs->ainode == AINode_Observer ||
                bs->ainode == AINode_Respawn || BotIsDead(bs));
        still = FloatTime() - bs->tac.moved_time;
        if (!idle && still > 3.0f) {
            stuck++;
        }
        ClientName(bs->client, netname, sizeof(netname));
        G_Printf("%-20s %3i/%-3i %-10s %d v %-5d %5s %-16s %-10s %s%.1fs\n",
                 netname,
                 cl->ps.stats[STAT_HEALTH], cl->ps.stats[STAT_ARMOR],
                 bs->tac.posture == TACTIC_PUSH       ? "push"
                 : bs->tac.posture == TACTIC_FALLBACK ? "fall back"
                                                      : "even",
                 bs->tac.allies, bs->tac.foes,
                 bs->enemy >= 0 ? "yes" : "-",
                 BotNodeName(bs),
                 bs->ltgtype == LTG_DEFENDKEYAREA ? "defending"
                 : bs->ltgtype                    ? "team goal"
                                                  : "roaming",
                 idle ? "idle, " : "", still);
    }
    G_Printf("%i bots: %i pushing, %i even, %i falling back; %i with an enemy, %i dodging\n",
             bots, posture[TACTIC_PUSH], posture[TACTIC_EVEN], posture[TACTIC_FALLBACK],
             fighting, dodging);
    if (stuck) {
        G_Printf("^3%i of them have not moved in over three seconds\n", stuck);
    }
}

/*
==================
BotRoomToMove

Is there somewhere to go in this direction - not blocked, and not off an edge.

The edge half is the point. A bot backing away from an enemy has its back to
wherever it is going, and the stock AI never looks: it composes a direction,
hands it to trap_BotMoveInDirection, and if the move is refused it flips the
strafe and tries once more with the same blocked component still in the vector.
Both attempts fail the same way and the bot stands still, which is what "they
back up into the edge and stop" looks like from the outside.
==================
*/
int BotRoomToMove(bot_state_t* bs, vec3_t dir, float dist) {
    gentity_t* ent;
    vec3_t start, end, below;
    bsp_trace_t trace;

    if (bs->entitynum < 0 || bs->entitynum >= level.num_entities) {
        return qtrue;
    }
    ent = &g_entities[bs->entitynum];

    VectorCopy(bs->origin, start);
    VectorMA(start, dist, dir, end);

    /*
    MASK_SOLID, not MASK_PLAYERSOLID. The difference is CONTENTS_BODY - other
    players - and including them was a real bug the moment this check was applied
    to every direction rather than just backward: on a full server a bot has
    somebody within 72 units most of the time, so every direction reported "no
    room" and the bot stopped moving. Reported as bots getting stuck on each
    other in CTF on japanesecastles, which is a map where a whole team funnels
    through one corridor.

    A player is not a wall. Bots are supposed to push through each other and the
    engine slides them past; what this function is for is geometry and edges.
    */
    BotAI_Trace(&trace, start, ent->r.mins, ent->r.maxs, end, bs->entitynum, MASK_SOLID);
    if (trace.fraction < 0.9f) {
        return qfalse;
    }

    /*
    And a floor to land on. 128 units is about where a drop stops being a step
    and starts being a decision - far enough to allow the ledges bots drop off
    all the time, close enough to catch the ones that are really a hole.
    */
    VectorCopy(trace.endpos, below);
    below[2] -= 128;
    // MASK_SOLID here too: another player standing in the drop is not a floor,
    // and treating one as a floor is how a bot decides a hole is safe
    BotAI_Trace(&trace, trace.endpos, ent->r.mins, ent->r.maxs, below, bs->entitynum, MASK_SOLID);
    if (trace.fraction >= 1.0f) {
        return qfalse;
    }
    return qtrue;
}
