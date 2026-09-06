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

// [QL] defined further down, next to the room census it reads
static void BotAvoidCrowdedRoute(bot_state_t* bs);

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
        bs->tac.reportedstuck = qfalse;
        VectorCopy(bs->origin, bs->tac.lastorigin);
    } else if (bs->tac.moved_time <= 0.0f) {
        bs->tac.moved_time = FloatTime();
        VectorCopy(bs->origin, bs->tac.lastorigin);
    }

    /*
    [QL] Say it when it happens, not at the end of the map.

    The map report catches a bot that is stuck when the map ends, which has
    turned out to be the wrong moment twice - once because everything reads as
    stuck at intermission, and once because the report someone actually needed
    was of a pile-up on the first spawn, twenty minutes earlier. This fires on
    the way *into* a stuck episode, once, with the two things that separate the
    causes:

      node and goal   - what it was trying to do
      area            - and whether the AAS covers where it is standing at all.
                        A bot in an area with no reachability is not blocked by
                        anything, it is somewhere the router cannot plan from,
                        and no amount of sidestepping will help it
    */
    /*
    [QL] Go round before being stuck in it, not after.

    The re-roll below fires at three seconds of not moving, which is the right
    answer once a bot is already in the queue and the wrong moment to notice
    one. A bot standing in a room that holds a sixth of its own team, on its way
    somewhere, has enough to decide with: BotGetAlternateRouteGoal now picks the
    emptiest way round, and the counts move as team mates commit, so the tenth
    bot to ask gets a different answer from the second.

    Every four seconds at most. Re-picking a route more often than it takes to
    walk any of it is how a bot oscillates between two corridors and arrives on
    neither.
    */
    // [QL] refuse the crowded door; see BotAvoidCrowdedRoute
    BotAvoidCrowdedRoute(bs);

    if (BotTacticsEnabled() && gametype == GT_CTF &&
        bs->tac.reroute_time < FloatTime() &&
        (bs->ltgtype == LTG_GETFLAG || bs->ltgtype == LTG_RUSHBASE ||
         bs->ltgtype == LTG_RETURNFLAG || bs->ltgtype == LTG_ATTACKENEMYBASE)) {
        bs->tac.reroute_time = FloatTime() + 4.0f;
        if (BotRoomCrowding(bs, bs->origin) > 6) {
            BotGetAlternateRouteGoal(bs, BotOppositeTeam(bs));
        }
    }

    if (!bs->tac.reportedstuck && FloatTime() - bs->tac.moved_time > 3.0f) {
        int area = bs->areanum;

        bs->tac.reportedstuck = qtrue;
        /*
        [QL] Stuck on the way to a flag: try the other way round.

        The alternate route is picked once, when the goal is set, and never
        again - so thirty bots heading for the same flag pick the same route,
        queue in the same corridor, and nothing in the stock AI ever
        reconsiders. Sidestepping cannot help: the route is not blocked by one
        body, it is blocked by the twenty ahead all going the same way.

        Re-rolling here is the cheapest thing that answers it. The bot has
        already stood still for three seconds, so whatever it was doing is not
        working, and BotGetAlternateRouteGoal picks among the map's alt route
        goals - bots that re-roll at different moments spread across them
        instead of all switching to the same second route.
        */
        if (bot_tactics.integer && gametype == GT_CTF &&
            (bs->ltgtype == LTG_GETFLAG || bs->ltgtype == LTG_RUSHBASE ||
             bs->ltgtype == LTG_RETURNFLAG || bs->ltgtype == LTG_ATTACKENEMYBASE)) {
            BotGetAlternateRouteGoal(bs, BotOppositeTeam(bs));
        }
        if (bot_debugTactics.integer) {
            char where[64];

            /*
            [QL] Name the room, not just the AAS area number.

            "assess what room theyre in" - the map already has the answer.
            target_location entities are what the HUD prints beside a name, and
            japanesecastles has 85 of them: Flagroom, Main Entrance, Red
            Courtyard, Balcony. An area number says nothing to anyone reading a
            log; "twelve bots stuck in Main Entrance" says where to look.
            */
            if (!Team_GetLocationMsg(&g_entities[bs->entitynum], where, sizeof(where))) {
                Q_strncpyz(where, "nowhere named", sizeof(where));
            }
            BotAI_Print(PRT_MESSAGE,
                        "%s: stuck %.1fs in %s, area %i%s, ltg %i, %d allies %d foes, enemy %s\n",
                        g_entities[bs->entitynum].client->pers.netname,
                        FloatTime() - bs->tac.moved_time, where, area,
                        (area && trap_AAS_AreaReachability(area)) ? "" : " (NO REACHABILITY)",
                        bs->ltgtype, bs->tac.allies, bs->tac.foes,
                        bs->enemy >= 0 ? "yes" : "no");
        }
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

    /*
    [QL] On change, not on refresh.

    This used to print whenever a bot could see anybody, and the picture is
    refreshed four or five times a second - so at sixty bots it was upwards of
    two hundred lines a second, which is not a diagnostic, it is a denial of
    service against the log it is written to. The cvar's own description said
    "every posture change" and the code did not.
    */
    if (bot_debugTactics.integer && bs->tac.posture != bs->tac.reportedposture) {
        bs->tac.reportedposture = bs->tac.posture;
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
BotLongTermGoalName

[QL] Which long-term goal a bot is on, by name.

This column used to read "team goal" for anything that was not defence, which
is every CTF job there is. A 62-bot report printed it 62 times and answered
nothing: the question the report exists to answer - are they all escorting one
carrier - was the one thing it could not say. The counts are in ai_main.h.
==================
*/
static const char* BotLongTermGoalName(bot_state_t* bs) {
    switch (bs->ltgtype) {
        case 0: return "roaming";
        case LTG_TEAMHELP: return "helping";
        case LTG_TEAMACCOMPANY: return "escorting";
        case LTG_DEFENDKEYAREA: return "defending";
        case LTG_GETFLAG: return "get flag";
        case LTG_RUSHBASE: return "carrying";
        case LTG_RETURNFLAG: return "return flag";
        case LTG_CAMP:
        case LTG_CAMPORDER: return "camping";
        case LTG_PATROL: return "patrol";
        case LTG_GETITEM: return "get item";
        case LTG_KILL: return "kill";
        case LTG_HARVEST: return "harvest";
        case LTG_ATTACKENEMYBASE: return "attack base";
        default: return "other";
    }
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
                 BotLongTermGoalName(bs),
                 idle ? "idle, " : "", still);
    }
    G_Printf("%i bots: %i pushing, %i even, %i falling back; %i with an enemy, %i dodging\n",
             bots, posture[TACTIC_PUSH], posture[TACTIC_EVEN], posture[TACTIC_FALLBACK],
             fighting, dodging);
    if (stuck) {
        G_Printf("^3%i of them have not moved in over three seconds\n", stuck);
    }

    /*
    [QL] Where everybody is, by room name.

    The per-bot table says what each one is doing and is sixty lines long; this
    is the one line that answers "are they all in the same place". target_location
    entities are the map's own names for its rooms - the ones the HUD prints
    beside a player's name - so this reads the way somebody watching the match
    would describe it.
    */
    {
        char where[64];
        const char* names[MAX_CLIENTS];
        int counts[MAX_CLIENTS];
        int numrooms = 0;
        int j;

        for (i = 0; i < level.maxclients; i++) {
            if (!g_entities[i].inuse || !g_entities[i].client || !botstates[i] ||
                !botstates[i]->inuse) {
                continue;
            }
            if (g_entities[i].client->sess.sessionTeam == TEAM_SPECTATOR) {
                continue;
            }
            if (!Team_GetLocationMsg(&g_entities[i], where, sizeof(where))) {
                continue;
            }
            for (j = 0; j < numrooms; j++) {
                if (!Q_stricmp(names[j], where)) {
                    counts[j]++;
                    break;
                }
            }
            if (j == numrooms && numrooms < MAX_CLIENTS) {
                /* the configstring the message came from is static for the map,
                   so keeping the pointer is safe until the map ends - and this
                   only runs when the map ends */
                names[numrooms] = G_NewString(where);
                counts[numrooms] = 1;
                numrooms++;
            }
        }
        if (numrooms) {
            G_Printf("rooms:");
            for (j = 0; j < numrooms; j++) {
                G_Printf(" %s %i%s", names[j], counts[j], (j < numrooms - 1) ? "," : "\n");
            }
        }
    }
}

/*
==================
The room census

[QL] How many of a team are in each of the map's named rooms, and how many are
on their way to one.

Asked for as: "maybe they should be aware of what room theyre in. So they can be
like 'Theres a lot already in that room or heading to that room, maybe I should
go a different way.'" The scoreboard in the report that came with it reads
Main Stairway 13, Flagroom 5, Main Entrance 4 - thirteen bots in one stairwell,
single file, because AAS hands every one of them the same cheapest chain and
nothing in the AI has ever been able to say "that way is full".

target_location entities are the map's own names for its rooms and are already
what the HUD prints beside a player's name, so the vocabulary exists; it just was
not wired to anything the bots could read. They are a linked list through
nextTrain with no index of their own, so the list is walked once per map into
roomEnt[] and everything after that is an ordinal.

Two counts, because they answer different halves of the question:

  here   - bodies in the room now
  bound  - bots whose current goal is in that room, who are not there yet

A room that is empty but has eleven bots walking into it is not a way round, and
counting only the first number is how a queue re-forms one corner further on.
==================
*/
#define MAX_ROOMS 128
#define ROOM_CENSUS_INTERVAL 0.5f

static gentity_t* roomEnt[MAX_ROOMS];
static int numRooms;
static int roomHere[MAX_ROOMS][TEAM_NUM_TEAMS];
static int roomBound[MAX_ROOMS][TEAM_NUM_TEAMS];
static float roomCensus_time;

/*
==================
BotRoomsReset

[QL] Forget the room list; the next census rebuilds it. Called when the game
module starts a map, because these are file statics and a map change otherwise
leaves pointers into the previous level's entities.
==================
*/
void BotRoomsReset(void) {
    numRooms = 0;
    roomCensus_time = 0;
    memset(roomEnt, 0, sizeof(roomEnt));
    memset(roomHere, 0, sizeof(roomHere));
    memset(roomBound, 0, sizeof(roomBound));
}

/*
==================
BotRoomAt

[QL] Which room a point is in - nearest by distance, no visibility test.

Team_GetLocation does the same walk with a trap_InPVS check, which is right for
labelling a player on the scoreboard and wrong here: this is asked about goal
positions the bot cannot see yet, which is the entire point of asking.
==================
*/
static int BotRoomAt(vec3_t origin) {
    float best, len;
    vec3_t dir;
    int i, bestroom;

    bestroom = -1;
    best = 8192.0f * 8192.0f;
    for (i = 0; i < numRooms; i++) {
        VectorSubtract(origin, roomEnt[i]->r.currentOrigin, dir);
        len = VectorLengthSquared(dir);
        if (len < best) {
            best = len;
            bestroom = i;
        }
    }
    return bestroom;
}

/*
==================
BotRoomCensus

[QL] Recount, twice a second. Up to 85 rooms against 64 players is a few
thousand distance tests, which is nothing at that interval and would be
noticeable every frame for every bot.
==================
*/
static void BotRoomCensus(void) {
    gentity_t* eloc;
    bot_state_t* other;
    int i, room, team;

    if (roomCensus_time > FloatTime() - ROOM_CENSUS_INTERVAL) {
        return;
    }
    roomCensus_time = FloatTime();

    if (!numRooms) {
        for (eloc = level.locationHead; eloc && numRooms < MAX_ROOMS; eloc = eloc->nextTrain) {
            roomEnt[numRooms++] = eloc;
        }
    }
    if (!numRooms) {
        return;
    }
    memset(roomHere, 0, sizeof(roomHere));
    memset(roomBound, 0, sizeof(roomBound));

    for (i = 0; i < level.maxclients; i++) {
        if (!g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        team = g_entities[i].client->sess.sessionTeam;
        if (team < 0 || team >= TEAM_NUM_TEAMS) {
            continue;
        }
        if (g_entities[i].client->ps.stats[STAT_HEALTH] <= 0) {
            continue;
        }
        room = BotRoomAt(g_entities[i].r.currentOrigin);
        if (room >= 0) {
            roomHere[room][team]++;
        }
        // and where it is trying to get to
        other = botstates[i];
        if (!other || !other->inuse) {
            continue;
        }
        if (other->altroutegoal.areanum) {
            room = BotRoomAt(other->altroutegoal.origin);
        } else if (other->ltgtype && other->teamgoal.areanum) {
            room = BotRoomAt(other->teamgoal.origin);
        } else {
            continue;
        }
        if (room >= 0) {
            roomBound[room][team]++;
        }
    }
}

/*
==================
BotRoomCrowding

[QL] How many of this bot's team are in, or heading for, the room containing a
point. The bot itself is not discounted - it is one body either way, and the
comparison between candidates is what matters.
==================
*/
int BotRoomCrowding(bot_state_t* bs, vec3_t origin) {
    int room, team;

    if (!BotTacticsEnabled()) {
        return 0;
    }
    BotRoomCensus();
    if (!numRooms) {
        return 0;
    }
    team = g_entities[bs->client].client->sess.sessionTeam;
    if (team < 0 || team >= TEAM_NUM_TEAMS) {
        return 0;
    }
    room = BotRoomAt(origin);
    if (room < 0) {
        return 0;
    }
    return roomHere[room][team] + roomBound[room][team];
}

/*
==================
BotAvoidCrowdedRoute

[QL] Make the router itself refuse the door everyone else is using.

Alternative route goals could not fix "most still exit the left path", and it is
worth being clear why: an alt route goal only changes the *middle* of a journey.
The leg from the flag room to that goal is still the cheapest reachability chain,
and out of one room to anywhere on the far side of the map that is the same door
for every bot. Diversity has to be injected where the door is chosen, which is
BotGetReachabilityToGoal.

That function already has the hook. It walks the reachabilities out of the
current area, takes the cheapest that survives its filters, and one of the
filters is BotAvoidSpots - a per-movestate list of places this bot will not route
through. It is what the prox mine code uses. A spot on the crowd queued in a
doorway makes that doorway's reachability fail the filter, and the bot takes the
next cheapest way out instead: the right-hand path.

Per bot, so the team does not move as one - and because BotCheckSnapshot clears
the avoid list every think and this runs immediately after it, the decision is
remade from scratch several times a second and can never go stale.

Three guards, and they exist because refusing every exit leaves a bot with no
reachability at all, which is worse than a queue:

  - the crowd must be *ahead*. A centroid behind or on top of the bot would
    reject the reachabilities leading away from it as readily as the ones
    leading into it.
  - not while fighting. BotAttackMove owns the movement then.
  - and if the bot has not moved for two seconds, stop avoiding. If going round
    were working it would be moving; standing still politely is not better than
    taking the crowded door.
==================
*/
#define CROWD_AVOID_MIN 4        // bodies ahead before it is a queue
#define CROWD_AVOID_RANGE 400.0f  // how far ahead to look
#define CROWD_AVOID_RADIUS 100.0f  // and how much of the map to refuse
#define CROWD_AVOID_NEAR 64.0f     // a crowd closer than this is not "ahead"

static void BotAvoidCrowdedRoute(bot_state_t* bs) {
    vec3_t fwd, dir, centre;
    float dist, speed, weight, total;
    int i, count, team, carrier, radius;

    if (!BotTacticsEnabled() || !bs->ltgtype) {
        return;
    }
    /*
    [QL] A carrier keeps avoiding with an enemy in sight; everyone else stops.

    "the bots still exit on the left side, even after they get the enemy flag.
    instead of going through the right side and pathing to avoid combat heavy
    areas." Both halves of that were structural here: this counted only team
    mates, and it switched itself off the moment an enemy appeared - so the one
    bot that most needs to route away from a fight was the one guaranteed not
    to. A carrier should not be trading at all; going round *is* its job.
    */
    carrier = BotCTFCarryingFlag(bs) || Bot1FCTFCarryingFlag(bs) ||
              BotHarvesterCarryingCubes(bs);
    if (bs->enemy >= 0 && !carrier) {
        return;
    }
    /*
    Five seconds, not two. Two was shorter than it takes a bot to get out of a
    queue even when it has somewhere better to go: a field log has 18 stuck
    episodes in one area with 11 to 13 allies around, and the reports fire at
    three seconds - by which point avoidance had been off for a second and the
    bot was back in the same doorway. Long enough to actually try the other way,
    and it still gives up rather than standing still forever.
    */
    if (FloatTime() - bs->tac.moved_time > 5.0f) {
        return;
    }
    // which way is the bot actually going
    VectorCopy(bs->cur_ps.velocity, fwd);
    fwd[2] = 0;
    speed = VectorNormalize(fwd);
    if (speed < 20.0f) {
        AngleVectors(bs->viewangles, fwd, NULL, NULL);
        fwd[2] = 0;
        if (VectorNormalize(fwd) < 0.1f) {
            return;
        }
    }

    VectorClear(centre);
    count = 0;
    total = 0.0f;
    team = g_entities[bs->client].client->sess.sessionTeam;
    for (i = 0; i < level.maxclients; i++) {
        if (i == bs->client || !g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        if (g_entities[i].client->sess.sessionTeam == TEAM_SPECTATOR) {
            continue;
        }
        if (g_entities[i].client->ps.stats[STAT_HEALTH] <= 0) {
            continue;
        }
        /*
        [QL] Enemies count, and count for more.

        A team mate in the way is a queue. An enemy in the way is a fight, and
        for a bot carrying the objective a fight is the thing it is trying not
        to have. Three to one, so two enemies ahead are worth going round for on
        their own where it takes four team mates.
        */
        weight = (g_entities[i].client->sess.sessionTeam == team) ? 1.0f : 3.0f;
        VectorSubtract(g_entities[i].r.currentOrigin, bs->origin, dir);
        dir[2] = 0;
        dist = VectorLength(dir);
        if (dist < CROWD_AVOID_NEAR || dist > CROWD_AVOID_RANGE) {
            continue;
        }
        VectorScale(dir, 1.0f / dist, dir);
        if (DotProduct(dir, fwd) < 0.5f) {
            continue;  // not in the way
        }
        VectorMA(centre, weight, g_entities[i].r.currentOrigin, centre);
        total += weight;
        count++;
    }
    if (total < (float)CROWD_AVOID_MIN) {
        return;
    }
    VectorScale(centre, 1.0f / total, centre);
    // and it has to still be ahead once averaged
    VectorSubtract(centre, bs->origin, dir);
    dir[2] = 0;
    if (VectorNormalize(dir) < CROWD_AVOID_NEAR || DotProduct(dir, fwd) < 0.5f) {
        return;
    }
    // a carrier gives it a wider berth - it cannot afford the fight at all
    radius = carrier ? (int)(CROWD_AVOID_RADIUS * 1.6f) : (int)CROWD_AVOID_RADIUS;
    /* [QL] AVOID_COST, not AVOID_ALWAYS - see BotGetReachabilityToGoal. A refusal
       leaves a bot with no route when every exit passes the same crowd, and it
       waits in the doorway instead; a cost leaves it a route it would simply
       rather not take. */
    trap_BotAddAvoidSpot(bs->ms, centre, (float)radius, AVOID_COST);
}

/*
==================
BotEnemyFlagAtBase

[QL] qtrue when the flag this bot is trying to capture is still on its stand.

bs->redflagstatus / blueflagstatus are the bot's own mirror of the flag state,
kept from the team sound events in BotCheckEvents, and BotCTFSeekGoals has always
trusted them. The long term goal handler never asked.
==================
*/
int BotEnemyFlagAtBase(bot_state_t* bs) {
    if (gametype != GT_CTF) {
        return qtrue;  // no opinion
    }
    if (BotTeam(bs) == TEAM_RED) {
        return bs->blueflagstatus == 0;
    }
    return bs->redflagstatus == 0;
}

/*
==================
BotTeamSpacing

[QL] Keep a body's width between team mates, so a squad is a line rather than a
pile.

The problem, from the field: twenty-one allies and no enemies in one flag room,
several of them reporting stuck for over three seconds, the whole team occupying
about two players' worth of floor. AAS has no concept of another player being in
the way - it routes over static geometry and every bot with the same goal from
the same area gets the identical chain - so a team converging on one point
converges on one *point*.

This is RecastNavigation's DetourCrowd separation idea (zlib, R17) written for
our movement rather than ported: sum a push away from each neighbour, weighted
by how close it is, and blend it into the direction the bot is already going.
There is no crowd simulation and no navmesh here; the useful part of DetourCrowd
at this scale is just that one force.

Deliberately narrow, because movement is the part of this AI that has broken
most often:

  - carrying an objective overrides it entirely. A flag carrier being shoved
    sideways by its own escort is worse than the escort standing too close, and
    the carrier is the one player whose exact path matters.
  - only with both feet on the ground, so a jump pad, a lift or a ledge hop is
    never redirected mid-air.
  - only with no enemy. In a fight BotAttackMove is already choosing where to
    stand and the two would argue; the pile-ups are in transit.
  - and never into a wall or off an edge - BotRoomToMove is the same check the
    dodge and the sidestep use.
==================
*/
#define BOT_TEAMSPACE 72.0f      // how much room a bot wants beside it
#define BOT_TEAMSPACE_HEIGHT 48.0f  // ignore team mates on another floor
#define BOT_TEAMSPACE_BLEND 0.5f    // vs. the direction it was already going

void BotTeamSpacing(bot_state_t* bs) {
    vec3_t sep, dir, vel, move;
    float dist, weight, speed;
    int i, near;

    if (!BotTacticsEnabled() || !TeamPlayIsOn()) {
        return;
    }
    if (BotIsDead(bs) || BotIntermission(bs) || BotIsObserver(bs)) {
        return;
    }
    // the override: whoever is carrying the objective goes where it means to
    if (BotCTFCarryingFlag(bs) || Bot1FCTFCarryingFlag(bs) || BotHarvesterCarryingCubes(bs)) {
        return;
    }
    if (bs->enemy >= 0) {
        return;  // BotAttackMove owns the movement while there is something to shoot
    }
    if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE) {
        return;  // airborne: jump pad, lift, or a gap - leave it alone
    }

    VectorClear(sep);
    near = 0;
    for (i = 0; i < level.maxclients; i++) {
        if (i == bs->client || !g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        if (g_entities[i].client->sess.sessionTeam !=
            g_entities[bs->client].client->sess.sessionTeam) {
            continue;
        }
        if (g_entities[i].client->ps.stats[STAT_HEALTH] <= 0) {
            continue;
        }
        VectorSubtract(bs->origin, g_entities[i].r.currentOrigin, dir);
        if (fabs(dir[2]) > BOT_TEAMSPACE_HEIGHT) {
            continue;
        }
        dir[2] = 0;
        dist = VectorLength(dir);
        if (dist >= BOT_TEAMSPACE) {
            continue;
        }
        near++;
        if (dist < 1.0f) {
            // exactly on top of each other: push somewhere deterministic per
            // pair rather than nowhere, or both bots stay put forever
            dir[0] = (bs->client < i) ? 1.0f : -1.0f;
            dir[1] = 0;
            dist = 1.0f;
        }
        VectorScale(dir, 1.0f / dist, dir);
        weight = 1.0f - (dist / BOT_TEAMSPACE);
        VectorMA(sep, weight, dir, sep);
    }
    if (!near) {
        return;
    }
    if (VectorNormalize(sep) < 0.1f) {
        return;  // evenly surrounded; any direction is as blocked as the rest
    }
    if (!BotRoomToMove(bs, sep, 64)) {
        return;
    }

    /*
    Blend with what the bot was already doing. Standing still and stacked is the
    case that needs the whole push; moving is a nudge, so the goal still wins and
    a corridor is walked down rather than bounced along.
    */
    VectorCopy(bs->cur_ps.velocity, vel);
    vel[2] = 0;
    speed = VectorNormalize(vel);
    if (speed > 100.0f) {
        VectorMA(vel, BOT_TEAMSPACE_BLEND, sep, move);
        if (VectorNormalize(move) < 0.1f) {
            return;
        }
    } else {
        VectorCopy(sep, move);
    }
    trap_EA_Move(bs->client, move, 400);
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

/*
==================
BotCTFRoleMix

[QL] What fraction of the team should be doing each job right now.

The stock split is a die roll per bot - 40% chance of going for the flag, 30% of
defending, 30% of roaming - which is not a plan, it is a distribution. Four bots
can all roll "attack" and leave the base empty, and nothing about the roll
changes when the flags move.

These are targets for the *team*, and BotCTFPickRole below fills the biggest gap
rather than rolling dice, so twelve bots reliably end up near the mix rather than
somewhere in a binomial spread around it.

The mix moves with the game:

  nothing taken     the standing posture: press, but keep the door shut
  ours is out       the flag is walking away and getting it back beats anything
                    that could be won at the far end of the map
  theirs is out     we have a carrier, and a carrier alone is a carrier who dies
                    on the way home - escort becomes the biggest single job
  both are out      the messy one. Hold what we have, walk ours home, and do not
                    send anybody on a third errand
==================
*/
static void BotCTFRoleMix(bot_state_t* bs, float* mix) {
    qboolean ourflagout, theirflagout;
    float jitter;
    int i;

    if (BotTeam(bs) == TEAM_RED) {
        ourflagout = bs->redflagstatus != 0;
        theirflagout = bs->blueflagstatus != 0;
    } else {
        ourflagout = bs->blueflagstatus != 0;
        theirflagout = bs->redflagstatus != 0;
    }

    if (ourflagout && theirflagout) {
        mix[CTFROLE_ATTACK] = 0.15f;
        mix[CTFROLE_DEFEND] = 0.45f;
        mix[CTFROLE_ESCORT] = 0.30f;
        mix[CTFROLE_ROAM] = 0.10f;
    } else if (ourflagout) {
        mix[CTFROLE_ATTACK] = 0.25f;
        mix[CTFROLE_DEFEND] = 0.60f;
        mix[CTFROLE_ESCORT] = 0.00f;
        mix[CTFROLE_ROAM] = 0.15f;
    } else if (theirflagout) {
        mix[CTFROLE_ATTACK] = 0.25f;
        mix[CTFROLE_DEFEND] = 0.30f;
        mix[CTFROLE_ESCORT] = 0.35f;
        mix[CTFROLE_ROAM] = 0.10f;
    } else {
        mix[CTFROLE_ATTACK] = 0.45f;
        mix[CTFROLE_DEFEND] = 0.35f;
        mix[CTFROLE_ESCORT] = 0.00f;
        mix[CTFROLE_ROAM] = 0.20f;
    }

    /*
    And it is never exactly those numbers. A fixed mix is a team that plays the
    same match twice; this wanders by up to a tenth on a slow clock shared by the
    whole team - the same value for every bot at a given moment, so the team
    leans one way together rather than each bot wobbling on its own.
    */
    jitter = sin((double)level.time * 0.00013) * 0.10f;
    mix[CTFROLE_ATTACK] += jitter;
    mix[CTFROLE_DEFEND] -= jitter;
    for (i = 0; i < CTFROLE_COUNT; i++) {
        if (mix[i] < 0.0f) {
            mix[i] = 0.0f;
        }
    }
}

/*
==================
BotCTFPickRole

The job with the biggest shortfall against the mix above, counting what the rest
of the team is already doing. Bots only - botstates[] is the only place a role is
legible, so a human holding the base is not counted and the bots will over-defend
behind one. That is the safe direction to be wrong in.
==================
*/
/*
==================
BotCTFRoleWanted

[QL] The mix turned into head counts, with escort capped in absolute terms.

Escorting is a job for a handful of bodies whatever the team size. It does not
scale: the useful part is two or three players between the carrier and whatever
is chasing, and past that every extra escort is one more body in the same
corridor. A third of a 32-man team is eleven of them converging on one player -
which is what 575 of 1094 stuck episodes were, and why the carrier could not
walk out of its own escort.

So the fraction sets the shape and this sets the ceiling. At four a side the cap
never binds and the mix is untouched; at thirty-two a side it is the whole
difference between a screen and a scrum.
==================
*/
#define CTF_MAX_ESCORTS 4

static void BotCTFRoleWanted(bot_state_t* bs, int teamsize, float* want) {
    float mix[CTFROLE_COUNT];
    int i;

    BotCTFRoleMix(bs, mix);
    for (i = 0; i < CTFROLE_COUNT; i++) {
        want[i] = mix[i] * (float)teamsize;
    }
    if (want[CTFROLE_ESCORT] > (float)CTF_MAX_ESCORTS) {
        want[CTFROLE_ESCORT] = (float)CTF_MAX_ESCORTS;
    }
}

/*
==================
BotCTFRoleCounts

[QL] What the rest of this bot's team is doing, by role, and how big the team is.

Split out of BotCTFPickRole so the escort cap can ask the same question without
picking a role. Bots only - botstates[] is the only place a role is legible, so
a human holding the base is not counted.
==================
*/
static void BotCTFRoleCounts(bot_state_t* bs, int* have, int* teamsize) {
    int i, team;
    float zone, ownd, enemyd;
    vec3_t dir;

    for (i = 0; i < CTFROLE_COUNT; i++) {
        have[i] = 0;
    }
    *teamsize = 0;
    team = g_entities[bs->client].client->sess.sessionTeam;

    /*
    [QL] The zone radius scales with the map, as Xonotic's does: their
    havocbot_middlepoint_radius is half the distance between the flag stands and
    the defence census uses half of that again. A quarter of the base separation
    is a flag room and its approach on a small map and still a flag room on a
    big one, which a fixed number in units is not.
    */
    VectorSubtract(ctf_redflag.origin, ctf_blueflag.origin, dir);
    zone = VectorLength(dir) * 0.25f;
    if (zone < 256.0f) {
        zone = 256.0f;
    }

    for (i = 0; i < level.maxclients; i++) {
        if (!g_entities[i].inuse || !g_entities[i].client) {
            continue;
        }
        if (g_entities[i].client->sess.sessionTeam != team) {
            continue;
        }
        (*teamsize)++;
        if (i == bs->client) {
            continue;
        }
        if (g_entities[i].client->ps.stats[STAT_HEALTH] <= 0) {
            continue;  // a corpse is not holding anything
        }
        /*
        [QL] Where they are, before what they say they are doing.

        This is the one idea worth taking wholesale from Xonotic's havocbot: its
        havocbot_ctf_teamcount counts live team mates within a radius of a
        point, and roles are decided by comparing bodies near the defence point,
        the middle and the offence point. It never asks another bot what its
        goal is.

        Counting ltgtype is what hid E67 for three builds. Every defender was
        being converted to attack the moment our flag was taken, the census read
        "nought defenders" because it was reading intentions, and the role
        picker kept issuing an order that was destroyed before anyone acted on
        it. A census of bodies cannot be lied to that way: a bot standing in the
        flag room is defending it whatever ltgtype says, and one that gets
        converted and walks away stops counting when it leaves.

        Position first, then ltgtype for the ones in neither zone - which is
        where escorting and roaming actually differ, and position cannot tell
        them apart.
        */
        /*
        [QL] Escort is asked of ltgtype first, before position.

        Position is the right signal for attack and defence, which are about
        holding ground. Escort is not: it is about *who* the bot is following,
        and an escort standing in the enemy flag room is doing its job, not
        attacking. Classifying it by position hid every escort behind a zone -
        a field log on the first build of this census read "have 26/5/0/0" with
        148 of 228 stuck episodes in LTG_TEAMACCOMPANY, which meant
        BotCTFRoleCrowded saw nought escorts and the E67 cap silently stopped
        capping.
        */
        if (botstates[i] && botstates[i]->inuse &&
            botstates[i]->ltgtype == LTG_TEAMACCOMPANY) {
            have[CTFROLE_ESCORT]++;
            continue;
        }
        VectorSubtract(g_entities[i].r.currentOrigin, ctf_redflag.origin, dir);
        ownd = VectorLength(dir);
        VectorSubtract(g_entities[i].r.currentOrigin, ctf_blueflag.origin, dir);
        enemyd = VectorLength(dir);
        if (team != TEAM_RED) {
            float swap = ownd;
            ownd = enemyd;
            enemyd = swap;
        }
        if (ownd < zone) {
            have[CTFROLE_DEFEND]++;
            continue;
        }
        if (enemyd < zone) {
            have[CTFROLE_ATTACK]++;
            continue;
        }
        if (!botstates[i] || !botstates[i]->inuse) {
            have[CTFROLE_ROAM]++;
            continue;
        }
        switch (botstates[i]->ltgtype) {
            case LTG_GETFLAG:
            case LTG_ATTACKENEMYBASE:
                have[CTFROLE_ATTACK]++;
                break;
            case LTG_DEFENDKEYAREA:
                have[CTFROLE_DEFEND]++;
                break;
            case LTG_TEAMACCOMPANY:
                have[CTFROLE_ESCORT]++;
                break;
            default:
                have[CTFROLE_ROAM]++;
                break;
        }
    }
    if (*teamsize < 1) {
        *teamsize = 1;
    }
}

/*
==================
BotCTFRoleCrowded

[QL] qtrue when the team already has as many of this job as the mix wants.

This is what stops the stock "follow the flag carrier" branch from turning into
a mob. That branch has no cap at all: any bot that can see its own carrier and
is not already defending switches to LTG_TEAMACCOMPANY for TEAM_ACCOMPANY_TIME,
which is ten minutes. At four a side that is a plan. At thirty-two a side a
field log shows 562 of 842 stuck episodes were bots in LTG_TEAMACCOMPANY -
twenty-odd of them pathing to the same point three metres from one player, who
consequently cannot move, and in instagib is a rail magnet. 540 flag grabs in
that match, 534 carriers fragged, and the game went to a third overtime.

Half a bot of slack, so a team that is exactly on quota does not flicker.
==================
*/
int BotCTFRoleCrowded(bot_state_t* bs, int role) {
    float want[CTFROLE_COUNT];
    int have[CTFROLE_COUNT];
    int teamsize;

    if (!BotTacticsEnabled() || gametype != GT_CTF) {
        return qfalse;  // no opinion; stock behaviour stands
    }
    if (role < 0 || role >= CTFROLE_COUNT) {
        return qfalse;
    }
    BotCTFRoleCounts(bs, have, &teamsize);
    BotCTFRoleWanted(bs, teamsize, want);

    return (float)have[role] >= want[role] + 0.5f;
}

int BotCTFPickRole(bot_state_t* bs) {
    float want[CTFROLE_COUNT];
    int have[CTFROLE_COUNT];
    int i, teamsize, best;
    float bestdeficit, deficit;

    if (!BotTacticsEnabled() || gametype != GT_CTF) {
        return -1;
    }
    BotCTFRoleCounts(bs, have, &teamsize);
    BotCTFRoleWanted(bs, teamsize, want);

    best = CTFROLE_ROAM;
    bestdeficit = -999;
    for (i = 0; i < CTFROLE_COUNT; i++) {
        // escort is only a job when there is somebody to escort
        if (i == CTFROLE_ESCORT && BotTeamFlagCarrier(bs) < 0) {
            continue;
        }
        deficit = want[i] - (float)have[i];
        if (deficit > bestdeficit) {
            bestdeficit = deficit;
            best = i;
        }
    }
    if (bot_debugTactics.integer) {
        G_Printf("%s: ctf role %i (team %i, want %.1f/%.1f/%.1f/%.1f, have %i/%i/%i/%i)\n",
                 g_entities[bs->entitynum].client->pers.netname, best, teamsize,
                 want[0], want[1], want[2], want[3],
                 have[0], have[1], have[2], have[3]);
    }
    return best;
}
