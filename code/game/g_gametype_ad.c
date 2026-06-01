/*
 * g_gametype_ad.c -- Attack & Defend (GT_AD) round state machine
 *
 * Ported from ql-decompiled/game/g_ca_ad.c (Quake Live build 1069)
 *
 * AD: Two-turn rounds where teams alternate attacking/defending flags.
 * Attackers score by capturing; defenders score by preventing capture.
 */
#include "g_local.h"

#define AD_SCORE_HISTORY_SIZE 20

// AD score history ring buffer
static int ad_scoreRaw[AD_SCORE_HISTORY_SIZE];
static int ad_scoreSorted[AD_SCORE_HISTORY_SIZE];
static int ad_scoreHistoryIndex;

// AD previous scores per turn
static int ad_prevScore[2];  // [0]=red, [1]=blue
static int ad_turnDelta[2];

// ============================================================================
// AD_UpdateScoreHistory
// ============================================================================
static void AD_UpdateScoreHistory(void) {
    int turn = level.roundState.turn;
    int idx, i;

    ad_turnDelta[turn] = level.teamScores[turn + 1] - ad_prevScore[turn];
    ad_scoreRaw[ad_scoreHistoryIndex % AD_SCORE_HISTORY_SIZE] = ad_turnDelta[turn];
    ad_prevScore[turn] = level.teamScores[turn + 1];

    ad_scoreHistoryIndex++;
    idx = ad_scoreHistoryIndex % AD_SCORE_HISTORY_SIZE;

    if (idx == ad_scoreHistoryIndex) {
        for (i = 0; i < AD_SCORE_HISTORY_SIZE; i++) {
            if (i == idx)
                ad_scoreSorted[i] = -2;
            else if (i > idx)
                ad_scoreSorted[i] = -1;
            else
                ad_scoreSorted[i] = ad_scoreRaw[i];
        }
    } else {
        int offset = idx + (turn != 0 ? 1 : 0) + 1;
        int count = 0;

        if (offset < AD_SCORE_HISTORY_SIZE) {
            count = AD_SCORE_HISTORY_SIZE - offset;
            memcpy(ad_scoreSorted, &ad_scoreRaw[offset], count * sizeof(int));
        }
        if (offset > 0) {
            memcpy(&ad_scoreSorted[count], ad_scoreRaw, (idx + (turn != 0 ? 1 : 0) + 1) * sizeof(int));
        }
        if (turn == 0) {
            ad_scoreSorted[AD_SCORE_HISTORY_SIZE - 1] = -1;
        }
    }

    trap_SendServerCommand(-1,
        va("scores_ad %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
            ad_scoreSorted[0], ad_scoreSorted[1], ad_scoreSorted[2], ad_scoreSorted[3],
            ad_scoreSorted[4], ad_scoreSorted[5], ad_scoreSorted[6], ad_scoreSorted[7],
            ad_scoreSorted[8], ad_scoreSorted[9], ad_scoreSorted[10], ad_scoreSorted[11],
            ad_scoreSorted[12], ad_scoreSorted[13], ad_scoreSorted[14], ad_scoreSorted[15],
            ad_scoreSorted[16], ad_scoreSorted[17], ad_scoreSorted[18], ad_scoreSorted[19],
            level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE]));
}

// ============================================================================
// AD_CheckExitRules - only checks after both turns (turn == 1)
// ============================================================================
qboolean AD_CheckExitRules(int doExit) {
    int mercyTime;

    if (level.roundState.turn != 1)
        return qfalse;
    if (level.teamScores[TEAM_RED] == level.teamScores[TEAM_BLUE])
        return qfalse;

    if (g_timelimit.integer != 0 &&
        g_timelimit.integer * 60000 <= level.time - level.startTime) {
        if (!doExit) return qtrue;
        trap_SendServerCommand(-1, "print \"Timelimit hit.\n\"");
        LogExit(0, 0, "Timelimit hit.");
        return qtrue;
    }

    if (g_scorelimit.integer != 0) {
        if (g_scorelimit.integer <= level.teamScores[TEAM_RED] &&
            level.teamScores[TEAM_BLUE] < level.teamScores[TEAM_RED]) {
            if (!doExit) return qtrue;
            trap_SendServerCommand(-1, "print \"Red hit the scorelimit.\n\"");
            LogExit(0, 0, "Scorelimit hit.");
            return qtrue;
        }
        if (g_scorelimit.integer <= level.teamScores[TEAM_BLUE] &&
            level.teamScores[TEAM_RED] < level.teamScores[TEAM_BLUE]) {
            if (!doExit) return qtrue;
            trap_SendServerCommand(-1, "print \"Blue hit the scorelimit.\n\"");
            LogExit(0, 0, "Scorelimit hit.");
            return qtrue;
        }
    }

    if (g_mercylimit.integer == 0)
        return qfalse;

    mercyTime = g_mercytime.integer * 60000 + level.timeOvertime;
    if (level.time - level.startTime < mercyTime)
        return qfalse;

    if (level.teamScores[TEAM_RED] - level.teamScores[TEAM_BLUE] >= g_mercylimit.integer) {
        if (!doExit) return qtrue;
        trap_SendServerCommand(-1, "print \"Red hit the mercylimit.\n\"");
        LogExit(0, 0, "Mercylimit hit.");
        return qtrue;
    }
    if (level.teamScores[TEAM_BLUE] - level.teamScores[TEAM_RED] >= g_mercylimit.integer) {
        if (!doExit) return qtrue;
        trap_SendServerCommand(-1, "print \"Blue hit the mercylimit.\n\"");
        LogExit(0, 0, "Mercylimit hit.");
        return qtrue;
    }

    return qfalse;
}

// ============================================================================
// AD_GetRoundState - advance the round state if the pending timer elapsed.
// Returns the current state, or -1 while still waiting.
// Address: 0x10035830
// ============================================================================
int AD_GetRoundState(void) {
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return -1;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        AD_RoundStateTransition();
    }
    return level.roundState.eCurrent;
}

// ============================================================================
// AD_AdjustDamage (.so AD_AdjustDamage, binary: 0x10035880)
//
// GT_AD damage adjust, called from G_Damage section 20 as
// (targ, attacker, &take, &asave). Byte-identical to the CA adjust
// (AccuracyMessage / CA_AdjustDamage 0x100380d0) EXCEPT:
//   - it advances the round via AD_RoundStateTransition (not CA_*), and
//   - it does NOT call CalculateRanks() after the +100-damage score bump.
// Self/team suppression keys off g_dmflags (binary DAT_10597dcc). Returns
// qfalse to abort ALL damage.
// ============================================================================
qboolean AD_AdjustDamage(gentity_t *target, gentity_t *attacker,
                             int *take, int *asave) {
    int flags = g_dmflags.integer;

    if (target == attacker) {
        if (flags & 4) *take = 0;
        flags &= 8;
    } else {
        gclient_t *tCl = target->client;
        gclient_t *aCl = attacker->client;

        if (tCl == NULL)
            goto checkRound;

        if (aCl != NULL &&
            (g_gametype.integer >= GT_TEAM || tCl->sess.sessionTeam == TEAM_SPECTATOR) &&
            tCl->sess.sessionTeam == aCl->sess.sessionTeam &&
            (flags & 1)) {
            *take = 0;
        }

        if (tCl == NULL || aCl == NULL ||
            (g_gametype.integer < GT_TEAM && tCl->sess.sessionTeam != TEAM_SPECTATOR) ||
            tCl->sess.sessionTeam != aCl->sess.sessionTeam)
            goto checkRound;

        flags &= 2;
    }

    if (flags != 0)
        *asave = 0;

checkRound:
    if (level.warmupTime != 0)
        return qtrue;

    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return qfalse;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        AD_RoundStateTransition();  // [QL] AD variant (vs CA_RoundStateTransition)
    }

    if (level.roundState.eCurrent != RS_PLAYING)
        return qfalse;

    if (attacker->client != NULL && !OnSameTeam(target, attacker) &&
        target->client != NULL && target->health > 0) {
        int takeClamp = target->client->ps.stats[STAT_HEALTH];  // @0xc0
        if (takeClamp > *take) takeClamp = *take;
        int armorClamp = target->client->ps.stats[STAT_ARMOR];  // @0xd0
        if (armorClamp > *asave) armorClamp = *asave;

        *take = takeClamp;
        *asave = armorClamp;

        attacker->client->pers.teamState.dmgAccumulator += armorClamp + takeClamp;
        if (attacker->client->pers.teamState.dmgAccumulator >= 100) {
            attacker->client->pers.teamState.dmgAccumulator -= 100;
            attacker->client->ps.persistant[PERS_SCORE]++;
            // [QL] AD does NOT call CalculateRanks() here (CA does).
        }
    }

    return qtrue;
}

// ============================================================================
// AD_CheckRoundEnd - true if the round should end: a team wiped, the flag was
// captured, or the round timelimit ran out.
// Address: 0x10035ae0
// ============================================================================
qboolean AD_CheckRoundEnd(int redAlive, int blueAlive) {
    if (redAlive != 0 && blueAlive != 0 && level.roundState.capture != 1 &&
        (roundtimelimit.integer == 0 ||
         level.time - level.roundState.startTime < roundtimelimit.integer * 1000)) {
        return qfalse;
    }
    return qtrue;
}

// ============================================================================
// AD_RoundStateTransition - two-turn round state machine
// ============================================================================
void AD_RoundStateTransition(void) {
    int i;
    int winTeam = 0;

    if (level.roundState.tNext != 0) {
        // [QL] binary jumps to the invalid-state G_Error when this is reached with
        // a pending timer that has NOT yet elapsed (all callers clear tNext first).
        if (level.time < level.roundState.tNext)
            G_Error("AD_RoundStateTransition: invalid state");
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        AD_RoundStateTransition();
    }

    switch (level.roundState.eCurrent) {
    case RS_WARMUP:
        ADScoreboardMessage(NULL);
        trap_SetConfigstring(CS_ROUND_STATUS,
            "\\time\\-1\\round\\0\\turn\\0\\state\\0");
        return;

    case RS_COUNTDOWN:
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            gentity_t *ent = &g_entities[i];
            if (cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR) {
                cl->ps.pm_type = PM_FREEZE;
                ClientSpawn(ent);
                cl->ps.pm_flags |= PMF_FROZEN;
            }
        }
        Team_LivingTeamCounts(NULL, NULL);

        if (g_roundWarmupDelay.integer == 0) {
            level.roundState.tNext = 0;
            level.roundState.eCurrent = RS_PLAYING;
            AD_RoundStateTransition();
        } else {
            level.roundState.tNext = level.time + g_roundWarmupDelay.integer;
            level.roundState.eNext = RS_PLAYING;
        }

        trap_SetConfigstring(CS_ROUND_STATUS,
            va("\\time\\%d\\round\\%d\\turn\\%d\\state\\1",
                level.time + g_roundWarmupDelay.integer, level.roundState.round, level.roundState.turn));
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
                if (cl->sess.sessionTeam != TEAM_SPECTATOR) {
                    cl->ps.pm_type = PM_NORMAL;
                }
                if (g_spawnArmor.integer != 0) {
                    // [QL] binary writes powerups[0] (client+0x140), NOT PW_QUAD
                    // (0x150). All round gametypes (CA/FT/AD/RR) + ClientSpawn use
                    // this powerups[0] spawn-protection timer slot.
                    cl->ps.powerups[0] =
                        (level.time / 1000) * 1000 + g_spawnArmor.integer;
                }
            }
        }

        level.roundState.capture = 0;
        level.roundState.touch = 0;

        trap_SetConfigstring(CS_ROUND_TIME, va("%d", level.time));
        trap_SetConfigstring(CS_ROUND_STATUS,
            va("\\turn\\%d\\state\\2", level.roundState.turn));
        return;

    case RS_ROUND_OVER:
    {
        int redAlive, blueAlive;

        // Freeze all
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            if (cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR) {
                cl->ps.pm_flags |= PMF_FROZEN;
                // clear flag-carry powerups (binary clears powerups[1]/powerups[2])
                cl->ps.powerups[PW_REDFLAG] = 0;
                cl->ps.powerups[PW_BLUEFLAG] = 0;
            }
        }

        Team_LivingTeamCounts(&redAlive, &blueAlive);

        // Determine winner
        if (level.roundState.capture == 1) {
            winTeam = (level.roundState.turn == 0) ? TEAM_RED : TEAM_BLUE;
        } else if (redAlive == 0 && blueAlive != 0) {
            if (level.roundState.turn == 1) {
                level.teamScores[TEAM_BLUE] += g_adElimScoreBonus.integer;
            }
            winTeam = TEAM_BLUE;
        } else if (blueAlive == 0 && redAlive != 0) {
            if (level.roundState.turn == 0) {
                level.teamScores[TEAM_RED] += g_adElimScoreBonus.integer;
            }
            winTeam = TEAM_RED;
        }

        // Award medals
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            gentity_t *ent = &g_entities[i];
            if (cl->pers.connected != CON_CONNECTED) continue;

            // Accuracy award: >50% hit rate this round
            if (cl->round_shots != 0) {
                int acc = (cl->round_hits * 100) / cl->round_shots;
                if ((double)acc > 50.0) {
                    gentity_t *te = G_TempEntity(ent->r.currentOrigin, EV_AWARD);
                    te->r.svFlags |= SVF_BROADCAST;
                    te->s.otherEntityNum = ent->s.number;
                    te->s.eventParm = AWARD_ACCURACY;
                    te->s.otherEntityNum2 = 1;
                    cl->rewardTime = level.time + REWARD_SPRITE_TIME;
                }
            }

            // Perfect award: on winning team with 0 damage taken
            if (cl->round_damage == 0) {
                gentity_t *te = G_TempEntity(ent->r.currentOrigin, EV_AWARD);
                te->r.svFlags |= SVF_BROADCAST;
                te->s.otherEntityNum = ent->s.number;
                te->s.eventParm = AWARD_PERFECT;
                te->s.otherEntityNum2 = 1;
                cl->rewardTime = level.time + REWARD_SPRITE_TIME;
            }
        }

        CalculateRanks();
        AD_UpdateScoreHistory();

        // Return flags
        Team_ReturnFlag(TEAM_RED);
        Team_ReturnFlag(TEAM_BLUE);

        STAT_AddRoundOverStat(level.roundState.round - 1, winTeam, winTeam == 0);

        // Check for game end (only after second turn). On exit, schedule
        // RS_EXIT and return without toggling the turn or playing the sound.
        if (level.roundState.turn == 1 && level.teamScores[TEAM_RED] != level.teamScores[TEAM_BLUE]) {
            if ((g_timelimit.integer != 0 &&
                 g_timelimit.integer * 60000 <= level.time - level.startTime) ||
                (g_scorelimit.integer != 0 &&
                 ((g_scorelimit.integer <= level.teamScores[TEAM_RED] &&
                   level.teamScores[TEAM_BLUE] < level.teamScores[TEAM_RED]) ||
                  (g_scorelimit.integer <= level.teamScores[TEAM_BLUE] &&
                   level.teamScores[TEAM_RED] < level.teamScores[TEAM_BLUE])))) {
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
            if (winTeam == TEAM_RED) soundType = GTS_RED_WINS_ROUND;    // 0x10
            else if (winTeam == TEAM_BLUE) soundType = GTS_BLUE_WINS_ROUND;  // 0x11
            else soundType = GTS_DRAW_ROUND;  // 0x12
            // [QL] binary carries the GTS index in eventParm (0xc0) and broadcasts
            gentity_t *te = G_TempEntity(vec3_origin, EV_GLOBAL_TEAM_SOUND);
            te->r.svFlags |= SVF_BROADCAST;
            te->s.eventParm = soundType;
        }

        level.roundState.tNext = level.time + 3500;
        level.roundState.eNext = RS_COUNTDOWN;
        trap_SetConfigstring(CS_ROUND_TIME, va("%d", -1));

        level.roundState.turn ^= 1;
        if (level.roundState.turn == 0) {
            level.roundState.round++;
        }
        return;
    }

    case RS_EXIT:
        AD_CheckExitRules(1);
        return;

    default:
        G_Error("AD_RoundStateTransition: invalid state");
        return;
    }
}

// ============================================================================
// AD_Think - called from player_die. Advances the round timer and fires the
// last-man-standing warning for the attacking team only.
//
// [QL] The binary (AD_Think @ 0x10036400) does NOT end the round here. Round
// end (team wiped / flag capped / round timeout) is driven per-frame by
// G_RunFrame via AD_GetRoundState + AD_CheckRoundEnd, not by this function.
// Address: 0x10036400
// ============================================================================
void AD_Think(void) {
    int redAlive, blueAlive;
    int lastManTeam;

    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        AD_RoundStateTransition();
    }

    if (level.roundState.eCurrent != RS_PLAYING)
        return;
    if (level.intermissionQueued || level.intermissionTime || level.warmupTime)
        return;

    // [QL] attacking team this turn (binary reads it via ESI = AD_GetAttackingTeam):
    // turn 0 -> RED attacks, turn 1 -> BLUE attacks.
    lastManTeam = (level.roundState.turn != 0) ? TEAM_BLUE : TEAM_RED;

    Team_LivingTeamCounts(&redAlive, &blueAlive);

    // Last man standing warning, only for the attacking team's final player.
    if (redAlive != 0 && blueAlive != 0 && g_lastManStandingWarning.integer != 0) {
        if (redAlive == 1 && lastManTeam == TEAM_RED) {
            LastManStanding(TEAM_RED);
        } else if (blueAlive == 1 && lastManTeam == TEAM_BLUE) {
            LastManStanding(TEAM_BLUE);
        }
    }
}

// ============================================================================
// AD_IsInPlayState - returns attacking team (1=RED, 2=BLUE), 0 if not playing
// ============================================================================
int AD_IsInPlayState(void) {
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return 0;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        AD_RoundStateTransition();
    }
    if (level.roundState.eCurrent != RS_PLAYING)
        return 0;
    return (level.roundState.turn != 0) ? 2 : 1;
}

// ============================================================================
// AD_CanScore - returns defending team number, 0 if not playing
// ============================================================================
int AD_CanScore(void) {
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return 0;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        AD_RoundStateTransition();
    }
    if (level.roundState.eCurrent != RS_PLAYING)
        return 0;
    return 2 - (level.roundState.turn != 0 ? 1 : 0);
}

/*
==================
ADScoreboardMessage  ( .so: AD_ClearScoreHistory )
Address: 0x100365a0

Reset the AD score-history ring buffer and broadcast the initial scores_ad
command. Called from RS_WARMUP (the binary also calls it from G_InitGame). The
20 history slots read back as -1 because the arrays were just cleared.

[QL] The binary does NOT reset ad_prevScore here (that pair is only ever
assigned inside AD_UpdateScoreHistory), so neither does this.

ent is unused: the binary takes no argument, kept only to match the
g_local.h prototype.
==================
*/
void ADScoreboardMessage(gentity_t *ent) {
    int i;

    (void)ent;

    for (i = 0; i < AD_SCORE_HISTORY_SIZE; i++) {
        ad_scoreRaw[i] = -1;
        ad_scoreSorted[i] = -1;
    }
    ad_scoreHistoryIndex = 0;

    trap_SendServerCommand(-1,
        va("scores_ad %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
           -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
           -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
           level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE]));
}
