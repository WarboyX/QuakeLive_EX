/*
 * g_gametype_ca.c -- Clan Arena (GT_CA) round state machine
 *
 * Binary-verified against qagamex86.dll build 1069.
 *
 * CA: Round-based elimination. Teams fight until one is eliminated.
 * Rounds award +1 to the winning team's score.
 *
 * Key functions and their binary addresses:
 *   CA_GetRoundState          0x10038080
 *   CA_AdjustDamage         0x100380d0
 *   CA_CheckExitRules      0x100382e0
 *   CA_RoundStateTransition 0x10038420
 *   CA_Think            0x10038be0
 *   TeamCount_Health       0x1006b100
 *   LastManStanding        0x1006b200
 *   ClanArenaScoreboardMessage    0x1003e4f0
 */
#include "g_local.h"

// ============================================================================
// Helper functions
// ============================================================================

// [QL] Team health+armor totals for tiebreaker (binary: 0x1006b100)
// Takes a 4-element int array, fills healthTotals[team] with sum of
// (health + armor) for all alive players (pm_type == PM_NORMAL) on that team.
// Note: binary passes this via registers (fastcall), not as explicit params
// to Team_LivingTeamCounts.
static void TeamCount_Health(int *healthTotals) {
    int i;
    healthTotals[TEAM_FREE] = 0;
    healthTotals[TEAM_RED] = 0;
    healthTotals[TEAM_BLUE] = 0;
    healthTotals[TEAM_SPECTATOR] = 0;

    for (i = 0; i < level.maxclients; i++) {
        gclient_t *cl = &level.clients[i];
        if (cl->pers.connected != CON_CONNECTED) continue;
        if (cl->ps.pm_type != PM_NORMAL) continue;
        healthTotals[cl->sess.sessionTeam] +=
            cl->ps.stats[STAT_HEALTH] + cl->ps.stats[STAT_ARMOR];
    }
}


// [QL] PlayerAwardEV (0x10046730) is the shared byte-faithful helper in g_combat.c
// (declared in g_local.h). CA/AD/FT/RR round transitions call it with an award_t
// subtype (AWARD_ACCURACY/AWARD_PERFECT); it writes s.generic1=subtype +
// localPersistant[subtype+1]++. The old wrong-payload static here was removed.

// team colour code for the round-win print
static const char *CA_TeamColor(int team) {
    if (team == TEAM_RED) return "^1";
    if (team == TEAM_BLUE) return "^4";
    return "^7";
}

// Stats stubs - ZMQ stats publishing requires Steam backend

// ============================================================================
// CA_GetRoundState (binary: 0x10038080)
//
// Advance the round state if the pending timer has elapsed.
// Returns the current round state, or -1 if still waiting.
// ============================================================================
int CA_GetRoundState(void) {
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return -1;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        CA_RoundStateTransition();
    }
    return level.roundState.eCurrent;
}

// ============================================================================
// CA_RoundStateTransition (binary: 0x10038420)
//
// State machine for Clan Arena rounds.
//   RS_WARMUP(0), RS_COUNTDOWN(1), RS_PLAYING(3), RS_ROUND_OVER(4), RS_EXIT(5)
//   State 2 is unused and causes an error.
// ============================================================================
void CA_RoundStateTransition(void) {
    int i;
    int winTeam = 0;

    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        CA_RoundStateTransition();
    }

    switch (level.roundState.eCurrent) {
    case RS_WARMUP:
        trap_SetConfigstring(CS_ROUND_STATUS, "\\time\\-1\\round\\0");
        return;

    case RS_COUNTDOWN:
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            gentity_t *ent = &g_entities[i];
            if (cl->pers.connected == CON_CONNECTED &&
                cl->sess.sessionTeam != TEAM_SPECTATOR) {
                cl->ps.pm_type = PM_NORMAL;
                ClientSpawn(ent);
                cl->ps.pm_flags |= PMF_FROZEN;
            }
        }
        Team_LivingTeamCounts(NULL, NULL);

        if (g_roundWarmupDelay.integer == 0) {
            level.roundState.tNext = 0;
            level.roundState.eCurrent = RS_PLAYING;
            CA_RoundStateTransition();
        } else {
            level.roundState.tNext = level.time + g_roundWarmupDelay.integer;
            level.roundState.eNext = RS_PLAYING;
        }

        level.roundState.round =
            level.teamScores[TEAM_BLUE] + level.teamScores[TEAM_RED] + 1;
        trap_SetConfigstring(CS_ROUND_STATUS,
            va("\\time\\%d\\round\\%d",
                level.time + g_roundWarmupDelay.integer,
                level.roundState.round));
        return;

    case RS_PLAYING:
        // Unfreeze all connected players and reset per-round stats
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            if (cl->pers.connected == CON_CONNECTED) {
                cl->ps.pm_flags &= ~PMF_FROZEN;
                cl->round_shots = 0;
                cl->round_hits = 0;
                cl->round_damage = 0;
                cl->expandedStats.killStreak = 0;
                if (g_spawnArmor.integer != 0) {
                    cl->ps.powerups[PW_QUAD] =
                        (level.time / 1000) * 1000 + g_spawnArmor.integer;
                }
            }
        }

        level.roundState.round =
            level.teamScores[TEAM_BLUE] + level.teamScores[TEAM_RED] + 1;
        trap_SetConfigstring(CS_ROUND_TIME, va("%d", level.time));
        trap_SetConfigstring(CS_ROUND_STATUS,
            va("\\round\\%d", level.roundState.round));
        return;

    case RS_ROUND_OVER:
    {
        int aliveCounts[4] = {0};
        int healthTotals[4] = {0};

        // Freeze all non-spectator players
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            if (cl->pers.connected == CON_CONNECTED &&
                cl->sess.sessionTeam != TEAM_SPECTATOR) {
                cl->ps.pm_flags |= PMF_FROZEN;
            }
        }

        Team_LivingTeamCounts(&aliveCounts[TEAM_RED], &aliveCounts[TEAM_BLUE]);
        TeamCount_Health(healthTotals);

        // Determine winner. The binary only bumps the round counter,
        // recalculates ranks and announces when the round is decided. A
        // genuine draw (both teams alive, no tiebreaker winner) falls
        // straight through without any of that.
        if (aliveCounts[TEAM_RED] == 0 || aliveCounts[TEAM_BLUE] == 0) {
            // One (or both) teams eliminated
            if (aliveCounts[TEAM_RED] == 0) {
                if (aliveCounts[TEAM_BLUE] != 0) {
                    winTeam = TEAM_BLUE;
                    level.teamScores[TEAM_BLUE]++;
                }
            } else {
                winTeam = TEAM_RED;
                level.teamScores[TEAM_RED]++;
            }

            level.roundState.round++;
            CalculateRanks();

            if (aliveCounts[winTeam] < 2) {
                trap_SendServerCommand(-1,
                    va("print \"%s%s TEAM^3 WINS the round!^7 (%i hp remaining)\n\"",
                        CA_TeamColor(winTeam), TeamName(winTeam), healthTotals[winTeam]));
            } else {
                trap_SendServerCommand(-1,
                    va("print \"%s%s TEAM^3 WINS the round!^7 (%i players remaining)\n\"",
                        CA_TeamColor(winTeam), TeamName(winTeam), aliveCounts[winTeam]));
            }
        } else if (g_roundDrawLivingCount.integer != 0 &&
                   aliveCounts[TEAM_RED] != aliveCounts[TEAM_BLUE]) {
            // Both alive - decide on living count
            if (aliveCounts[TEAM_BLUE] < aliveCounts[TEAM_RED]) {
                winTeam = TEAM_RED;
                level.teamScores[TEAM_RED]++;
            } else {
                winTeam = TEAM_BLUE;
                level.teamScores[TEAM_BLUE]++;
            }

            level.roundState.round++;
            CalculateRanks();

            trap_SendServerCommand(-1,
                va("print \"%s%s TEAM^3 WINS the round!^7 (%i players remaining)\n\"",
                    CA_TeamColor(winTeam), TeamName(winTeam), aliveCounts[winTeam]));
        } else if (g_roundDrawHealthCount.integer != 0 &&
                   healthTotals[TEAM_RED] != healthTotals[TEAM_BLUE]) {
            // Both alive - decide on total health
            if (healthTotals[TEAM_BLUE] < healthTotals[TEAM_RED]) {
                winTeam = TEAM_RED;
                level.teamScores[TEAM_RED]++;
            } else {
                winTeam = TEAM_BLUE;
                level.teamScores[TEAM_BLUE]++;
            }

            level.roundState.round++;
            CalculateRanks();

            trap_SendServerCommand(-1,
                va("print \"%s%s TEAM^3 WINS the round!^7 (^1%i^7 hp vs ^4%i^7 hp)\n\"",
                    CA_TeamColor(winTeam), TeamName(winTeam),
                    healthTotals[TEAM_RED], healthTotals[TEAM_BLUE]));
        }
        // Genuine draw: round not bumped, no announce (matches binary)

        // Award medals
        for (i = 0; i < level.maxclients; i++) {
            gclient_t *cl = &level.clients[i];
            gentity_t *ent = &g_entities[i];

            if (cl->pers.connected != CON_CONNECTED)
                continue;

            // Accuracy award: >50% hit rate this round
            if (cl->round_shots != 0) {
                int acc = (cl->round_hits * 100) / cl->round_shots;
                if ((double)acc > 50.0) {
                    PlayerAwardEV(ent, AWARD_ACCURACY);
                    STAT_AddPlayerMedalStat(ent, "ACCURACY");
                }
            }

            // Perfect award: on winning team with 0 damage taken
            if (cl->sess.sessionTeam == winTeam &&
                cl->round_damage == 0) {
                PlayerAwardEV(ent, AWARD_PERFECT);
                STAT_AddPlayerMedalStat(ent, "PERFECT");
            }

            // Clutch: one-time Steam achievement for last-alive win with kills
            // Steam stat 0x31 (49): awarded once when sole survivor wins
            // with 2+ round kills (tracked via expandedStats.killStreak)
            if (cl->sess.sessionTeam == winTeam) {
                int hasClutch = trap_GetSteamStat(cl->ps.clientNum, 0x31);
                if (hasClutch == 0 &&
                    aliveCounts[cl->sess.sessionTeam] == 1 &&
                    cl->expandedStats.killStreak > 2) {
                    trap_IncrementSteamStat(cl->ps.clientNum, 0x31);
                }
            }
        }

        STAT_AddRoundOverStat(level.roundState.round - 1, winTeam, winTeam == 0);

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
        // GT_RR uses different sound indices than CA
        {
            int soundType;
            if (g_gametype.integer == GT_RR) {
                if (winTeam == TEAM_RED || winTeam == TEAM_BLUE)
                    soundType = GTS_ROUND_OVER;   // 0x14
                else
                    soundType = GTS_DRAW_ROUND;   // 0x12
            } else {
                if (winTeam == TEAM_RED)
                    soundType = GTS_RED_WINS_ROUND;    // 0x10
                else if (winTeam == TEAM_BLUE)
                    soundType = GTS_BLUE_WINS_ROUND;   // 0x11
                else
                    soundType = GTS_DRAW_ROUND;        // 0x12
            }
            {
                // [QL] binary carries the GTS index in eventParm (0xc0), not otherEntityNum2
                // Binary sets gentity offset 0x1e0 |= 0x20 == r.svFlags |= SVF_BROADCAST
                // (SVF_BROADCAST is 0x20; EF_NODRAW is 0x80 - the earlier EF_NODRAW was wrong).
                gentity_t *te = G_TempEntity(vec3_origin, EV_GLOBAL_TEAM_SOUND);
                te->r.svFlags |= SVF_BROADCAST;
                te->s.eventParm = soundType;
            }
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
        G_Error("CA_RoundStateTransition: invalid state");
        return;
    }
}

// ============================================================================
// CA_CheckExitRules (binary: 0x100382e0)
//
// Check if CA game should end (timelimit, roundlimit, or mercylimit).
// ============================================================================
qboolean CA_CheckExitRules(int doExit) {
    int mercyTime;

    if (level.teamScores[TEAM_RED] == level.teamScores[TEAM_BLUE])
        return qfalse;

    if (g_timelimit.integer != 0 &&
        g_timelimit.integer * 60000 <= level.time - level.startTime) {
        if (!doExit) return qtrue;
        trap_SendServerCommand(-1, "print \"Timelimit hit.\n\"");
        LogExit(0, 0, "Timelimit hit.");
        return qtrue;
    }

    if (roundlimit.integer != 0) {
        if (roundlimit.integer <= level.teamScores[TEAM_RED]) {
            if (!doExit) return qtrue;
            trap_SendServerCommand(-1, "print \"Red hit the roundlimit.\n\"");
            LogExit(0, 0, "Roundlimit hit.");
            return qtrue;
        }
        if (roundlimit.integer <= level.teamScores[TEAM_BLUE]) {
            if (!doExit) return qtrue;
            trap_SendServerCommand(-1, "print \"Blue hit the roundlimit.\n\"");
            LogExit(0, 0, "Roundlimit hit.");
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
// CA_AdjustDamage (binary: 0x100380d0)
//
// Called from G_Damage section 20 as (targ, attacker, &take, &asave). The two
// int* are the post-CheckArmor health-damage (take) and armour-save (asave)
// locals; the routine may zero/clamp both and returns qfalse to abort ALL
// damage. Also implements the CA score-per-damage mechanic (+1 score per 100
// clamped damage). The self/team suppression keys off g_dmflags (binary
// DAT_10597dcc), NOT g_accuracyFlags. All three round adjusters and G_Damage
// section 21 read the same 0x10597dcc.
// ============================================================================
qboolean CA_AdjustDamage(gentity_t *target, gentity_t *attacker,
                             int *take, int *asave) {
    int flags = g_dmflags.integer;

    if (target == attacker) {
        // Self-damage: DF_NO_SELF_DAMAGE zeroes take; DF_NO_SELF_ARMOR_DAMAGE
        // (bit 8) survives into the asave gate below.
        if (flags & 4) *take = 0;
        flags &= 8;
    } else {
        gclient_t *tCl = target->client;
        gclient_t *aCl = attacker->client;

        if (tCl == NULL)
            goto checkRound;

        // Team damage filter (bit 1 zeroes take on same-team hits)
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

        flags &= 2;  // team: bit 2 gates the asave zero below
    }

    if (flags != 0)
        *asave = 0;

checkRound:
    if (level.warmupTime != 0)
        return qtrue;

    // Check round timer
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return qfalse;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        CA_RoundStateTransition();
    }

    // Must be in playing state
    if (level.roundState.eCurrent != RS_PLAYING)
        return qfalse;

    // CA score-per-damage: clamp take to the target's remaining health and
    // asave to its remaining armour, write the clamped values back, then
    // accumulate their sum and award +1 score (with rank recompute) per 100.
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
            CalculateRanks();
        }
    }

    return qtrue;
}

// ============================================================================
// CA_Think (binary: 0x10038be0)
//
// Called both per-frame from G_RunFrame AND from player_die for GT_CA.
// Checks alive counts and triggers LastManStanding warnings.
// Round-over is NOT triggered here - it happens via the death call path.
// ============================================================================
void CA_Think(void) {
    int redAlive, blueAlive;

    // Advance pending timer
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        CA_RoundStateTransition();
    }

    if (level.roundState.eCurrent != RS_PLAYING)
        return;
    if (level.intermissionQueued || level.intermissionTime || level.warmupTime)
        return;

    Team_LivingTeamCounts(&redAlive, &blueAlive);

    // Check if round is over (one team eliminated)
    if (redAlive == 0 || blueAlive == 0) {
        level.roundState.eCurrent = RS_ROUND_OVER;
        CA_RoundStateTransition();
        return;
    }

    // Last man standing warning
    if (g_lastManStandingWarning.integer != 0) {
        if (redAlive == 1) {
            LastManStanding(TEAM_RED);
        }
        if (blueAlive == 1) {
            LastManStanding(TEAM_BLUE);
        }
    }
}

// ============================================================================
// ClanArenaScoreboardMessage (binary: 0x1003e4f0)
//
// Clan Arena scoreboard. 16 fields per player.
// Format: "scores_ca %i %i %i%s" numPlayers redScore blueScore playerData
// Fields per player (16):
//   client team score ping time kills deaths accuracy bestWeapon
//   bestWeaponAccuracy damageDone impressive excellent gauntlet perfect alive
// ============================================================================
/*
==================
ClanArenaStatisticsMessage

[QL] CA per-player detail stats sent as the "castats" command (parsed client-side by
CG_ParseTeamStats_CA). Per player: damageDone damageTaken + per-weapon
(accuracy numWeaponKills) * 15. Emitted at intermission from Cmd_Score_f for GT_CA.
(Ghidra mislabelled this CaptureTheFlagStatisticsMessage; it emits castats, so it is the CA
impl - corrected here and in the Ghidra project.)
Address: 0x1003e700
==================
*/
void ClanArenaStatisticsMessage(gentity_t *ent) {
    char entry[1024];
    char string[1024];
    int stringlength;
    int i, w;
    gclient_t *cl;

    for (i = 0; i < level.numConnectedClients; i++) {
        cl = &level.clients[level.sortedClients[i]];

        string[0] = 0;
        stringlength = 0;

        // First: damageDone damageTaken
        Com_sprintf(entry, sizeof(entry), " %i %i",
                    cl->expandedStats.totalDamageDealt,
                    cl->expandedStats.totalDamageTaken);
        stringlength = strlen(entry);
        if (stringlength < (int)sizeof(string)) {
            strcpy(string, entry);
        }

        // Per-weapon: accuracy numWeaponKills (weapons 0-14)
        for (w = 0; w < 15; w++) {
            int j, weapAcc = 0;

            if (cl->expandedStats.shotsHit[w] && cl->expandedStats.shotsFired[w]) {
                weapAcc = cl->expandedStats.shotsHit[w] * 100 / cl->expandedStats.shotsFired[w];
            }

            Com_sprintf(entry, sizeof(entry), " %i %i",
                        weapAcc, cl->expandedStats.numWeaponKills[w]);
            j = strlen(entry);
            if (G_ScoreboardTruncated(stringlength + j, i))
                break;
            strcpy(string + stringlength, entry);
            stringlength += j;
        }

        trap_SendServerCommand(ent - g_entities,
            va("castats %i%s", i, string));
    }
}

// ============================================================================
void ClanArenaScoreboardMessage(gentity_t *ent) {
    char entry[1024];
    char string[MAX_STRING_CHARS];
    int stringlength;
    int chunkStart, numInChunk;
    qboolean firstChunk;
    int i, j;
    gclient_t *cl;

    string[0] = 0;
    stringlength = 0;
    chunkStart = 0;
    numInChunk = 0;
    firstChunk = qtrue;

    for (i = 0; i < level.numConnectedClients; i++) {
        int ping, accuracy, perfect, bestWeapon, bestWeaponAccuracy;
        int alive;

        cl = &level.clients[level.sortedClients[i]];

        if (cl->accuracy_shots) {
            accuracy = cl->accuracy_hits * 100 / cl->accuracy_shots;
        } else {
            accuracy = 0;
        }

        perfect = (cl->ps.persistant[PERS_RANK] == 0 &&
                   cl->ps.persistant[PERS_KILLED] == 0) ? 1 : 0;
        alive = (cl->ps.pm_type == PM_NORMAL) ? 1 : 0;

        if (cl->pers.connected == CON_CONNECTING) {
            ping = -1;
        } else {
            ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
        }

        bestWeapon = STAT_GetBestWeapon(cl);
        if (cl->expandedStats.shotsFired[bestWeapon] > 0) {
            bestWeaponAccuracy = cl->expandedStats.shotsHit[bestWeapon] * 100
                                / cl->expandedStats.shotsFired[bestWeapon];
        } else {
            bestWeaponAccuracy = 0;
        }

        // 16 fields per player
        Com_sprintf(entry, sizeof(entry),
                    " %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i",
                    level.sortedClients[i],
                    cl->sess.sessionTeam,
                    cl->ps.persistant[PERS_SCORE], ping,
                    (level.time - cl->pers.enterTime) / 60000,
                    cl->expandedStats.numKills, cl->expandedStats.numDeaths,
                    accuracy, bestWeapon, bestWeaponAccuracy,
                    cl->expandedStats.totalDamageDealt,
                    cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
                    cl->ps.persistant[PERS_EXCELLENT_COUNT],
                    cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
                    perfect, alive);
        j = strlen(entry);

        // [QL] see the comment in g_gametype_ffa.c: flush a chunk rather than
        // stop at the first message.
        if (stringlength + j >= MAX_SCOREBOARD_PAYLOAD && numInChunk > 0) {
            if (firstChunk) {
                trap_SendServerCommand(ent - g_entities,
                                       va("scores_ca %i %i %i%s", numInChunk,
                                          level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], string));
                firstChunk = qfalse;
            } else {
                trap_SendServerCommand(ent - g_entities,
                                       va("scores_ca2 %i %i%s", chunkStart, numInChunk, string));
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
                               va("scores_ca %i %i %i%s", numInChunk,
                                  level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], string));
    } else if (numInChunk > 0) {
        trap_SendServerCommand(ent - g_entities,
                               va("scores_ca2 %i %i%s", chunkStart, numInChunk, string));
    }
}
