/*
 * g_gametype_duel.c -- Duel / Tournament (GT_DUEL, 1)
 *
 * 1v1 mode with a queue. Players wait as spectators and rotate in.
 * Queue management: AddDuelPlayer() (below). Ready countdown: CheckTournament().
 * Scoreboard: DuelScoreboardMessage (below).
 */
#include "g_local.h"

// Duel uses fraglimit/timelimit exit rules.
// No round state.

/*
==================
DuelScoreboardMessage_impl

Address: 0x1003d0b0

[QL] In the binary this builds a generic "scores %i %i %i%s" string and *returns*
it (tail-call to va) to Cmd_Score_impl, which uses it ONLY as the default-gametype
fallback and sends it to the single requesting client. GT_DUEL never reaches this
path - it uses DuelScoreboardMessage / "scores_duel" instead. QL therefore never
broadcasts a bare "scores" command for duel, and the cgame scoreboard parser drops
it. The previous trap_SendServerCommand(-1, "scores ...") here was a stray emit
(this function has no callers in ioquakelive), so it has been removed.
==================
*/
void DuelScoreboardMessage_impl(void) {
    // Intentionally empty: see note above. Duel scoreboards go out via
    // DuelScoreboardMessage() as "scores_duel".
}

/*
==================
DuelScoreboardMessage

Full duel scoreboard with per-weapon stats and item timing data.
Per player: 14 base fields + 5*14 weapon fields + 8 item timing fields (via 2 passes).
Builds separate strings for player 1 and player 2 with "self" and "opponent" views.
Address: 0x1003d4a0

Per-weapon fields (weapons 1-14, 5 fields each):
  shotsHit shotsFired accuracy damageDealt numWeaponKills

Base fields per player (14):
  client score ping time frags deaths accuracy bestWeapon damageDone
  impressive excellent gauntlet perfect

Item timing fields (4 categories, 2 fields each):
  redArmorPickups redArmorAvgTime yellowArmorPickups yellowArmorAvgTime
  greenArmorPickups greenArmorAvgTime megaHealthPickups megaHealthAvgTime

Final format: "scores_duel %i %s %s"
  numPlayers player1String player2String
==================
*/
void DuelScoreboardMessage(gentity_t *ent) {
    char weaponString[1024];
    char playerBuf[1024];
    char selfView[1024];
    char oppView[1024];
    char selfView2[1024];
    char oppView2[1024];
    int weaponLen;
    int pass, w;
    int player1, player2;
    gclient_t *cl;
    int numPlayers;
    qboolean showDetail1 = qfalse, showDetail2 = qfalse;

    // Determine the two duel players
    if (level.numPlayingClients == 0) {
        player1 = -1;
        player2 = -1;
    } else if (level.numPlayingClients == 1) {
        player1 = level.sortedClients[0];
        player2 = -1;
    } else {
        // Lower clientNum first
        if (level.sortedClients[0] < level.sortedClients[1]) {
            player1 = level.sortedClients[0];
            player2 = level.sortedClients[1];
        } else {
            player1 = level.sortedClients[1];
            player2 = level.sortedClients[0];
        }
        level.clientNum1stPlayer = player1;
        level.clientNum2ndPlayer = player2;
    }

    // Build scoreboard for player 1
    cl = &level.clients[player1];
    {
        int ping, accuracy, perfect, bestWeapon;
        float redArmorAvg = 0, yellowArmorAvg = 0, greenArmorAvg = 0, megaHealthAvg = 0;
        int redArmorTotal = 0, yellowArmorTotal = 0, greenArmorTotal = 0, megaHealthTotal = 0;

        if (cl->pers.connected == CON_CONNECTING) {
            ping = -1;
        } else {
            ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
        }

        if (cl->accuracy_shots) {
            accuracy = cl->accuracy_hits * 100 / cl->accuracy_shots;
        } else {
            accuracy = 0;
        }
        perfect = (cl->ps.persistant[PERS_RANK] == 0 && cl->ps.persistant[PERS_KILLED] == 0) ? 1 : 0;
        bestWeapon = STAT_GetBestWeapon(cl);

        // Build per-weapon stats string (weapons 1-14)
        weaponString[0] = 0;
        weaponLen = 0;
        for (w = 1; w < 15; w++) {
            char wentry[1024];
            int wj, weapAcc = 0;

            if (cl->expandedStats.shotsHit[w] && cl->expandedStats.shotsFired[w]) {
                weapAcc = cl->expandedStats.shotsHit[w] * 100 / cl->expandedStats.shotsFired[w];
            }
            Com_sprintf(wentry, sizeof(wentry), " %i %i %i %i %i",
                        cl->expandedStats.shotsHit[w],
                        cl->expandedStats.shotsFired[w],
                        weapAcc,
                        cl->expandedStats.damageDealt[w],
                        cl->expandedStats.numWeaponKills[w]);
            wj = strlen(wentry);
            if (weaponLen + wj >= (int)sizeof(weaponString))
                break;
            strcpy(weaponString + weaponLen, wentry);
            weaponLen += wj;
        }

        // Two passes: pass 0 = without item timing, pass 1 = with item timing
        for (pass = 0; pass < 2; pass++) {
            if (pass == 1) {
                // Calculate item timing averages
                redArmorTotal = cl->expandedStats.numRedArmorPickups;
                if (redArmorTotal != cl->expandedStats.numFirstRedArmorPickups &&
                    redArmorTotal - cl->expandedStats.numFirstRedArmorPickups > 0) {
                    int diff = redArmorTotal - cl->expandedStats.numFirstRedArmorPickups;
                    redArmorAvg = ((float)cl->expandedStats.redArmorPickupTime / 1000.0f) / (float)diff;
                }
                yellowArmorTotal = cl->expandedStats.numYellowArmorPickups;
                if (yellowArmorTotal != cl->expandedStats.numFirstYellowArmorPickups &&
                    yellowArmorTotal - cl->expandedStats.numFirstYellowArmorPickups > 0) {
                    int diff = yellowArmorTotal - cl->expandedStats.numFirstYellowArmorPickups;
                    yellowArmorAvg = ((float)cl->expandedStats.yellowArmorPickupTime / 1000.0f) / (float)diff;
                }
                greenArmorTotal = cl->expandedStats.numGreenArmorPickups;
                if (greenArmorTotal != cl->expandedStats.numFirstGreenArmorPickups &&
                    greenArmorTotal - cl->expandedStats.numFirstGreenArmorPickups > 0) {
                    int diff = greenArmorTotal - cl->expandedStats.numFirstGreenArmorPickups;
                    greenArmorAvg = ((float)cl->expandedStats.greenArmorPickupTime / 1000.0f) / (float)diff;
                }
                megaHealthTotal = cl->expandedStats.numMegaHealthPickups;
                if (megaHealthTotal != cl->expandedStats.numFirstMegaHealthPickups &&
                    megaHealthTotal - cl->expandedStats.numFirstMegaHealthPickups > 0) {
                    int diff = megaHealthTotal - cl->expandedStats.numFirstMegaHealthPickups;
                    megaHealthAvg = ((float)cl->expandedStats.megaHealthPickupTime / 1000.0f) / (float)diff;
                }
            }

            // 14 base + 4*2 item timing + weapon string appended
            Com_sprintf(playerBuf, sizeof(playerBuf),
                        "%i %i %i %i %i %i %i %i %i %i %i %i %i "
                        "%i %3.2f %i %3.2f %i %3.2f %i %3.2f%s",
                        player1,
                        cl->ps.persistant[PERS_SCORE], ping, (level.time - cl->pers.enterTime) / 60000,
                        cl->expandedStats.numKills, cl->expandedStats.numDeaths,
                        accuracy, bestWeapon, cl->expandedStats.totalDamageDealt,
                        cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
                        cl->ps.persistant[PERS_EXCELLENT_COUNT],
                        cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
                        perfect,
                        redArmorTotal, (double)redArmorAvg,
                        yellowArmorTotal, (double)yellowArmorAvg,
                        greenArmorTotal, (double)greenArmorAvg,
                        megaHealthTotal, (double)megaHealthAvg,
                        weaponString);

            if (pass == 0) {
                Q_strncpyz(oppView, playerBuf, sizeof(oppView));
            } else {
                Q_strncpyz(selfView, playerBuf, sizeof(selfView));
            }
        }
    }

    // Check if requesting client is player 1 or spectator
    if (ent->client == cl || ent->client->sess.sessionTeam == TEAM_SPECTATOR || level.intermissionTime) {
        showDetail1 = qtrue;
    }

    // Archive player 1's detail view
    if (player1 != -1) {
        if (player1 == level.clientNum1stPlayer) {
            Q_strncpyz(level.scoreboardArchive1, selfView, sizeof(level.scoreboardArchive1));
        } else {
            Q_strncpyz(level.scoreboardArchive2, selfView, sizeof(level.scoreboardArchive2));
        }
    }

    // Build scoreboard for player 2 (if exists)
    if (level.numPlayingClients > 1) {
        cl = &level.clients[player2];
        {
            int ping, accuracy, perfect, bestWeapon;
            float redArmorAvg = 0, yellowArmorAvg = 0, greenArmorAvg = 0, megaHealthAvg = 0;
            int redArmorTotal = 0, yellowArmorTotal = 0, greenArmorTotal = 0, megaHealthTotal = 0;

            if (cl->pers.connected == CON_CONNECTING) {
                ping = -1;
            } else {
                ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
            }

            if (cl->accuracy_shots) {
                accuracy = cl->accuracy_hits * 100 / cl->accuracy_shots;
            } else {
                accuracy = 0;
            }
            perfect = (cl->ps.persistant[PERS_RANK] == 0 && cl->ps.persistant[PERS_KILLED] == 0) ? 1 : 0;
            bestWeapon = STAT_GetBestWeapon(cl);

            weaponString[0] = 0;
            weaponLen = 0;
            for (w = 1; w < 15; w++) {
                char wentry[1024];
                int wj, weapAcc = 0;

                if (cl->expandedStats.shotsHit[w] && cl->expandedStats.shotsFired[w]) {
                    weapAcc = cl->expandedStats.shotsHit[w] * 100 / cl->expandedStats.shotsFired[w];
                }
                Com_sprintf(wentry, sizeof(wentry), " %i %i %i %i %i",
                            cl->expandedStats.shotsHit[w],
                            cl->expandedStats.shotsFired[w],
                            weapAcc,
                            cl->expandedStats.damageDealt[w],
                            cl->expandedStats.numWeaponKills[w]);
                wj = strlen(wentry);
                if (weaponLen + wj >= (int)sizeof(weaponString))
                    break;
                strcpy(weaponString + weaponLen, wentry);
                weaponLen += wj;
            }

            for (pass = 0; pass < 2; pass++) {
                if (pass == 1) {
                    redArmorTotal = cl->expandedStats.numRedArmorPickups;
                    if (redArmorTotal != cl->expandedStats.numFirstRedArmorPickups &&
                        redArmorTotal - cl->expandedStats.numFirstRedArmorPickups > 0) {
                        int diff = redArmorTotal - cl->expandedStats.numFirstRedArmorPickups;
                        redArmorAvg = ((float)cl->expandedStats.redArmorPickupTime / 1000.0f) / (float)diff;
                    }
                    yellowArmorTotal = cl->expandedStats.numYellowArmorPickups;
                    if (yellowArmorTotal != cl->expandedStats.numFirstYellowArmorPickups &&
                        yellowArmorTotal - cl->expandedStats.numFirstYellowArmorPickups > 0) {
                        int diff = yellowArmorTotal - cl->expandedStats.numFirstYellowArmorPickups;
                        yellowArmorAvg = ((float)cl->expandedStats.yellowArmorPickupTime / 1000.0f) / (float)diff;
                    }
                    greenArmorTotal = cl->expandedStats.numGreenArmorPickups;
                    if (greenArmorTotal != cl->expandedStats.numFirstGreenArmorPickups &&
                        greenArmorTotal - cl->expandedStats.numFirstGreenArmorPickups > 0) {
                        int diff = greenArmorTotal - cl->expandedStats.numFirstGreenArmorPickups;
                        greenArmorAvg = ((float)cl->expandedStats.greenArmorPickupTime / 1000.0f) / (float)diff;
                    }
                    megaHealthTotal = cl->expandedStats.numMegaHealthPickups;
                    if (megaHealthTotal != cl->expandedStats.numFirstMegaHealthPickups &&
                        megaHealthTotal - cl->expandedStats.numFirstMegaHealthPickups > 0) {
                        int diff = megaHealthTotal - cl->expandedStats.numFirstMegaHealthPickups;
                        megaHealthAvg = ((float)cl->expandedStats.megaHealthPickupTime / 1000.0f) / (float)diff;
                    }
                }

                Com_sprintf(playerBuf, sizeof(playerBuf),
                            "%i %i %i %i %i %i %i %i %i %i %i %i %i "
                            "%i %3.2f %i %3.2f %i %3.2f %i %3.2f%s",
                            player2,
                            cl->ps.persistant[PERS_SCORE], ping, (level.time - cl->pers.enterTime) / 60000,
                            cl->expandedStats.numKills, cl->expandedStats.numDeaths,
                            accuracy, bestWeapon, cl->expandedStats.totalDamageDealt,
                            cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
                            cl->ps.persistant[PERS_EXCELLENT_COUNT],
                            cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
                            perfect,
                            redArmorTotal, (double)redArmorAvg,
                            yellowArmorTotal, (double)yellowArmorAvg,
                            greenArmorTotal, (double)greenArmorAvg,
                            megaHealthTotal, (double)megaHealthAvg,
                            weaponString);

                if (pass == 0) {
                    Q_strncpyz(oppView2, playerBuf, sizeof(oppView2));
                } else {
                    Q_strncpyz(selfView2, playerBuf, sizeof(selfView2));
                }
            }

            if (ent->client == cl || ent->client->sess.sessionTeam == TEAM_SPECTATOR || level.intermissionTime) {
                showDetail2 = qtrue;
            }

            // Archive player 2's detail view
            if (player2 != -1) {
                Q_strncpyz(level.scoreboardArchive2, selfView2, sizeof(level.scoreboardArchive2));
            }
        }
    }

    numPlayers = level.numPlayingClients < 2 ? level.numPlayingClients : 2;

    // Send the scores
    if (level.intermissionQueued || level.intermissionTime) {
        // During intermission, use archived data
        trap_SendServerCommand(ent - g_entities,
            va("scores_duel 2 %s %s", level.scoreboardArchive1, level.scoreboardArchive2));
    } else if (level.numPlayingClients > 1) {
        trap_SendServerCommand(ent - g_entities,
            va("scores_duel %i %s %s",
               numPlayers,
               showDetail1 ? selfView : oppView,
               showDetail2 ? selfView2 : oppView2));
    } else if (numPlayers > 0) {
        trap_SendServerCommand(ent - g_entities,
            va("scores_duel %i %s", numPlayers, selfView));
    } else {
        trap_SendServerCommand(ent - g_entities,
            va("scores_duel %i %s", numPlayers, ""));
    }
}

/*
=============
AddDuelPlayer

[QL] Duel-only: pull in spectators from the queue when a slot opens. Called every
frame from G_RunFrame. Only runs for GT_DUEL and never while an intermission is
queued/active or the match is paused/timed-out.
.so symbol: AddDuelPlayer   Binary: 0x100557f0 (Ghidra had mislabelled this
"CheckTournament"; the real CheckTournament is the ready countdown below).

Note: the binary also calls G_AutoRecordAndScreenshot(-1) when a player is
pulled in; ioquakelive has no equivalent yet, so that step is skipped.
=============
*/
void AddDuelPlayer(void) {
    int i;
    int numNonSpec;
    int prevPlaying;
    gclient_t *cl;

    if (g_gametype.integer != GT_DUEL) {
        return;
    }
    // Binary guards on level.timePauseBegin (global 0x105dea08, mislabelled
    // "level_restarted"), i.e. the pause/timeout start time - not level.restarted.
    if (level.intermissionQueued || level.intermissionTime || level.timePauseBegin) {
        return;
    }

    // Count connected non-spectators
    numNonSpec = 0;
    for (i = 0; i < level.maxclients; i++) {
        cl = level.clients + i;
        if (cl->pers.connected != CON_DISCONNECTED &&
            cl->sess.sessionTeam != TEAM_SPECTATOR) {
            numNonSpec++;
        }
    }

    // If we already have 2 players, nothing to do
    if (numNonSpec >= 2) {
        return;
    }

    // Pull the next queued spectator in. AddTournamentPlayer sets warmupTime=-1
    // and moves them to TEAM_FREE, but only if the queue had someone.
    prevPlaying = level.numPlayingClients;
    AddTournamentPlayer();
    if (level.numPlayingClients == prevPlaying) {
        // queue empty, nobody came in
        return;
    }

    // A player was added: drop back to PRE_GAME warmup and kill any ready countdown
    SetWarmupState(-1);
    if (level.allReadyTime != 0) {
        level.allReadyTime = 0;
        trap_SetConfigstring(CS_ALLREADY_TIME, va("%i", 0));
    }

    // Clear the ready flag on every connected client and refresh their userinfo.
    // Binary operates on pers.ready (0x2E8), not sess.specOnly.
    for (i = 0; i < level.maxclients; i++) {
        cl = level.clients + i;
        if (cl->pers.connected == CON_CONNECTED) {
            if (cl->pers.ready == 1) {
                cl->pers.ready = 0;
            }
            ClientUserinfoChanged(i);
        }
    }
}

/*
=============
CheckTournament

[QL] Duel ready-start countdown. When exactly one of the two duellists has
readied (numPlayingClients == 2 && numReadyHumans == 1), arm an "allready"
countdown of g_warmupReadyDelay seconds and publish it in CS_ALLREADY_TIME. When
it elapses, g_warmupReadyDelayAction decides the still-not-ready player's fate
(1 = un-ready if ready else force spectate; 2 = force ready).
Binary FUN_10058600 (no dedicated .so symbol - inlined into G_RunFrame on Linux).
Called from CheckWarmupAndForfeit while in PRE_GAME.
=============
*/
void CheckTournament(void) {
    int i;
    gentity_t *ent;
    gclient_t *cl;

    if (g_gametype.integer != GT_DUEL) {
        return;
    }
    if (g_warmupReadyDelay.integer == 0) {
        return;
    }

    if (level.numPlayingClients == 2 && level.numReadyHumans == 1) {
        if (level.allReadyTime != 0) {
            if (level.time <= level.allReadyTime) {
                // Countdown still running.
                return;
            }
            // Countdown elapsed: apply g_warmupReadyDelayAction to the duellists.
            for (i = 0; i < level.maxclients; i++) {
                ent = &g_entities[i];
                if (!ent->client) {
                    continue;
                }
                cl = ent->client;
                if (cl->pers.connected != CON_CONNECTED) {
                    continue;
                }
                if (cl->sess.sessionTeam != TEAM_FREE) {
                    continue;
                }
                if (g_warmupReadyDelayAction.integer == 1) {
                    if (cl->pers.ready == 1) {
                        cl->pers.ready = 0;
                        ClientUserinfoChanged(cl->ps.clientNum);
                    } else {
                        // Binary: SetTeam_Execute(ent, TEAM_SPECTATOR, 0, 0) then sets
                        // gentity+0x36c = 1. SetTeam("s") is the reimpl equivalent and
                        // refreshes userinfo itself; the 0x36c flag is not modelled.
                        SetTeam(ent, "s");
                    }
                } else if (g_warmupReadyDelayAction.integer == 2) {
                    cl->pers.ready = 1;
                }
            }
            level.allReadyTime = 0;
            trap_SetConfigstring(CS_ALLREADY_TIME, va("%i", 0));
            return;
        }
        // Arm the countdown.
        level.allReadyTime = g_warmupReadyDelay.integer * 1000 + level.time;
    } else {
        // Condition no longer holds: cancel any running countdown.
        if (level.allReadyTime == 0) {
            return;
        }
        level.allReadyTime = 0;
    }
    trap_SetConfigstring(CS_ALLREADY_TIME, va("%i", level.allReadyTime));
}
