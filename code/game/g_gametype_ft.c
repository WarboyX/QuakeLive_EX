/*
 * g_gametype_ft.c -- Freeze Tag (GT_FREEZETAG) round state machine
 *
 * Ported from ql-decompiled/game/g_freeze.c (Quake Live build 1069)
 *
 * Players are frozen (PM_FREEZE) instead of killed. Teammates can thaw
 * frozen allies by standing near them. Round ends when all members of
 * one team are frozen.
 */
#include "g_local.h"

// ============================================================================
// Helper stubs (same as CA)
// ============================================================================
// [QL] byte-faithful to the shared binary TeamCount_Health (0x1006b100): for
// every connected player whose ps.pm_type == PM_NORMAL, add the *gentity*
// ->health (ent+0x320) to healthTotals[sessionTeam]. The binary sums ent->health
// only, NOT ps.stats[STAT_HEALTH] + ps.stats[STAT_ARMOR]. aliveCounts is unused
// here (the caller fills it separately via Team_LivingTeamCounts/UpdateTeamAliveCount).
static void TeamCount_Health_FT(int *aliveCounts, int *healthTotals) {
    int i;
    (void)aliveCounts;
    healthTotals[TEAM_FREE] = 0;
    healthTotals[TEAM_RED] = 0;
    healthTotals[TEAM_BLUE] = 0;
    healthTotals[TEAM_SPECTATOR] = 0;

    for (i = 0; i < level.maxclients; i++) {
        gclient_t *cl = &level.clients[i];
        if (cl->pers.connected != CON_CONNECTED) continue;
        if (cl->ps.pm_type != PM_NORMAL) continue;
        healthTotals[cl->sess.sessionTeam] += g_entities[i].health;
    }
}

static const char *FT_TeamColor(int team) {
    if (team == TEAM_RED) return "^1";
    if (team == TEAM_BLUE) return "^4";
    return "^7";
}

void Freeze_RoundStateTransition(void);

// ============================================================================
// Freeze_GetRoundState (binary: 0x1004bdf0)
//
// Advance the round state if the pending timer has elapsed, then return the
// current round state. Returns -1 while the timer is still counting down.
// Same shape as CA_GetRoundState.
// ============================================================================
int Freeze_GetRoundState(void) {
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return -1;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        Freeze_RoundStateTransition();
    }
    return level.roundState.eCurrent;
}

// ============================================================================
// Freeze_TeamFrozen (binary: 0x1004c0d0, .so Freeze_TeamFrozen)
//
// Returns qtrue if all players on the given team are frozen.
// [QL] byte-faithful: the binary's "not frozen" test is ps.powerups[PW_FREEZE]
// (ps+0x17c) == 0, NOT stats[STAT_HEALTH] > 0. A frozen player carries
// powerups[PW_FREEZE] == 0x7fffffff; only that field distinguishes frozen.
// ============================================================================
qboolean Freeze_TeamFrozen(int team) {
    int i;
    for (i = 0; i < level.maxclients; i++) {
        gclient_t *cl = &level.clients[i];
        if (cl->pers.connected == CON_CONNECTED &&
            cl->sess.sessionTeam == team &&
            cl->ps.powerups[PW_FREEZE] == 0) {
            return qfalse;
        }
    }
    return qtrue;
}

// ============================================================================
// Freeze_GameIsOver
//
// Check if the freeze round is over. Uses alive counts, frozen status,
// and optional round time limit. Sets prevRoundWinningTeam.
// ============================================================================
qboolean Freeze_GameIsOver(int *aliveCounts, int *healthTotals) {
    // Round time limit check
    if (roundtimelimit.integer != 0 &&
        roundtimelimit.integer * 1000 <= level.time - level.roundState.startTime) {
        if (g_roundDrawLivingCount.integer != 0) {
            if (aliveCounts[TEAM_BLUE] < aliveCounts[TEAM_RED]) {
                level.roundState.prevRoundWinningTeam = TEAM_RED;
                return qtrue;
            }
            if (aliveCounts[TEAM_RED] < aliveCounts[TEAM_BLUE]) {
                level.roundState.prevRoundWinningTeam = TEAM_BLUE;
                return qtrue;
            }
        }
        if (g_roundDrawHealthCount.integer != 0) {
            if (healthTotals[TEAM_BLUE] < healthTotals[TEAM_RED]) {
                level.roundState.prevRoundWinningTeam = TEAM_RED;
                return qtrue;
            }
            if (healthTotals[TEAM_RED] < healthTotals[TEAM_BLUE]) {
                level.roundState.prevRoundWinningTeam = TEAM_BLUE;
                return qtrue;
            }
        }
        level.roundState.prevRoundWinningTeam = TEAM_FREE;
        return qtrue;
    }

    // Check alive counts
    if (aliveCounts[TEAM_RED] == 0 && aliveCounts[TEAM_BLUE] == 0) {
        level.roundState.prevRoundWinningTeam = TEAM_FREE;
        return qtrue;
    }
    if (aliveCounts[TEAM_RED] == 0) {
        level.roundState.prevRoundWinningTeam = TEAM_BLUE;
        return qtrue;
    }
    if (aliveCounts[TEAM_BLUE] == 0) {
        level.roundState.prevRoundWinningTeam = TEAM_RED;
        return qtrue;
    }

    // Check frozen status (team frozen = all health <= 0)
    {
        qboolean redFrozen = Freeze_TeamFrozen(TEAM_RED);
        qboolean blueFrozen = Freeze_TeamFrozen(TEAM_BLUE);

        if (redFrozen && blueFrozen) {
            level.roundState.prevRoundWinningTeam = TEAM_FREE;
            return qtrue;
        }
        if (blueFrozen) {
            level.roundState.prevRoundWinningTeam = TEAM_RED;
            return qtrue;
        }
        if (redFrozen) {
            level.roundState.prevRoundWinningTeam = TEAM_BLUE;
            return qtrue;
        }
    }

    return qfalse;
}

// ============================================================================
// Freeze_RoundStateTransition
//
// Handle state transitions for Freeze Tag rounds.
/*
============================================================================
Freeze_InitRoundState

[QL] Start the Freeze Tag round machine.

Nothing did. GT_RR gets RR_InitRoundState() from G_InitGame's round-based
switch and GT_FREEZE had no case at all, so level.roundState stayed at its
zero-initialised state: eCurrent RS_WARMUP, tNext 0. Freeze_GetRoundState only
promotes eNext into eCurrent when tNext is non-zero, and every one of the five
Freeze_RoundStateTransition call sites is reachable only once the machine is
already turning. So it never turned.

The visible result was that nobody froze. Freeze_PlayerFrozen refuses unless
roundState.eCurrent is RS_PLAYING, and the state sat at RS_WARMUP for the whole
match - through warmup, through "MATCH IN PROGRESS", through every kill. A
declined freeze is an ordinary death, so it looked like the freeze code was
wrong rather than never reached.

Same shape as RR's: park in RS_WARMUP if warmup is running, otherwise drop
straight into the countdown, and let the transition do the rest.
============================================================================
*/
void Freeze_InitRoundState(void) {
    level.roundState.tNext = 0;
    level.roundState.eCurrent = (level.warmupTime == 0) ? RS_COUNTDOWN : RS_WARMUP;
    Freeze_RoundStateTransition();
    level.roundState.round = 0;
}

// ============================================================================
void Freeze_RoundStateTransition(void) {
    if (g_debugFreeze.integer) {
        G_Printf("freeze round state: current %i, next %i, tNext %i, level.time %i\n",
                 level.roundState.eCurrent, level.roundState.eNext,
                 level.roundState.tNext, level.time);
    }

    int i;

    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        Freeze_RoundStateTransition();
    }

    switch (level.roundState.eCurrent) {
    case RS_WARMUP:
        trap_SetConfigstring(CS_ROUND_STATUS, "\\time\\-1\\round\\0");
        return;

    case RS_COUNTDOWN:
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            gentity_t *ent = &g_entities[i];

            if (cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR)
                continue;

            // Handle frozen/dead players from previous round
            if (cl->ps.pm_type == PM_FREEZE) {
                if (g_freezeThawWinningTeam.integer != 0 ||
                    cl->sess.sessionTeam != level.roundState.prevRoundWinningTeam) {
                    // Thaw event
                    G_TempEntity(ent->r.currentOrigin, EV_THAW_PLAYER);
                    cl->ps.pm_type = PM_NORMAL;
                    ClientSpawn(ent);
                }
            } else if (cl->ps.pm_type == PM_SPECTATOR || cl->ps.pm_type == PM_DEAD) {
                cl->ps.pm_type = PM_NORMAL;
                ClientSpawn(ent);
            }

            // Clamp powerup timers when removing powerups on round start.
            // Binary clamps powerups[4]..powerups[8] (QUAD..INVIS), not FLIGHT.
            if (g_freezeRemovePowerupsOnRound.integer != 0) {
                int j;
                for (j = PW_QUAD; j <= PW_INVIS; j++) {
                    if (cl->ps.powerups[j] > level.time + 3000) {
                        cl->ps.powerups[j] = level.time + 3000;
                    }
                }
            }

            // Reset weapons on round start
            if (g_freezeResetWeaponsOnRound.integer != 0 && cl->ps.pm_type != PM_FREEZE) {
                SelectSpawnWeapon(ent);
            }

            // Reset health/armor on round start
            if (g_freezeResetHealthOnRound.integer != 0 && cl->ps.pm_type != PM_FREEZE) {
                ent->health = cl->pers.maxHealth;
                cl->ps.stats[STAT_HEALTH] = ent->health;
                if (g_freezeResetArmorOnRound.integer != 0) {
                    cl->ps.stats[STAT_ARMOR] = g_startingArmor.integer;
                }
            }

            // Freeze all players (if not first round)
            if (level.roundState.eCurrent != RS_WARMUP) {
                cl->ps.pm_flags |= PMF_FROZEN;
            }
        }

        // Remove powerup items from map
        if (g_freezeRemovePowerupsOnRound.integer != 0) {
            gentity_t *mapEnt;
            for (mapEnt = &g_entities[0]; mapEnt < &g_entities[level.num_entities]; mapEnt++) {
                if (mapEnt->inuse && mapEnt->item &&
                    mapEnt->item->giType == IT_POWERUP &&
                    (mapEnt->flags & FL_DROPPED_ITEM)) {
                    G_FreeEntity(mapEnt);
                }
            }
        }

        Team_LivingTeamCounts(NULL, NULL);
        level.roundState.round = level.teamScores[TEAM_BLUE] + level.teamScores[TEAM_RED] + 1;

        {
            int countdown = g_freezeRoundDelay.integer;
            if (level.roundState.round == 1) {
                countdown = g_roundWarmupDelay.integer;
            }
            if (countdown == 0) {
                level.roundState.tNext = 0;
                level.roundState.eCurrent = RS_PLAYING;
                Freeze_RoundStateTransition();
            } else {
                level.roundState.tNext = level.time + countdown;
                level.roundState.eNext = RS_PLAYING;
            }
        }

        if (level.roundState.eCurrent != RS_WARMUP) {
            trap_SetConfigstring(CS_ROUND_STATUS,
                va("\\time\\%d\\round\\%d",
                    level.roundState.tNext, level.roundState.round));
        }
        return;

    case RS_PLAYING:
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            if (cl->pers.connected == CON_CONNECTED) {
                cl->ps.pm_flags &= ~PMF_FROZEN;
                cl->round_shots = 0;
                cl->round_hits = 0;
                cl->round_damage = 0;
                cl->expandedStats.killStreak = 0;
                if (g_spawnArmor.integer != 0) {
                    // [QL] binary writes powerups[PW_NONE] (client+0x140), not PW_QUAD (+0x150)
                    cl->ps.powerups[PW_NONE] =
                        (level.time / 1000) * 1000 + g_spawnArmor.integer;
                }
            }
        }
        trap_SetConfigstring(CS_ROUND_TIME, va("%d", level.time));
        trap_SetConfigstring(CS_ROUND_STATUS,
            va("\\round\\%d", level.roundState.round));
        return;

    case RS_ROUND_OVER:
    {
        int winTeam = level.roundState.prevRoundWinningTeam;
        int aliveCounts[4] = {0}, healthTotals[4] = {0};

        // Freeze all non-spectators
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            if (cl->pers.connected == CON_CONNECTED &&
                cl->sess.sessionTeam != TEAM_SPECTATOR) {
                cl->ps.pm_flags |= PMF_FROZEN;
            }
        }

        // Award team score
        if (winTeam == TEAM_RED) {
            level.teamScores[TEAM_RED]++;
        } else if (winTeam == TEAM_BLUE) {
            level.teamScores[TEAM_BLUE]++;
        }

        Team_LivingTeamCounts(&aliveCounts[TEAM_RED], &aliveCounts[TEAM_BLUE]);
        TeamCount_Health_FT(aliveCounts, healthTotals);

        // Announce winner
        if (winTeam == TEAM_RED || winTeam == TEAM_BLUE) {
            const char *teamName = TeamName(winTeam);
            const char *teamColor = FT_TeamColor(winTeam);
            int count = aliveCounts[winTeam];

            if (count < 2) {
                trap_SendServerCommand(-1,
                    va("print \"%s%s TEAM^3 WINS the round!^7 (%i hp remaining)\n\"",
                        teamColor, teamName, healthTotals[winTeam]));
            } else {
                trap_SendServerCommand(-1,
                    va("print \"%s%s TEAM^3 WINS the round!^7 (%i players remaining)\n\"",
                        teamColor, teamName, count));
            }
        }

        // Award medals
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            gentity_t *ent2 = &g_entities[i];
            if (cl->pers.connected != CON_CONNECTED) continue;

            // Accuracy award: >50% hit rate this round
            if (cl->round_shots != 0) {
                int acc = (cl->round_hits * 100) / cl->round_shots;
                if ((double)acc > 50.0) {
                    gentity_t *te = G_TempEntity(ent2->r.currentOrigin, EV_AWARD);
                    te->r.svFlags |= SVF_BROADCAST;
                    te->s.otherEntityNum = ent2->s.number;
                    te->s.eventParm = AWARD_ACCURACY;
                    te->s.otherEntityNum2 = 1;
                    cl->rewardTime = level.time + REWARD_SPRITE_TIME;
                }
            }

            // Perfect award: on winning team with 0 damage taken
            if (cl->sess.sessionTeam == winTeam && cl->round_damage == 0) {
                gentity_t *te = G_TempEntity(ent2->r.currentOrigin, EV_AWARD);
                te->r.svFlags |= SVF_BROADCAST;
                te->s.otherEntityNum = ent2->s.number;
                te->s.eventParm = AWARD_PERFECT;
                te->s.otherEntityNum2 = 1;
                cl->rewardTime = level.time + REWARD_SPRITE_TIME;
            }
        }

        level.roundState.round++;
        CalculateRanks();

        // Check for game end
        if (level.teamScores[TEAM_RED] != level.teamScores[TEAM_BLUE]) {
            if ((g_timelimit.integer != 0 &&
                 g_timelimit.integer * 60000 <= level.time - level.startTime) ||
                (roundlimit.integer != 0 &&
                 (roundlimit.integer <= level.teamScores[TEAM_RED] ||
                  roundlimit.integer <= level.teamScores[TEAM_BLUE]))) {
                level.roundState.tNext = level.time + 1500;
                level.roundState.eNext = RS_EXIT;
                return;
            }
            if (g_mercylimit.integer != 0) {
                int mercyTime = g_mercytime.integer * 60000 + level.timeOvertime;
                if (mercyTime <= level.time - level.startTime) {
                    if (g_mercylimit.integer <= level.teamScores[TEAM_RED] - level.teamScores[TEAM_BLUE] ||
                        g_mercylimit.integer <= level.teamScores[TEAM_BLUE] - level.teamScores[TEAM_RED]) {
                        level.roundState.tNext = level.time + 1500;
                        level.roundState.eNext = RS_EXIT;
                        return;
                    }
                }
            }
        }

        // Round-end sound
        {
            int soundType;
            if (winTeam == TEAM_RED) soundType = GTS_REDTEAM_SCORED;     // 0x08
            else if (winTeam == TEAM_BLUE) soundType = GTS_BLUETEAM_SCORED;  // 0x09
            else soundType = GTS_DRAW_ROUND;  // 0x12
            // [QL] binary carries the GTS index in eventParm (0xc0), not otherEntityNum2
            gentity_t *te = G_TempEntity(vec3_origin, EV_GLOBAL_TEAM_SOUND);
            te->r.svFlags |= SVF_BROADCAST;   // binary writes r.svFlags|=0x20 (gentity+0x1e0), not s.eFlags
            te->s.eventParm = soundType;
        }

        level.roundState.tNext = level.time + 3500;
        level.roundState.eNext = RS_COUNTDOWN;
        trap_SetConfigstring(CS_ROUND_TIME, va("%d", -1));
        return;
    }

    case RS_EXIT:
        CA_CheckExitRules(1);
        return;

    default:
        G_Error("Freeze_RoundStateTransition: invalid state");
        return;
    }
}

// ============================================================================
// Freeze_Think
//
// Per-frame logic for Freeze Tag. Checks if round is over.
// Called from G_RunFrame.
// ============================================================================
void Freeze_Think(void) {
    int state;
    int aliveCounts[4] = {0}, healthTotals[4] = {0};

    if (level.intermissionQueued || level.intermissionTime || level.warmupTime)
        return;

    /*
    [QL] Warmup has ended - this function returns above while it is running - but
    the round machine is still parked in RS_WARMUP with no pending transition,
    so nothing would ever move it. Kick it into the countdown, which is what
    schedules RS_PLAYING.
    */
    if (level.roundState.eCurrent == RS_WARMUP && level.roundState.tNext == 0) {
        level.roundState.eCurrent = RS_COUNTDOWN;
        Freeze_RoundStateTransition();
    }

    /*
    [QL] Say so if the round machine is stuck, without needing g_debugFreeze.

    Freeze_PlayerFrozen only freezes at RS_PLAYING, and a declined freeze is
    indistinguishable from an ordinary death - the player dies and respawns, and
    nothing anywhere says why. That has now cost two rounds of "freeze tag is
    still not working" with no evidence in the log to work from.

    This fires once per level, only when the mode is actually broken: the match
    is live, the longest configured countdown has had time to elapse twice over,
    and the state still is not RS_PLAYING. A working server never prints it.
    */
    {
        static int reportedAtLevelTime;
        int settleTime = 2 * (g_roundWarmupDelay.integer + g_freezeRoundDelay.integer) + 5000;

        if (level.roundState.eCurrent != RS_PLAYING &&
            level.roundState.eCurrent != RS_ROUND_OVER &&
            reportedAtLevelTime != level.startTime &&
            level.time - level.startTime > settleTime) {
            reportedAtLevelTime = level.startTime;
            G_Printf(S_COLOR_YELLOW "WARNING: Freeze Tag round state is stuck at %i after %i ms "
                     "live (needs RS_PLAYING %i, next %i, tNext %i). Players will die instead of "
                     "freezing - set g_debugFreeze 1 for the transition trace.\n",
                     level.roundState.eCurrent, level.time - level.startTime, RS_PLAYING,
                     level.roundState.eNext, level.roundState.tNext);
        }
    }

    // [QL] keep the round clock frozen during a pause/timeout: the binary bumps
    // roundState.startTime by this frame's msec (level.frametime) while paused so
    // elapsed time (level.time - startTime) does not advance.
    if (level.timePauseBegin != 0) {
        level.roundState.startTime += level.frametime;
    }

    // Advance the pending timer and grab the current state
    state = Freeze_GetRoundState();
    if (state == RS_PLAYING) {
        Team_LivingTeamCounts(&aliveCounts[TEAM_RED], &aliveCounts[TEAM_BLUE]);
        TeamCount_Health_FT(aliveCounts, healthTotals);
        if (Freeze_GameIsOver(aliveCounts, healthTotals)) {
            level.roundState.tNext = 0;
            level.roundState.eCurrent = RS_ROUND_OVER;
            Freeze_RoundStateTransition();
        }
    }
}

// ============================================================================
// Freeze_InstaKill (binary: 0x1004be40, .so Freeze_InstaKill 0x76fa0)
//
// Destroy (mode 1) or auto-thaw-respawn (mode 0) a frozen body.
//   mode 1 = real destruction of a frozen body (called from player_die and
//            G_Damage): clears PW_FREEZE, sets GIB health, arms respawnTime.
//            With PW_FREEZE cleared, the tail Kamikaze_DeathActivate skips the
//            thaw-respawn and plays the gib/kamikaze effect.
//   mode 0 = auto-thaw timeout respawn (called from Freeze_ClientThawCheck):
//            leaves PW_FREEZE set so the tail Kamikaze_DeathActivate fires
//            EV_THAW_PLAYER + ClientSpawn (respawn).
// [QL] byte-faithful to the disassembly. Field offsets mapped to named reimpl
// fields; DAT_105aac6c (InstaKill respawn delay) resolves to g_respawn_delay_min
// (decl-phase cvar-table mapping; default "2100").
/*
============================================================================
Freeze_DeathFinalize

[QL] Thaw the statue and put the player back in the game.

This is what the binary's death finalizer at 0x10046f60 does, and it is NOT
Quake 3's Kamikaze_DeathActivate. Both call sites below used to call that one,
because the port matched the two by name:

    void Kamikaze_DeathActivate(gentity_t *ent) {
        G_StartKamikaze(ent);
        G_FreeEntity(ent);
    }

Quake 3's version is the think function of a temporary "kamikaze timer" entity -
it detonates and then frees *that* entity. Handed a player instead, G_FreeEntity
memsets the client's gentity and clears inuse while svs.clients[n].gentity still
points at it and ent->client is left NULL. neverFree is only set on the body
queue, so nothing stops it. The next thing to touch that client dereferences
freed memory.

In instagib Freeze Tag that is the first kill of the match:

    Kill: 3 0 10: Keel killed Daemia by MOD_RAILGUN
    ----- Server Shutdown (Received signal 11) -----

A frozen player is not a corpse to dispose of - they are a live client whose
statue is being broken. Clear the freeze state, announce the thaw, and respawn.
============================================================================
*/
static void Freeze_DeathFinalize(gentity_t *ent) {
    if (!ent || !ent->client) {
        return;
    }

    // Only a frozen player is finalized here; the binary gates on PW_FREEZE.
    if (ent->client->ps.powerups[PW_FREEZE] == 0) {
        return;
    }

    ent->client->ps.powerups[PW_FREEZE] = 0;
    ent->s.powerups &= ~(1 << PW_FREEZE);
    ent->client->ps.freezetime = 0;
    ent->client->ps.thawtime = 0;
    ent->client->ps.thawClientNum_valid = 0;
    ent->takedamage = qtrue;

    G_TempEntity(ent->r.currentOrigin, EV_THAW_PLAYER);

    ent->client->ps.pm_type = PM_NORMAL;
    ClientSpawn(ent);
}

// ============================================================================
void Freeze_InstaKill(gentity_t *self, int mode) {
    gclient_t *client = self->client;
    gentity_t *tent;
    int team;

    if (mode == 1) {
        int redCount = 0, blueCount = 0;

        client->ps.powerups[PW_FREEZE] = 0;                 // client+0x17c
        // self+0xc4: clear bit15 (PW_FREEZE) in the networked s.powerups bitmask
        self->s.powerups &= 0xffff7fff;
        client->ps.pm_type = PM_DEAD;                       // client+0x4
        client->respawnTime = level.time + g_respawn_delay_min.integer; // client+0x4e4
        self->health = -40;                                 // self+0x320 (GIB_HEALTH)

        Team_LivingTeamCounts(&redCount, &blueCount);
        // Last one on the team -> skip the destruction effects (goto 0x1004bf81)
        if ((redCount == 0 && client->sess.sessionTeam == TEAM_RED) ||
            (blueCount == 0 && client->sess.sessionTeam == TEAM_BLUE))
            return;
    }

    client->ps.thawClientNum_valid = 0;                     // client+0x1f8

    // EV_OBITUARY: "world thawed/killed you" broadcast
    tent = G_TempEntity(self->r.currentOrigin, EV_OBITUARY);
    tent->s.eventParm = MOD_THAW;                           // 0x1e
    tent->s.otherEntityNum = client->ps.clientNum;          // victim
    tent->s.otherEntityNum2 = ENTITYNUM_WORLD;              // 0x3fe attacker = world
    tent->r.svFlags = SVF_BROADCAST;                        // plain assign 0x20

    // EV_GLOBAL_TEAM_SOUND: body-destroyed announce (origin = s.pos.trBase)
    team = client->sess.sessionTeam;
    tent = G_TempEntity(self->s.pos.trBase, EV_GLOBAL_TEAM_SOUND);
    tent->r.svFlags |= SVF_BROADCAST;
    tent->s.eventParm = (team != TEAM_BLUE) + 2;            // RED->3, BLUE->2

    // [QL] death finalizer; respawns iff still frozen (PW_FREEZE != 0).
    // NOT Kamikaze_DeathActivate - that one frees the entity, and handed a
    // player it took the server down on the first kill. See Freeze_DeathFinalize.
    Freeze_DeathFinalize(self);
    client->ps.freezetime = 0;                              // client+0x1f0
}

// ============================================================================
// Freeze_AutoThaw (binary: 0x1004cca0, .so Freeze_AutoThaw 0x762f0)
//
// Called from G_Damage when a player is frozen. Advances any pending round-state
// timer, then, while the round is live, announces "last man standing" if the
// victim's team is now down to a single survivor. Byte-identical twin of the
// CA_PlayerKilled body (0x10038be0). The gate global DAT_1059686c is
// g_lastManStandingWarning; the binary's eCurrent==3 is RS_PLAYING (the round-state
// enum has RS_SHUFFLE at 2). team is the victim's sess.sessionTeam.
// ============================================================================
void Freeze_AutoThaw(int team) {
    int redAlive, blueAlive;

    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        Freeze_RoundStateTransition();
    }

    if (level.roundState.eCurrent != RS_PLAYING)
        return;
    if (level.intermissionQueued || level.intermissionTime || level.warmupTime)
        return;

    Team_LivingTeamCounts(&redAlive, &blueAlive);
    if (redAlive == 0 || blueAlive == 0)
        return;
    if (!g_lastManStandingWarning.integer)
        return;

    if (redAlive == 1 && team == TEAM_RED) {
        LastManStanding(TEAM_RED);
        return;
    }
    if (blueAlive == 1 && team == TEAM_BLUE) {
        LastManStanding(TEAM_BLUE);
    }
}

// ============================================================================
// Freeze_ClientThawCheck (binary: 0x1004cdc0, .so Freeze_ClientThawCheck 0x76a40)
//
// Per-frame thaw scan for a frozen player. Byte-faithful rewrite of the real
// binary body (radius teammate scan via trap_EntitiesInBox + LOS trap_Trace,
// ps.thawtime countdown by frame msec, thaw-progress bits in ps.generic1,
// auto-thaw timeout -> Freeze_InstaKill, thaw-tick EV_THAW_TICK, and the ASSIST
// award on completion). Called from ClientThink_real for each frozen player.
//
// [QL] SIGNATURE: byte-faithful prototype
//   void Freeze_ClientThawCheck(gentity_t *self, int msec)
// (binary 0x1004cdc0, __thiscall ECX=self + [EBP+8]=msec). `msec` is the clamped
// pmove frame delta passed by ClientThink_real, the same per-command delta Pmove
// used this frame (ucmd->serverTime - ps.commandTime, clamped to <=200). The
// ps.thawtime countdown is decremented by exactly that value, matching the binary.
// ============================================================================
void Freeze_ClientThawCheck(gentity_t *ent, int msec) {
    gclient_t *cl = ent->client;
    gentity_t *thawer = NULL;               // EBX / pgVar8
    int oldSeconds = 0;                      // EDI
    int entityList[MAX_GENTITIES];           // local_1010[1024]
    int numNearby = 0;
    int i;
    int maxThaw = g_freezeThawTime.integer;  // DAT_105a472c

    // --- (A) thaw-progress display bits (runs unconditionally, every frame) ---
    {
        int t = cl->ps.thawtime;             // client+0x1f4
        int one3 = maxThaw / 3;              // signed div
        if (t > one3) {
            if (t > one3 * 2) cl->ps.generic1 &= ~3;   // >2/3 left -> bits=0
            else              cl->ps.generic1 |=  1;   // 1/3..2/3  -> bit0
        } else {
            cl->ps.generic1 |= 2;                       // <1/3 left -> bit1
        }
    }

    // --- (B) gating ---
    if (level.intermissionQueued != 0 || level.intermissionTime != 0)
        return;

    if (level.warmupTime != 0 && g_gametype.integer == GT_FREEZE) {
        // warmup FT: bleed the timer, no thaw mechanics
        cl->ps.thawtime -= msec;
        if (cl->ps.thawtime >= 1) return;
        goto do_thaw;                        // 0x1004d19d
    }

    // [QL] round gate: the binary reads a distinct "match-live" global @0x105a03ac
    // (FLAG#3, unmapped in the reimpl; Ghidra alias "roundState_eCurrent" but NOT the
    // real roundState.eCurrent @0x105df580). Using roundState.eCurrent as a stand-in.
    // TODO: identify the real gate global @0x105a03ac.
    if (level.roundState.eCurrent != RS_WARMUP && Freeze_GetRoundState() != RS_PLAYING)
        return;

    // --- (C) auto-thaw timeout -> Freeze_InstaKill(self, 0) (respawn) ---
    if (g_freezeAutoThawTime.integer != 0 &&                            // DAT_104b26ec
        (level.time - cl->ps.freezetime) >= g_freezeAutoThawTime.integer) {
        Freeze_InstaKill(ent, 0);
        return;
    }

    // --- (D) radius scan for a valid thawing teammate ---
    {
        float r = (float)g_freezeThawRadius.integer;   // DAT_105a3d0c, FILD (integer!)
        vec3_t mins, maxs;
        for (i = 0; i < 3; i++) {
            mins[i] = ent->r.currentOrigin[i] - r;     // box centre = self r.currentOrigin
            maxs[i] = ent->r.currentOrigin[i] + r;
        }
        numNearby = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES); // trap+0xa4
        for (i = 0; i < numNearby; i++) {
            gentity_t *other = &g_entities[entityList[i]];
            if (entityList[i] != 0 && other->client == NULL) continue;         // (entnum==0 || has client)
            if (other == ent)                              continue;
            if (ent->client == NULL)                       continue;
            if (other->client == NULL)                     continue;
            if (g_gametype.integer <= 2 &&                                     // FFA/Duel/Race guard (false in FT)
                other->client->sess.sessionTeam != TEAM_SPECTATOR) continue;
            if (other->client->sess.sessionTeam != ent->client->sess.sessionTeam) continue;
            if (other->client->ps.powerups[PW_FREEZE] != 0) continue;          // teammate also frozen
            if (other->health <= 0)                        continue;
            if (g_freezeThawThroughSurface.integer == 0) {                     // DAT_105e1c2c
                trace_t tr;
                trap_Trace(&tr, ent->client->ps.origin, NULL, NULL,           // start = self ps.origin
                           other->client->ps.origin,                          // end   = other ps.origin
                           ENTITYNUM_NONE, CONTENTS_SOLID);
                if (tr.fraction != 1.0f) continue;                             // require clear LOS
            }
            thawer = other;                               // found a thawer (0x1004d1bb)
            break;
        }
    }

    if (thawer != NULL) {
        // lock in the thawer clientNum the first frame
        if (cl->ps.thawClientNum_valid == 0) {
            cl->ps.thawClientNum       = thawer->client->ps.clientNum;   // client+0x1fc
            cl->ps.thawClientNum_valid = 1;                              // client+0x1f8
        }
        // re-resolve the locked thawer inside the current box results.
        // [QL] byte-faithful this is Freeze_PlayerFrozen(list, count, clientNum), a
        // LOOKUP helper (.so 0x76940). The reimpl Freeze_PlayerFrozen (g_local.h:932)
        // is still the freeze-on-death stand-in shape, so the lookup is inlined here.
        if (cl->ps.thawClientNum_valid != 0) {
            int k;
            for (k = 0; k < numNearby; k++) {
                gentity_t *cand = &g_entities[entityList[k]];
                if (cand->client &&
                    cand->client->ps.clientNum == cl->ps.thawClientNum) {
                    thawer = cand;
                    break;
                }
            }
        }
        oldSeconds = (cl->ps.thawtime == 0) ? 0 : cl->ps.thawtime / 1000;
        cl->ps.thawtime -= msec;
        if (g_debugThawTime.integer != 0) {                    // DAT_10595aec
            trap_SendServerCommand(thawer->client->ps.clientNum,
                va("print \"Thaw time %i.\n\"", cl->ps.thawtime));   // trap+0x60
        }
        if (oldSeconds > 0 && oldSeconds != cl->ps.thawtime / 1000 &&
            g_freezeThawTick.integer != 0) {                   // DAT_105a30ac
            gentity_t *te = G_TempEntity(ent->r.currentOrigin, EV_THAW_TICK);   // 0x58
            te->r.ownerNum = ent->client->ps.clientNum;        // te+0x238 (PVS-limited, not broadcast)
        }
    } else {
        // no thawer this frame: thaw progress decays (refills toward max)
        cl->ps.thawtime += msec;
        cl->ps.thawClientNum_valid = 0;
        if (cl->ps.thawtime > maxThaw) cl->ps.thawtime = maxThaw;
    }

    // check_complete (0x1004d080)
    if (cl->ps.thawtime >= 1) return;                          // still thawing

    if (thawer != NULL) {
        cl->ps.thawClientNum_valid = 0;
        thawer->client->pers.teamState.assists++;              // client+0x310 (thaws-given #1)
        thawer->client->expandedStats.numAssists++;            // client+0x7fc (thaws-given #2)
        AddScore(thawer, ent->r.currentOrigin, 2);             // ScorePlum reads thawer origin
        thawer->client->ps.persistant[PERS_ASSIST_COUNT]++;    // client+0x12c
        thawer->client->ps.eFlags =                            // client+0x68: clear award bits, set assist
            (thawer->client->ps.eFlags & 0xfffc77b7) | EF_AWARD_ASSIST;
        thawer->client->rewardTime = level.time + REWARD_SPRITE_TIME;   // client+0x4e8 (+2000)
        STAT_AddPlayerMedalStat(thawer, "ASSIST");             // 0x100715d0

        // EV_OBITUARY: "thawer thawed you"
        {
            gentity_t *tent = G_TempEntity(ent->r.currentOrigin, EV_OBITUARY);  // 0x3a
            tent->s.eventParm       = MOD_THAW;                // 0x1e
            tent->s.otherEntityNum  = ent->client->ps.clientNum;      // victim
            tent->s.otherEntityNum2 = thawer->client->ps.clientNum;   // attacker = thawer
            tent->r.svFlags         = SVF_BROADCAST;           // 0x20
        }
    }

    // EV_GLOBAL_TEAM_SOUND: thaw announce (unconditional, origin = s.pos.trBase)
    {
        int team = ent->client->sess.sessionTeam;
        gentity_t *tent = G_TempEntity(ent->s.pos.trBase, EV_GLOBAL_TEAM_SOUND); // 0x2c
        tent->r.svFlags  |= SVF_BROADCAST;                     // 0x20
        tent->s.eventParm = (team != TEAM_BLUE) + 2;           // RED->3, BLUE->2
    }

do_thaw:                                                       // 0x1004d19d
    // still frozen (PW_FREEZE != 0) -> EV_THAW_PLAYER + ClientSpawn
    Freeze_DeathFinalize(ent);
}

// ============================================================================
// Freeze_PlayerFrozen
//
// Called when a player is "killed" in Freeze Tag. Freezes them instead.
// ============================================================================
void Freeze_PlayerFrozen(gentity_t *self) {
    /*
    [QL] Say why a player did not freeze, when asked.

    This is the one gate on the whole mechanic, and it is invisible from the
    outside: a declined freeze looks exactly like an ordinary death, which is
    what "nobody is freezing" reports as. g_debugFreeze 1 names the round state
    instead of leaving it to be guessed at - same approach as g_debugWarmup,
    which is what settled the match-never-starts bug.
    */
    if (level.roundState.eCurrent != RS_PLAYING) {
        if (g_debugFreeze.integer) {
            G_Printf("freeze declined for %s: roundState.eCurrent is %i, needs RS_PLAYING (%i) "
                     "[warmupTime %i, intermission %i]\n",
                     self->client->pers.netname, level.roundState.eCurrent, RS_PLAYING,
                     level.warmupTime, level.intermissionTime);
        }
        return;
    }

    if (g_debugFreeze.integer) {
        G_Printf("freeze: %s frozen (thawtime %i)\n",
                 self->client->pers.netname, g_freezeThawTime.integer);
    }

    self->client->ps.stats[STAT_HEALTH] = 0;
    self->client->ps.pm_type = PM_FREEZE;
    self->takedamage = qfalse;
    self->health = -40;

    // [QL] real playerState freeze fields (binary player_die FreezeTag fork,
    // 0x100473e0). The rewritten Freeze_ClientThawCheck operates on THESE, not the
    // fabricated freezePlayer frame-counter:
    //   powerups[PW_FREEZE] (client+0x17c) = 0x7fffffff -> marks the frozen statue
    //     (only this field distinguishes frozen; also the ClientThink_real gate)
    //   freezetime (client+0x1f0) = level.time -> stamps the freeze for auto-thaw
    //   thawtime   (client+0x1f4) = g_freezeThawTime.integer -> ms countdown seeded
    //     to the full thaw time (binary: ps.thawtime = DAT_105a472c)
    self->client->ps.powerups[PW_FREEZE] = 0x7fffffff;
    self->client->ps.freezetime = level.time;
    self->client->ps.thawtime = g_freezeThawTime.integer;

    // [QL] the networked presentation bit the client reads to draw the statue.
    // g_combat.c's other freeze path sets s.powerups = 0x8000 for this; without
    // it the playerState says frozen but nothing on the wire tells other
    // clients, so the body would render as an ordinary corpse.
    self->s.powerups |= (1 << PW_FREEZE);

    // legacy flag still read by ClientThink_real's pm_type block (g_active.c) to
    // keep PM_FREEZE instead of PM_DEAD; ps.thawtime above is now the real counter.
    self->client->freezePlayer = qtrue;

    Team_LivingTeamCounts(NULL, NULL);
}

/*
============
Freeze_ClientBegin

[QL] Freeze Tag begin - like RoundBased but clears freeze/dead pm_type first
============
*/
void Freeze_ClientBegin(gentity_t* ent) {
    gclient_t* client = ent->client;

    if (level.roundState.eCurrent == 0) {
        // pre-round (RS_WARMUP): binary clears prevRoundWinningTeam first.
        level.roundState.prevRoundWinningTeam = TEAM_FREE;
        if (client->ps.pm_type == PM_FREEZE || client->ps.pm_type == PM_DEAD) {
            client->ps.pm_type = PM_NORMAL;
        }
        ClientSpawn(ent);
        SelectSpawnWeapon(ent);
    } else if (level.roundState.eCurrent == 1) {
        // countdown
        if (client->ps.pm_type == PM_FREEZE || client->ps.pm_type == PM_DEAD) {
            client->ps.pm_type = PM_NORMAL;
        }
        ClientSpawn(ent);
        SelectSpawnWeapon(ent);
        // [QL] binary sets pm_flags |= PMF_FROZEN (0x4), NOT PMF_TIME_KNOCKBACK.
        // NOTE: the true binary guard is a separate gate global (0x105a03ac,
        // Ghidra alias "roundState_eCurrent"), distinct from roundState.eCurrent
        // (0x105df580); its reimpl mapping is unresolved. In this branch
        // eCurrent==1, so the guard holds here regardless.
        if (level.roundState.eCurrent != 0) {
            client->ps.pm_flags |= PMF_FROZEN;
        }
    } else {
        // round active: spectator mode
        int alive[4] = {0};
        client->ps.pm_type = PM_SPECTATOR;
        ClientSpawn(ent);
        SelectSpawnWeapon(ent);
        Team_LivingTeamCounts(&alive[TEAM_RED], &alive[TEAM_BLUE]);
        // [QL] binary Freeze_ClientBegin (0x1004bfa0) gates auto-follow on
        // g_teamSpecFreeCam==0 (DAT_104b202c), not level.warmupTime, and requires
        // both teams to have living players (both alive counts > 0).
        if (!g_teamSpecFreeCam.integer
            && client->sess.sessionTeam != TEAM_SPECTATOR
            && alive[TEAM_RED] > 0 && alive[TEAM_BLUE] > 0) {
            Cmd_FollowCycle_f(ent, 1);
        }
    }
}

/*
==================
FreezeTagScoreboardMessage

Freeze Tag scoreboard. Same team stats structure as TDM (14 categories).
17 fields per player.
Address: 0x1003ef80

Format: "scores_ft %i %i ... %i %i %i%s"
  28 team stats + numPlayers + teamScoreRed + teamScoreBlue + playerData

Fields per player (17):
  client team score ping time frags deaths accuracy bestWeapon
  impressive excellent gauntlet assist teamKills teamKilled damageDone alive
==================
*/
void FreezeTagScoreboardMessage(gentity_t *ent) {
    char entry[1024];
    char string[MAX_STRING_CHARS];
    char header[512];
    int stringlength, budget;
    int chunkStart, numInChunk;
    qboolean firstChunk;
    int i, j;
    gclient_t *cl;
    int viewerTeam;
    int redStats[14], blueStats[14];

    string[0] = 0;
    stringlength = 0;
    chunkStart = 0;
    numInChunk = 0;
    firstChunk = qtrue;

    // Load team item stats (same 14 categories as TDM)
    redStats[0]  = level.numRedArmorPickups[TEAM_RED];
    redStats[1]  = level.numYellowArmorPickups[TEAM_RED];
    redStats[2]  = level.numGreenArmorPickups[TEAM_RED];
    redStats[3]  = level.numMegaHealthPickups[TEAM_RED];
    redStats[4]  = level.numQuadDamagePickups[TEAM_RED];
    redStats[5]  = level.numBattleSuitPickups[TEAM_RED];
    redStats[6]  = level.numRegenerationPickups[TEAM_RED];
    redStats[7]  = level.numHastePickups[TEAM_RED];
    redStats[8]  = level.numInvisibilityPickups[TEAM_RED];
    redStats[9]  = level.quadDamagePossessionTime[TEAM_RED];
    redStats[10] = level.battleSuitPossessionTime[TEAM_RED];
    redStats[11] = level.regenerationPossessionTime[TEAM_RED];
    redStats[12] = level.hastePossessionTime[TEAM_RED];
    redStats[13] = level.invisibilityPossessionTime[TEAM_RED];

    blueStats[0]  = level.numRedArmorPickups[TEAM_BLUE];
    blueStats[1]  = level.numYellowArmorPickups[TEAM_BLUE];
    blueStats[2]  = level.numGreenArmorPickups[TEAM_BLUE];
    blueStats[3]  = level.numMegaHealthPickups[TEAM_BLUE];
    blueStats[4]  = level.numQuadDamagePickups[TEAM_BLUE];
    blueStats[5]  = level.numBattleSuitPickups[TEAM_BLUE];
    blueStats[6]  = level.numRegenerationPickups[TEAM_BLUE];
    blueStats[7]  = level.numHastePickups[TEAM_BLUE];
    blueStats[8]  = level.numInvisibilityPickups[TEAM_BLUE];
    blueStats[9]  = level.quadDamagePossessionTime[TEAM_BLUE];
    blueStats[10] = level.battleSuitPossessionTime[TEAM_BLUE];
    blueStats[11] = level.regenerationPossessionTime[TEAM_BLUE];
    blueStats[12] = level.hastePossessionTime[TEAM_BLUE];
    blueStats[13] = level.invisibilityPossessionTime[TEAM_BLUE];

    // Hide opponent team's stats
    //
    // [QL] Hoisted out of the player loop, where it used to run once per
    // player. The viewer does not change between iterations and the memset is
    // idempotent, so the values sent are the same; the header is now built
    // once, before the entries, because its length sets the chunk budget.
    viewerTeam = ent->client->sess.sessionTeam;
    if (viewerTeam == TEAM_RED && level.intermissionTime == 0) {
        memset(blueStats, 0, sizeof(blueStats));
    } else if (viewerTeam == TEAM_BLUE && level.intermissionTime == 0) {
        memset(redStats, 0, sizeof(redStats));
    }

    // [QL] the twenty-eight team totals; the count and team scores are appended
    // at send time, once a chunk knows how many entries it carries
    Com_sprintf(header, sizeof(header),
                "scores_ft %i %i %i %i %i %i %i %i %i %i %i %i %i %i "
                "%i %i %i %i %i %i %i %i %i %i %i %i %i %i",
                redStats[0], redStats[1], redStats[2], redStats[3], redStats[4],
                redStats[5], redStats[6], redStats[7], redStats[8],
                redStats[9], redStats[10], redStats[11], redStats[12], redStats[13],
                blueStats[0], blueStats[1], blueStats[2], blueStats[3], blueStats[4],
                blueStats[5], blueStats[6], blueStats[7], blueStats[8],
                blueStats[9], blueStats[10], blueStats[11], blueStats[12], blueStats[13]);
    budget = G_ScoreboardBudget(strlen(header));

    for (i = 0; i < level.numConnectedClients; i++) {
        int ping, accuracy, bestWeapon;
        int alive;

        cl = &level.clients[level.sortedClients[i]];

        if (cl->accuracy_shots) {
            accuracy = cl->accuracy_hits * 100 / cl->accuracy_shots;
        } else {
            accuracy = 0;
        }

        alive = (cl->ps.pm_type == PM_NORMAL) ? 1 : 0;

        if (cl->pers.connected == CON_CONNECTING) {
            ping = -1;
        } else {
            ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
        }

        bestWeapon = STAT_GetBestWeapon(cl);

        // 17 fields per player
        Com_sprintf(entry, sizeof(entry),
                    " %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i",
                    level.sortedClients[i],
                    cl->sess.sessionTeam,
                    cl->ps.persistant[PERS_SCORE], ping,
                    (level.time - cl->pers.enterTime) / 60000,
                    cl->expandedStats.numKills, cl->expandedStats.numDeaths,
                    accuracy, bestWeapon,
                    cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
                    cl->ps.persistant[PERS_EXCELLENT_COUNT],
                    cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
                    cl->ps.persistant[PERS_ASSIST_COUNT],
                    cl->expandedStats.numTeamKills,
                    cl->expandedStats.numTeamKilled,
                    cl->expandedStats.totalDamageDealt,
                    alive);
        j = strlen(entry);

        // [QL] see the comment in g_gametype_ffa.c: flush a chunk rather than
        // stop at the first message.
        if (stringlength + j >= budget && numInChunk > 0) {
            if (firstChunk) {
                trap_SendServerCommand(ent - g_entities,
                                       va("%s %i %i %i%s", header, numInChunk,
                                          level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], string));
                firstChunk = qfalse;
            } else {
                trap_SendServerCommand(ent - g_entities,
                                       va("scores_ft2 %i %i%s", chunkStart, numInChunk, string));
            }
            chunkStart = i;
            numInChunk = 0;
            stringlength = 0;
            string[0] = '\0';
        }

        strcpy(string + stringlength, entry);
        stringlength += j;
        numInChunk++;
    }

    if (firstChunk) {
        trap_SendServerCommand(ent - g_entities,
                               va("%s %i %i %i%s", header, numInChunk,
                                  level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], string));
    } else if (numInChunk > 0) {
        trap_SendServerCommand(ent - g_entities,
                               va("scores_ft2 %i %i%s", chunkStart, numInChunk, string));
    }
}

/*
[QL] The Freeze Tag intermission scoreboard reuses TeamDeathmatchStatisticsMessage
("tdmstats"). The "ctfstats" emitter that Ghidra mislabelled FTScoreboardMessage_impl
is the CTF impl and now lives in g_gametype_ctf.c as CaptureTheFlagStatisticsMessage.
*/
