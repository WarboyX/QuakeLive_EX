/*
 * g_gametype_tdm.c -- Team Deathmatch (GT_TEAM, 3)
 *
 * Team-based fraglimit/timelimit mode. Team scoring via kills.
 * Scoreboard: TeamDeathmatchScoreboardMessage, TeamDeathmatchStatisticsMessage (below).
 */
#include "g_local.h"

// TDM uses fraglimit/timelimit exit rules + mercylimit.
// Team balance: g_teamForceBalance, g_teamSizeMin, g_teamsize.
// No round state.

/*
==================
TeamDeathmatchScoreboardMessage

TDM scoreboard. Includes team item pickup stats (14 categories * 2 teams = 28 header values).
Hides opponent team's item stats unless spectating or at intermission.
15 fields per player.
Address: 0x1003df10

In the binary this is a *builder*: it returns the
"scores_tdm" string (char *) via va() and returns NULL on overflow; the
trap_SendServerCommand is done by the caller (Cmd_Score), which also
applies the `strlen(msg) < 1024 && !level.scoringDisabled` gate and falls back
to FreeForAllScoreboardMessage otherwise. ioquakelive uses a self-consistent
send-internally model (this fn is void and sends), so that gate/fallback is not
reproduced here.

Header (31 values before player data):
  redArmor[RED] redArmor[BLUE] yellowArmor[R] yellowArmor[B] greenArmor[R] greenArmor[B]
  megaHealth[R] megaHealth[B] quad[R] quad[B] battleSuit[R] battleSuit[B]
  regen[R] regen[B] haste[R] haste[B] invis[R] invis[B]
  quadTime[R] quadTime[B] battleSuitTime[R] battleSuitTime[B]
  regenTime[R] regenTime[B] hasteTime[R] hasteTime[B] invisTime[R] invisTime[B]
  numPlayers teamScoreRed teamScoreBlue

Fields per player (15):
  client team score ping time frags deaths accuracy bestWeapon
  impressive excellent gauntlet teamKills teamKilled damageDone
==================
*/
void TeamDeathmatchScoreboardMessage(gentity_t *ent) {
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

    // Load team item stats (14 categories)
    // Order: redArmor, yellowArmor, greenArmor, megaHealth, quad, battleSuit,
    //        regen, haste, invis, quadTime, battleSuitTime, regenTime, hasteTime, invisTime
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

    // Hide opponent team's stats (unless spectating or intermission).
    // NOTE (byte-faithful): the binary zeroes only 13 of the 14 categories:
    // index 8 (numInvisibilityPickups) of the hidden team is left visible.
    // Both the RED-viewer and BLUE-viewer branches skip [8] (original QL bug).
    //
    // [QL] Hoisted out of the player loop, where it used to run once per player.
    // The viewer does not change between iterations and zeroing is idempotent,
    // so the values sent are the same; the header is now built once, before the
    // entries, because its length sets the chunk budget.
    viewerTeam = ent->client->sess.sessionTeam;
    if (viewerTeam == TEAM_RED && level.intermissionTime == 0) {
        for (j = 0; j < 14; j++) {
            if (j != 8)
                blueStats[j] = 0;
        }
    } else if (viewerTeam == TEAM_BLUE && level.intermissionTime == 0) {
        for (j = 0; j < 14; j++) {
            if (j != 8)
                redStats[j] = 0;
        }
    }

    /*
    [QL] The twenty-eight team totals, without the count and team scores that
    follow them - those are appended at send time, once the chunk knows how many
    entries it carries. Some of these are possession times in milliseconds, so
    the header can run to a couple of hundred bytes and the entry budget has to
    be measured against it rather than assumed.
    */
    Com_sprintf(header, sizeof(header),
                "scores_tdm %i %i %i %i %i %i %i %i %i %i %i %i %i %i "
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

        cl = &level.clients[level.sortedClients[i]];

        if (cl->accuracy_shots) {
            accuracy = cl->accuracy_hits * 100 / cl->accuracy_shots;
        } else {
            accuracy = 0;
        }

        if (cl->pers.connected == CON_CONNECTING) {
            ping = -1;
        } else {
            ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
        }

        bestWeapon = STAT_GetBestWeapon(cl);

        // 15 fields per player
        Com_sprintf(entry, sizeof(entry), " %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i",
                    level.sortedClients[i],
                    cl->sess.sessionTeam,
                    cl->ps.persistant[PERS_SCORE], ping,
                    (level.time - cl->pers.enterTime) / 60000,
                    cl->expandedStats.numKills, cl->expandedStats.numDeaths,
                    accuracy, bestWeapon,
                    cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
                    cl->ps.persistant[PERS_EXCELLENT_COUNT],
                    cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
                    cl->expandedStats.numTeamKills,
                    cl->expandedStats.numTeamKilled,
                    cl->expandedStats.totalDamageDealt);
        j = strlen(entry);

        // [QL] see the comment in g_gametype_ffa.c: flush a chunk rather than
        // stop at the first message. The first one keeps the original
        // "scores_tdm" shape so a stock client is short but correct;
        // "scores_tdm2 <startIndex> <count>" carries the rest and a stock
        // client has no handler for it.
        if (stringlength + j >= budget && numInChunk > 0) {
            if (firstChunk) {
                trap_SendServerCommand(ent - g_entities,
                                       va("%s %i %i %i%s", header, numInChunk,
                                          level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], string));
                firstChunk = qfalse;
            } else {
                trap_SendServerCommand(ent - g_entities,
                                       va("scores_tdm2 %i %i%s", chunkStart, numInChunk, string));
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
                               va("scores_tdm2 %i %i%s", chunkStart, numInChunk, string));
    }
}

/*
==================
TeamDeathmatchStatisticsMessage

TDM detail stats sent per-player as "tdmstats" command.
11 fields per player: suicides teamKills teamKilled damageDone damageTaken
  redArmorPickups yellowArmorPickups greenArmorPickups megaHealthPickups
  quadPickups battleSuitPickups
Address: 0x1003e3a0
==================
*/
void TeamDeathmatchStatisticsMessage(gentity_t *ent) {
    char entry[1024];
    char string[1024];
    int i;
    gclient_t *cl;

    for (i = 0; i < level.numConnectedClients; i++) {
        cl = &level.clients[level.sortedClients[i]];

        string[0] = 0;
        Com_sprintf(entry, sizeof(entry),
                    " %i %i %i %i %i %i %i %i %i %i %i",
                    cl->expandedStats.numSuicides,
                    cl->expandedStats.numTeamKills,
                    cl->expandedStats.numTeamKilled,
                    cl->expandedStats.totalDamageDealt,
                    cl->expandedStats.totalDamageTaken,
                    cl->expandedStats.numRedArmorPickups,
                    cl->expandedStats.numYellowArmorPickups,
                    cl->expandedStats.numGreenArmorPickups,
                    cl->expandedStats.numMegaHealthPickups,
                    cl->expandedStats.numQuadDamagePickups,
                    cl->expandedStats.numBattleSuitPickups);
        if (strlen(entry) > sizeof(string) - 1)
            break;      // byte-faithful: overflow stops the loop (no send for this
                        // or any later player); binary breaks, does not skip-and-send.
        strcpy(string, entry);
        trap_SendServerCommand(ent - g_entities,
            va("tdmstats %i%s", i, string));
    }
}
