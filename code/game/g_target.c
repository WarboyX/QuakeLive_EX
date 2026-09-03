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
#include "g_local.h"

//==========================================================

/*QUAKED target_give (1 0 0) (-8 -8 -8) (8 8 8)
Gives the activator all the items pointed to.
*/
void Use_Target_Give(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    gentity_t* t;
    trace_t trace;

    if (!activator->client) {
        return;
    }

    if (!ent->target) {
        return;
    }

    memset(&trace, 0, sizeof(trace));
    t = NULL;
    while ((t = G_Find(t, FOFS(targetname), ent->target)) != NULL) {
        if (!t->item) {
            continue;
        }
        Touch_Item(t, activator, &trace);

        // make sure it isn't going to respawn or show any events
        t->nextthink = 0;
        trap_UnlinkEntity(t);
    }
}

void SP_target_give(gentity_t* ent) {
    ent->use = Use_Target_Give;
}

//==========================================================

/*QUAKED target_remove_powerups (1 0 0) (-8 -8 -8) (8 8 8)
takes away all the activators powerups.
Used to drop flight powerups into death puts.
*/
void Use_target_remove_powerups(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    if (!activator->client) {
        return;
    }

    if (activator->client->ps.powerups[PW_REDFLAG]) {
        Team_ReturnFlag(TEAM_RED);
    } else if (activator->client->ps.powerups[PW_BLUEFLAG]) {
        Team_ReturnFlag(TEAM_BLUE);
    } else if (activator->client->ps.powerups[PW_NEUTRALFLAG]) {
        Team_ReturnFlag(TEAM_FREE);
    }

    memset(activator->client->ps.powerups, 0, sizeof(activator->client->ps.powerups));
}

void SP_target_remove_powerups(gentity_t* ent) {
    ent->use = Use_target_remove_powerups;
}

//==========================================================

/*QUAKED target_delay (1 0 0) (-8 -8 -8) (8 8 8)
"wait" seconds to pause before firing targets.
"random" delay variance, total delay = delay +/- random seconds
*/
void Think_Target_Delay(gentity_t* ent) {
    G_UseTargets(ent, ent->activator);
}

void Use_Target_Delay(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    ent->nextthink = level.time + (ent->wait + ent->random * crandom()) * 1000;
    ent->think = Think_Target_Delay;
    ent->activator = activator;
}

void SP_target_delay(gentity_t* ent) {
    // check delay for backwards compatibility
    if (!G_SpawnFloat("delay", "0", &ent->wait)) {
        G_SpawnFloat("wait", "1", &ent->wait);
    }

    if (!ent->wait) {
        ent->wait = 1;
    }
    ent->use = Use_Target_Delay;
}

//==========================================================

/*QUAKED target_score (1 0 0) (-8 -8 -8) (8 8 8)
"count" number of points to add, default 1

The activator is given this many points.
*/
void Use_Target_Score(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    AddScore(activator, ent->r.currentOrigin, ent->count);
}

void SP_target_score(gentity_t* ent) {
    if (!ent->count) {
        ent->count = 1;
    }
    ent->use = Use_Target_Score;
}

//==========================================================

/*QUAKED target_print (1 0 0) (-8 -8 -8) (8 8 8) redteam blueteam private
"message"	text to print
If "private", only the activator gets the message.  If no checks, all clients get the message.
*/
void Use_Target_Print(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    if (activator->client && (ent->spawnflags & 4)) {
        trap_SendServerCommand(activator - g_entities, va("cp \"%s\"", ent->message));
        return;
    }

    if (ent->spawnflags & 3) {
        if (ent->spawnflags & 1) {
            G_TeamCommand(TEAM_RED, va("cp \"%s\"", ent->message));
        }
        if (ent->spawnflags & 2) {
            G_TeamCommand(TEAM_BLUE, va("cp \"%s\"", ent->message));
        }
        return;
    }

    trap_SendServerCommand(-1, va("cp \"%s\"", ent->message));
}

void SP_target_print(gentity_t* ent) {
    ent->use = Use_Target_Print;
}

//==========================================================

/*QUAKED target_speaker (1 0 0) (-8 -8 -8) (8 8 8) looped-on looped-off global activator
"noise"		wav file to play

A global sound will play full volume throughout the level.
Activator sounds will play on the player that activated the target.
Global and activator sounds can't be combined with looping.
Normal sounds play each time the target is used.
Looped sounds will be toggled by use functions.
Multiple identical looping sounds will just increase volume without any speed cost.
"wait" : Seconds between auto triggerings, 0 = don't auto trigger
"random"	wait variance, default is 0
*/
void Use_Target_Speaker(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    // [QL] a player attached to the grapple hook does not trigger the speaker
    if (activator->client && activator->client->hook) {
        return;
    }

    if (ent->spawnflags & 3) {  // looping sound toggles
        if (ent->s.loopSound)
            ent->s.loopSound = 0;  // turn it off
        else
            ent->s.loopSound = ent->noise_index;  // start it
    } else {                                      // normal sound
        if (ent->spawnflags & 8) {
            G_AddEvent(activator, EV_GENERAL_SOUND, ent->noise_index);
        } else if (ent->spawnflags & 4) {
            G_AddEvent(ent, EV_GLOBAL_SOUND, ent->noise_index);
        } else {
            G_AddEvent(ent, EV_GENERAL_SOUND, ent->noise_index);
        }
    }
}

void SP_target_speaker(gentity_t* ent) {
    char buffer[MAX_QPATH];
    char* s;

    G_SpawnFloat("wait", "0", &ent->wait);
    G_SpawnFloat("random", "0", &ent->random);

    if (!G_SpawnString("noise", "NOSOUND", &s)) {
        G_Error("target_speaker without a noise key at %s", vtos(ent->s.origin));
    }

    // force all client relative sounds to be "activator" speakers that
    // play on the entity that activates it
    if (s[0] == '*') {
        ent->spawnflags |= 8;
    }

    /*
    [QL] Add an extension only when there is not one already.

    id's test was `!strstr(s, ".wav")`, written when .wav was the only sound
    format there was. Quake Live's maps point their speakers at .ogg files, so
    "sound/nctsdm1/waves.ogg" fails that test and is turned into
    "sound/nctsdm1/waves.ogg.wav", which does not exist.

    That is not a silent failure. S_RegisterSound returns 0 for a sound it could
    not load, and handle 0 is not silence: S_BeginRegistration registers
    sound/feedback/hit.wav first precisely so that slot is occupied. So every
    ambient speaker on such a map ends up looping the hit beep at its own
    position, attenuated by distance - a repeating sound that gets louder as you
    walk towards nothing in particular. arkinholm has five of them (waves,
    gulls, cave_drips, fire1, frogs) and the console names all five.
    */
    if (!COM_GetExtension(s)[0]) {
        Com_sprintf(buffer, sizeof(buffer), "%s.wav", s);
    } else {
        Q_strncpyz(buffer, s, sizeof(buffer));
    }
    ent->noise_index = G_SoundIndex(buffer);

    // a repeating speaker can be done completely client side
    ent->s.eType = ET_SPEAKER;
    ent->s.eventParm = ent->noise_index;
    ent->s.frame = ent->wait * 10;
    ent->s.clientNum = ent->random * 10;

    // check for prestarted looping sound
    if (ent->spawnflags & 1) {
        ent->s.loopSound = ent->noise_index;
    }

    ent->use = Use_Target_Speaker;

    if (ent->spawnflags & 4) {
        ent->r.svFlags |= SVF_BROADCAST;
    }

    VectorCopy(ent->s.origin, ent->s.pos.trBase);

    // must link the entity so we get areas and clusters so
    // the server can determine who to send updates to
    trap_LinkEntity(ent);
}

//==========================================================

/*QUAKED target_laser (0 .5 .8) (-8 -8 -8) (8 8 8) START_ON
When triggered, fires a laser.  You can either set a target or a direction.
*/
void target_laser_think(gentity_t* self) {
    vec3_t end;
    trace_t tr;
    vec3_t point;

    // if pointed at another entity, set movedir to point at it
    if (self->enemy) {
        VectorMA(self->enemy->s.origin, 0.5, self->enemy->r.mins, point);
        VectorMA(point, 0.5, self->enemy->r.maxs, point);
        VectorSubtract(point, self->s.origin, self->movedir);
        VectorNormalize(self->movedir);
    }

    // fire forward and see what we hit
    VectorMA(self->s.origin, 2048, self->movedir, end);

    trap_Trace(&tr, self->s.origin, NULL, NULL, end, self->s.number, CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_CORPSE);

    if (tr.entityNum) {
        // hurt it if we can
        G_Damage(&g_entities[tr.entityNum], self, self->activator, self->movedir,
                 tr.endpos, self->damage, DAMAGE_NO_KNOCKBACK, MOD_TARGET_LASER);
    }

    VectorCopy(tr.endpos, self->s.origin2);

    trap_LinkEntity(self);
    self->nextthink = level.time + (1000 / sv_fps.integer);  // binary uses sv_fps
}

void target_laser_on(gentity_t* self) {
    if (!self->activator)
        self->activator = self;
    target_laser_think(self);
}

void target_laser_off(gentity_t* self) {
    trap_UnlinkEntity(self);
    self->nextthink = 0;
}

void target_laser_use(gentity_t* self, gentity_t* other, gentity_t* activator) {
    self->activator = activator;
    if (self->nextthink > 0)
        target_laser_off(self);
    else
        target_laser_on(self);
}

void target_laser_start(gentity_t* self) {
    gentity_t* ent;

    self->s.eType = ET_BEAM;

    if (self->target) {
        ent = G_Find(NULL, FOFS(targetname), self->target);
        if (!ent) {
            G_Printf("%s at %s: %s is a bad target\n", self->classname, vtos(self->s.origin), self->target);
        }
        self->enemy = ent;
    } else {
        G_SetMovedir(self->s.angles, self->movedir);
    }

    self->use = target_laser_use;
    self->think = target_laser_think;

    if (!self->damage) {
        self->damage = 1;
    }

    if (self->spawnflags & 1)
        target_laser_on(self);
    else
        target_laser_off(self);
}

void SP_target_laser(gentity_t* self) {
    // let everything else get spawned before we start firing
    self->think = target_laser_start;
    self->nextthink = level.time + (1000 / sv_fps.integer);  // binary uses sv_fps
}

//==========================================================

void target_teleporter_use(gentity_t* self, gentity_t* other, gentity_t* activator) {
    gentity_t* dest;

    if (!activator->client)
        return;
    dest = G_PickTarget(self->target);
    if (!dest) {
        G_Printf("Couldn't find teleporter destination\n");
        return;
    }

    TeleportPlayer(activator, dest->s.origin, dest->s.angles);
}

/*QUAKED target_teleporter (1 0 0) (-8 -8 -8) (8 8 8)
The activator will be teleported away.
*/
void SP_target_teleporter(gentity_t* self) {
    if (!self->targetname)
        G_Printf("untargeted %s at %s\n", self->classname, vtos(self->s.origin));

    self->use = target_teleporter_use;
}

//==========================================================

/*QUAKED target_relay (.5 .5 .5) (-8 -8 -8) (8 8 8) RED_ONLY BLUE_ONLY RANDOM
This doesn't perform any actions except fire its targets.
The activator can be forced to be from a certain team.
if RANDOM is checked, only one of the targets will be fired, not all of them
*/
void target_relay_use(gentity_t* self, gentity_t* other, gentity_t* activator) {
    if ((self->spawnflags & 1) && activator->client && activator->client->sess.sessionTeam != TEAM_RED) {
        return;
    }
    if ((self->spawnflags & 2) && activator->client && activator->client->sess.sessionTeam != TEAM_BLUE) {
        return;
    }
    if (self->spawnflags & 4) {
        gentity_t* ent;

        ent = G_PickTarget(self->target);
        if (ent && ent->use) {
            ent->use(ent, self, activator);
        }
        return;
    }
    G_UseTargets(self, activator);
}

void SP_target_relay(gentity_t* self) {
    self->use = target_relay_use;
}

//==========================================================

/*QUAKED target_kill (.5 .5 .5) (-8 -8 -8) (8 8 8)
Kills the activator.
*/
void target_kill_use(gentity_t* self, gentity_t* other, gentity_t* activator) {
    G_Damage(activator, NULL, NULL, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
}

void SP_target_kill(gentity_t* self) {
    self->use = target_kill_use;
}

//==========================================================

/*QUAKED target_cvar (.5 .5 .5) (-8 -8 -8) (8 8 8)
[QL] Sets a server cvar when used. The cvar name comes from the "cvar" key,
the value from the "cvarValue" key. The value is stored in ent->random.
*/
// Address: 0x10067cb0
void Use_Target_Cvar(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    // binary formats the cvar name through va("%s", ...) as well as the value
    trap_Cvar_Set(va("%s", ent->cvar), va("%f", ent->random));
}

// Address: 0x10067d00
void SP_target_cvar(gentity_t* ent) {
    char* s;

    G_SpawnString("cvarValue", "-1.0f", &s);
    ent->random = atof(s);
    ent->use = Use_Target_Cvar;
}

//==========================================================

// [QL] Steam achievement ids granted by target_achievement, indexed 1..7
static const int target_achievementIds[8] = { 0, 2, 3, 4, 5, 6, 7, 8 };

/*QUAKED target_achievement (1 0 0) (-8 -8 -8) (8 8 8)
[QL] Grants a Steam achievement to the activator. The "achievement" key picks
which one (1-7).
*/
// Address: 0x10067d40
void Use_Target_Achievement(gentity_t* ent, gentity_t* other, gentity_t* activator) {
    int index;

    if (!activator->client) {
        return;
    }

    index = ent->health;
    if (index == 0 || index >= 8) {
        return;
    }

    if (!trap_HasAchievement(activator->client->ps.clientNum, target_achievementIds[index])) {
        trap_SetAchievement(activator->client->ps.clientNum, target_achievementIds[index]);
    }
}

// Address: 0x10067dc0
void SP_target_achievement(gentity_t* ent) {
    char* s;

    // binary reads the "id" spawn key (default "0"), not "achievement"
    G_SpawnString("id", "0", &s);
    ent->health = atoi(s);
    ent->use = Use_Target_Achievement;
}

/*QUAKED target_position (0 0.5 0) (-4 -4 -4) (4 4 4)
Used as a positional target for in-game calculation, like jumppad targets.
*/
void SP_target_position(gentity_t* self) {
    G_SetOrigin(self, self->s.origin);
}

static void target_location_linkup(gentity_t* ent) {
    int i;
    int n;
    int overflow = 0;

    if (level.locationLinked)
        return;

    level.locationLinked = qtrue;

    level.locationHead = NULL;

    trap_SetConfigstring(CS_LOCATIONS, "unknown");

    /*
    [QL] Stop at MAX_LOCATIONS.

    This loop had no bound on n, and wrote trap_SetConfigstring(CS_LOCATIONS + n)
    for every target_location in the map. The block is only MAX_LOCATIONS (64)
    entries - CS_LOCATIONS 593 through 656 - and everything the protocol needs
    sits immediately after it:

        657 CS_LAST_GENERIC      658 CS_FLAGSTATUS      659 CS_SCORES1PLAYER
        661 CS_ROUND_WARMUP      662 CS_ROUND_START_TIME
        663 CS_TEAMCOUNT_RED     664 CS_TEAMCOUNT_BLUE  665 CS_SHADERSTATE
        666 CS_NEXTMAP           669 CS_PAUSE_START_TIME
        671 CS_TIMEOUTS_RED      692 CS_REDTEAMBASE     693 CS_BLUETEAMBASE

    So a map with more than 64 of them overwrote flag status, round state, team
    counts, the next map, the pause and timeout clocks and the award slots with
    location names - silently, because SetConfigstring does not care what it is
    given. That is what put "Main Entrance" and "Blue Courtyard" on the CTF
    scoreboard's team headers: those are CS 692 and 693, which is
    CS_LOCATIONS + 99 and + 100, so they are simply the map's hundredth and
    hundred-and-first locations landing where the base names belong.

    Raising MAX_LOCATIONS is not the fix. The configstring layout is the wire
    protocol, and moving CS_LAST_GENERIC would put every index after it at a
    different number than a stock Quake Live client expects - the "Seen by:
    stock QL only" trap from CLAUDE.md. The block is the size it is; what has to
    change is writing past the end of it.

    Locations past the limit keep index 0, which is the "unknown" entry set
    above, so they degrade to an unnamed location instead of corrupting the
    match. The warning names the map's count because a map quietly losing its
    location names is worth knowing about.
    */
    for (i = 0, ent = g_entities, n = 1;
         i < level.num_entities;
         i++, ent++) {
        if (ent->classname && !Q_stricmp(ent->classname, "target_location")) {
            if (n < MAX_LOCATIONS) {
                // lets overload some variables!
                ent->health = n;  // use for location marking
                trap_SetConfigstring(CS_LOCATIONS + n, ent->message);
                n++;
                ent->nextTrain = level.locationHead;
                level.locationHead = ent;
            } else {
                /*
                [QL] Left out of the list entirely, not linked with index 0.

                Linking them with the "unknown" index made Team_GetLocation able
                to *win* with one: it picks the nearest entity in PVS, so a
                player standing beside an over-limit location got location 0 and
                the overlay read ALIVE - while a few steps away, next to a
                location that did make the cut, it read properly. That is the
                reported "blue base locations are less defined than red base":
                the map's locations are numbered in spawn order, so the 22 that
                overflow are not spread evenly around the map, they are whichever
                end of it got built last.

                Dropping them from the list makes the nearest *named* location
                win instead, which is a slightly wrong name rather than no name.
                */
                ent->health = 0;
                overflow++;
            }
        }
    }

    if (overflow > 0) {
        G_Printf("^3WARNING: this map has %i target_location entities, %i more than "
                 "the %i the protocol has room for. The extra ones report "
                 "\"unknown\" rather than being written past the end of the "
                 "configstring block, which would overwrite flag status, round "
                 "state, team counts and the next map.\n",
                 n - 1 + overflow, overflow, MAX_LOCATIONS - 1);
    }

    // All linked together now
}

/*QUAKED target_location (0 0.5 0) (-8 -8 -8) (8 8 8)
Set "message" to the name of this location.
Set "count" to 0-7 for color.
0:white 1:red 2:green 3:yellow 4:blue 5:cyan 6:magenta 7:white

Closest target_location in sight used for the location, if none
in site, closest in distance
*/
void SP_target_location(gentity_t* self) {
    self->think = target_location_linkup;
    self->nextthink = level.time + 200;  // Let them all spawn first

    G_SetOrigin(self, self->s.origin);
}
