/*
 * g_gametype_common.c -- Shared gametype functions
 *
 * Functions used by multiple gametypes (CA, AD, etc.)
 */
#include "g_local.h"

/*
============
ClientBegin_RoundBased

[QL] CA/AD begin - spawn behaviour depends on round state:
  RS_WARMUP (0): normal spawn + weapon select
  RS_COUNTDOWN (1): spawn but frozen until the round starts
  round active: put the player in spectator until the next round
Address: 0x10035a10. The .so lists AD_ClientBegin and CA_ClientBegin separately; the
x86 build folded them (identical bodies) into this one function, so it keeps a shared
name rather than a single .so symbol.
============
*/
void ClientBegin_RoundBased(gentity_t* ent) {
    gclient_t* client = ent->client;
    int redAlive, blueAlive;

    if (level.roundState.eCurrent == RS_WARMUP) {
        // pre-round: normal spawn
        ClientSpawn(ent);
        SelectSpawnWeapon(ent);
    } else if (level.roundState.eCurrent == RS_COUNTDOWN) {
        // countdown: spawn but freeze until the round begins
        ClientSpawn(ent);
        SelectSpawnWeapon(ent);
        client->ps.pm_flags |= PMF_FROZEN;
    } else {
        // round active: force to spectator, wait for next round
        client->ps.pm_type = PM_SPECTATOR;
        ClientSpawn(ent);
        Team_LivingTeamCounts(&redAlive, &blueAlive);
        // auto-follow when freecam is off, not on the spectator team, and both teams are alive
        if (!g_teamSpecFreeCam.integer
            && client->sess.sessionTeam != TEAM_SPECTATOR
            && redAlive > 0 && blueAlive > 0) {
            Cmd_FollowCycle_f(ent, 1);
        }
    }
}

/*
============
ClientBegin_RoundBased_impl

[QL] Round-active respawn path for CA/AD (called from ClientRespawn). Same as the
active-round branch of ClientBegin_RoundBased but queues the old body first.
Address: 0x10038260
============
*/
void ClientBegin_RoundBased_impl(gentity_t* ent) {
    gclient_t* client = ent->client;
    int redAlive, blueAlive;

    CopyToBodyQue(ent);
    client->ps.pm_type = PM_SPECTATOR;
    ClientSpawn(ent);
    Team_LivingTeamCounts(&redAlive, &blueAlive);
    if (!g_teamSpecFreeCam.integer
        && client->sess.sessionTeam != TEAM_SPECTATOR
        && redAlive > 0 && blueAlive > 0) {
        Cmd_FollowCycle_f(ent, 1);
    }
}

/*
============
RR_GetRoundState

[QL] Returns the current round state, advancing a scheduled transition if its time has
come. Returns -1 while a transition is pending but not yet due. Shared by RR and the
per-client timer in g_active.c.
Address: 0x100642e0
============
*/
int RR_GetRoundState(void) {
    if (level.roundState.tNext != 0) {
        if (level.time < level.roundState.tNext)
            return -1;
        level.roundState.tNext = 0;
        level.roundState.eCurrent = level.roundState.eNext;
        level.roundState.startTime = level.time;
        RR_RoundStateTransition();
    }
    return level.roundState.eCurrent;
}

/*
============
CheckRoundTimeout

[QL] Returns 1 if the round should end: either team eliminated, or roundtimelimit exceeded.
When exactly one blue is alive at timeout, records that client as rr_lastBlueClient for the
next round. Called from RR_Think.
Address: 0x100646d0
============
*/
int CheckRoundTimeout(int redAlive, int blueAlive) {
    int i;

    if (redAlive != 0 && blueAlive != 0) {
        if (roundtimelimit.integer == 0 ||
            level.time - level.roundState.startTime < roundtimelimit.integer * 1000) {
            return 0;
        }
        // timed out with both teams alive: if one blue left, remember them
        if (blueAlive == 1) {
            for (i = 0; i < level.numPlayingClients; i++) {
                gclient_t* cl = &level.clients[level.sortedClients[i]];
                if (cl->pers.connected == CON_CONNECTED &&
                    cl->sess.sessionTeam == TEAM_BLUE) {
                    rr_lastBlueClient = cl->ps.clientNum;
                    break;
                }
            }
        }
    }
    return 1;
}

/*
==================
STAT_AddPlayerMedalStat / STAT_AddRoundOverStat

[QL] Stats publishing stubs. The original binary uses C++ jsoncpp
for stats; these are no-ops in the C reimplementation.
Shared by CA, RR, DOM gametypes.
==================
*/
void STAT_AddPlayerMedalStat(gentity_t *ent, const char *medal) {
    (void)ent; (void)medal;
}

void STAT_AddRoundOverStat(int round, int winTeam, int isDraw) {
    (void)round; (void)winTeam; (void)isDraw;
}

/*
==================
STAT_AddScore

[QL] 0x10072250. Forwards a per-weapon accuracy event to the engine stats trap
(syscall table +0x324, index 201). The binary is a thin 2-arg fastcall(statValue,
clientNum) wrapper; that engine trap is QL-external (ZMQ stats backend) with no
ioquakelive equivalent, so the emission is a flagged stub. The
warmupTime==0 && !scoringDisabled guard IS faithful. Callers (FireWeapon scoring,
G_ExplodeMissile, G_MissileImpact accuracy paths) pass (ent, weapon, amount) in
reimpl form. Real external wire deferred to the stats backend (Phase 6).
==================
*/
void STAT_AddScore(gentity_t *ent, int weapon, int amount) {
    if (level.warmupTime != 0 || level.scoringDisabled) {
        return;
    }
    (void)ent; (void)weapon; (void)amount;   // external QL stats trap - no-op in standalone
}

/*
==================
STAT_AddPlayerDeathStat

[QL] 0x10072610 (.so STAT_AddPlayerDeathStat; the "STAT_PublishDeath" Ghidra label
is wrong). self = victim, attacker = killer. The ZMQ PLAYER_KILL/PLAYER_DEATH JSON
emit is stubbed (external); the LOCAL expandedStats accumulation below IS in scope.
Called from player_die (replaces its old inline numKills/numDeaths double-counts).
==================
*/
void STAT_AddPlayerDeathStat(gentity_t *self, gentity_t *attacker, int mod) {
    int weapon;

    if (level.scoringDisabled) {
        return;
    }

    // killer side
    weapon = BG_GetWeaponFromMeansOfDeath(mod);
    if (attacker->client && !OnSameTeam(self, attacker) && attacker != self) {
        attacker->client->expandedStats.numKills++;
        attacker->client->expandedStats.numWeaponKills[weapon]++;
        attacker->client->expandedStats.killStreak++;
    }

    // victim side
    if (mod != MOD_SWITCH_TEAMS) {
        self->client->expandedStats.numDeaths++;
        weapon = BG_GetWeaponFromMeansOfDeath(mod);
        if (weapon != 0) {
            self->client->expandedStats.numWeaponDeaths[weapon]++;
        }
    }

    // the victim's own kill streak ends on death
    if (self->client->expandedStats.maxKillStreak < self->client->expandedStats.killStreak) {
        self->client->expandedStats.maxKillStreak = self->client->expandedStats.killStreak;
    }
    self->client->expandedStats.killStreak = 0;
}

/*
==================
STAT_AddDamageStat

[QL] 0x100714b0 (.so STAT_AddDamageStat; the "STAT_PublishDamage" Ghidra label is
wrong). targ = victim, attacker = dealer. The ZMQ emit is stubbed; the LOCAL
per-weapon damage-dealt/taken accumulation below IS in scope. Called from G_Damage.
Attacker (dealt) uses RAW damage; victim (taken) clamps to remaining health+armour.
==================
*/
void STAT_AddDamageStat(gentity_t *targ, gentity_t *attacker, int mod, int damage) {
    int weapon;

    if (attacker == targ) {
        return;
    }

    weapon = BG_GetWeaponFromMeansOfDeath(mod);

    // victim (taken) side - clamped to remaining hp+armour
    if (targ->client && (attacker->client == NULL || !OnSameTeam(targ, attacker))) {
        int clamped = damage;
        int cap = targ->client->ps.stats[STAT_HEALTH] + targ->client->ps.stats[STAT_ARMOR];
        if (cap < clamped) {
            clamped = cap;
        }
        targ->client->expandedStats.totalDamageTaken += clamped;
        if (weapon != 0) {
            targ->client->expandedStats.damageTaken[weapon] += clamped;
        }
        targ->client->expandedStats.previousHealth = targ->client->ps.stats[STAT_HEALTH];
        targ->client->expandedStats.previousArmor = targ->client->ps.stats[STAT_ARMOR];
    }

    // attacker (dealt) side - raw damage
    if (weapon != 0 && attacker && attacker->client && targ->client &&
        (targ->health > 0 || g_instaGib.integer) && !OnSameTeam(targ, attacker)) {
        attacker->client->expandedStats.totalDamageDealt += damage;
        attacker->client->expandedStats.damageDealt[weapon] += damage;
    }
}

/*
==================
LastManStanding

[QL] Binary: 0x1006b200
Plays GTS_LAST_STANDING sound event and sends g_lastManStandingMessage
as a centerprint to the sole survivor on the given team.
Used by CA and RR gametypes.
==================
*/
void LastManStanding(int team) {
    int i;
    for (i = 0; i < level.maxclients; i++) {
        gclient_t *cl = &level.clients[i];
        if (cl->pers.connected != CON_CONNECTED) continue;
        if (cl->ps.pm_type != PM_NORMAL) continue;
        if (cl->sess.sessionTeam != team) continue;

        {
            gentity_t *te = G_TempEntity(vec3_origin, EV_GLOBAL_TEAM_SOUND);
            te->r.svFlags |= SVF_BROADCAST;
            te->s.eventParm = GTS_LAST_STANDING;  // 0x13 = 19
            // [QL] GTS team travels in modelindex2 (es 0xac), not otherEntityNum.
            te->s.modelindex2 = cl->sess.sessionTeam;
            trap_SendServerCommand(i,
                va("cp \"%s\n\"", g_lastManStandingMessage.string));
        }
        return;
    }
}

/*
==================
G_ObfuscateEnemyInfoInSnapshotCheck

[QL] Binary: 0x10052e80
Returns qtrue if clientNum2's info should be VISIBLE to clientNum1.
The engine strips position data when this returns qfalse.
Return qtrue = allow visibility (don't strip), qfalse = obfuscate (strip position).

Matches binary exactly. Only meaningful for team gametypes (gametype >= 3).
FFA/duel always return qtrue (no obfuscation).
==================
*/
qboolean G_ObfuscateEnemyInfoInSnapshotCheck(int clientNum1, int clientNum2) {
    if (clientNum1 >= MAX_CLIENTS || clientNum2 >= MAX_CLIENTS)
        return qtrue;  // out of range - don't strip

    // FFA and duel: no obfuscation
    if (g_gametype.integer < GT_TEAM)
        return qtrue;

    // 1FCTF: flag carrier is always visible to everyone
    if (g_gametype.integer == GT_1FCTF) {
        if (g_entities[clientNum2].s.powerups & (1 << PW_NEUTRALFLAG))
            return qtrue;
    }

    // Team games: same team always visible
    if (level.clients[clientNum1].sess.sessionTeam ==
        level.clients[clientNum2].sess.sessionTeam)
        return qtrue;

    // Spectators always see everyone
    if (level.clients[clientNum1].sess.sessionTeam == TEAM_SPECTATOR)
        return qtrue;

    // RR Infected: enemies are also visible (infection needs position tracking)
    if (g_gametype.integer == GT_RR && g_rrInfected.integer != 0)
        return qtrue;

    // Default for team games: obfuscate enemy positions
    return qfalse;
}

/*
==================
SendScoreboardMessageToTeam

Sends the current scoreboard to all connected clients on a given team.
The actual scoreboard content should have been prepared via va() before calling.
Address: 0x10067f80
==================
*/
/*
==================
STAT_GetBestWeapon

[QL] Returns the weapon index with the highest damage dealt for a player.
Falls back to the highest weapon in their loadout if no damage dealt.
Address: 0x1007b820
==================
*/
int STAT_GetBestWeapon(gclient_t *cl) {
    int bestWeapon = 0;
    int bestDamage = 0;
    int weapon;
    int fallbackWeapon = 1;

    // Find highest weapon in loadout
    for (weapon = 2; weapon < WP_NUM_WEAPONS; weapon++) {
        if (cl->ps.stats[STAT_WEAPONS] & (1 << (weapon - 1))) {
            fallbackWeapon = weapon;
        }
    }

    // Find weapon with most damage dealt
    for (weapon = 1; weapon < WP_NUM_WEAPONS; weapon++) {
        if (cl->expandedStats.damageDealt[weapon] > bestDamage) {
            bestDamage = cl->expandedStats.damageDealt[weapon];
            bestWeapon = weapon;
        }
    }

    if (bestWeapon == 0) {
        bestWeapon = fallbackWeapon;
    }
    return bestWeapon;
}

void SendScoreboardMessageToTeam(int team) {
    int i;
    gclient_t *cl;

    for (i = 0; i < level.maxclients; i++) {
        cl = &level.clients[i];
        if (cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam == team) {
            trap_SendServerCommand(i, NULL);  // sends last va() result
        }
    }
}
