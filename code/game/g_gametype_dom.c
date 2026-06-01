/*
 * g_gametype_dom.c -- Domination (GT_DOMINATION, 10)
 *
 * Reversed from qagamex86.dll (Quake Live build 1069), Ghidra port 8193.
 *
 * Each "team_dom_point" runs a per-frame framethink that scans for nearby
 * players and drives a capture accumulator in ent->health toward -1000 (red)
 * or +1000 (blue). Reaching an extreme captures the point for that team and
 * fires GTS_DOMINATION_POINT_CAPTURE. A separate think (DOM_Think) ticks every
 * g_domScoreRate seconds and adds one point per held control point to the
 * owning team's score. DOM_CheckPlayers publishes the per-team owned-point
 * counts to configstrings 700/701 each frame.
 *
 * Binary addresses (qagamex86.dll):
 *   DOM_CheckFlagScore    0x1004ac40
 *   DOM_AddTeamScores     0x1004adb0
 *   DOM_FrameThink        0x1004aea0   (ent->framethink, per-frame capture)
 *   DOM_Think             0x1004b790   (ent->think, periodic scoring)
 *   DOM_FlagFirstThink    0x1004b8e0
 *   DOM_CheckPlayers      0x1004bac0
 *   DOM_FragBonuses       0x1004bb40
 *   SP_team_dom_point     0x1004bcd0
 */
#include "g_local.h"

#define MAX_DOM_POINTS      5
#define DOM_CAPTURE_LIMIT   1000    // accumulator extreme: -1000 red, +1000 blue

// team_dom_point registry (binary: DAT_105df534 / DAT_105df548)
static gentity_t *domPoints[MAX_DOM_POINTS];
static int numDomPoints;

// DOM_Think cross-point scoring state (binary: DAT_105df54c / DAT_105df56c..578)
static int domScoreTick;
static int domPendingScores[4];   // indexed by team_t

// Forward declarations
static void DOM_Think(gentity_t *ent);
static void DOM_FrameThink(gentity_t *ent);

#define DOM_AWARD_MASK  (EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | \
                         EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | \
                         EF_AWARD_DEFEND | EF_AWARD_CAP)

// ============================================================================
// DOM_CheckFlagScore (binary: 0x1004ac40)
//
// Award medals when a point is captured. The capturer gets CAPTURE (+25), the
// other players present get ASSIST (+15). ent->domCapturer picks the capturer.
// ============================================================================
static void DOM_CheckFlagScore(gentity_t *ent, gentity_t **players, int count) {
    int i;

    for (i = 0; i < count; i++) {
        gentity_t *player = players[i];
        gclient_t *cl = player->client;

        if (ent->domCapturer != NULL && player == ent->domCapturer) {
            cl->pers.teamState.captures++;
            cl->expandedStats.numCaptures++;
            cl->ps.persistant[PERS_CAPTURES]++;
            cl->ps.eFlags &= ~DOM_AWARD_MASK;
            cl->ps.eFlags |= EF_AWARD_CAP;
            cl->rewardTime = level.time + REWARD_SPRITE_TIME;
            AddScore(player, player->r.currentOrigin, 25);
            STAT_AddPlayerMedalStat(player, "CAPTURE");
        } else {
            cl->pers.teamState.assists++;
            cl->expandedStats.numAssists++;
            cl->ps.persistant[PERS_ASSIST_COUNT]++;
            cl->ps.eFlags &= ~DOM_AWARD_MASK;
            cl->ps.eFlags |= EF_AWARD_ASSIST;
            cl->rewardTime = level.time + REWARD_SPRITE_TIME;
            AddScore(player, player->r.currentOrigin, 15);
            STAT_AddPlayerMedalStat(player, "ASSIST");
        }
    }
}

// ============================================================================
// DOM_AddTeamScores (binary: 0x1004adb0)
//
// Pick the capturer for the point from the players stood on it this frame.
// One team present: keep the existing capturer if still on the point,
// otherwise the first listed player. Both teams present: no capturer.
// ============================================================================
static void DOM_AddTeamScores(int redCount, int blueCount, gentity_t *ent,
                              gentity_t **redPlayers, gentity_t **bluePlayers) {
    gentity_t *prev;
    int j;

    if (redCount == 0) {
        if (blueCount > 0) {
            if (blueCount == 1) {
                ent->domCapturer = bluePlayers[0];
                return;
            }
            prev = ent->domCapturer;
            if (prev != NULL && prev->client != NULL) {
                if (prev->client->sess.sessionTeam == TEAM_RED) {
                    ent->domCapturer = bluePlayers[0];
                    return;
                }
                if (prev->client->sess.sessionTeam == TEAM_BLUE) {
                    for (j = 0; j < blueCount; j++) {
                        if (prev == bluePlayers[j])
                            return;   // still on the point
                    }
                    ent->domCapturer = bluePlayers[0];
                    return;
                }
            }
        }
    } else if (redCount > 0) {
        if (blueCount == 0) {
            if (redCount == 1) {
                ent->domCapturer = redPlayers[0];
                return;
            }
            prev = ent->domCapturer;
            if (prev != NULL && prev->client != NULL) {
                if (prev->client->sess.sessionTeam == TEAM_BLUE) {
                    ent->domCapturer = redPlayers[0];
                    return;
                }
                if (prev->client->sess.sessionTeam == TEAM_RED) {
                    for (j = 0; j < redCount; j++) {
                        if (prev == redPlayers[j])
                            return;
                    }
                    ent->domCapturer = redPlayers[0];
                    return;
                }
            }
        } else if (blueCount > 0) {
            ent->domCapturer = NULL;   // contested
        }
    }
}

// ============================================================================
// DOM_FrameThink (binary: 0x1004aea0)  ent->framethink
//
// Runs every frame. Scans players within ent->count units, advances the
// capture accumulator (ent->health) and fires the capture event when it hits
// an extreme. Red drives the accumulator to -1000, blue to +1000.
// ============================================================================
static void DOM_FrameThink(gentity_t *ent) {
    int i;
    int redCount = 0, blueCount = 0;
    gentity_t *redPlayers[MAX_CLIENTS];
    gentity_t *bluePlayers[MAX_CLIENTS];
    float radiusSq;
    int deltaBase;
    int capDelta;
    int oldAccum, accum, progress;
    float scale;
    qboolean neutralized = qfalse;

    ent->s.weapon = 0;

    // find players stood on the point
    // Binary scans a fixed MAX_CLIENTS (64) slots (compiler unrolled the loop
    // 4x over 16 iterations), not level.maxclients; non-client slots are
    // filtered by the client!=NULL check below.
    radiusSq = (float)(ent->count * ent->count);
    for (i = 0; i < MAX_CLIENTS; i++) {
        gentity_t *player = &g_entities[i];
        gclient_t *cl = player->client;

        if (!player->inuse || !cl)
            continue;
        if (cl->pers.connected != CON_CONNECTED)
            continue;
        if (cl->sess.sessionTeam == TEAM_SPECTATOR)
            continue;
        if (player->health <= 0)
            continue;
        if (DistanceSquared(ent->r.currentOrigin, player->r.currentOrigin) > radiusSq)
            continue;

        if (cl->sess.sessionTeam == TEAM_RED)
            redPlayers[redCount++] = player;
        else
            bluePlayers[blueCount++] = player;
    }

    DOM_AddTeamScores(redCount, blueCount, ent, redPlayers, bluePlayers);

    // player count shown to clients while only one team is present
    ent->s.otherEntityNum2 = 0;
    if (redCount == 0 || blueCount == 0)
        ent->s.otherEntityNum2 = redCount + blueCount;

    // per-frame accumulator step: reaches 1000 over g_domCapTime seconds
    deltaBase = 0;
    if (g_domCapTime.integer > 0)
        deltaBase = level.frametime / g_domCapTime.integer;

    scale = g_domTeammateCapScale.value;
    capDelta = 0;

    if (g_domEnableContention.integer != 0 && g_domNeutralFlag.integer != 0 &&
        redCount != 0 && blueCount != 0) {
        // contested with neutral flag: bleed the accumulator back toward zero
        oldAccum = ent->health;
        if (oldAccum != 0) {
            float step = (float)deltaBase * (1.0f + scale);
            if (oldAccum > 0)
                capDelta = -(int)step;
            else
                capDelta = (int)step;
            if (capDelta != 0 && ent->health + capDelta == 0)
                neutralized = qtrue;
        }
    } else {
        // more players on a point capture it faster
        if (redCount > 0)
            capDelta = -(int)((float)deltaBase * (1.0f + scale * (redCount - 1)));
        if (blueCount > 0)
            capDelta = (int)((float)deltaBase * (1.0f + scale * (blueCount - 1)) + capDelta);
    }

    oldAccum = ent->health;
    accum = oldAccum + capDelta;
    if (accum > DOM_CAPTURE_LIMIT)
        accum = DOM_CAPTURE_LIMIT;
    else if (accum < -DOM_CAPTURE_LIMIT)
        accum = -DOM_CAPTURE_LIMIT;
    ent->health = accum;

    progress = (accum < 0 ? -accum : accum) / 10;
    ent->s.generic1 = progress;                     // 0..100 capture bar
    ent->s.weapon = 0;
    ent->s.eventParm = (accum > 0) + 1;             // 1 = red side, 2 = blue side

    // visual state: 2 = evenly contested, 1 = under attack / partway, 0 = idle
    if (redCount != 0 && blueCount != 0 && redCount == blueCount) {
        ent->s.weapon = 2;
    } else if (redCount != 0 && blueCount != 0) {
        ent->s.weapon = 1;
    } else if ((ent->s.modelindex2 == 0 && progress < 100 && progress != 0) ||
               ((float)progress < g_domDistressThreshold.value && progress != 0)) {
        ent->s.weapon = 1;
    } else {
        ent->s.weapon = 0;
    }

    if (capDelta == 0)
        return;

    // owner is losing the point (accumulator returning through zero) or the
    // contested bleed reached neutral
    {
        int absOld = oldAccum < 0 ? -oldAccum : oldAccum;
        int absDelta = deltaBase < 0 ? -deltaBase : deltaBase;
        if (neutralized || (absOld < absDelta && ent->s.modelindex2 != 0)) {
            if (g_domEnableContention.integer == 0) {
                // no contention: flip straight to the other team's extreme
                ent->health = (ent->s.modelindex2 == TEAM_RED) ? DOM_CAPTURE_LIMIT
                                                               : -DOM_CAPTURE_LIMIT;
                ent->s.modelindex2 = 0;
            } else {
                gentity_t *te;
                ent->s.modelindex2 = 0;
                te = G_TempEntity(ent->s.pos.trBase, EV_GLOBAL_TEAM_SOUND);
                te->r.svFlags |= SVF_BROADCAST;
                te->s.eventParm = GTS_DOMINATION_POINT_CAPTURE;
                te->s.modelindex2 = ent->s.modelindex2;   // 0 = neutralised
            }
        }
    }

    // reached an extreme while neutral: capture for that team
    accum = ent->health;
    if ((accum == DOM_CAPTURE_LIMIT || accum == -DOM_CAPTURE_LIMIT) &&
        ent->s.modelindex2 == 0) {
        int team = (accum == -DOM_CAPTURE_LIMIT) ? TEAM_RED : TEAM_BLUE;
        gentity_t *te;

        ent->s.modelindex2 = team;

        // Binary passes vec3_origin (DAT_104b1438) for the capture broadcast,
        // not ent->s.pos.trBase (which the neutralise event above does use).
        te = G_TempEntity(vec3_origin, EV_GLOBAL_TEAM_SOUND);
        te->r.svFlags |= SVF_BROADCAST;
        te->s.eventParm = GTS_DOMINATION_POINT_CAPTURE;
        te->s.modelindex2 = team;
        te->s.powerups = level.warmupTime ? 0 : ent->s.powerups;
        if (level.warmupTime && ent->domCapturer && ent->domCapturer->client) {
            // the capturer hears their own local sound, so exclude them here
            te->r.svFlags |= SVF_NOTSINGLECLIENT;
            te->r.singleClient = ent->domCapturer->client->ps.clientNum;
        }

        if (team == TEAM_RED)
            DOM_CheckFlagScore(ent, redPlayers, redCount);
        else
            DOM_CheckFlagScore(ent, bluePlayers, blueCount);
    }
}

// ============================================================================
// DOM_Think (binary: 0x1004b790)  ent->think
//
// Fires every g_domScoreRate seconds. Each point adds one to its owner's
// pending score; once every point has ticked, the pending totals are applied
// to the team scores and a lead-change announcement is sent.
// ============================================================================
static void DOM_Think(gentity_t *ent) {
    int red, blue, newRed, newBlue;
    int announce;

    ent->nextthink = g_domScoreRate.integer * 1000 + level.time;

    if (level.warmupTime || level.intermissionQueued || level.intermissionTime)
        return;

    if (ent->s.modelindex2 != 0)
        domPendingScores[ent->s.modelindex2]++;

    domScoreTick++;
    if (domScoreTick < numDomPoints)
        return;

    // NOTE: after the tick, QL runs a "your team is being dominated" voice cue
    // (binary 0x1004b804-0x1004b8cc). It counts control points that are either
    // unowned or owned by domPoints[0]'s team; if that count >= 3 it walks every
    // client on that team and, via game syscalls syscall_ptr+0x330 / +0x32c
    // (unmapped traps ~#203/#204, arg 41) gated on a per-client cooldown
    // accumulator at gclient offset 0x88c (incremented by g_domScoreRate each
    // tick), fires a directed voice line. Neither the traps, the constant 41,
    // nor the 0x88c field are present in the reimpl headers, and the cue has no
    // effect on scores/ownership, so it is not reproduced here.

    red = level.teamScores[TEAM_RED];
    blue = level.teamScores[TEAM_BLUE];
    newRed = red + domPendingScores[TEAM_RED];
    newBlue = blue + domPendingScores[TEAM_BLUE];

    domScoreTick = 0;

    if (level.intermissionQueued || level.intermissionTime)
        return;

    if (!level.warmupTime) {
        announce = 0;
        if (blue < red) {
            if (newBlue > newRed)
                announce = GTS_BLUETEAM_TOOK_LEAD;
        } else if (red < blue) {
            if (newRed > newBlue)
                announce = GTS_REDTEAM_TOOK_LEAD;
        } else {   // tied
            if (newRed > newBlue)
                announce = GTS_REDTEAM_TOOK_LEAD;
            else if (newBlue > newRed)
                announce = GTS_BLUETEAM_TOOK_LEAD;
        }
        if (announce) {
            gentity_t *te = G_TempEntity(vec3_origin, EV_GLOBAL_TEAM_SOUND);
            te->r.svFlags |= SVF_BROADCAST;
            te->s.eventParm = announce;
        }
    }

    level.teamScores[TEAM_RED] += domPendingScores[TEAM_RED];
    level.teamScores[TEAM_BLUE] += domPendingScores[TEAM_BLUE];
    domPendingScores[TEAM_FREE] = 0;
    domPendingScores[TEAM_RED] = 0;
    domPendingScores[TEAM_BLUE] = 0;
    domPendingScores[TEAM_SPECTATOR] = 0;
    CalculateRanks();
}

// ============================================================================
// DOM_FlagFirstThink (binary: 0x1004b8e0)
//
// Drop the point to the ground, then switch over to the real think/framethink.
// ============================================================================
static void DOM_FlagFirstThink(gentity_t *ent) {
    trace_t tr;
    vec3_t start, end;
    vec3_t mins = { -15, -15, -15 };
    vec3_t maxs = { 15, 15, 35 };

    VectorCopy(ent->r.currentOrigin, start);
    VectorCopy(ent->r.currentOrigin, end);
    end[2] -= 256.0f;

    trap_Trace(&tr, start, mins, maxs, end, ENTITYNUM_NONE, CONTENTS_SOLID);
    if (tr.startsolid) {
        // buried: retry from a bit higher up
        start[2] += 128.0f;
        trap_Trace(&tr, start, mins, maxs, end, ENTITYNUM_NONE, CONTENTS_SOLID);
        if (tr.startsolid)
            goto done;
    }

    if (tr.fraction < 1.0f) {
        VectorCopy(tr.endpos, ent->s.pos.trBase);
        ent->s.pos.trType = TR_STATIONARY;
        ent->s.pos.trTime = 0;
        ent->s.pos.trDuration = 0;
        VectorClear(ent->s.pos.trDelta);
        VectorCopy(tr.endpos, ent->r.currentOrigin);
    }

done:
    ent->nextthink = g_domScoreRate.integer * 1000 + level.time;
    ent->think = DOM_Think;
    ent->framethink = DOM_FrameThink;
}

// ============================================================================
// DOM_CheckPlayers (binary: 0x1004bac0)
//
// Publish the number of control points each team holds. Called every frame
// from G_RunFrame. CS 700 = red owned, CS 701 = blue owned.
// ============================================================================
void DOM_CheckPlayers(void) {
    int i;
    int red = 0, blue = 0;

    for (i = 0; i < numDomPoints; i++) {
        if (domPoints[i]->s.modelindex2 == TEAM_RED)
            red++;
        else if (domPoints[i]->s.modelindex2 == TEAM_BLUE)
            blue++;
    }

    trap_SetConfigstring(700, va("%d", red));
    trap_SetConfigstring(701, va("%d", blue));
}

// ============================================================================
// DOM_FragBonuses (binary: 0x1004bb40)
//
// DEFEND (+10) for killing an enemy near a control point your team holds.
// Needs distance < 500 and a PVS line of sight to the point.
// ============================================================================
void DOM_FragBonuses(gentity_t *attacker, gentity_t *victim) {
    int i;

    // Binary has no null-guard here; the caller (player_die) only invokes this
    // when g_gametype==GT_DOMINATION with a valid attacker->client, and a dying
    // entity always has victim->client.
    for (i = 0; i < numDomPoints; i++) {
        gentity_t *point = domPoints[i];

        if (point->s.modelindex2 != attacker->client->sess.sessionTeam)
            continue;
        if (Distance(point->r.currentOrigin, victim->r.currentOrigin) >= 500.0f)
            continue;
        if (!trap_InPVS(point->r.currentOrigin, victim->r.currentOrigin))
            continue;
        if (attacker->client->sess.sessionTeam == victim->client->sess.sessionTeam)
            continue;

        attacker->client->pers.teamState.basedefense++;
        attacker->client->expandedStats.numDefends++;
        AddScore(attacker, attacker->r.currentOrigin, 10);
        attacker->client->ps.persistant[PERS_DEFEND_COUNT]++;
        attacker->client->ps.eFlags &= ~DOM_AWARD_MASK;
        attacker->client->ps.eFlags |= EF_AWARD_DEFEND;
        attacker->client->rewardTime = level.time + REWARD_SPRITE_TIME;
        STAT_AddPlayerMedalStat(attacker, "DEFEND");
        return;   // one bonus per kill
    }
}

// ============================================================================
// SP_team_dom_point (binary: 0x1004bcd0)
//
// Spawn a control point. Only active in GT_DOMINATION.
// ============================================================================
void SP_team_dom_point(gentity_t *ent) {
    char *s;

    if (g_gametype.integer != GT_DOMINATION) {
        G_FreeEntity(ent);
        return;
    }

    ent->r.svFlags |= SVF_BROADCAST;
    ent->health = 0;                              // capture accumulator
    ent->s.groundEntityNum = ent->count;          // capture radius, for cgame
    ent->s.loopSound = G_SoundIndex("sound/ambient/loop_elec_hum_01_short.wav");
    ent->s.modelindex = G_ModelIndex("models/flag3/d_flag3.md3");
    G_SpawnString("identifier", "1", &s);
    ent->s.powerups = atoi(s);                    // control point id
    ent->s.modelindex2 = 0;                       // neutral

    ent->think = DOM_FlagFirstThink;
    ent->nextthink = level.time + 1;

    if (numDomPoints < MAX_DOM_POINTS)
        domPoints[numDomPoints++] = ent;

    trap_LinkEntity(ent);
}
