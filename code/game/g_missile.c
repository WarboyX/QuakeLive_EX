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

// Binary uses 1000/sv_fps.integer instead of hardcoded 50ms
#define MISSILE_PRESTEP_TIME (1000 / sv_fps.integer)

// [QL] s.eFlags bit set by weapon_nailgun_fire on a bouncing nail. G_MissileImpact
// tests the bounce mask as 0x100030 (EF_BOUNCE|EF_BOUNCE_HALF|EF_QL_NAILBOUNCE) and
// caps nail bounces at g_nailbounce. Not yet named in bg_public.h (see report).
#define EF_QL_NAILBOUNCE 0x100000

/*
================
G_BounceMissile
// Address: 0x1005b3c0
// [QL] Ghidra mislabels this "G_BounceItem"; the .so symbol at the g_missile.c
// cluster (0x86140) is G_BounceMissile. The physicsBounce variant Ghidra labels
// "G_BounceMissile" (0x10050c70) is G_BounceProjectile (.so 0x98680),
// used only by G_RunItem.
================
*/
void G_BounceMissile(gentity_t* ent, trace_t* trace) {
    vec3_t velocity;
    float dot;
    int hitTime;

    // reflect the velocity on the trace plane
    hitTime = ( level.time - level.frametime ) + (level.time - ( level.time - level.frametime )) * trace->fraction;
    BG_EvaluateTrajectoryDelta(&ent->s.pos, hitTime, velocity);
    dot = DotProduct(velocity, trace->plane.normal);
    VectorMA(velocity, -2 * dot, trace->plane.normal, ent->s.pos.trDelta);

    if (ent->s.eFlags & EF_BOUNCE_HALF) {
        VectorScale(ent->s.pos.trDelta, 0.65, ent->s.pos.trDelta);
        // check for stop
        if (trace->plane.normal[2] > 0.2 && VectorLength(ent->s.pos.trDelta) < 40) {
            // [QL] binary snaps endpos, then G_SetOrigin; it does NOT set ent->s.time
            SnapVector(trace->endpos);
            G_SetOrigin(ent, trace->endpos);
            return;
        }
    }

    VectorAdd(ent->r.currentOrigin, trace->plane.normal, ent->r.currentOrigin);
    VectorCopy(ent->r.currentOrigin, ent->s.pos.trBase);
    ent->s.pos.trTime = level.time;
}

/*
================
G_ExplodeMissile

Explode a missile without an impact
================
*/
void G_ExplodeMissile(gentity_t* ent) {
    vec3_t dir;
    vec3_t origin;

    BG_EvaluateTrajectory(&ent->s.pos, level.time, origin);
    SnapVector(origin);
    G_SetOrigin(ent, origin);

    // we don't have a valid direction, so just point straight up
    dir[0] = dir[1] = 0;
    dir[2] = 1;

    ent->s.eType = ET_GENERAL;
    G_AddEvent(ent, EV_MISSILE_MISS, DirToByte(dir));

    ent->freeAfterEvent = qtrue;

    // splash damage
    // [QL] binary-verified: time-expired missiles pass NULL inflictor (no splash offset)
    if (ent->splashDamage) {
        if (G_RadiusDamage(ent->r.currentOrigin, NULL, ent->parent, ent->splashDamage, ent->splashRadius, ent, 0, ent->splashMethodOfDeath)) {
            g_entities[ent->r.ownerNum].client->accuracy_hits++;
            g_entities[ent->r.ownerNum].client->expandedStats.shotsHit[ent->s.weapon]++;
            // [QL] binary inlines STAT_AddScore (engine syscall table +0x324); it
            //      internally gates on !warmupTime && !scoringDisabled.
            if (g_gametype.integer != GT_RACE) {
                STAT_AddScore(&g_entities[ent->r.ownerNum], ent->s.weapon, 1);
            }
            if (BG_IsRoundBasedGameType(g_gametype.integer)) {
                g_entities[ent->r.ownerNum].client->round_hits++;
            }
        }
    }

    trap_LinkEntity(ent);
}

/*
================
ProximityMine_Explode
================
*/
static void ProximityMine_Explode(gentity_t* mine) {
    G_ExplodeMissile(mine);
    // if the prox mine has a trigger free it
    if (mine->activator) {
        G_FreeEntity(mine->activator);
        mine->activator = NULL;
    }
}

/*
================
ProximityMine_Die
================
*/
static void ProximityMine_Die(gentity_t* ent, gentity_t* inflictor, gentity_t* attacker, int damage, int mod) {
    ent->think = ProximityMine_Explode;
    ent->nextthink = level.time + 1;
}

/*
================
ProximityMine_Trigger
================
*/
void ProximityMine_Trigger(gentity_t* trigger, gentity_t* other, trace_t* trace) {
    vec3_t v;
    gentity_t* mine;

    if (!other->client) {
        return;
    }

    // trigger is a cube, do a distance test now to act as if it's a sphere
    VectorSubtract(trigger->s.pos.trBase, other->s.pos.trBase, v);
    if (VectorLength(v) > trigger->parent->splashRadius) {
        return;
    }

    if (g_gametype.integer >= GT_TEAM) {
        // don't trigger same team mines
        if (trigger->parent->s.generic1 == other->client->sess.sessionTeam) {
            return;
        }
    }

    // ok, now check for ability to damage so we don't get triggered through walls, closed doors, etc...
    if (!CanDamage(other, trigger->s.pos.trBase)) {
        return;
    }

    // trigger the mine!
    mine = trigger->parent;
    mine->s.loopSound = 0;
    G_AddEvent(mine, EV_PROXIMITY_MINE_TRIGGER, 0);
    mine->nextthink = level.time + 500;

    G_FreeEntity(trigger);
}

/*
================
ProximityMine_Activate
================
*/
static void ProximityMine_Activate(gentity_t* ent) {
    gentity_t* trigger;
    float r;

    ent->think = ProximityMine_Explode;
    // [QL] g_proxMineTimeout is in SECONDS in QL (default 20s), binary does *1000 here
    // (verified from QLDS qagamex86.dll: IMUL EAX, EAX, 0x3e8 at 0x1005b8b1)
    ent->nextthink = level.time + g_proxMineTimeout.integer * 1000;

    ent->takedamage = qtrue;
    ent->health = 1;
    ent->die = ProximityMine_Die;

    ent->s.loopSound = G_SoundIndex("sound/weapons/proxmine/wstbtick.wav");

    // build the proximity trigger
    trigger = G_Spawn();

    trigger->classname = "proxmine_trigger";

    r = ent->splashRadius;
    VectorSet(trigger->r.mins, -r, -r, -r);
    VectorSet(trigger->r.maxs, r, r, r);

    G_SetOrigin(trigger, ent->s.pos.trBase);

    trigger->parent = ent;
    trigger->r.contents = CONTENTS_TRIGGER;
    trigger->touch = ProximityMine_Trigger;

    trap_LinkEntity(trigger);

    // set pointer to trigger so the entity can be freed when the mine explodes
    ent->activator = trigger;
}

/*
================
ProximityMine_ExplodeOnPlayer
================
*/
static void ProximityMine_ExplodeOnPlayer(gentity_t* mine) {
    gentity_t* player;

    player = mine->enemy;
    player->client->ps.eFlags &= ~EF_TICKING;

    if (player->client->invulnerabilityTime > level.time) {
        G_Damage(player, mine->parent, mine->parent, vec3_origin, mine->s.origin, 1000, DAMAGE_NO_KNOCKBACK, MOD_JUICED);
        player->client->invulnerabilityTime = 0;
        G_TempEntity(player->client->ps.origin, EV_JUICED);
    } else {
        G_SetOrigin(mine, player->s.pos.trBase);
        // make sure the explosion gets to the client
        mine->r.svFlags &= ~SVF_NOCLIENT;
        mine->splashMethodOfDeath = MOD_PROXIMITY_MINE;
        G_ExplodeMissile(mine);
    }
}

/*
================
ProximityMine_Player
================
*/
static void ProximityMine_Player(gentity_t* mine, gentity_t* player) {
    if (mine->s.eFlags & EF_NODRAW) {
        return;
    }

    G_AddEvent(mine, EV_PROXIMITY_MINE_STICK, 0);

    if (player->s.eFlags & EF_TICKING) {
        // [QL] binary (0x1005bb20) only accumulates splashDamage; the Q3
        //      splashRadius *= 1.5 line is NOT present.
        player->activator->splashDamage += mine->splashDamage;
        mine->think = G_FreeEntity;
        mine->nextthink = level.time;
        return;
    }

    player->client->ps.eFlags |= EF_TICKING;
    player->activator = mine;

    mine->s.eFlags |= EF_NODRAW;
    mine->r.svFlags |= SVF_NOCLIENT;
    mine->s.pos.trType = TR_LINEAR;
    VectorClear(mine->s.pos.trDelta);

    mine->enemy = player;
    mine->think = ProximityMine_ExplodeOnPlayer;
    if (player->client->invulnerabilityTime > level.time) {
        mine->nextthink = level.time + 2 * 1000;
    } else {
        mine->nextthink = level.time + 10 * 1000;
    }
}

/*
================
G_MissileImpact
================
*/
void G_MissileImpact(gentity_t* ent, trace_t* trace) {
    gentity_t* other;
    qboolean hitClient = qfalse;
    vec3_t forward, impactpoint, bouncedir;
    int eFlags;
    other = &g_entities[trace->entityNum];

    // check for bounce
    // [QL] binary mask is 0x100030 (EF_BOUNCE|EF_BOUNCE_HALF|EF_QL_NAILBOUNCE).
    //      Only EF_QL_NAILBOUNCE missiles are capped (g_nailbounce); every bounce
    //      increments bouncecount.
    if (!other->takedamage &&
        (ent->s.eFlags & (EF_BOUNCE | EF_BOUNCE_HALF | EF_QL_NAILBOUNCE)) &&
        (!(ent->s.eFlags & EF_QL_NAILBOUNCE) || ent->bouncecount < g_nailbounce.integer)) {
        G_BounceMissile(ent, trace);
        G_AddEvent(ent, EV_GRENADE_BOUNCE, 0);
        ent->bouncecount++;
        return;
    }

    if (other->takedamage) {
        if (ent->s.weapon != WP_PROX_LAUNCHER) {
            if (other->client && other->client->invulnerabilityTime > level.time) {
                //
                VectorCopy(ent->s.pos.trDelta, forward);
                VectorNormalize(forward);
                if (G_InvulnerabilityEffect(other, forward, ent->s.pos.trBase, impactpoint, bouncedir)) {
                    VectorCopy(bouncedir, trace->plane.normal);
                    eFlags = ent->s.eFlags & EF_BOUNCE_HALF;
                    ent->s.eFlags &= ~EF_BOUNCE_HALF;
                    G_BounceMissile(ent, trace);
                    ent->s.eFlags |= eFlags;
                }
                ent->target_ent = other;
                return;
            }
        }
    }

    // impact damage
    if (other->takedamage) {
        // FIXME: wrong damage direction?
        if (ent->damage) {
            vec3_t velocity;

            if (LogAccuracyHit(other, &g_entities[ent->r.ownerNum])) {
                g_entities[ent->r.ownerNum].client->accuracy_hits++;
                g_entities[ent->r.ownerNum].client->expandedStats.shotsHit[ent->s.weapon]++;
                if (g_gametype.integer != GT_RACE) {
                    STAT_AddScore(&g_entities[ent->r.ownerNum], ent->s.weapon, 1);
                }
                hitClient = qtrue;
                if (BG_IsRoundBasedGameType(g_gametype.integer)) {
                    g_entities[ent->r.ownerNum].client->round_hits++;
                }
            }
            BG_EvaluateTrajectoryDelta(&ent->s.pos, level.time, velocity);
            if (VectorLength(velocity) == 0) {
                velocity[2] = 1;  // stepped on a grenade
            }
            G_Damage(other, ent, &g_entities[ent->r.ownerNum], velocity,
                     ent->s.origin, ent->damage,
                     0, ent->methodOfDeath);
        }
    }

    if (ent->s.weapon == WP_PROX_LAUNCHER) {
        if (ent->s.pos.trType != TR_GRAVITY) {
            return;
        }

        // if it's a player, stick it on to them (flag them and remove this entity)
        if (other->s.eType == ET_PLAYER && other->health > 0) {
            ProximityMine_Player(ent, other);
            return;
        }

        SnapVectorTowards(trace->endpos, ent->s.pos.trBase);
        G_SetOrigin(ent, trace->endpos);
        ent->s.pos.trType = TR_STATIONARY;
        VectorClear(ent->s.pos.trDelta);

        G_AddEvent(ent, EV_PROXIMITY_MINE_STICK, trace->surfaceFlags);

        ent->think = ProximityMine_Activate;
        ent->nextthink = level.time + 2000;

        vectoangles(trace->plane.normal, ent->s.angles);
        ent->s.angles[0] += 90;

        // link the prox mine to the other entity
        ent->enemy = other;
        ent->die = ProximityMine_Die;
        VectorCopy(trace->plane.normal, ent->movedir);
        VectorSet(ent->r.mins, -4, -4, -4);
        VectorSet(ent->r.maxs, 4, 4, 4);
        trap_LinkEntity(ent);

        return;
    }

    if (!strcmp(ent->classname, "hook")) {
        gentity_t* nent;
        vec3_t v;

        nent = G_Spawn();
        if (other->takedamage && other->client) {
            G_AddEvent(nent, EV_MISSILE_HIT, DirToByte(trace->plane.normal));
            nent->s.otherEntityNum = other->s.number;

            ent->enemy = other;

            v[0] = other->r.currentOrigin[0] + (other->r.mins[0] + other->r.maxs[0]) * 0.5;
            v[1] = other->r.currentOrigin[1] + (other->r.mins[1] + other->r.maxs[1]) * 0.5;
            v[2] = other->r.currentOrigin[2] + (other->r.mins[2] + other->r.maxs[2]) * 0.5;

            SnapVectorTowards(v, ent->s.pos.trBase);  // save net bandwidth
        } else {
            VectorCopy(trace->endpos, v);
            G_AddEvent(nent, EV_MISSILE_MISS, DirToByte(trace->plane.normal));
            ent->enemy = NULL;
        }

        SnapVectorTowards(v, ent->s.pos.trBase);  // save net bandwidth

        nent->freeAfterEvent = qtrue;
        // change over to a normal entity right at the point of impact
        nent->s.eType = ET_GENERAL;
        ent->s.eType = ET_GRAPPLE;

        G_SetOrigin(ent, v);
        G_SetOrigin(nent, v);

        ent->think = Weapon_HookThink;
        ent->nextthink = level.time - MISSILE_PRESTEP_TIME;  // [QL] think immediately

        ent->parent->client->ps.pm_flags |= PMF_GRAPPLE_PULL;
        VectorCopy(ent->r.currentOrigin, ent->parent->client->ps.grapplePoint);

        trap_LinkEntity(ent);
        trap_LinkEntity(nent);

        return;
    }

    // is it cheaper in bandwidth to just remove this ent and create a new
    // one, rather than changing the missile into the explosion?

    if (other->takedamage && other->client) {
        G_AddEvent(ent, EV_MISSILE_HIT, DirToByte(trace->plane.normal));
        ent->s.otherEntityNum = other->s.number;
    } else if (trace->surfaceFlags & SURF_METALSTEPS) {
        G_AddEvent(ent, EV_MISSILE_MISS_METAL, DirToByte(trace->plane.normal));
    } else {
        G_AddEvent(ent, EV_MISSILE_MISS, DirToByte(trace->plane.normal));
    }

    ent->freeAfterEvent = qtrue;

    // change over to a normal entity right at the point of impact
    ent->s.eType = ET_GENERAL;

    SnapVectorTowards(trace->endpos, ent->s.pos.trBase);  // save net bandwidth

    G_SetOrigin(ent, trace->endpos);

    // [QL] Rocket splash-through-walls check
    {
        int isRocketSplashThrough = 0;
        if (ent->s.weapon == WP_ROCKET_LAUNCHER &&
            (g_forceDmgThroughSurface.integer || (trace->surfaceFlags & SURF_NOMARKS))) {
            // check angular threshold: dot product of surface normal against downward
            float dot = -trace->plane.normal[2];
            if (dot <= g_dmgThroughSurfaceAngularThreshold.value) {
                vec3_t testStart;
                trace_t tr2;
                VectorMA(trace->endpos, g_dmgThroughSurfaceDistance.value, trace->plane.normal, testStart);
                trap_Trace(&tr2, testStart, NULL, NULL, trace->endpos, ENTITYNUM_NONE, MASK_SHOT);
                if (tr2.fraction < 1.0f && !tr2.startsolid && !tr2.allsolid) {
                    isRocketSplashThrough = 1;
                }
            }
        }

        // splash damage (doesn't apply to person directly hit)
        if (ent->splashDamage) {
            int hit;
            if (isRocketSplashThrough) {
                hit = G_RadiusDamageThrough(trace->endpos, ent, ent->parent, ent->splashDamage, ent->splashRadius,
                                            other, 0, ent->splashMethodOfDeath);
            } else {
                hit = G_RadiusDamage(trace->endpos, ent, ent->parent, ent->splashDamage, ent->splashRadius,
                                     other, 0, ent->splashMethodOfDeath);
            }
            if (hit && !hitClient) {
                g_entities[ent->r.ownerNum].client->accuracy_hits++;
                g_entities[ent->r.ownerNum].client->expandedStats.shotsHit[ent->s.weapon]++;
                if (g_gametype.integer != GT_RACE) {
                    STAT_AddScore(&g_entities[ent->r.ownerNum], ent->s.weapon, 1);
                }
                if (BG_IsRoundBasedGameType(g_gametype.integer)) {
                    g_entities[ent->r.ownerNum].client->round_hits++;
                }
            }
        }
    }

    trap_LinkEntity(ent);
}

/*
================
G_RunMissile
================
*/
void G_RunMissile(gentity_t* ent) {
    vec3_t origin;
    trace_t tr;
    int passent;

    // get current position
    BG_EvaluateTrajectory(&ent->s.pos, level.time, origin);

    // if this missile bounced off an invulnerability sphere
    if (ent->target_ent) {
        passent = ent->target_ent->s.number;
    }

    // prox mines that left the owner bbox will attach to anything, even the owner
    else if (ent->s.weapon == WP_PROX_LAUNCHER && ent->count) {
        passent = ENTITYNUM_NONE;
    }

    else {
        // ignore interactions with the missile owner
        passent = ent->r.ownerNum;
    }
    // trace a line from the previous position to the current position
    trap_Trace(&tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin, passent, ent->clipmask);

    if (tr.startsolid || tr.allsolid) {
        // make sure the tr.entityNum is set to the entity we're stuck in
        trap_Trace(&tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, ent->r.currentOrigin, passent, ent->clipmask);
        tr.fraction = 0;
    } else {
        VectorCopy(tr.endpos, ent->r.currentOrigin);
    }

    trap_LinkEntity(ent);

    if (tr.fraction != 1) {
        // never explode or bounce on sky
        if (tr.surfaceFlags & SURF_NOIMPACT) {
            // If grapple, reset owner
            if (ent->parent && ent->parent->client && ent->parent->client->hook == ent) {
                ent->parent->client->hook = NULL;
            }
            G_FreeEntity(ent);
            return;
        }
        G_MissileImpact(ent, &tr);
        if (ent->s.eType != ET_MISSILE) {
            return;  // exploded
        }
    }

    // if the prox mine wasn't yet outside the player body
    if (ent->s.weapon == WP_PROX_LAUNCHER && !ent->count) {
        // check if the prox mine is outside the owner bbox
        trap_Trace(&tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, ent->r.currentOrigin, ENTITYNUM_NONE, ent->clipmask);
        if (!tr.startsolid || tr.entityNum != ent->r.ownerNum) {
            ent->count = 1;
        }
    }

    // check think function after bouncing
    G_RunThink(ent);
}

//=============================================================================

/*
=================
CreateMissile
// Address: 0x1005cba0
[QL] Shared missile factory used by fire_grenade / fire_rocket / fire_plasma /
fire_bfg. Spawns the entity, sets ET_MISSILE + SVF_USE_CURRENT_ORIGIN, the
MASK_SHOT clip (minus CONTENTS_BODY under pmove_NoPlayerClip: 0x6000001 ->
0x4000001), snaps origin and velocity, and gives the bolt a tiny +/-0.01 bbox.
trType is TR_GRAVITY unless weapon_gravity_rl is 0 or g_guidedRocket is set.
NOTE: VectorNormalize(dir) happens INSIDE here, so callers must not pre-normalize.
=================
*/
static gentity_t* CreateMissile(vec3_t start, vec3_t dir, int speed, gentity_t* owner) {
    gentity_t* bolt;

    bolt = G_Spawn();
    bolt->think = G_ExplodeMissile;
    bolt->s.eType = ET_MISSILE;
    bolt->s.eFlags = 0;
    bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
    bolt->r.ownerNum = owner->s.number;
    bolt->parent = owner;
    bolt->clipmask = MASK_SHOT;  // [QL] SOLID|BODY|CORPSE = 0x6000001
    bolt->target_ent = NULL;

    // [QL] pmove_NoPlayerClip strips CONTENTS_BODY so missiles pass through players
    if (pmove_NoPlayerClip.integer) {
        bolt->clipmask &= ~CONTENTS_BODY;  // 0x6000001 -> 0x4000001
    }

    VectorCopy(start, bolt->s.pos.trBase);
    SnapVector(bolt->s.pos.trBase);
    VectorCopy(bolt->s.pos.trBase, bolt->r.currentOrigin);

    // [QL] weapon_gravity_rl selects gravity; g_guidedRocket forces linear
    if (weapon_gravity_rl.integer == 0 || g_guidedRocket.integer != 0) {
        bolt->s.pos.trType = TR_LINEAR;
    } else {
        bolt->s.pos.trType = TR_GRAVITY;
    }
    bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;  // move a bit on the very first frame

    VectorNormalize(dir);
    VectorScale(dir, speed, bolt->s.pos.trDelta);
    SnapVector(bolt->s.pos.trDelta);  // save net bandwidth

    // small bbox for precise collision
    VectorSet(bolt->r.mins, -0.01f, -0.01f, -0.01f);
    VectorSet(bolt->r.maxs, 0.01f, 0.01f, 0.01f);

    return bolt;
}

//=============================================================================

/*
=================
fire_plasma
// Address: 0x1005d0e0
=================
*/
gentity_t* fire_plasma(gentity_t* self, vec3_t start, vec3_t dir) {
    gentity_t* bolt;

    bolt = CreateMissile(start, dir, g_velocity_pg.integer, self);
    bolt->classname = "plasma";
    bolt->nextthink = level.time + 10000;
    bolt->s.weapon = WP_PLASMAGUN;
    bolt->r.ownerNum = self->s.number;
    bolt->s.otherEntityNum = self->s.number;
    bolt->damage = g_damage_pg.integer;
    bolt->splashDamage = g_splashdamage_pg.integer;
    bolt->splashRadius = g_splashradius_pg.integer;
    bolt->methodOfDeath = MOD_PLASMA;
    bolt->splashMethodOfDeath = MOD_PLASMA_SPLASH;
    bolt->s.pos.trType = weapon_gravity_pg.integer ? TR_GRAVITY : TR_LINEAR;  // [QL]

    // [QL] TODO: binary fire_plasma has a projectile speed-SCALE cvar
    //      (DAT_105aa008, default 1.0 - NOT g_velocity_pg which is 2000) that, when != 1.0,
    //      rescales nextthink via ftol() and sets an alternate think (LAB_1005d090). Real cvar
    //      name + exact ftol formula unresolved; dead at default 1.0. Resolve in #74 groundwork.

    return bolt;
}

//=============================================================================

/*
=================
fire_grenade
// Address: 0x1005cd40
=================
*/
gentity_t* fire_grenade(gentity_t* self, vec3_t start, vec3_t dir) {
    gentity_t* bolt;

    bolt = CreateMissile(start, dir, g_velocity_gl.integer, self);
    bolt->classname = "grenade";
    bolt->nextthink = level.time + 2500;
    bolt->s.weapon = WP_GRENADE_LAUNCHER;
    bolt->r.ownerNum = self->s.number;
    bolt->s.otherEntityNum = self->s.number;
    bolt->damage = g_damage_gl.integer;
    bolt->splashDamage = g_splashdamage_gl.integer;
    bolt->splashRadius = g_splashradius_gl.integer;
    bolt->methodOfDeath = MOD_GRENADE;
    bolt->splashMethodOfDeath = MOD_GRENADE_SPLASH;
    bolt->s.eFlags = EF_BOUNCE_HALF;
    bolt->s.pos.trType = TR_GRAVITY;
    bolt->s.clientNum = self->s.clientNum;  // [QL] binary copies self->s.clientNum

    return bolt;
}

//=============================================================================

/*
=================
fire_bfg
// Address: 0x1005d210
=================
*/
gentity_t* fire_bfg(gentity_t* self, vec3_t start, vec3_t dir) {
    gentity_t* bolt;

    bolt = CreateMissile(start, dir, g_velocity_bfg.integer, self);
    bolt->classname = "bfg";
    bolt->nextthink = level.time + 10000;
    bolt->s.weapon = WP_BFG;
    bolt->r.ownerNum = self->s.number;
    bolt->s.otherEntityNum = self->s.number;
    bolt->damage = g_damage_bfg.integer;
    bolt->splashDamage = g_splashdamage_bfg.integer;
    bolt->splashRadius = g_splashradius_bfg.integer;
    bolt->methodOfDeath = MOD_BFG;
    bolt->splashMethodOfDeath = MOD_BFG_SPLASH;
    // [QL] binary does NOT override trType here; BFG keeps CreateMissile's
    //      weapon_gravity_rl-based trajectory (weapon_gravity_bfg is unused).

    // [QL] TODO: binary fire_bfg has a projectile speed-SCALE cvar
    //      (DAT_1059db28, default 1.0) that rescales nextthink via ftol() when != 1.0.
    //      Real cvar name + formula unresolved; dead at default. Resolve in #74 groundwork.

    return bolt;
}

/*
=================
G_GuidedRocketThink
// Address: 0x1005ce30
// .so name: G_GuidedRocketThink (Ghidra mislabels it "Weapon_RocketHomingThink")
[QL] Steers a guided rocket toward the shooter's view direction each frame.
Speed is HARDCODED to 20; trTime is NOT updated; think interval is 25ms.
=================
*/
static void G_GuidedRocketThink(gentity_t *ent) {
    if (ent->parent->client) {
        vec3_t forward;
        AngleVectors(ent->parent->client->ps.viewangles, forward, NULL, NULL);
        VectorCopy(forward, ent->movedir);
        VectorCopy(ent->r.currentOrigin, ent->s.pos.trBase);
        VectorScale(forward, 20, ent->s.pos.trDelta);
        // [QL] binary does NOT set ent->s.pos.trTime here
    }
    ent->nextthink = level.time + 25;
}

//=============================================================================

/*
=================
fire_rocket
// Address: 0x1005cf90
=================
*/
gentity_t* fire_rocket(gentity_t* self, vec3_t start, vec3_t dir) {
    gentity_t* bolt;

    bolt = CreateMissile(start, dir, g_velocity_rl.integer, self);
    bolt->classname = "rocket";
    bolt->nextthink = level.time + 15000;
    bolt->s.weapon = WP_ROCKET_LAUNCHER;
    bolt->r.ownerNum = self->s.number;
    bolt->s.otherEntityNum = self->s.number;
    bolt->damage = g_damage_rl.integer;
    bolt->splashDamage = 100;  // [QL] binary hardcodes 100 (NOT g_splashdamage_rl)
    bolt->splashRadius = g_splashradius_rl.integer;
    bolt->methodOfDeath = MOD_ROCKET;
    bolt->splashMethodOfDeath = MOD_ROCKET_SPLASH;

    // [QL] guided rocket mode
    if (g_guidedRocket.integer) {
        bolt->nextthink = level.time - MISSILE_PRESTEP_TIME;  // [QL] think next frame
        bolt->think = G_GuidedRocketThink;
    }

    // [QL] TODO: binary fire_rocket has a projectile speed-SCALE cvar
    //      (DAT_105a5808, default 1.0) that rescales nextthink via ftol() when != 1.0.
    //      Real cvar name + formula unresolved; dead at default. Resolve in #74 groundwork.

    return bolt;
}

/*
=================
fire_grapple
// Address: 0x1005d2e0
=================
*/
gentity_t* fire_grapple(gentity_t* self, vec3_t start, vec3_t dir) {
    gentity_t* hook;

    VectorNormalize(dir);

    hook = G_Spawn();
    hook->classname = "hook";
    hook->nextthink = level.time + 10000;
    hook->think = Weapon_HookFree;  // [QL] Ghidra label at 0x1006e320 is a guess ("G_ReleaseGrapple"); .so symbol is Weapon_HookFree
    hook->s.eType = ET_MISSILE;
    hook->r.svFlags = SVF_USE_CURRENT_ORIGIN;
    hook->s.weapon = WP_GRAPPLING_HOOK;
    hook->r.ownerNum = self->s.number;
    hook->methodOfDeath = MOD_GRAPPLE;
    hook->clipmask = MASK_SHOT;  // [QL] 0x6000001, minus CONTENTS_BODY under pmove
    if (pmove_NoPlayerClip.integer) {
        hook->clipmask &= ~CONTENTS_BODY;
    }
    hook->parent = self;
    hook->target_ent = NULL;
    hook->bouncecount = 0;
    hook->count = 0;
    hook->s.pos.trType = TR_LINEAR;
    hook->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;  // move a bit on the very first frame
    hook->s.otherEntityNum = self->s.number;                 // use to match beam in client
    hook->damage = g_damage_gh.integer;                      // [QL] binary sets damage from g_damage_gh

    VectorCopy(start, hook->s.pos.trBase);
    VectorScale(dir, g_velocity_gh.integer, hook->s.pos.trDelta);
    SnapVector(hook->s.pos.trDelta);  // save net bandwidth
    VectorCopy(start, hook->r.currentOrigin);

    self->client->hook = hook;

    return hook;
}

/*
=================
fire_nail
[QL] NOTE: qagamex86.dll has NO standalone fire_nail; the nail spawn is inlined
into weapon_nailgun_fire (0x1006da90, g_weapon.c). This Q3-style helper is kept
only because g_weapon.c still calls it. The binary sets the bouncing-nail flag
as EF_QL_NAILBOUNCE (0x100000), not EF_BOUNCE. See report.
=================
*/
gentity_t* fire_nail(gentity_t* self, vec3_t start, vec3_t forward, vec3_t right, vec3_t up) {
    gentity_t* bolt;
    vec3_t dir;
    vec3_t end;
    float r, u, scale;

    bolt = G_Spawn();
    bolt->classname = "nail";
    bolt->nextthink = level.time + 4500;  // [QL] 0x1194 = 4500
    bolt->think = G_ExplodeMissile;
    bolt->s.eType = ET_MISSILE;
    bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
    bolt->s.weapon = WP_NAILGUN;
    bolt->r.ownerNum = self->s.number;
    bolt->parent = self;
    bolt->damage = g_damage_ng.integer;
    bolt->methodOfDeath = MOD_NAIL;
    bolt->clipmask = pmove_NoPlayerClip.integer ? CONTENTS_SOLID : MASK_SHOT;  //
    bolt->target_ent = NULL;
    bolt->s.otherEntityNum = self->s.number;  // [QL]

    bolt->s.pos.trType = weapon_gravity_ng.integer ? TR_GRAVITY : TR_LINEAR;  // [QL]
    bolt->s.pos.trTime = level.time;
    VectorCopy(start, bolt->s.pos.trBase);

    // [QL] nail bounce
    if (g_nailbounce.integer) {
        if ((rand() % 100) >= (100 - g_nailbouncepercentage.integer)) {
            bolt->s.eFlags = EF_BOUNCE;
        }
    }

    r = random() * M_PI * 2.0f;
    u = sin(r) * crandom() * g_nailspread.integer * 16;
    r = cos(r) * crandom() * g_nailspread.integer * 16;
    VectorMA(start, 8192 * 16, forward, end);
    VectorMA(end, r, right, end);
    VectorMA(end, u, up, end);
    VectorSubtract(end, start, dir);
    VectorNormalize(dir);

    scale = g_nailspeed.integer + random() * 1800;
    VectorScale(dir, scale, bolt->s.pos.trDelta);
    SnapVector(bolt->s.pos.trDelta);

    VectorCopy(start, bolt->r.currentOrigin);

    return bolt;
}

/*
=================
fire_prox
// Address: 0x1005d480
=================
*/
gentity_t* fire_prox(gentity_t* self, vec3_t start, vec3_t dir) {
    gentity_t* bolt;

    VectorNormalize(dir);

    bolt = G_Spawn();
    bolt->classname = "prox mine";
    bolt->nextthink = level.time + 3000;
    bolt->think = G_ExplodeMissile;
    bolt->s.eType = ET_MISSILE;
    bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
    bolt->s.weapon = WP_PROX_LAUNCHER;
    bolt->s.eFlags = 0;
    bolt->r.ownerNum = self->s.number;
    bolt->s.otherEntityNum = self->s.number;  // [QL] binary sets s.otherEntityNum (0x94)
    bolt->parent = self;
    bolt->damage = g_damage_pl.integer;
    bolt->splashDamage = g_splashdamage_pl.integer;
    bolt->splashRadius = g_splashradius_pl.integer;
    bolt->methodOfDeath = MOD_PROXIMITY_MINE;
    bolt->splashMethodOfDeath = MOD_PROXIMITY_MINE;
    bolt->clipmask = MASK_SHOT;  // [QL] 0x6000001, minus CONTENTS_BODY under pmove
    if (pmove_NoPlayerClip.integer) {
        bolt->clipmask &= ~CONTENTS_BODY;
    }
    bolt->target_ent = NULL;
    // count is used to check if the prox mine left the player bbox
    // if count == 1 then the prox mine left the player bbox and can attack to it
    bolt->count = 0;

    // [QL] team stored in s.generic1 (0xE0); ProximityMine_Trigger reads it back
    bolt->s.generic1 = self->client->sess.sessionTeam;

    bolt->s.pos.trType = TR_GRAVITY;
    bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;  // move a bit on the very first frame
    VectorCopy(start, bolt->s.pos.trBase);
    VectorScale(dir, 700, bolt->s.pos.trDelta);
    SnapVector(bolt->s.pos.trDelta);  // save net bandwidth

    VectorCopy(start, bolt->r.currentOrigin);

    return bolt;
}
