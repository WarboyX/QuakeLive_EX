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
//
// g_combat.c

#include "g_local.h"

/*
============
ScorePlum
============
*/
void ScorePlum(gentity_t* ent, vec3_t origin, int score) {
    gentity_t* plum;

    plum = G_TempEntity(origin, EV_SCOREPLUM);
    // only send this temp entity to a single client
    plum->r.svFlags |= SVF_SINGLECLIENT;
    plum->r.singleClient = ent->s.number;
    //
    plum->s.otherEntityNum = ent->s.number;
    plum->s.time = score;
}

/*
============
DamagePlum

[QL] non-shotgun floating damage number (binary 0x10046680). Spawns an
EV_DAMAGEPLUM over the victim (z + 32) carrying the amount in s.time, the
attacker's clientNum (the recipient of the number) in s.clientNum, and the hit
weapon in s.generic1. Delivered single-client unless g_damagePlums == 2 (then
broadcast). Shotgun damage is instead accumulated and flushed once per blast.
============
*/
static void DamagePlum(gentity_t *attacker, gentity_t *victim, int damage, int mod) {
    gentity_t *te;
    vec3_t org;

    VectorCopy(victim->r.currentOrigin, org);
    org[2] += 32.0f;

    te = G_TempEntity(org, EV_DAMAGEPLUM);
    te->r.svFlags |= (g_damagePlums.integer != 2) ? SVF_SINGLECLIENT : SVF_BROADCAST;
    te->r.singleClient = attacker->client->ps.clientNum;
    te->s.time = damage;
    te->s.clientNum = attacker->client->ps.clientNum;
    te->s.generic1 = BG_GetWeaponFromMeansOfDeath(mod);
}

/*
============
AddScore

Adds score to both the client and his team
============
*/
void AddScore( gentity_t *ent, vec3_t origin, int score ) {
	if ( !ent->client ) {
		return;
	}
	// [QL] Additional guards matching binary
	if ( level.warmupTime ) {
		return;
	}
	if ( level.gameStatsReported ) {
		return;
	}
	if ( level.intermissionQueued || level.intermissionTime ) {
		return;
	}
	if ( g_training.integer && !(ent->r.svFlags & SVF_BOT) ) {
		return;  // only bots score in training mode
	}
	if ( g_gametype.integer == GT_RACE ) {
		return;  // no scoring in race mode
	}
	if ( level.scoringDisabled ) {
		return;
	}

	// [QL] GT_RR: negative score clamping
	if ( g_gametype.integer == GT_RR && !g_rrAllowNegativeScores.integer ) {
		if ( ent->client->ps.persistant[PERS_SCORE] + score < 0 ) {
			score = -ent->client->ps.persistant[PERS_SCORE];
		}
	}

	ScorePlum( ent, origin, score );
	ent->client->ps.persistant[PERS_SCORE] += score;

	// [QL] binary AddScore updates the team score via AddTeamScore only for TDM
	// (gametype == GT_TEAM); CTF/1FCTF/HARV/DOM maintain level.teamScores through
	// their own capture/point paths. The old ">= GT_TEAM" test folded individual
	// bonuses into those modes' team scores, inflating them.
	if ( g_gametype.integer == GT_TEAM ) {
		AddTeamScore( origin, ent->client->ps.persistant[PERS_TEAM], score );
	}

	CalculateRanks();
}

/*
=================
TossClientItems

Toss the weapon and powerups for the killed player
=================
*/
void TossClientItems(gentity_t* self) {
    gitem_t* item;
    int weapon;
    float angle;
    int i;
    gentity_t* drop;

    /*
    [QL] No weapon drops in instagib.

    Instagib gives everyone the same weapon and infinite ammo, so a dropped one
    is worth nothing to pick up - it just litters the map with railguns and
    gives the pickup sound and the item timers something to report that does not
    matter. The A2M modes are all instagib and all had this.

    Gated on the mode rather than on a new cvar: a server that hands out a fixed
    loadout and no ammo pickups has already decided this question, and a cvar
    nobody sets is a cvar nobody reads (see E8). Powerups below still drop -
    those are still worth taking.
    */
    if (g_instaGib.integer || (g_dmflags.integer & DF_INSTAGIB)) {
        goto dropPowerups;
    }

    // drop the weapon if not a gauntlet or machinegun
    weapon = self->s.weapon;

    // make a special check to see if they are changing to a new
    // weapon that isn't the mg or gauntlet.  Without this, a client
    // can pick up a weapon, be killed, and not drop the weapon because
    // their weapon change hasn't completed yet and they are still holding the MG.
    if (weapon == WP_MACHINEGUN || weapon == WP_GRAPPLING_HOOK) {
        if (self->client->ps.weaponstate == WEAPON_DROPPING) {
            weapon = self->client->pers.cmd.weapon;
        }
        if (!(self->client->ps.stats[STAT_WEAPONS] & (1 << weapon))) {
            weapon = WP_NONE;
        }
    }

    if (weapon > WP_MACHINEGUN && weapon != WP_GRAPPLING_HOOK &&
        self->client->ps.ammo[weapon]) {
        // find the item type for this weapon
        item = BG_FindItemForWeapon(weapon);

        // spawn the item
        Drop_Item(self, item, 0);
    }

dropPowerups:
    // drop all the powerups if not in teamplay
    if (g_gametype.integer != GT_TEAM) {
        angle = 45;
        for (i = 1; i < PW_NUM_POWERUPS; i++) {
            if (self->client->ps.powerups[i] > level.time) {
                item = BG_FindItemForPowerup(i);
                if (!item) {
                    continue;
                }
                drop = Drop_Item(self, item, angle);
                // decide how many seconds it has left
                drop->count = (self->client->ps.powerups[i] - level.time) / 1000;
                if (drop->count < 1) {
                    drop->count = 1;
                }
                angle += 45;
            }
        }
    }
}

/*
=================
TossClientCubes
=================
*/
extern gentity_t* neutralObelisk;

void TossClientCubes(gentity_t* self) {
    gitem_t* item;
    gentity_t* drop;
    vec3_t velocity;
    vec3_t angles;
    vec3_t origin;

    self->client->ps.generic1 = 0;

    // this should never happen but we should never
    // get the server to crash due to skull being spawned in
    if (!G_EntitiesFree()) {
        return;
    }

    if (self->client->sess.sessionTeam == TEAM_RED) {
        item = BG_FindItem("Red Cube");
    } else {
        item = BG_FindItem("Blue Cube");
    }

    angles[YAW] = (float)(level.time % 360);
    angles[PITCH] = 0;  // always forward
    angles[ROLL] = 0;

    AngleVectors(angles, velocity, NULL, NULL);
    VectorScale(velocity, 150, velocity);
    velocity[2] += 200 + crandom() * 50;

    if (neutralObelisk) {
        VectorCopy(neutralObelisk->s.pos.trBase, origin);
        origin[2] += 44;
    } else {
        VectorClear(origin);
    }

    drop = LaunchItem(item, origin, velocity);

    drop->nextthink = level.time + g_cubeTimeout.integer * 1000;
    drop->think = G_FreeEntity;
    drop->spawnflags = self->client->sess.sessionTeam;
}

/*
=================
TossClientPersistantPowerups
=================
*/
void TossClientPersistantPowerups(gentity_t* ent) {
    gentity_t* powerup;

    if (!ent->client) {
        return;
    }

    if (!ent->client->persistantPowerup) {
        return;
    }

    powerup = ent->client->persistantPowerup;

    powerup->r.svFlags &= ~SVF_NOCLIENT;
    powerup->s.eFlags &= ~EF_NODRAW;
    powerup->r.contents = CONTENTS_TRIGGER;
    trap_LinkEntity(powerup);

    ent->client->ps.stats[STAT_PERSISTANT_POWERUP] = 0;
    ent->client->persistantPowerup = NULL;
}

/*
==================
LookAtKiller
==================
*/
void LookAtKiller(gentity_t* self, gentity_t* inflictor, gentity_t* attacker) {
    vec3_t dir;

    if (attacker && attacker != self) {
        VectorSubtract(attacker->s.pos.trBase, self->s.pos.trBase, dir);
    } else if (inflictor && inflictor != self) {
        VectorSubtract(inflictor->s.pos.trBase, self->s.pos.trBase, dir);
    } else {
        self->client->ps.stats[STAT_DEAD_YAW] = self->s.angles[YAW];
        return;
    }

    self->client->ps.stats[STAT_DEAD_YAW] = vectoyaw(dir);
}

/*
==================
GibEntity
==================
*/
void GibEntity(gentity_t* self, int killer) {
    gentity_t* ent;
    int i;

    // if this entity still has kamikaze
    if (self->s.eFlags & EF_KAMIKAZE) {
        // check if there is a kamikaze timer around for this owner
        for (i = 0; i < level.num_entities; i++) {
            ent = &g_entities[i];
            if (!ent->inuse)
                continue;
            if (ent->activator != self)
                continue;
            if (strcmp(ent->classname, "kamikaze timer"))
                continue;
            G_FreeEntity(ent);
            break;
        }
    }
    G_AddEvent(self, EV_GIB_PLAYER, killer);
    self->takedamage = qfalse;
    self->s.eType = ET_INVISIBLE;
    self->r.contents = 0;
}

/*
==================
body_die
==================
*/
void body_die(gentity_t* self, gentity_t* inflictor, gentity_t* attacker, int damage, int meansOfDeath) {
    if (self->health > GIB_HEALTH) {
        return;
    }
    // [QL] Binary always gibs when health drops below threshold; client decides
    // whether to render blood (cg_gibs/cg_blood). Removed Q3 server-side com_blood.
    GibEntity(self, 0);
}

// these are just for logging, the client prints its own messages
char* modNames[] = {
    "MOD_UNKNOWN",
    "MOD_SHOTGUN",
    "MOD_GAUNTLET",
    "MOD_MACHINEGUN",
    "MOD_GRENADE",
    "MOD_GRENADE_SPLASH",
    "MOD_ROCKET",
    "MOD_ROCKET_SPLASH",
    "MOD_PLASMA",
    "MOD_PLASMA_SPLASH",
    "MOD_RAILGUN",
    "MOD_LIGHTNING",
    "MOD_BFG",
    "MOD_BFG_SPLASH",
    "MOD_WATER",
    "MOD_SLIME",
    "MOD_LAVA",
    "MOD_CRUSH",
    "MOD_TELEFRAG",
    "MOD_FALLING",
    "MOD_SUICIDE",
    "MOD_TARGET_LASER",
    "MOD_TRIGGER_HURT",
    "MOD_NAIL",
    "MOD_CHAINGUN",
    "MOD_PROXIMITY_MINE",
    "MOD_KAMIKAZE",
    "MOD_JUICED",
    "MOD_GRAPPLE",
    // [QL] MOD 29-33 added by Quake Live (binary: PTR_s_MOD_UNKNOWN_1008fd58, 34 entries)
    "MOD_SWITCHTEAM",
    "MOD_THAW",
    "MOD_LIGHTNING_DISCHARGE",
    "MOD_HMG",
    "MOD_RAILGUN_HEADSHOT"};

/*
==================
Kamikaze_DeathActivate
==================
*/
void Kamikaze_DeathActivate(gentity_t* ent) {
    G_StartKamikaze(ent);
    G_FreeEntity(ent);
}

/*
==================
Kamikaze_DeathTimer
==================
*/
void Kamikaze_DeathTimer(gentity_t* self) {
    gentity_t* ent;

    ent = G_Spawn();
    ent->classname = "kamikaze timer";
    VectorCopy(self->s.pos.trBase, ent->s.pos.trBase);
    ent->r.svFlags |= SVF_NOCLIENT;
    ent->think = Kamikaze_DeathActivate;
    ent->nextthink = level.time + 5 * 1000;

    ent->activator = self;
}

/*
==================
CheckAlmostCapture
==================
*/
void CheckAlmostCapture(gentity_t* self, gentity_t* attacker) {
    gentity_t* ent;
    vec3_t dir;
    char* classname;

    // if this player was carrying a flag
    if (self->client->ps.powerups[PW_REDFLAG] ||
        self->client->ps.powerups[PW_BLUEFLAG] ||
        self->client->ps.powerups[PW_NEUTRALFLAG]) {
        // get the goal flag this player should have been going for
        // [QL] GT_AD carries flags like CTF; head for your own team's flag stand
        if (g_gametype.integer == GT_CTF || g_gametype.integer == GT_AD) {
            if (self->client->sess.sessionTeam == TEAM_BLUE) {
                classname = "team_CTF_blueflag";
            } else {
                classname = "team_CTF_redflag";
            }
        } else {
            if (self->client->sess.sessionTeam == TEAM_BLUE) {
                classname = "team_CTF_redflag";
            } else {
                classname = "team_CTF_blueflag";
            }
        }
        ent = NULL;
        do {
            ent = G_Find(ent, FOFS(classname), classname);
        } while (ent && (ent->flags & FL_DROPPED_ITEM));
        // if we found the destination flag and it's not picked up
        // [QL] binary tests s.eFlags & EF_NODRAW, not svFlags
        if (ent && !(ent->s.eFlags & EF_NODRAW)) {
            // if the player was *very* close
            VectorSubtract(self->client->ps.origin, ent->s.origin, dir);
            if (VectorLength(dir) < 200) {
                self->client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_HOLYSHIT;
                if (attacker->client) {
                    attacker->client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_HOLYSHIT;
                    // [QL] numHolyShits: killer denied an enemy carrier near the objective
                    if (self->client->sess.sessionTeam != attacker->client->sess.sessionTeam) {
                        attacker->client->expandedStats.numHolyShits++;
                        if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x18) &&
                            attacker->client->expandedStats.numHolyShits > 1) {
                            trap_SetAchievement(attacker->client->ps.clientNum, 0x18);
                        }
                    }
                }
            }
        }
    }
}

/*
==================
CheckAlmostScored
==================
*/
void CheckAlmostScored(gentity_t* self, gentity_t* attacker) {
    gentity_t* ent;
    vec3_t dir;
    char* classname;

    // if the player was carrying cubes
    if (self->client->ps.generic1) {
        if (self->client->sess.sessionTeam == TEAM_BLUE) {
            classname = "team_redobelisk";
        } else {
            classname = "team_blueobelisk";
        }
        ent = G_Find(NULL, FOFS(classname), classname);
        // if we found the destination obelisk
        if (ent) {
            // if the player was *very* close
            VectorSubtract(self->client->ps.origin, ent->s.origin, dir);
            if (VectorLength(dir) < 200) {
                self->client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_HOLYSHIT;
                if (attacker->client) {
                    attacker->client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_HOLYSHIT;
                    // [QL] numHolyShits: killer denied an enemy cube-carrier near the obelisk
                    if (self->client->sess.sessionTeam != attacker->client->sess.sessionTeam) {
                        attacker->client->expandedStats.numHolyShits++;
                    }
                }
            }
        }
    }
}

// [QL] forward decl - binary sets the EF_TICKING activator's think to SP_info_null
// (frees on next think), not G_FreeEntity. Defined in g_misc.c.
void SP_info_null(gentity_t* self);

/*
==================
PlayerAwardEV
[QL] .so PlayerAwardEV (0x6f990) / Win 0x10046730. Broadcasts an EV_AWARD temp entity
for a medal earned by 'self'. 'award' is the award_t subtype (0..9) carried in s.generic1;
the localPersistant slot is (award+1) (localPersistant[0] is never an award slot).
==================
*/
void PlayerAwardEV(gentity_t* self, int award) {
    gentity_t* te;
    int idx = award + 1;

    self->client->ps.localPersistant[idx]++;
    te = G_TempEntity(self->client->ps.origin, EV_AWARD);
    te->r.svFlags |= SVF_BROADCAST;
    te->s.clientNum = self->client->ps.clientNum;
    te->s.generic1 = award;
    te->s.modelindex2 = self->client->ps.localPersistant[idx];
    self->client->rewardTime = level.time + REWARD_SPRITE_TIME;
}

/*
==================
PlayerAwardEF
[QL] .so PlayerAwardEF (0x6fa20) / Win 0x100467b0. Sets a persistent EF_AWARD_* sprite bit
and increments ps.persistant[persIdx]. Clears the six switchable award bits (mask ~0x38848);
EF_AWARD_DENIED is NOT cleared.
==================
*/
void PlayerAwardEF(gentity_t* self, int persIdx, int efBit) {
    self->client->ps.persistant[persIdx]++;
    self->client->ps.eFlags &= ~(EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_CAP |
                                 EF_AWARD_IMPRESSIVE | EF_AWARD_DEFEND | EF_AWARD_ASSIST);
    self->client->ps.eFlags |= efBit;
    self->client->rewardTime = level.time + REWARD_SPRITE_TIME;
}

/*
[QL] Is the match young enough that a death should not be held against you?

The first seconds of a round are the ones nobody controls: everybody arrives at
once, spawn points are crowded, and a telefrag or a fall taken before you have a
weapon in hand is not a mistake anyone made. Scoring those costs a player a
frag and a death for being unlucky about where the game put them.

g_matchStartGrace is that window in seconds; 0 turns it off. It suppresses the
-1 for a suicide or a world death and the death that goes on the player's own
record; the killer of a genuine frag still scores normally, because that is a
real kill and not an artefact of the start.

The window runs from level.graceEndTime, which G_InitGame sets on load and
SetWarmupState sets again the moment the game state becomes IN_PROGRESS.
Measuring it off level.startTime alone did not work: a server that sits in
PRE_GAME while people warm up and frag each other - which is most of them - had
spent the ten seconds long before the match began. Both moments arm it now,
and warmup is no longer excluded, because a server whose scores are ticking
over in PRE_GAME is scoring whatever the state machine calls it.
==================
*/
static qboolean G_InMatchStartGrace(void) {
    if (g_matchStartGrace.integer <= 0) {
        return qfalse;
    }
    return level.time < level.graceEndTime;
}

/*
==================
player_die
==================
*/
void player_die(gentity_t* self, gentity_t* inflictor, gentity_t* attacker, int damage, int meansOfDeath) {
    gentity_t* ent;
    gentity_t* te;
    int anim;
    int contents;
    int killer;
    int i;
    char *killerName, *obit;
    qboolean isLiveFreeze;

    // 1. prologue guard (0x100473e8)
    if (self->client->ps.pm_type == PM_DEAD || self->client->ps.pm_type == PM_FREEZE) {
        return;
    }
    if (level.intermissionQueued || level.intermissionTime) {
        return;
    }

    // 2. bot humour (training mode)
    if ((self->r.svFlags & SVF_BOT) && (g_training.integer == 1 || g_training.integer == 2)) {
        // [QL] binary uses syscall +0x15c (force client console command); reimpl approximates
        // with trap_SendServerCommand (imprecise, cosmetic). TODO: exact bot force-command trap.
        trap_SendServerCommand(self->client->ps.clientNum,
            "say Oops! Um... I meant to do that! Just don't you do it, ok?");
    }

    // 3. almost-capture / almost-score
    CheckAlmostCapture(self, attacker);
    CheckAlmostScored(self, attacker);

    // 4. release own grapple
    if (self->client->hook) {
        Weapon_HookFree(self->client->hook);
    }

    // 5. release enemies grappled to self
    for (i = 0; i < level.maxclients; i++) {
        gclient_t* cl = &level.clients[i];
        if (cl->pers.connected != CON_CONNECTED) {
            continue;
        }
        if (cl->sess.sessionTeam == TEAM_SPECTATOR) {
            continue;
        }
        if (cl->hook && cl->hook->enemy == self) {
            Weapon_HookFree(cl->hook);
        }
    }

    // 6. EF_TICKING (prox/kamikaze bomb) cleanup
    if ((self->client->ps.eFlags & EF_TICKING) && self->activator) {
        self->client->ps.eFlags &= ~EF_TICKING;
        self->activator->think = SP_info_null;  // [QL] binary: SP_info_null, not G_FreeEntity
        self->activator->nextthink = level.time;
    }

    /*
    7. set pm_type (Freeze-Tag fork)

    [QL] This used to set pm_type and thawtime by hand and stop there, which is
    not enough to freeze anybody. What marks a frozen player is
    ps.powerups[PW_FREEZE]: pmove treats a zero-health player as dead unless it
    is set (bg_pmove.c), ClientThink_real keeps PM_FREEZE instead of PM_DEAD on
    it (g_active.c), Freeze_ClientThawCheck counts down against it, and
    Freeze_DeathFinalize refuses to respawn without it. None of that was ever
    reached, so players in Freeze Tag simply died.

    Freeze_PlayerFrozen sets all of it - and it was defined in g_gametype_ft.c
    with no callers anywhere in the tree. Same shape as the unassigned ice
    handles and the registered-but-unread cvars: written, never wired.

    It declines to freeze outside a live round (RS_PLAYING), so a death during
    warmup still falls through to PM_DEAD and an ordinary respawn, which is what
    warmup should do.
    */
    if (!level.intermissionTime && !level.intermissionQueued &&
        g_gametype.integer == GT_FREEZE && meansOfDeath != MOD_SWITCH_TEAMS) {
        Freeze_PlayerFrozen(self);
    }
    if (self->client->ps.powerups[PW_FREEZE] == 0) {
        self->client->ps.pm_type = PM_DEAD;
    }

    // 8. resolve killer / obituary string
    if (attacker) {
        killer = attacker->s.number;
        if (attacker->client) {
            killerName = attacker->client->pers.netname;
        } else {
            killerName = "<non-client>";
        }
    } else {
        killer = ENTITYNUM_WORLD;
        killerName = "<world>";
    }
    if (killer < 0 || killer >= MAX_CLIENTS) {
        killer = ENTITYNUM_WORLD;
        killerName = "<world>";
    }
    if (meansOfDeath < 0 || meansOfDeath >= ARRAY_LEN(modNames)) {
        obit = "<bad obituary>";
    } else {
        obit = modNames[meansOfDeath];
    }

    // 9. shotgun damage-plum temp entity (before the "Kill:" log)
    if (attacker && attacker->client && meansOfDeath == MOD_SHOTGUN) {
        te = G_TempEntity(self->r.currentOrigin, EV_SHOTGUN_KILL);
        te->s.generic1 = attacker->client->damagePlum[self->s.number];  // accumulated shotgun dmg
        te->s.otherEntityNum = attacker->s.number;
        te->s.otherEntityNum2 = self->s.number;
    }

    // 10. kill log
    G_LogPrintf("Kill: %i %i %i: %s killed %s by %s\n",
                killer, self->s.number, meansOfDeath, killerName,
                self->client->pers.netname, obit);

    // 12. EV_OBITUARY (gated on !scoringDisabled)
    if (!level.scoringDisabled) {
        ent = G_TempEntity(self->r.currentOrigin, EV_OBITUARY);
        ent->s.eventParm = meansOfDeath;
        ent->s.otherEntityNum = self->s.number;
        ent->s.otherEntityNum2 = killer;
        ent->r.svFlags = SVF_BROADCAST;
    }

    // 13. enemy / PERS_KILLED / death stat.  STAT_AddPlayerDeathStat does numKills/numDeaths/
    //     killStreak/per-weapon internally (when !scoringDisabled), so player_die must NOT
    //     increment numKills/numDeaths itself.
    self->enemy = attacker;
    if (!G_InMatchStartGrace()) {
        self->client->ps.persistant[PERS_KILLED]++;
    }
    STAT_AddPlayerDeathStat(self, attacker, meansOfDeath);

    // 14. scoring + award engine
    if (attacker && attacker->client) {
        attacker->client->lastkilled_client = self->s.number;
        self->client->lastClientKilled = attacker->s.number;

        if (attacker == self) {
            // 14b. self kill
            if (!G_InMatchStartGrace()) {
                AddScore(attacker, self->r.currentOrigin, -1);
            }
            if (meansOfDeath == MOD_SUICIDE) {   // [QL] binary increments unconditionally
                self->client->expandedStats.numSuicides++;
            }
        } else if (OnSameTeam(self, attacker)) {
            // 14b. friendly fire (complaint system)
            if (g_complaintLimit.integer && BG_IsScoreBasedGameType() &&
                level.warmupTime == 0 && meansOfDeath != MOD_TELEFRAG &&
                !(attacker->r.svFlags & SVF_BOT)) {
                if (attacker->client->sess.privileges == 2) {  // VERIFY C1: admin (privileges==2)
                    trap_SendServerCommand(self->s.number, "complaint -4");
                } else {
                    trap_SendServerCommand(self->s.number, va("complaint %i", attacker->s.number));
                    self->client->pers.complaintClient = attacker->s.clientNum;   // VERIFY C2
                    self->client->pers.complaintEndTime = level.time + 15500;
                    attacker->client->pers.damageToTeammates = 0;   // VERIFY C3 (not "complaintCount")
                    self->client->pers.damageFromTeammates = 0;
                }
            }
            AddScore(attacker, self->r.currentOrigin, -1);
            attacker->client->expandedStats.numTeamKills++;   // [QL] binary: unconditional
            self->client->expandedStats.numTeamKilled++;
        } else {
            // 14c. enemy kill (award engine)

            // ---- score ----
            if (g_quadHog.integer == 0) {
                int score = 1;
                if (g_gametype.integer == GT_RR && g_rrInfected.integer &&
                    self->client->sess.sessionTeam == TEAM_RED) {  // VERIFY C4: victim's team
                    score = g_rrInfectedZombieFragBonus.integer;
                }
                AddScore(attacker, self->r.currentOrigin, score);
                if (g_gametype.integer == GT_RR && g_rrDamageScoreBonus.integer) {
                    AddScore(attacker, self->r.currentOrigin, g_rrDamageScoreBonus.integer);
                }
            } else if (g_quadHog.integer == 1 && g_gametype.integer == GT_FFA &&
                       (attacker->client->ps.powerups[PW_QUAD] ||
                        self->client->ps.powerups[PW_QUAD])) {  // VERIFY C5: attacker OR victim quad
                AddScore(attacker, self->r.currentOrigin, 1);
            }

            // ---- battlesuit / quad kill counters (ps.stats[5]/[13] are award-gate counters) ----
            if (attacker->client->ps.powerups[PW_BATTLESUIT]) {
                attacker->client->ps.stats[5]++;  // [QL] running battlesuit-kill counter (client+0xd4)
            }
            if (attacker->client->ps.powerups[PW_QUAD]) {
                attacker->client->ps.stats[13]++;  // [QL] running quad-kill counter (client+0xf4, QUADGOD gate)
                attacker->client->expandedStats.numQuadDamageKills++;  // [QL] binary: unconditional
                if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x2F) &&
                    attacker->client->expandedStats.numQuadDamageKills > 9) {
                    trap_SetAchievement(attacker->client->ps.clientNum, 0x2F);
                }
            }

            // ---- FIRSTFRAG (once per match; level.firstFrag[0]=='\0' is the gate) ----
            if (level.firstFrag[0] == '\0' && level.warmupTime == 0) {
                // [QL] binary calls G_GetClientCleanNameByClient(level.firstFrag, attacker->client)
                // to fill the buffer (closing the gate). pers.netname is already the cleaned name.
                Q_strncpyz(level.firstFrag, attacker->client->pers.netname, sizeof(level.firstFrag));
                attacker->client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_FIRSTFRAG;
                PlayerAwardEV(attacker, AWARD_FIRSTFRAG);
                STAT_AddPlayerMedalStat(attacker, "FIRSTFRAG");
            }

            // ---- GAUNTLET humiliation ----
            if (meansOfDeath == MOD_GAUNTLET &&
                !(g_gametype.integer == GT_RR && g_rrInfected.integer &&
                  attacker->client->sess.sessionTeam == TEAM_RED)) {
                PlayerAwardEF(attacker, PERS_GAUNTLET_FRAG_COUNT, EF_AWARD_GAUNTLET);
                self->client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_GAUNTLETREWARD;
                STAT_AddPlayerMedalStat(attacker, "GAUNTLET");
                if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x30) &&
                    g_gametype.integer == GT_DUEL) {
                    trap_SetAchievement(attacker->client->ps.clientNum, 0x30);
                }
            }

            // ---- EXCELLENT (two kills within CARNAGE_REWARD_TIME) ----
            if (level.time - attacker->client->lastKillTime < CARNAGE_REWARD_TIME) {
                PlayerAwardEF(attacker, PERS_EXCELLENT_COUNT, EF_AWARD_EXCELLENT);
                STAT_AddPlayerMedalStat(attacker, "EXCELLENT");
                if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x17) &&
                    attacker->client->ps.persistant[PERS_EXCELLENT_COUNT] > 2) {
                    trap_SetAchievement(attacker->client->ps.clientNum, 0x17);
                }
            }

            // ---- REVENGE ----
            self->client->revengeCounter[attacker->s.number]++;
            if (attacker->client->revengeCounter[self->s.number] > 2) {
                PlayerAwardEV(attacker, AWARD_REVENGE);
                STAT_AddPlayerMedalStat(attacker, "REVENGE");
            }
            attacker->client->revengeCounter[self->s.number] = 0;

            // ---- COMBOKILL (two rail hits on the victim within 1.5s, 2nd by attacker) ----
            {
                // [QL] VERIFY: the binary reads the VICTIM's (self->client) lasthurt tracking,
                // not the attacker's - the second rail hit on the victim was by the attacker.
                int wpn = BG_GetWeaponFromMeansOfDeath(meansOfDeath);
                if ((meansOfDeath == MOD_RAILGUN || meansOfDeath == MOD_RAILGUN_HEADSHOT) &&
                    wpn != 0 && wpn != WP_ROCKET_LAUNCHER &&
                    self->client->lasthurt_client[1] == attacker->s.number &&
                    self->client->lasthurt_time[0] < self->client->lasthurt_time[1] + 1500) {
                    PlayerAwardEV(attacker, AWARD_COMBOKILL);
                    STAT_AddPlayerMedalStat(attacker, "COMBOKILL");
                }
            }

            // ---- shotgun airborne kill (stat) OR MIDAIR / HEADSHOT / PERFORATED ----
            if (meansOfDeath == MOD_SHOTGUN) {
                if (self->client->ps.groundEntityNum == ENTITYNUM_NONE && self->waterlevel == 0) {
                    attacker->client->expandedStats.numMidairShotgunKills++;  // [QL] binary: unconditional
                    if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x35) &&
                        attacker->client->expandedStats.numMidairShotgunKills > 4) {
                        trap_SetAchievement(attacker->client->ps.clientNum, 0x35);
                    }
                }
            } else {
                // MIDAIR: splash weapon vs airborne target with clear space below
                if ((meansOfDeath == MOD_GRENADE || meansOfDeath == MOD_ROCKET ||
                     meansOfDeath == MOD_PLASMA || meansOfDeath == MOD_BFG ||
                     meansOfDeath == MOD_NAIL) &&
                    self->client->ps.groundEntityNum == ENTITYNUM_NONE &&
                    self->waterlevel == 0) {
                    vec3_t end;
                    trace_t tr;
                    VectorCopy(self->client->ps.origin, end);
                    end[2] -= g_midAirMinHeight.value;
                    trap_Trace(&tr, self->client->ps.origin, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
                    if (tr.fraction == 1.0f) {
                        PlayerAwardEV(attacker, AWARD_MIDAIR);
                        STAT_AddPlayerMedalStat(attacker, "MIDAIR");
                    }
                }
                if (meansOfDeath == MOD_RAILGUN_HEADSHOT) {
                    PlayerAwardEV(attacker, AWARD_HEADSHOT);
                    STAT_AddPlayerMedalStat(attacker, "HEADSHOT");
                } else if (meansOfDeath == MOD_TELEFRAG) {
                    PlayerAwardEV(attacker, AWARD_PERFORATED);
                    STAT_AddPlayerMedalStat(attacker, "PERFORATED");
                }
            }

            // ---- QUADGOD (every 10th quad kill) ----
            if (attacker->client->ps.powerups[PW_QUAD]) {
                int qk = attacker->client->ps.stats[13];
                if (qk > 9 && (qk % 10) == 0) {
                    PlayerAwardEV(attacker, AWARD_QUADGOD);
                    STAT_AddPlayerMedalStat(attacker, "QUADGOD");
                }
            }

            // ---- KAMIKAZE achievement ----
            if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x12) &&
                meansOfDeath == MOD_KAMIKAZE) {
                trap_SetAchievement(attacker->client->ps.clientNum, 0x12);
            }

            attacker->client->lastKillTime = level.time;
        }
    } else {
        // 14a. world / non-client attacker
        if (!G_InMatchStartGrace()) {
            AddScore(self, self->r.currentOrigin, -1);
        }
        if (meansOfDeath == MOD_WATER || meansOfDeath == MOD_SLIME || meansOfDeath == MOD_LAVA ||
            meansOfDeath == MOD_CRUSH || meansOfDeath == MOD_TELEFRAG || meansOfDeath == MOD_FALLING ||
            meansOfDeath == MOD_TARGET_LASER || meansOfDeath == MOD_TRIGGER_HURT) {
            self->client->expandedStats.numSuicides++;   // [QL] binary: unconditional
        }
    }

    // 15. full frag-bonus scoring (defence/assist/carrier).  The reimpl's Team_FragBonuses
    //     unifies the binary's Team_FragBonuses (carrier-kill broadcast) + Team_FragBonuses_Full
    //     (defence/assist scoring); a single call covers both split calls (11 + 15).
    Team_FragBonuses(self, inflictor, attacker);
    if (g_gametype.integer == GT_DOMINATION && attacker && attacker->client) {
        DOM_FragBonuses(attacker, self);
    }

    // 16. flag return on suicide (if g_returnFlagOnSuicide) / always on team-switch
    if ((meansOfDeath == MOD_SUICIDE && g_returnFlagOnSuicide.integer) ||
        meansOfDeath == MOD_SWITCH_TEAMS) {
        if (self->client->ps.powerups[PW_NEUTRALFLAG]) {
            Team_ReturnFlag(TEAM_FREE);
            self->client->ps.powerups[PW_NEUTRALFLAG] = 0;
        } else if (self->client->ps.powerups[PW_REDFLAG]) {
            Team_ReturnFlag(TEAM_RED);
            self->client->ps.powerups[PW_REDFLAG] = 0;
        } else if (self->client->ps.powerups[PW_BLUEFLAG]) {
            Team_ReturnFlag(TEAM_BLUE);
            self->client->ps.powerups[PW_BLUEFLAG] = 0;
        }
    }

    // 17. respawn any held keys back into the world (moved late, matching binary order)
    if (self->client->ps.stats[STAT_KEY]) {
        if (self->client->ps.stats[STAT_KEY] & KEY_SILVER) G_RespawnKey(KEY_SILVER);
        if (self->client->ps.stats[STAT_KEY] & KEY_GOLD)   G_RespawnKey(KEY_GOLD);
        if (self->client->ps.stats[STAT_KEY] & KEY_MASTER) G_RespawnKey(KEY_MASTER);
        self->client->ps.stats[STAT_KEY] = 0;
    }

    // 18. nodrop: return any held flag; otherwise toss items
    contents = trap_PointContents(self->r.currentOrigin, -1);
    if (contents & CONTENTS_NODROP) {
        if (self->client->ps.powerups[PW_NEUTRALFLAG]) {
            Team_ReturnFlag(TEAM_FREE);
        } else if (self->client->ps.powerups[PW_REDFLAG]) {
            Team_ReturnFlag(TEAM_RED);
        } else if (self->client->ps.powerups[PW_BLUEFLAG]) {
            Team_ReturnFlag(TEAM_BLUE);
        }
    } else {
        TossClientItems(self);
    }
    TossClientPersistantPowerups(self);

    // 19. harvester cubes
    if (g_gametype.integer == GT_HARVESTER && meansOfDeath != MOD_SWITCH_TEAMS) {
        TossClientCubes(self);
    }

    // 20. remove powerups + refresh scoreboards for followers
    memset(self->client->ps.powerups, 0, sizeof(self->client->ps.powerups));
    Cmd_Score_f(self);
    for (i = 0; i < level.maxclients; i++) {
        gclient_t* cl = &level.clients[i];
        if (cl->pers.connected != CON_CONNECTED) {
            continue;
        }
        if (cl->sess.sessionTeam != TEAM_SPECTATOR) {
            continue;
        }
        if (cl->sess.spectatorClient == self->s.number && g_entities[i].client) {
            Cmd_Score_f(&g_entities[i]);
        }
    }

    // 21. corpse presentation.  A live Freeze-Tag frozen player keeps normal contents +
    //     bounding box so teammates can touch to thaw (isLiveFreeze).
    self->takedamage = qtrue;
    self->s.weapon = WP_NONE;
    self->s.powerups = 0;
    self->s.loopSound = 0;

    isLiveFreeze = (!level.intermissionTime && !level.intermissionQueued &&
                    g_gametype.integer == GT_FREEZE && meansOfDeath != MOD_SWITCH_TEAMS);

    if (!isLiveFreeze) {
        self->r.contents = CONTENTS_CORPSE;
    }

    // LookAtKiller (inlined; QL viewangles live at ps+0xa0)
    self->s.angles[0] = 0;
    self->s.angles[2] = 0;
    if (attacker && attacker != self) {
        vec3_t dir;
        dir[0] = attacker->s.pos.trBase[0] - self->s.pos.trBase[0];
        dir[1] = attacker->s.pos.trBase[1] - self->s.pos.trBase[1];
        dir[2] = attacker->s.pos.trBase[2] - self->s.pos.trBase[2];
        self->s.angles[1] = vectoyaw(dir);
    }
    self->client->ps.viewangles[0] = 0;
    self->client->ps.viewangles[1] = self->s.angles[1];
    self->client->ps.viewangles[2] = self->s.angles[2];

    if (!isLiveFreeze) {
        self->r.maxs[2] = -8;
    }

    // 22. respawn time
    {
        qboolean roundGT = (g_gametype.integer == GT_CA || g_gametype.integer == GT_FREEZE ||
                            g_gametype.integer == GT_AD || g_gametype.integer == GT_RR);
        qboolean handled = qfalse;
        if (roundGT && (level.warmupTime > 0 || level.roundState.eCurrent == RS_COUNTDOWN)) {
            self->client->respawnTime = level.time + 500;
            handled = qtrue;
        }
        if (!handled) {
            if (level.intermissionTime || level.intermissionQueued ||
                g_gametype.integer != GT_FREEZE || meansOfDeath == MOD_SWITCH_TEAMS) {
                int delay = g_forcerespawn.integer ? level.forceRespawnDelay
                                                   : g_respawn_delay_min.integer;
                self->client->respawnTime = level.time + delay;
            }
        }
    }

    // 23/24. suicide penalty + gib / death-anim / freeze (control flow per VERIFY C7)
    {
        qboolean doDeathBranch = qtrue;
        if (meansOfDeath == MOD_SUICIDE) {
            if (g_suicidePenaltyTime.integer != 0) {
                self->client->respawnTime += g_suicidePenaltyTime.integer;
            }
            // SUICIDE_ACTIVATE: unless live-freeze, gib-finalize and SKIP the gib/death block
            if (!(!level.intermissionTime && !level.intermissionQueued &&
                  g_gametype.integer == GT_FREEZE)) {
                GibEntity(self, killer);  // [QL] binary Kamikaze_DeathActivate (gib finalizer)
                doDeathBranch = qfalse;
            }
        }

        if (doDeathBranch) {
            if (self->health <= GIB_HEALTH &&
                (level.intermissionTime || level.intermissionQueued ||
                 g_gametype.integer != GT_FREEZE)) {
                // ---- GIB + RAMPAGE ----
                GibEntity(self, killer);
                if (attacker && attacker->client) {
                    if (attacker != self && !OnSameTeam(attacker, self)) {
                        int r = attacker->client->rampageCounter;
                        if (r == 0 ||
                            (r == 1 && level.time <= attacker->client->lastGibTime + 3000)) {
                            attacker->client->rampageCounter++;
                        } else if (r > 1 && level.time < attacker->client->lastGibTime + 3000) {
                            PlayerAwardEV(attacker, AWARD_RAMPAGE);
                            STAT_AddPlayerMedalStat(attacker, "RAMPAGE");
                            attacker->client->rampageCounter = 0;
                        }
                    }
                    attacker->client->lastGibTime = level.time;
                }
            } else {
                // ---- death anim (or FREEZE) ----
                static int deathAnim;

                switch (deathAnim) {
                    case 0:  anim = BOTH_DEATH1; break;
                    case 1:  anim = BOTH_DEATH2; break;
                    case 2:
                    default: anim = BOTH_DEATH3; break;
                }
                G_AddEvent(self, EV_DEATH1 + deathAnim, killer);
                deathAnim = (deathAnim + 1) % 3;

                // prevent the health from staying at gib level (no-blood option)
                if (self->health <= GIB_HEALTH) {
                    self->health = GIB_HEALTH + 1;
                }

                if (!level.intermissionTime && !level.intermissionQueued &&
                    g_gametype.integer == GT_FREEZE && meansOfDeath != MOD_SWITCH_TEAMS) {
                    // ---- FREEZE the player instead of body_die ----
                    self->client->ps.powerups[PW_FREEZE] = 0x7fffffff;
                    self->s.powerups = 0x8000;  // per-entity freeze/ice presentation bit
                    self->client->ps.freezetime = level.time;
                    self->die = player_die;
                    // insta-kill (gib the frozen body) for {CRUSH, TELEFRAG, LIGHTNING_DISCHARGE}
                    if (meansOfDeath > MOD_LAVA &&
                        (meansOfDeath < MOD_FALLING || meansOfDeath == MOD_LIGHTNING_DISCHARGE)) {
                        Freeze_InstaKill(self, 1);
                    }
                } else {
                    // ---- normal body_die ----
                    self->client->ps.legsAnim =
                        ((self->client->ps.legsAnim & ANIM_TOGGLEBIT) ^ ANIM_TOGGLEBIT) | anim;
                    self->client->ps.torsoAnim =
                        ((self->client->ps.torsoAnim & ANIM_TOGGLEBIT) ^ ANIM_TOGGLEBIT) | anim;
                    self->die = body_die;
                }

                // kamikaze timer entity (both freeze & normal paths)
                if (self->s.eFlags & EF_KAMIKAZE) {
                    gentity_t* e = G_Spawn();
                    e->classname = "kamikaze timer";
                    VectorCopy(self->s.pos.trBase, e->s.pos.trBase);
                    e->r.svFlags |= SVF_NOCLIENT;
                    e->think = G_FreeEntity;  // [QL] binary: G_FreeEntity, nextthink +3000
                    e->nextthink = level.time + 3000;
                    e->activator = self;
                }
            }
        }
    }

    // 25. CA end-of-round achievements
    if (g_gametype.integer == GT_CA && attacker && attacker->client) {
        int aliveRed, aliveBlue;
        Team_LivingTeamCounts(&aliveRed, &aliveBlue);
        if (aliveRed == 0 || aliveBlue == 0) {
            if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x32) &&
                meansOfDeath == MOD_GAUNTLET) {
                trap_SetAchievement(attacker->client->ps.clientNum, 0x32);
            }
            if (!trap_HasAchievement(attacker->client->ps.clientNum, 0x33) &&
                (meansOfDeath == MOD_GRENADE || meansOfDeath == MOD_GRENADE_SPLASH)) {
                trap_SetAchievement(attacker->client->ps.clientNum, 0x33);
            }
        }
    }

    // 26. gametype death hooks + link
    if (g_gametype.integer == GT_CA) {
        CA_Think();  // [QL] TODO: binary CA_PlayerKilled (.so 0x60720); reimpl exposes CA_Think
    } else if (g_gametype.integer == GT_AD) {
        AD_Think();
    } else if (g_gametype.integer == GT_RR) {
        RR_OnPlayerDeath(self);  // [QL] TODO: binary RR_PlayerKilled (.so 0x905a0)
    }

    trap_LinkEntity(self);
}

/*
================
CheckArmor
================
*/
int CheckArmor(gentity_t* ent, int damage, int dflags) {
    gclient_t* client;
    int save;
    int count;
    double armorProtection;

    if (!damage)
        return 0;

    client = ent->client;

    if (!client)
        return 0;

    if (dflags & DAMAGE_NO_ARMOR)
        return 0;

    // [QL] Guard powerup: 75% protection
    if (client->ps.stats[STAT_PERSISTANT_POWERUP] == 4) {
        armorProtection = 0.75;
    } else if (!armor_tiered.integer || g_gametype.integer == GT_CA) {
        // Non-tiered or CA: standard 66%
        armorProtection = 0.66;
    } else {
        // Tiered armor based on STAT_ARMORTYPE
        switch (client->ps.stats[STAT_ARMORTYPE]) {
            case 0:  armorProtection = 0.50; break;  // green / none
            case 1:  armorProtection = 0.66; break;  // yellow
            case 2:  armorProtection = 0.75; break;  // red
            default: armorProtection = 0.66; break;
        }
    }

    save = (int)ceil((double)damage * armorProtection);
    count = client->ps.stats[STAT_ARMOR];
    if (save >= count)
        save = count;

    // [QL] binary CheckArmor (0x10048d30) is side-effect free: it only computes the
    // absorbed amount. The armor deduction and STAT_ARMORTYPE clear are performed by
    // G_Damage AFTER dmflags/AccuracyMessage may have zeroed asave, not here.
    return save;
}

/*
================
RaySphereIntersections
================
*/
int RaySphereIntersections(vec3_t origin, float radius, vec3_t point, vec3_t dir, vec3_t intersections[2]) {
    float b, c, d, t;

    //	| origin - (point + t * dir) | = radius
    //	a = dir[0]^2 + dir[1]^2 + dir[2]^2;
    //	b = 2 * (dir[0] * (point[0] - origin[0]) + dir[1] * (point[1] - origin[1]) + dir[2] * (point[2] - origin[2]));
    //	c = (point[0] - origin[0])^2 + (point[1] - origin[1])^2 + (point[2] - origin[2])^2 - radius^2;

    // normalize dir so a = 1
    VectorNormalize(dir);
    b = 2 * (dir[0] * (point[0] - origin[0]) + dir[1] * (point[1] - origin[1]) + dir[2] * (point[2] - origin[2]));
    c = (point[0] - origin[0]) * (point[0] - origin[0]) +
        (point[1] - origin[1]) * (point[1] - origin[1]) +
        (point[2] - origin[2]) * (point[2] - origin[2]) -
        radius * radius;

    d = b * b - 4 * c;
    if (d > 0) {
        t = (-b + sqrt(d)) / 2;
        VectorMA(point, t, dir, intersections[0]);
        t = (-b - sqrt(d)) / 2;
        VectorMA(point, t, dir, intersections[1]);
        return 2;
    } else if (d == 0) {
        t = (-b) / 2;
        VectorMA(point, t, dir, intersections[0]);
        return 1;
    }
    return 0;
}

/*
================
G_InvulnerabilityEffect
================
*/
int G_InvulnerabilityEffect(gentity_t* targ, vec3_t dir, vec3_t point, vec3_t impactpoint, vec3_t bouncedir) {
    gentity_t* impact;
    vec3_t intersections[2], vec;
    int n;

    if (!targ->client) {
        return qfalse;
    }
    VectorCopy(dir, vec);
    VectorInverse(vec);
    // sphere model radius = 42 units
    n = RaySphereIntersections(targ->client->ps.origin, 42, point, vec, intersections);
    if (n > 0) {
        impact = G_TempEntity(targ->client->ps.origin, EV_INVUL_IMPACT);
        VectorSubtract(intersections[0], targ->client->ps.origin, vec);
        vectoangles(vec, impact->s.angles);
        impact->s.angles[0] += 90;
        if (impact->s.angles[0] > 360)
            impact->s.angles[0] -= 360;
        if (impactpoint) {
            VectorCopy(intersections[0], impactpoint);
        }
        if (bouncedir) {
            VectorCopy(vec, bouncedir);
            VectorNormalize(bouncedir);
        }
        return qtrue;
    } else {
        return qfalse;
    }
}

/*
============
G_Damage

targ		entity that is being damaged
inflictor	entity that is causing the damage
attacker	entity that caused the inflictor to damage targ
    example: targ=monster, inflictor=rocket, attacker=player

dir			direction of the attack for knockback
point		point at which the damage is being inflicted, used for headshots
damage		amount of damage being inflicted
knockback	force to be applied against targ as a result of the damage

inflictor, attacker, dir, and point can be NULL for environmental effects

dflags		these flags are used to control how T_Damage works
    DAMAGE_RADIUS			damage was indirect (from a nearby explosion)
    DAMAGE_NO_ARMOR			armor does not protect from this damage
    DAMAGE_NO_KNOCKBACK		do not affect velocity, just view angles
    DAMAGE_NO_PROTECTION	kills godmode, armor, everything
============
*/

/*
================
DamageTier - [QL] maps an incoming damage amount to a 2-bit severity tier for the
crosshair hit-marker. DLL-only helper (Win 0x10048de0, inlined in the Linux build).
Thresholds (signed): 0-24 -> 0, 25-49 -> 1, 50-74 -> 2, 75+ -> 3.
================
*/
static int DamageTier(int damage) {
    if (damage > 74) return 3;
    if (damage > 49) return 2;
    return (damage > 24);
}

/*
================
G_KnockbackScale - [QL] per-weapon knockback multiplier from cvars
================
*/
static float G_KnockbackScale(int mod, qboolean isSelf) {
    // self-damage variants for RL and PG
    if (isSelf) {
        switch (mod) {
            case MOD_ROCKET:
            case MOD_ROCKET_SPLASH:
                return g_knockback_rl_self.value;
            case MOD_PLASMA:
            case MOD_PLASMA_SPLASH:
                return g_knockback_pg_self.value;
            default:
                break;
        }
    }
    switch (mod) {
        case MOD_GAUNTLET:       return g_knockback_g.value;
        case MOD_MACHINEGUN:     return g_knockback_mg.value;
        case MOD_SHOTGUN:        return g_knockback_sg.value;
        case MOD_GRENADE:
        case MOD_GRENADE_SPLASH: return g_knockback_gl.value;
        case MOD_ROCKET:
        case MOD_ROCKET_SPLASH:  return g_knockback_rl.value;
        case MOD_LIGHTNING:
        case MOD_LIGHTNING_DISCHARGE: return g_knockback_lg.value;  // [QL] binary: case 0xb,0x1f -> "lg"
        case MOD_RAILGUN:
        case MOD_RAILGUN_HEADSHOT:    return g_knockback_rg.value;  // [QL] binary: case 10,0x21 -> "rg"
        case MOD_PLASMA:
        case MOD_PLASMA_SPLASH:  return g_knockback_pg.value;
        case MOD_BFG:
        case MOD_BFG_SPLASH:     return g_knockback_bfg.value;
        case MOD_GRAPPLE:        return g_knockback_gh.value;
        case MOD_NAIL:           return g_knockback_ng.value;
        case MOD_PROXIMITY_MINE: return g_knockback_pl.value;
        case MOD_CHAINGUN:       return g_knockback_cg.value;
        case MOD_HMG:            return g_knockback_hmg.value;    // [QL] binary: case 0x20 -> "hmg"
        default:                 return 1.0f;
    }
}

// [QL] Round-based damage-adjust dispatch targets for G_Damage section 20 are
// CA_AdjustDamage / AD_AdjustDamage / RR_AdjustDamage, all prototyped in
// g_local.h. CA_AdjustDamage is the Win Ghidra mislabel "AccuracyMessage".

/*
============
G_ThrowFlag

// [QL] G_ThrowFlag (binary 0x10051420)
On a gauntlet kill (g_dropFlag) the victim tosses whatever flag it carries. Traces a
small box from the carrier origin to the drop point and only drops if the path is
clear. Flag priority matches the binary: neutral, then red, then blue. point is the
drop location (the G_Damage caller passes the victim ps.origin raised 64 units); when
NULL the drop is placed forward of the carrier.
============
*/
void G_ThrowFlag(gentity_t* self, vec3_t point) {
    gclient_t* client = self->client;
    vec3_t launch;
    vec3_t velocity = { 0, 0, 0 };
    vec3_t mins = { -15, -15, -15 };
    vec3_t maxs = { 15, 15, 15 };
    trace_t tr;
    int pw;
    gitem_t* item;
    gentity_t* drop;

    if (!client) {
        return;
    }
    if (client->ps.pm_type == PM_SPECTATOR) {
        return;
    }
    if (client->ps.pm_flags & PMF_PAUSED) {   // binary: (ps.pm_flags & 0x100) == 0
        return;
    }

    if (point) {
        VectorCopy(point, launch);
    } else {
        vec3_t forward;
        AngleVectors(client->ps.viewangles, forward, NULL, NULL);
        VectorMA(client->ps.origin, 64.0f, forward, launch);
    }

    // don't drop the flag into a wall
    trap_Trace(&tr, client->ps.origin, mins, maxs, launch, ENTITYNUM_NONE, CONTENTS_SOLID);
    if (tr.fraction != 1.0f) {
        return;
    }

    if (client->ps.powerups[PW_NEUTRALFLAG]) {
        pw = PW_NEUTRALFLAG;
    } else if (client->ps.powerups[PW_REDFLAG]) {
        pw = PW_REDFLAG;
    } else if (client->ps.powerups[PW_BLUEFLAG]) {
        pw = PW_BLUEFLAG;
    } else {
        return;
    }

    item = BG_FindItemForPowerup(pw);
    if (!item) {
        return;
    }

    drop = LaunchItem(item, launch, velocity);
    drop->s.modelindex2 = 1;               // dropped-item marker (binary +0x26c)
    drop->parent = self;                   // binary +0x2e0
    drop->nextthink = level.time + 10000;  // binary +0x2f4: 10s timeout
    client->ps.powerups[pw] = 0;
}

/*
============
Drop_DamagedHealth_Ring - launch count copies of item outward in a horizontal ring
around the target (binary inline loop, speed 200, up 225, spawn offset velocity*0.2).
============
*/
static void Drop_DamagedHealth_Ring(gentity_t* targ, gitem_t* item, int count) {
    float angle;
    float step;
    int i;

    if (!item || count <= 0) {
        return;
    }
    angle = (float)(rand() % 360);
    step = (float)(360 / count);
    for (i = 0; i < count; i++) {
        vec3_t velocity;
        vec3_t origin;
        float a = DEG2RAD(targ->s.pos.trBase[1] + angle);
        velocity[0] = cos(a) * 200.0f;
        velocity[1] = sin(a) * 200.0f;
        velocity[2] = 225.0f;
        origin[0] = targ->s.pos.trBase[0] + velocity[0] * 0.2f;
        origin[1] = targ->s.pos.trBase[1] + velocity[1] * 0.2f;
        origin[2] = targ->s.pos.trBase[2] + velocity[2] * 0.2f;
        LaunchItem(item, origin, velocity);
        angle += step;
    }
}

/*
============
Drop_DamagedHealth

// [QL] Drop_DamagedHealth (binary 0x1004f930)
"Bleeding health": the victim sheds small health pickups worth the damage taken.
amount/25 copies of "25 Health" and (amount%25)/5 copies of "5 Health" are launched
outward in a ring.
============
*/
void Drop_DamagedHealth(gentity_t* attacker, gentity_t* targ, int amount) {
    if (!g_dropDamagedHealth.integer) {
        return;
    }
    // binary skips self-drops when self health damage is disabled
    if (targ == attacker && (g_dmflags.integer & DF_NO_SELF_DAMAGE)) {
        return;
    }
    Drop_DamagedHealth_Ring(targ, BG_FindItem("25 Health"), amount / 25);
    Drop_DamagedHealth_Ring(targ, BG_FindItem("5 Health"), (amount % 25) / 5);
}

void G_Damage(gentity_t* targ, gentity_t* inflictor, gentity_t* attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod) {
    gclient_t* client;
    int take;
    int asave;
    int knockback;
    vec3_t bouncedir, impactpoint;

    client = targ->client;

    // 1. opening guards
    if (!targ->takedamage) {
        return;
    }
    if (client && client->noclip) {  // VERIFY #1: noclip guard is early (NOT pm_type)
        return;
    }
    if (level.intermissionQueued) {
        return;
    }
    if (level.intermissionTime) {
        return;
    }

    // 2. invulnerability mask + spectator
    if (client) {
        if (mod != MOD_JUICED && client->invulnerabilityTime > level.time) {
            if (dir && point) {
                G_InvulnerabilityEffect(targ, dir, point, impactpoint, bouncedir);
            }
            return;
        }
        if (client->ps.pm_type == PM_SPECTATOR) {  // VERIFY #1: pm_type==2 == PM_SPECTATOR
            return;
        }
    }

    // 3. world-default inflictor / attacker
    if (!inflictor) {
        inflictor = &g_entities[ENTITYNUM_WORLD];
    }
    if (!attacker) {
        attacker = &g_entities[ENTITYNUM_WORLD];
    }

    // 4. no player-vs-player damage in race mode
    // [QL] binary reads an unresolved cvar (DAT_105a0b8c) that blocks ALL client-vs-client
    // damage; its semantics == race/ghost mode. reimpl keys this off GT_RACE (as missile code does).
    if (g_gametype.integer == GT_RACE && attacker->client && targ->client && targ != attacker) {
        return;
    }

    // 5. shootable movers (doors/buttons)
    if (targ->s.eType == ET_MOVER) {
        if (targ->use && targ->moverState == MOVER_POS1) {
            targ->use(targ, inflictor, attacker);
        }
        return;
    }

    // 6. GT_OBELISK: attacking the obelisk
    if (g_gametype.integer == GT_OBELISK && CheckObeliskAttack(targ, attacker)) {
        return;
    }

    // 7. direction normalise
    if (!dir) {
        dflags |= DAMAGE_NO_KNOCKBACK;
    } else {
        VectorNormalize(dir);
    }

    // 8. knockback = (int)(scale * damage).  G_KnockbackScale handles the self rl_self/pg_self
    // variants internally (self RL/PG -> g_knockback_{rl,pg}_self, else per-mod cvar).
    knockback = (int)((float)damage * G_KnockbackScale(mod, (targ == attacker)));
    if (client && targ == attacker && g_gametype.integer == GT_RACE && knockback > 0) {
        client->race.weaponUsed = 1;
    }
    // [QL] Grapple knockback reduction (binary 0x1004901a, inside G_Damage): for a
    // non-self MOD_GRAPPLE hit whose hook entity carries a live wait marker, knockback
    // is cut to 20%. inflictor->wait (+0x370) is only the gate (compared != 0); the 0.2
    // is a hardcoded double constant (0x1008b350).
    if (mod == MOD_GRAPPLE && targ != attacker && inflictor->wait != 0.0f) {
        knockback = (int)(knockback * 0.2);
    }

    // 9. knockback caps
    if (knockback > g_max_knockback.integer) {
        knockback = g_max_knockback.integer;
    }
    if (targ->flags & FL_NO_KNOCKBACK) {
        knockback = 0;
    }
    if (dflags & DAMAGE_NO_KNOCKBACK) {
        knockback = 0;
    }

    // 10. [QL] MOD_ROCKET_SPLASH damage recalc (binary 0x10049081). The binary re-derives:
    //       damage = ftol((1.0 - G_DistanceToBBox(targ, inflictor->r.currentOrigin)
    //                / inflictor->splashRadius) * (int)g_global_0x105a9a6c) * inflictor->damageFactor
    //     Fields verified: +0x334 = splashRadius, +0x32c = damageFactor (NOT splashDamage @ +0x330).
    //     Omitted deliberately: inflictor->damageFactor is 0 for every rocket (neither fire_rocket
    //     nor CreateMissile writes +0x32c), and the global at 0x105a9a6c reads 0 with no identifiable
    //     cvar, so the literal recalc would zero rocket splash then clamp to 1. The real falloff
    //     (splashDamage * (1 - dist/radius)) is applied in G_RadiusDamage, which matches the binary
    //     G_RadiusDamage Path B; reproducing this branch verbatim would regress that.

    // 11. reduce damage by the attacker's handicap (AFTER knockback)
    if (attacker->client && attacker != targ) {
        char userinfo[MAX_INFO_STRING];
        int h;
        trap_GetUserinfo(attacker->client->ps.clientNum, userinfo, sizeof(userinfo));
        h = atoi(Info_ValueForKey(userinfo, "handicap"));
        if (h < 1 || h > 100) {
            h = 100;
        }
        damage = (damage * h) / 100;
    }

    // 12. apply knockback velocity + pm_time
    if (knockback && client) {
        vec3_t kvel;
        vec3_t kdir;
        int k = knockback;

        if (k < 0) {
            k = -k;
            VectorNegate(dir, kdir);
        } else {
            VectorCopy(dir, kdir);
        }
        VectorScale(kdir, g_knockback.value * (float)k / 200.0f, kvel);
        VectorAdd(client->ps.velocity, kvel, client->ps.velocity);

        // set the timer so the other client can't immediately cancel the movement
        if (client->ps.pm_time == 0) {
            float cr = g_knockback_cripple.value;
            float t = (float)(k * 2);
            float r = cr;
            if (cr <= t) {  // VERIFY V1: the 200 cap only applies on the cripple<=t branch
                r = t;
                if (t > 200.0f) {
                    r = 200.0f;
                }
            }
            client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
            client->ps.pm_time = (int)r;
        }
    }

    // 13. RR infected-gauntlet insta-666
    if (g_gametype.integer == GT_RR && g_rrInfected.integer && attacker->client &&
        attacker->client->sess.sessionTeam == TEAM_RED && mod == MOD_GAUNTLET) {
        damage = 666;
    }

    // 14. protection block
    if (!(dflags & DAMAGE_NO_PROTECTION)) {
        // friendly fire
        if (mod != MOD_JUICED && targ != attacker && !(dflags & DAMAGE_NO_TEAM_PROTECTION)) {
            if (OnSameTeam(targ, attacker) && !g_friendlyFire.integer) {
                return;
            }
        }
        // quad-only FFA (DAT_1059dd6c == g_quadHog): only quad holders may deal damage
        if (targ->client && attacker->client && g_quadHog.integer &&
            g_gametype.integer == GT_FFA && attacker != targ) {
            if (!attacker->client->ps.powerups[PW_QUAD] && !targ->client->ps.powerups[PW_QUAD]) {
                return;
            }
        }
        // proximity mine
        if (mod == MOD_PROXIMITY_MINE) {
            if (inflictor && inflictor->parent && OnSameTeam(targ, inflictor->parent)) {
                return;
            }
            if (targ == attacker) {
                return;
            }
        }
        // training bot-vs-bot
        if (g_training.integer && (attacker->r.svFlags & SVF_BOT) && (targ->r.svFlags & SVF_BOT)) {
            return;
        }
        // godmode
        if (targ->flags & FL_GODMODE) {
            // [QL] TODO: binary emits a bot-help taunt here (rand + SendServerCommand) before return; omitted.
            return;
        }
    }

    // 15. battlesuit + armored-spawn dampen
    if (client) {
        if (client->ps.powerups[PW_BATTLESUIT]) {
            G_AddEvent(targ, EV_POWERUP_BATTLESUIT, 0);
            if (mod == MOD_FALLING) {  // VERIFY: battlesuit blocks ONLY falling (not all radius)
                return;
            }
            damage = (int)((float)damage * g_battleSuitDampen.value);
        }
        if (client->ps.powerups[0] != 0) {  // armored/spawn-protection slot (powerups[0])
            damage = (int)((float)damage * g_spawnArmorDmgScale.value);  // .value @ 0x105ab7a8
        }
    }

    // 16. attacker hit counter + generic1 damage-tier (DamageTier spec + VERIFY)
    if (attacker->client && targ != attacker && targ->health > 0 &&
        targ->s.eType != ET_MISSILE && targ->s.eType != ET_GENERAL) {
        if (OnSameTeam(targ, attacker)) {
            attacker->client->ps.persistant[PERS_HITS]--;
            damage = (int)((float)damage * g_friendlyFireDampen.value);
        } else {
            attacker->client->ps.persistant[PERS_HITS]++;
            // [QL] top 2 bits of generic1 = damage-severity tier; low 6 bits (skull count) preserved
            attacker->client->ps.generic1 =
                (attacker->client->ps.generic1 & 0x3F) | (DamageTier(damage) << 6);
        }
    }

    // 17. self-damage
    if (targ == attacker) {
        if (g_instaGib.integer) {
            return;
        }
        damage = (int)((float)damage * 0.5f);
    }
    if (damage < 1) {
        damage = 1;
    }

    // 18. rune pre-multiply (stats[STAT_PERSISTANT_POWERUP]; VERIFY #2)
    if (client) {
        if (client->ps.stats[STAT_PERSISTANT_POWERUP] == 2) {
            damage = (int)((float)damage * 0.5f);
        } else if (client->ps.stats[STAT_PERSISTANT_POWERUP] == 1 &&
                   targ == attacker && mod != MOD_KAMIKAZE) {
            damage = 0;
        }
    }

    // 19. save some from armor
    asave = CheckArmor(targ, damage, dflags);
    take = damage - asave;

    // 20. CA/AD/RR damage adjust
    // [QL] The binary calls each gametype's adjust fn as (targ, &take, &asave)
    // where asave is the CheckArmor result local (NOT knockback, which was
    // already spent on the velocity push in section 12). Each returns 0 to
    // abort ALL damage (jump straight to the G_Damage return). CA and AD are
    // byte-identical except the round-transition + rank recompute; RR gates its
    // score bump on g_rrDamageScoreBonus and double-books the round score.
    if (!(dflags & DAMAGE_NO_PROTECTION)) {
        if (g_gametype.integer == GT_CA) {
            // .so CA_AdjustDamage (binary 0x100380d0)
            if (!CA_AdjustDamage(targ, attacker, &take, &asave)) {
                return;
            }
        } else if (g_gametype.integer == GT_AD) {
            // .so AD_AdjustDamage (binary 0x10035880)
            if (!AD_AdjustDamage(targ, attacker, &take, &asave)) {
                return;
            }
        } else if (g_gametype.integer == GT_RR) {
            // .so RR_AdjustDamage (binary 0x10064440)
            if (!RR_AdjustDamage(targ, attacker, &take, &asave)) {
                return;
            }
        }
    }

    // 21. dmflags self-damage suppression (not for CA/AD which have their own adjust)
    if (g_gametype.integer != GT_CA && g_gametype.integer != GT_AD &&
        g_dmflags.integer != 0 && targ == attacker) {
        if ((g_dmflags.integer & DF_NO_SELF_DAMAGE) && mod != MOD_KAMIKAZE) {
            take = 0;
        }
        if (g_dmflags.integer & DF_NO_SELF_ARMOR_DAMAGE) {
            asave = 0;
        }
    }

    // 22. freeze immunity + environmental insta-kill
    if (client && client->ps.powerups[PW_FREEZE]) {
        take = 0;
    }
    if ((client && client->ps.powerups[PW_FREEZE]) || (targ->s.powerups & 0x8000)) {
        if (client) {
            // [QL] TODO: binary QG_SpecialQuad(mod) -> Freeze_InstaKill branch not in reimpl.
            switch (mod) {
                case MOD_SLIME:
                case MOD_LAVA:
                case MOD_SUICIDE:
                case MOD_TRIGGER_HURT:
                    if (level.time > client->ps.freezetime + g_freezeEnvironmentalRespawnDelay.integer) {
                        Freeze_InstaKill(targ, 1);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    // 23. debug
    if (g_debugDamage.integer) {
        G_Printf("%i: client:%i health:%i damage:%i armor:%i\n", level.time, targ->s.number,
                 targ->health, take, asave);
    }

    // 24. damage tracking
    if (client) {
        client->damage_armor += asave;
        client->damage_blood += take;
        client->ps.persistant[PERS_ATTACKER] = attacker->s.number;
        if (dir) {
            VectorCopy(dir, client->damage_from);
            client->damage_fromWorld = qfalse;
        } else {
            VectorClear(client->damage_from);  // VERIFY #9: zeroed, not targ origin
            client->damage_fromWorld = qtrue;
        }
        client->lasthurt_client[1] = client->lasthurt_client[0];
        client->lasthurt_client[0] = attacker->s.number;
        client->lasthurt_mod[1] = client->lasthurt_mod[0];
        client->lasthurt_mod[0] = mod;
        client->lasthurt_time[1] = client->lasthurt_time[0];
        client->lasthurt_time[0] = level.time;
        // [QL] flag regen on the target when the regen gamerules are enabled (binary 0x1004996f/0x1004997e):
        //      if (g_regenHealth.integer) client->healthRegenActive = 1;
        //      if (g_regenArmor.integer)  client->armorRegenActive  = 1;
        if (g_regenHealth.integer) {
            client->healthRegenActive = qtrue;
        }
        if (g_regenArmor.integer) {
            client->armorRegenActive = qtrue;
        }
    }

    // 25. carrier hurt tracking
    if (g_gametype.integer == GT_CTF || g_gametype.integer == GT_1FCTF ||
        g_gametype.integer == GT_AD) {
        Team_CheckHurtCarrier(targ, attacker);
    }

    // 26. instagib resolution
    if (g_instaGib.integer && client) {
        if (client->ps.powerups[0] != 0) {  // armored: no instagib
            if (attacker->client && targ != attacker) {
                attacker->client->ps.persistant[PERS_HITS]--;
            }
            return;
        }
        switch (mod) {
            case MOD_SHOTGUN:
            case MOD_GAUNTLET:
            case MOD_MACHINEGUN:
            case MOD_GRENADE:
            case MOD_ROCKET:
            case MOD_PLASMA:
            case MOD_RAILGUN:
            case MOD_LIGHTNING:
            case MOD_BFG:
            case MOD_NAIL:
            case MOD_CHAINGUN:
            case MOD_PROXIMITY_MINE:
            case MOD_GRAPPLE:
            case MOD_HMG:
            case MOD_RAILGUN_HEADSHOT:
                targ->health = -39;  // VERIFY #8: -39 (NOT -999)
                if (attacker->client && targ != attacker) {
                    attacker->client->ps.persistant[PERS_ATTACKEE_ARMOR]++;  // lethal-hit kill counter
                }
                break;
            case MOD_GRENADE_SPLASH:
            case MOD_ROCKET_SPLASH:
            case MOD_PLASMA_SPLASH:
            case MOD_BFG_SPLASH:
                if (attacker->client && targ != attacker) {
                    attacker->client->ps.persistant[PERS_HITS]--;
                }
                return;
            default:
                break;
        }
    }

    // 27. no-op guard
    if (take == 0 && asave == 0) {
        return;
    }

    // 28. [QL] g_dropFlag gauntlet flag toss (binary 0x100499cb, inside G_Damage): on a
    // gauntlet kill the victim tosses any flag it carries. Binary gate is g_dropFlag != 0
    // (any bit; the manual /dropflag command separately tests bit 0), mod == MOD_GAUNTLET,
    // targ has a client. Drop point is the victim ps.origin raised 64 units.
    if (g_dropFlag.integer && mod == MOD_GAUNTLET && targ->client) {
        vec3_t flagPoint;
        flagPoint[0] = targ->client->ps.origin[0];
        flagPoint[1] = targ->client->ps.origin[1];
        flagPoint[2] = targ->client->ps.origin[2] + 64.0f;
        G_ThrowFlag(targ, flagPoint);
    }

    // 29. DamagePlum
    if (g_damagePlums.integer && targ->client && attacker->client &&
        attacker != targ && targ->health > 0) {
        if (mod == MOD_SHOTGUN) {
            attacker->client->damagePlum[targ->client->ps.clientNum] += damage;
        } else {
            DamagePlum(attacker, targ, damage, mod);
        }
    }

    // 30. damage stats + round damage
    STAT_AddDamageStat(targ, attacker, mod, damage);
    if (targ->client && BG_IsRoundBasedGameType(g_gametype.integer)) {
        targ->client->round_damage += damage;
    }

    // 31. kill-track + apply damage
    if (targ->health > 0 && (targ->health - take) <= 0 &&
        targ->client && attacker->client && targ != attacker) {
        attacker->client->ps.persistant[PERS_ATTACKEE_ARMOR]++;  // lethal-hit kill counter
    }
    targ->health -= take;
    if (targ->client) {
        targ->s.health = targ->health;
        targ->client->ps.stats[STAT_HEALTH] = targ->health;
        targ->client->ps.stats[STAT_ARMOR] -= asave;
        targ->s.armor = targ->client->ps.stats[STAT_ARMOR];
    }

    // 32. [QL] g_dropDamagedHealth "bleeding health" (binary Drop_DamagedHealth 0x1004f930,
    // called here from G_Damage): the victim sheds health pickups worth the damage just
    // taken. overkill trims the part of the hit that drove health below zero.
    if (g_dropDamagedHealth.integer && targ->client && take != 0) {
        int overkill = 0;
        if (targ->health < 0) {
            overkill = -targ->health;
        }
        Drop_DamagedHealth(attacker, targ, take - overkill);
    }

    // 33. vampiric leech
    if (g_vampiricDamage.value != 0.0f && attacker->client && targ->client && attacker != targ &&
        !OnSameTeam(attacker, targ) && attacker->health > 0 &&
        targ->client->ps.pm_type != PM_DEAD && take != 0 &&
        attacker->health < attacker->client->ps.stats[STAT_MAX_HEALTH] * 2) {
        int overkill = 0;
        int heal;
        if (targ->health < 0) {
            overkill = -targ->health;
        }
        heal = (int)((float)(take - overkill) * g_vampiricDamage.value);
        attacker->health += heal;
        attacker->s.health = attacker->health;
        attacker->client->ps.stats[STAT_HEALTH] = attacker->health;
        if (attacker->health > attacker->client->ps.stats[STAT_MAX_HEALTH] * 2) {
            attacker->health = attacker->client->ps.stats[STAT_MAX_HEALTH] * 2;
            attacker->s.health = attacker->health;
            attacker->client->ps.stats[STAT_HEALTH] = attacker->health;
        }
        attacker->s.health = attacker->client->ps.stats[STAT_HEALTH];
        if (g_debugVampiricDamage.integer) {
            G_Printf("%i: client:%i health:%i damage:%i leeched:%i\n",
                     level.time, targ->s.number, targ->health, take, heal);
        }
    }

    // 34. armor-type deplete
    if (targ->client && armor_tiered.integer && targ->client->ps.stats[STAT_ARMOR] <= 0) {
        targ->client->ps.stats[STAT_ARMORTYPE] = 0;
    }

    // 35. death / pain
    if (targ->health <= 0) {
        // [QL] binary gates FL_NO_KNOCKBACK on !FreezeTagInGame() && !FreezeTagInWarmup()
        // so a frozen body can still be shoved during a live/warmup freeze round.
        if (targ->client && !FreezeTagInGame() && !FreezeTagInWarmup()) {
            targ->flags |= FL_NO_KNOCKBACK;
        }
        if (targ->health < -999) {
            targ->health = -999;
        }
        targ->enemy = attacker;
        targ->die(targ, inflictor, attacker, take, mod);
        if (g_gametype.integer == GT_FREEZE && targ->client) {
            /*
            [QL] Destroy the frozen body only once the auto-thaw window has
            *elapsed*. This read

                level.time < targ->client->respawnTime + g_freezeAutoThawTime.integer

            which is the opposite: true for the whole window and false after it.
            targ->die() has just frozen this player, so respawnTime is around
            level.time and the test passed on the very frame of the freeze -
            every single freeze was destroyed immediately:

                Biker was railed by Major
                Biker was auto-thawed.
                Wrack was railed by Klesk
                Wrack was auto-thawed.

            which is why Freeze Tag looked like players were dying normally even
            once they were being frozen properly.

            Measured from ps.freezetime, the stamp Freeze_PlayerFrozen sets, so
            this agrees with the per-frame timeout in Freeze_ClientThawCheck
            rather than inventing a second clock off respawnTime. On the freezing
            frame the elapsed time is 0, so it no longer fires there.
            */
            if (g_freezeAutoThawTime.integer &&
                level.time - targ->client->ps.freezetime >= g_freezeAutoThawTime.integer) {
                Freeze_InstaKill(targ, 1);
                return;
            }
            Freeze_AutoThaw(targ->client->sess.sessionTeam);
            return;
        }
        return;
    }
    if (targ->pain) {
        targ->pain(targ, attacker, take);
    }
}

/*
============
CanDamage

Returns qtrue if the inflictor can directly damage the target.  Used for
explosions and melee attacks.
============
*/
qboolean CanDamage(gentity_t* targ, vec3_t origin) {
    vec3_t dest;
    trace_t tr;
    vec3_t midpoint;
    vec3_t offsetmins = {-15, -15, -15};
    vec3_t offsetmaxs = {15, 15, 15};

    // use the midpoint of the bounds instead of the origin, because
    // bmodels may have their origin is 0,0,0
    VectorAdd(targ->r.absmin, targ->r.absmax, midpoint);
    VectorScale(midpoint, 0.5, midpoint);

    VectorCopy(midpoint, dest);
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0 || tr.entityNum == targ->s.number)
        return qtrue;

    // this should probably check in the plane of projection,
    // rather than in world coordinate
    VectorCopy(midpoint, dest);
    dest[0] += offsetmaxs[0];
    dest[1] += offsetmaxs[1];
    dest[2] += offsetmaxs[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmaxs[0];
    dest[1] += offsetmins[1];
    dest[2] += offsetmaxs[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmins[0];
    dest[1] += offsetmaxs[1];
    dest[2] += offsetmaxs[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmins[0];
    dest[1] += offsetmins[1];
    dest[2] += offsetmaxs[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmaxs[0];
    dest[1] += offsetmaxs[1];
    dest[2] += offsetmins[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmaxs[0];
    dest[1] += offsetmins[1];
    dest[2] += offsetmins[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmins[0];
    dest[1] += offsetmaxs[1];
    dest[2] += offsetmins[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    VectorCopy(midpoint, dest);
    dest[0] += offsetmins[0];
    dest[1] += offsetmins[1];
    dest[2] += offsetmins[2];
    trap_Trace(&tr, origin, vec3_origin, vec3_origin, dest, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0)
        return qtrue;

    return qfalse;
}

/*
============
G_RadiusDamage
[QL] added inflictor param for splash offset, dflags param, per-target z-offset cvars
============
*/
qboolean G_RadiusDamage(vec3_t origin, gentity_t* inflictor, gentity_t* attacker, float damage, float radius, gentity_t* ignore, int dflags, int mod) {
    float points, dist;
    gentity_t* ent;
    int entityList[MAX_GENTITIES];
    int numListedEntities;
    vec3_t mins, maxs;
    vec3_t v;
    vec3_t dir;
    vec3_t effectiveOrigin;
    vec3_t rocketOffsetOrigin;
    int i, e;
    qboolean hitClient = qfalse;
    qboolean isRocket;
    int weapon;

    if (radius < 1) {
        radius = 1;
    }

    // [QL] determine weapon from inflictor for rocket-specific behavior
    weapon = inflictor ? inflictor->s.weapon : 0;
    isRocket = (weapon == WP_ROCKET_LAUNCHER);

    // [QL] shift effective explosion center along inflictor velocity (binary-verified)
    VectorCopy(origin, effectiveOrigin);
    if (inflictor && g_splashdamageOffset.value != 0.0f) {
        VectorMA(effectiveOrigin, g_splashdamageOffset.value, inflictor->s.pos.trDelta, effectiveOrigin);
    }

    // [QL] rocket-specific splash offset: shifts explosion center along direction (binary-verified)
    // g_rocketsplashOffset default -10.0 moves center backward along missile path,
    // which for downward rocket jumps shifts center UP toward player, increasing self-knockback
    VectorCopy(effectiveOrigin, rocketOffsetOrigin);
    if (isRocket && g_rocketsplashOffset.value != 0.0f) {
        vec3_t rocketDir;
        VectorCopy(inflictor->s.pos.trDelta, rocketDir);
        VectorNormalize(rocketDir);
        VectorMA(effectiveOrigin, g_rocketsplashOffset.value, rocketDir, rocketOffsetOrigin);
    }

    for (i = 0; i < 3; i++) {
        mins[i] = effectiveOrigin[i] - radius;
        maxs[i] = effectiveOrigin[i] + radius;
    }

    numListedEntities = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

    for (e = 0; e < numListedEntities; e++) {
        ent = &g_entities[entityList[e]];

        if (ent == ignore)
            continue;
        if (!ent->takedamage)
            continue;

        // find the distance from the edge of the bounding box
        for (i = 0; i < 3; i++) {
            if (effectiveOrigin[i] < ent->r.absmin[i]) {
                v[i] = ent->r.absmin[i] - effectiveOrigin[i];
            } else if (effectiveOrigin[i] > ent->r.absmax[i]) {
                v[i] = effectiveOrigin[i] - ent->r.absmax[i];
            } else {
                v[i] = 0;
            }
        }

        dist = VectorLength(v);
        if (dist >= radius) {
            continue;
        }

        points = damage * (1.0 - dist / radius);

        // [QL] binary-verified: rockets require double CanDamage check -
        // one from effectiveOrigin and one from rocketOffsetOrigin
        if (CanDamage(ent, effectiveOrigin) && (!isRocket || CanDamage(ent, rocketOffsetOrigin))) {
            if (LogAccuracyHit(ent, attacker)) {
                hitClient = qtrue;
            }
            VectorSubtract(ent->r.currentOrigin, effectiveOrigin, dir);
            // [QL] configurable z-offset: different for self-damage vs hitting others
            if (ent == attacker) {
                dir[2] += g_knockback_z_self.value;
            } else {
                dir[2] += g_knockback_z.value;
            }
            G_Damage(ent, inflictor, attacker, dir, effectiveOrigin, (int)points, dflags | DAMAGE_RADIUS, mod);
        }
    }

    return hitClient;
}

/*
============
G_RadiusDamageThrough
[QL] Like G_RadiusDamage but also damages through walls at reduced range.
If CanDamage fails from effectiveOrigin, tries rocketOffsetOrigin.
If both fail (rockets only), checks reduced radius and applies through-wall damage.
============
*/
qboolean G_RadiusDamageThrough(vec3_t origin, gentity_t* inflictor, gentity_t* attacker, float damage, float radius, gentity_t* ignore, int dflags, int mod) {
    float points, dist;
    gentity_t* ent;
    int entityList[MAX_GENTITIES];
    int numListedEntities;
    vec3_t mins, maxs;
    vec3_t v;
    vec3_t dir;
    vec3_t effectiveOrigin;
    vec3_t rocketOffsetOrigin;
    int i, e;
    qboolean hitClient = qfalse;
    qboolean isRocket;
    float throughRadius;

    if (radius < 1) {
        radius = 1;
    }

    isRocket = (inflictor && inflictor->s.weapon == WP_ROCKET_LAUNCHER);

    for (i = 0; i < 3; i++) {
        mins[i] = origin[i] - radius;
        maxs[i] = origin[i] + radius;
    }

    numListedEntities = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

    VectorCopy(origin, effectiveOrigin);
    VectorCopy(origin, rocketOffsetOrigin);
    if (inflictor) {
        VectorMA(effectiveOrigin, g_splashdamageOffset.value, inflictor->s.pos.trDelta, effectiveOrigin);
        VectorCopy(effectiveOrigin, rocketOffsetOrigin);
        if (isRocket && g_rocketsplashOffset.value != 0.0f) {
            vec3_t rocketDir;
            VectorNormalize2(inflictor->s.pos.trDelta, rocketDir);
            VectorMA(effectiveOrigin, g_rocketsplashOffset.value, rocketDir, rocketOffsetOrigin);
        }
    }

    throughRadius = radius * g_dmgThroughSurfaceDampening.value;

    for (e = 0; e < numListedEntities; e++) {
        ent = &g_entities[entityList[e]];

        if (ent == ignore)
            continue;
        if (!ent->takedamage)
            continue;

        // find distance from edge of bounding box
        for (i = 0; i < 3; i++) {
            if (origin[i] < ent->r.absmin[i]) {
                v[i] = ent->r.absmin[i] - origin[i];
            } else if (origin[i] > ent->r.absmax[i]) {
                v[i] = origin[i] - ent->r.absmax[i];
            } else {
                v[i] = 0;
            }
        }
        dist = VectorLength(v);
        if (dist > radius) {
            continue;
        }

        // Try normal CanDamage from effectiveOrigin
        if (CanDamage(ent, effectiveOrigin)) {
            VectorSubtract(ent->r.currentOrigin, origin, dir);
            goto applydamage;
        }

        // For rockets, try rocketOffsetOrigin
        if (isRocket) {
            if (CanDamage(ent, rocketOffsetOrigin)) {
                VectorSubtract(ent->r.currentOrigin, origin, dir);
                goto applydamage;
            }

            // Through-wall: reduced radius check
            if (dist <= throughRadius) {
                if (CanDamage(ent, origin)) {
                    VectorSubtract(ent->r.currentOrigin, ignore ? ignore->r.currentOrigin : origin, dir);
                    goto applydamage;
                }
            }
        }
        continue;

    applydamage:
        // Hit tracking
        if (ent->client && attacker->client && ent != attacker && !OnSameTeam(ent, attacker)) {
            hitClient = qtrue;
        }

        if (ent == attacker) {
            dir[2] += g_knockback_z_self.value;
        } else {
            dir[2] += g_knockback_z.value;
        }

        // g_playerCylinders skip logic
        if (!g_playerCylinders.integer || !inflictor ||
            inflictor->s.pos.trDelta[2] != 0.0f || ent == attacker) {
            points = damage * (1.0 - dist / radius);
            G_Damage(ent, inflictor, attacker, dir, effectiveOrigin, (int)points,
                     dflags | DAMAGE_RADIUS, mod);
        }
    }

    return hitClient;
}

/*
============
G_WaterRadiusDamage
[QL] Water-specific radius damage for LG discharge (MOD_LIGHTNING_DISCHARGE).
Only applies when g_infiniteAmmo is set and origin is in water.
============
*/
qboolean G_WaterRadiusDamage(vec3_t origin, gentity_t *attacker, float damage, float radius) {
    float points, dist;
    gentity_t *ent;
    int entityList[MAX_GENTITIES];
    int numListedEntities;
    vec3_t mins, maxs;
    vec3_t v, dir;
    int i, e;
    qboolean hitClient = qfalse;

    if (!g_infiniteAmmo.integer)
        return qfalse;

    if (!(trap_PointContents(origin, -1) & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA)))
        return qfalse;

    if (radius < 1) radius = 1;

    for (i = 0; i < 3; i++) {
        mins[i] = origin[i] - radius;
        maxs[i] = origin[i] + radius;
    }

    numListedEntities = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

    for (e = 0; e < numListedEntities; e++) {
        ent = &g_entities[entityList[e]];
        if (!ent->takedamage)
            continue;

        for (i = 0; i < 3; i++) {
            if (origin[i] < ent->r.absmin[i])
                v[i] = ent->r.absmin[i] - origin[i];
            else if (origin[i] > ent->r.absmax[i])
                v[i] = origin[i] - ent->r.absmax[i];
            else
                v[i] = 0;
        }
        dist = VectorLength(v);
        if (dist >= radius)
            continue;

        points = damage * (1.0f - dist / radius);

        if (CanDamage(ent, origin)) {
            if (ent->client && attacker->client && ent != attacker &&
                !OnSameTeam(ent, attacker)) {
                hitClient = qtrue;
            }

            VectorSubtract(ent->r.currentOrigin, origin, dir);
            if (ent == attacker)
                dir[2] += g_knockback_z_self.value;
            else
                dir[2] += g_knockback_z.value;

            G_Damage(ent, NULL, attacker, dir, origin, (int)points,
                     DAMAGE_RADIUS, MOD_LIGHTNING_DISCHARGE);
        }
    }

    return hitClient;
}
