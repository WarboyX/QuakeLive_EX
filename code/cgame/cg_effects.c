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
// cg_effects.c -- these functions generate localentities, usually as a result
// of event processing

#include "cg_local.h"

/*
==================
CG_BubbleTrail

Bullets shot underwater
==================
*/
void CG_BubbleTrail(vec3_t start, vec3_t end, float spacing) {
    vec3_t move;
    vec3_t vec;
    float len;
    int i;

    // [QL] cg_bubbleTrail controls bubble trail rendering (binary: 0x10012c53)
    // Note: Q3 used cg_noProjectileTrail here (disable), QL uses cg_bubbleTrail (enable)
    if (!cg_bubbleTrail.integer) {
        return;
    }

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    // advance a random amount first
    i = rand() % (int)spacing;
    VectorMA(move, i, vec, move);

    VectorScale(vec, spacing, vec);

    for (; i < len; i += spacing) {
        localEntity_t* le;
        refEntity_t* re;

        le = CG_AllocLocalEntity();
        le->leFlags = LEF_PUFF_DONT_SCALE;
        le->leType = LE_MOVE_SCALE_FADE;
        le->startTime = cg.time;
        le->endTime = cg.time + 1000 + random() * 250;
        le->lifeRate = 1.0 / (le->endTime - le->startTime);

        re = &le->refEntity;
        re->shaderTime = cg.time / 1000.0f;

        re->reType = RT_SPRITE;
        re->rotation = 0;
        re->radius = 3;
        re->customShader = cgs.media.waterBubbleShader;
        re->shaderRGBA[0] = 0xff;
        re->shaderRGBA[1] = 0xff;
        re->shaderRGBA[2] = 0xff;
        re->shaderRGBA[3] = 0xff;

        le->color[3] = 1.0;

        le->pos.trType = TR_LINEAR;
        le->pos.trTime = cg.time;
        VectorCopy(move, le->pos.trBase);
        le->pos.trDelta[0] = crandom() * 5;
        le->pos.trDelta[1] = crandom() * 5;
        le->pos.trDelta[2] = crandom() * 5 + 6;

        VectorAdd(move, vec, move);
    }
}

/*
=====================
CG_SmokePuff

Adds a smoke puff or blood trail localEntity.
=====================
*/
localEntity_t* CG_SmokePuff(const vec3_t p, const vec3_t vel, float radius, float r, float g, float b, float a, float duration, int startTime, int fadeInTime, int leFlags, qhandle_t hShader) {
    static int seed = 0x92;
    localEntity_t* le;
    refEntity_t* re;
    //	int fadeInTime = startTime + duration / 2;

    le = CG_AllocLocalEntity();
    le->leFlags = leFlags;
    le->radius = radius;

    re = &le->refEntity;
    re->rotation = Q_random(&seed) * 360;
    re->radius = radius;
    re->shaderTime = startTime / 1000.0f;

    le->leType = LE_MOVE_SCALE_FADE;
    le->startTime = startTime;
    le->fadeInTime = fadeInTime;
    le->endTime = startTime + duration;
    if (fadeInTime > startTime) {
        le->lifeRate = 1.0 / (le->endTime - le->fadeInTime);
    } else {
        le->lifeRate = 1.0 / (le->endTime - le->startTime);
    }
    le->color[0] = r;
    le->color[1] = g;
    le->color[2] = b;
    le->color[3] = a;

    le->pos.trType = TR_LINEAR;
    le->pos.trTime = startTime;
    VectorCopy(vel, le->pos.trDelta);
    VectorCopy(p, le->pos.trBase);

    VectorCopy(p, re->origin);
    re->customShader = hShader;

    // rage pro can't alpha fade, so use a different shader
    if (cgs.glconfig.hardwareType == GLHW_RAGEPRO) {
        re->customShader = cgs.media.smokePuffRageProShader;
        re->shaderRGBA[0] = 0xff;
        re->shaderRGBA[1] = 0xff;
        re->shaderRGBA[2] = 0xff;
        re->shaderRGBA[3] = 0xff;
    } else {
        re->shaderRGBA[0] = le->color[0] * 0xff;
        re->shaderRGBA[1] = le->color[1] * 0xff;
        re->shaderRGBA[2] = le->color[2] * 0xff;
        re->shaderRGBA[3] = 0xff;
    }

    re->reType = RT_SPRITE;
    re->radius = le->radius;

    return le;
}

/*
==================
CG_SpawnEffect

Player teleporting in or out
==================
*/
void CG_SpawnEffect(vec3_t org) {
    localEntity_t* le;
    refEntity_t* re;

    le = CG_AllocLocalEntity();
    le->leFlags = 0;
    le->leType = LE_FADE_RGB;
    le->startTime = cg.time;
    le->endTime = cg.time + 500;
    le->lifeRate = 1.0 / (le->endTime - le->startTime);

    le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

    re = &le->refEntity;

    re->reType = RT_MODEL;
    re->shaderTime = cg.time / 1000.0f;

    re->hModel = cgs.media.teleportEffectModel;
    AxisClear(re->axis);

    VectorCopy(org, re->origin);
    re->origin[2] += 16;
}

/*
==================
CG_FreezeEffect

[QL] Ice-crystal overlay on a frozen freeze-tag player.
leType LE_FREEZE (rendered by CG_AddScaleFade), RT_MODEL freeze model, colour
all 1.0, 10-second lifetime. Plays the freeze sound on CHAN_BODY at origin.

Axis: the binary calls AngleVectors on a zeroed angle vector (so forward =
(1,0,0)) and writes only axis[1] = cg.refdef.vieworg - forward; axis[0]/axis[2]
stay zeroed from the CG_AllocLocalEntity memset.
// Address: 0x10013920
==================
*/
void CG_FreezeEffect(vec3_t origin) {
    localEntity_t* le;
    refEntity_t* re;
    vec3_t zeroAngles, forward, right, up;

    le = CG_AllocLocalEntity();
    le->leType = LE_FREEZE;  // 0xc in binary
    le->leFlags = 0;
    le->startTime = cg.time;
    le->endTime = cg.time + 10000;
    le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;
    le->lifeRate = 1.0 / (le->endTime - le->startTime);

    re = &le->refEntity;

    re->reType = RT_MODEL;
    re->shaderTime = cg.time / 1000.0f;
    re->hModel = cgs.media.freezeModel;

    VectorCopy(origin, re->origin);

    // binary: AngleVectors of a zero angle vector -> forward = (1,0,0),
    // then axis[1] = cg.refdef.vieworg - forward
    VectorClear(zeroAngles);
    AngleVectors(zeroAngles, forward, right, up);
    re->axis[1][0] = cg.refdef.vieworg[0] - forward[0];
    re->axis[1][1] = cg.refdef.vieworg[1] - forward[1];
    re->axis[1][2] = cg.refdef.vieworg[2] - forward[2];

    trap_S_StartSound(origin, ENTITYNUM_NONE, CHAN_BODY, cgs.media.freezeSound);
}

/*
==================
CG_SpawnIceShard

[QL] Launches a single ice-shard fragment (same shape as CG_LaunchGib):
LE_FRAGMENT, axisDefault orientation, TR_GRAVITY trajectory (value 5 in the
binary), bounceFactor 0.6, endTime = startTime + 5000 + random()*3000.

The binary sets leBounceSoundType = leMarkType = 4 (QL bullet-burn/nail types);
ioquakelive's fragment handlers only render BLOOD/BURN, so value 4 leaves no
mark or bounce sound.

Calling convention (binary): hModel is the single stack argument, origin comes
in EBX and velocity in EDI; expressed here as a normal 3-arg C function.
// Address: 0x10014670
==================
*/
void CG_SpawnIceShard(const vec3_t origin, const vec3_t velocity, qhandle_t hModel) {
    localEntity_t* le;
    refEntity_t* re;

    le = CG_AllocLocalEntity();
    re = &le->refEntity;

    le->leType = LE_FRAGMENT;  // 3 in binary
    le->startTime = cg.time;
    le->endTime = le->startTime + 5000 + random() * 3000;

    VectorCopy(origin, re->origin);
    AxisCopy(axisDefault, re->axis);
    re->hModel = hModel;

    // [QL] pak00 ships no ice meshes, so the shard is the generic gib sphere and
    // the ice has to come from a shader over it. iceShardShader1 is registered
    // in CG_RegisterGraphics and was read by nothing until now; if the shader is
    // not in the pak its handle is 0 and the model keeps its own skin.
    if (cgs.media.iceShardShader1) {
        re->customShader = cgs.media.iceShardShader1;
    }

    le->pos.trType = TR_GRAVITY;  // 5 in binary
    VectorCopy(origin, le->pos.trBase);
    VectorCopy(velocity, le->pos.trDelta);
    le->pos.trTime = cg.time;

    le->bounceFactor = 0.6f;

    // [QL] binary value 4 (LEBS_NAIL / LEMT_BULLETBURN); no ioquakelive equivalent
    le->leBounceSoundType = 4;
    le->leMarkType = 4;
}

/*
==================
CG_ThawPlayer

[QL] Spawns ice shards when a frozen player is thawed. Takes the thaw origin as
a vec3_t (passed in ESI in the binary), NOT a centity_t. Spawns exactly 7 shards
at the same origin, each with an independent random velocity (crandom()*250 per
component). The two shard models alternate white,blue,white,... (indices 0..6).
// Address: 0x100147d0
==================
*/
void CG_ThawPlayer(const vec3_t origin) {
    qhandle_t whiteIce = cgs.media.iceWhiteModel;  // 0x10a5f428
    qhandle_t blueIce = cgs.media.iceBlueModel;    // 0x10a5f42c
    vec3_t vel;
    int i;
    qhandle_t model;

    for (i = 0; i < 7; i++) {
        vel[0] = crandom() * 250.0f;
        vel[1] = crandom() * 250.0f;
        vel[2] = crandom() * 250.0f;

        model = (i & 1) ? blueIce : whiteIce;  // white, blue, white, ...
        CG_SpawnIceShard(origin, vel, model);
    }
}

/*
===============
CG_LightningBoltBeam
===============
*/
void CG_LightningBoltBeam(vec3_t start, vec3_t end) {
    localEntity_t* le;
    refEntity_t* beam;

    le = CG_AllocLocalEntity();
    le->leFlags = 0;
    le->leType = LE_SHOWREFENTITY;
    le->startTime = cg.time;
    le->endTime = cg.time + 50;

    beam = &le->refEntity;

    VectorCopy(start, beam->origin);
    // this is the end point
    VectorCopy(end, beam->oldorigin);

    beam->reType = RT_LIGHTNING;
    beam->customShader = cgs.media.lightningShader[0];
}

/*
==================
CG_LightningDischargeEffect

[QL] Brief electrical-discharge sprite for the lightning gun's water/self
discharge. Takes the discharge intensity (no origin): the sprite is spawned
at the viewer (cg.refdef.vieworg). Final leType is LE_SCALE_FADE, handled by
CG_AddScaleFade.

  radius   = (intensity * 10 + 48) >> 4
  duration = intensity + 300

The binary calls CG_SpawnParticleEffect (0x10012f30), which is a
CG_SmokePuff-equivalent spawner (fadeInTime = 0, zero velocity). Since
LE_SCALE_FADE uses only re->origin and radius, CG_SmokePuff does the
job.
// Address: 0x10013310
==================
*/
void CG_LightningDischargeEffect(int intensity) {
    qhandle_t shader;
    localEntity_t* le;
    float radius;
    float duration;

    shader = trap_R_RegisterShader("models/weaphits/electric.tga");

    radius = (float)((intensity * 10 + 0x30) >> 4);
    duration = (float)(intensity + 300);

    le = CG_SmokePuff(cg.refdef.vieworg, vec3_origin,
                      radius,
                      1.0f, 1.0f, 1.0f, 1.0f,
                      duration,
                      cg.time,
                      0,   // fadeInTime
                      0,   // leFlags
                      shader);

    le->leType = LE_SCALE_FADE;  // 8 in binary (rendered by CG_AddScaleFade)
}

/*
==================
CG_KamikazeEffect
==================
*/
void CG_KamikazeEffect(vec3_t org) {
    localEntity_t* le;
    refEntity_t* re;

    le = CG_AllocLocalEntity();
    le->leFlags = 0;
    le->leType = LE_KAMIKAZE;
    le->startTime = cg.time;
    le->endTime = cg.time + 3000;  // 2250;
    le->lifeRate = 1.0 / (le->endTime - le->startTime);

    le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

    VectorClear(le->angles.trBase);

    re = &le->refEntity;

    re->reType = RT_MODEL;
    re->shaderTime = cg.time / 1000.0f;

    re->hModel = cgs.media.kamikazeEffectModel;

    VectorCopy(org, re->origin);
}

/*
==================
CG_ObeliskExplode
==================
*/
void CG_ObeliskExplode(vec3_t org, int entityNum) {
    localEntity_t* le;
    vec3_t origin;

    // create an explosion
    VectorCopy(org, origin);
    origin[2] += 64;
    le = CG_MakeExplosion(origin, vec3_origin,
                          cgs.media.dishFlashModel,
                          cgs.media.rocketExplosionShader,
                          600, qtrue);
    le->light = 300;
    le->lightColor[0] = 1;
    le->lightColor[1] = 0.75;
    le->lightColor[2] = 0.0;
}

/*
==================
CG_ObeliskPain
==================
*/
void CG_ObeliskPain(vec3_t org) {
    float r;
    sfxHandle_t sfx;

    // hit sound
    r = rand() & 3;
    if (r < 2) {
        sfx = cgs.media.obeliskHitSound1;
    } else if (r == 2) {
        sfx = cgs.media.obeliskHitSound2;
    } else {
        sfx = cgs.media.obeliskHitSound3;
    }
    trap_S_StartSound(org, ENTITYNUM_NONE, CHAN_BODY, sfx);
}

/*
==================
CG_InvulnerabilityImpact
==================
*/
void CG_InvulnerabilityImpact(vec3_t org, vec3_t angles) {
    localEntity_t* le;
    refEntity_t* re;
    int r;
    sfxHandle_t sfx;

    le = CG_AllocLocalEntity();
    le->leFlags = 0;
    le->leType = LE_INVULIMPACT;
    le->startTime = cg.time;
    le->endTime = cg.time + 1000;
    le->lifeRate = 1.0 / (le->endTime - le->startTime);

    le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

    re = &le->refEntity;

    re->reType = RT_MODEL;
    re->shaderTime = cg.time / 1000.0f;

    re->hModel = cgs.media.invulnerabilityImpactModel;

    VectorCopy(org, re->origin);
    AnglesToAxis(angles, re->axis);

    r = rand() & 3;
    if (r < 2) {
        sfx = cgs.media.invulnerabilityImpactSound1;
    } else if (r == 2) {
        sfx = cgs.media.invulnerabilityImpactSound2;
    } else {
        sfx = cgs.media.invulnerabilityImpactSound3;
    }
    trap_S_StartSound(org, ENTITYNUM_NONE, CHAN_BODY, sfx);
}

/*
==================
CG_InvulnerabilityJuiced
==================
*/
void CG_InvulnerabilityJuiced(vec3_t org) {
    localEntity_t* le;
    refEntity_t* re;
    vec3_t angles;

    le = CG_AllocLocalEntity();
    le->leFlags = 0;
    le->leType = LE_INVULJUICED;
    le->startTime = cg.time;
    le->endTime = cg.time + 10000;
    le->lifeRate = 1.0 / (le->endTime - le->startTime);

    le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

    re = &le->refEntity;

    re->reType = RT_MODEL;
    re->shaderTime = cg.time / 1000.0f;

    re->hModel = cgs.media.invulnerabilityJuicedModel;

    VectorCopy(org, re->origin);
    VectorClear(angles);
    AnglesToAxis(angles, re->axis);

    trap_S_StartSound(org, ENTITYNUM_NONE, CHAN_BODY, cgs.media.invulnerabilityJuicedSound);
}

/*
==================
CG_ScorePlum
==================
*/
void CG_ScorePlum(int client, vec3_t org, int score) {
    localEntity_t* le;
    refEntity_t* re;
    vec3_t angles;
    static vec3_t lastPos;

    // only visualize for the client that scored
    if (client != cg.predictedPlayerState.clientNum || cg_scorePlum.integer == 0) {
        return;
    }

    le = CG_AllocLocalEntity();
    le->leFlags = 0;
    le->leType = LE_SCOREPLUM;
    le->startTime = cg.time;
    le->endTime = cg.time + 4000;
    le->lifeRate = 1.0 / (le->endTime - le->startTime);

    le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;
    le->radius = score;

    VectorCopy(org, le->pos.trBase);
    if (org[2] >= lastPos[2] - 20 && org[2] <= lastPos[2] + 20) {
        le->pos.trBase[2] -= 20;
    }

    // CG_Printf( "Plum origin %i %i %i -- %i\n", (int)org[0], (int)org[1], (int)org[2], (int)Distance(org, lastPos));
    VectorCopy(org, lastPos);

    re = &le->refEntity;

    re->reType = RT_SPRITE;
    re->radius = 16;

    VectorClear(angles);
    AnglesToAxis(angles, re->axis);
}

// ==========================================================================
// [QL] Shared floating-effect pool
//
// cgamex86.dll keeps ONE pool of 32 screen-projected billboards (base
// DAT_10ab9978, 0x80 bytes each) used by damage numbers, player outlines,
// freeze/flag glows and head float-sprites. See floatingEffect_t in cg_local.h.
// The pool is a static array here; it is zero-initialised on cgame load and
// records self-expire in CG_UpdateFloatingEffects, so the binary keeps no
// per-frame full reset (records with a ~1ms lifeRate live a single frame).
// ==========================================================================

static floatingEffect_t cg_floatingEffects[MAX_FLOATING_EFFECTS];

/*
==================
CG_AllocFloatingEffect
// Address: 0x1002a0d0
[QL] Ghidra "CG_AllocMark" - a reused/mislabelled name; this is NOT the Q3 wall-mark
allocator (that lives in cg_marks.c). Returns the first free pool record cleared to sane
defaults (white, full alpha, spawnTime = cg.time, lifeRate 1ms), or NULL if all 32 slots
are busy. (The binary also has a niche gametype-2 alloc gate on two ps flags that is not
reproduced here.)
==================
*/
floatingEffect_t* CG_AllocFloatingEffect(void) {
    int i;
    floatingEffect_t* fx;

    for (i = 0; i < MAX_FLOATING_EFFECTS; i++) {
        fx = &cg_floatingEffects[i];
        if (!fx->active) {
            memset(fx, 0, sizeof(*fx));
            fx->active = 1;
            fx->spawnTime = cg.time;
            fx->lifeRate = 1.0f;  // 0x3f800000
            fx->color[0] = fx->color[1] = fx->color[2] = fx->color[3] = 1.0f;
            return fx;
        }
    }
    return NULL;
}

/*
==================
CG_ProjectFloatingEffect
[QL] World point -> 640x480 virtual screen (top-left origin). ioquakelive stand-in
for the engine projection pair the binary calls through its import table
(DAT_1074cccc +0x1e0 / +0x1e4); CG_DrawText / CG_DrawPic then map 640x480 to real pixels.
Returns qfalse when the point is behind the view (binary's forward-distance > 0 gate).
==================
*/
static qboolean CG_ProjectFloatingEffect(const vec3_t point, float* x, float* y) {
    vec3_t trans;
    float xc, yc, px, py, z;

    VectorSubtract(point, cg.refdef.vieworg, trans);
    z = DotProduct(trans, cg.refdef.viewaxis[0]);  // forward distance
    if (z < 0.001f) {
        return qfalse;
    }
    px = tan(cg.refdef.fov_x * (M_PI / 360.0f));
    py = tan(cg.refdef.fov_y * (M_PI / 360.0f));
    xc = 640.0f * 0.5f;
    yc = 480.0f * 0.5f;
    // viewaxis[1] is left, viewaxis[2] is up (Q3 convention)
    *x = xc - xc * DotProduct(trans, cg.refdef.viewaxis[1]) / (z * px);
    *y = yc - yc * DotProduct(trans, cg.refdef.viewaxis[2]) / (z * py);
    return qtrue;
}

/*
==================
CG_UpdateFloatingEffects
// Address: 0x1002a190
[QL] Ghidra mislabels this "CG_UpdateConfigStrings"; not a config-string updater, it
ages the floating-effect pool. Every active record, once per frame:
frees it when cg.time >= spawnTime + lifeRate, and (for records with doFade set) fades
colour alpha from 1 to 0 across [spawnTime + fadeStart, spawnTime + lifeRate].
==================
*/
void CG_UpdateFloatingEffects(void) {
    int i;
    floatingEffect_t* fx;
    float fadeAt, a;

    for (i = 0; i < MAX_FLOATING_EFFECTS; i++) {
        fx = &cg_floatingEffects[i];
        if (!fx->active) {
            continue;
        }
        if (cg.time >= fx->spawnTime + fx->lifeRate) {
            fx->active = 0;  // expire
            continue;
        }
        if (fx->doFade && fx->lifeRate > 0.0f) {
            fadeAt = fx->spawnTime + fx->fadeStart;
            if (fadeAt <= cg.time) {
                a = 1.0f - (cg.time - fadeAt) / (fx->lifeRate - fx->fadeStart);
                if (a < 0.0f) {
                    a = 0.0f;
                } else if (a > 1.0f) {
                    a = 1.0f;
                }
                fx->color[3] = a;
            }
        }
    }
}

/*
==================
CG_DrawFloatingEffects
// Address: 0x10011680
[QL] Ghidra "CG_DrawDamagePlums" - renders the WHOLE pool, not only plums. Projects each
active record to screen, sizes its billboard (max of the min-pixel floor and the projected
height of a worldSize-tall object) and draws its damage-number text and/or billboard pic,
tinting through trap_R_SetColor with the (faded) record colour and applying the linear +
quadratic screen drift.
==================
*/
void CG_DrawFloatingEffects(void) {
    int i;
    floatingEffect_t* fx;
    vec3_t base, top;
    float sx, sy, tx, ty;
    float size, projected, life, arc, mx, my;

    for (i = 0; i < MAX_FLOATING_EFFECTS; i++) {
        fx = &cg_floatingEffects[i];
        if (!fx->active) {
            continue;
        }

        // billboard base point: world origin lifted by zOffset
        VectorCopy(fx->origin, base);
        base[2] += fx->zOffset;
        if (!CG_ProjectFloatingEffect(base, &sx, &sy)) {
            continue;
        }

        // on-screen size = max(minPixels, 2 * projected height of a worldSize-tall object)
        size = fx->minPixels;
        VectorCopy(base, top);
        VectorMA(top, fx->worldSize * 0.5f, cg.refdef.viewaxis[2], top);
        if (CG_ProjectFloatingEffect(top, &tx, &ty)) {
            projected = fabs(ty - sy) * 2.0f;
            if (projected > size) {
                size = projected;
            }
        }

        // life fraction and quadratic drift (binary uses pow(life, 2))
        life = (cg.time - fx->spawnTime) / fx->lifeRate;
        arc = life * life;
        mx = fx->velX * life + fx->accX * arc;
        my = fx->velY * life + fx->accY * arc;

        trap_R_SetColor(fx->color);

        if (fx->text[0]) {
            CG_DrawText(sx - size * 0.5f + mx, sy - size + my, 0, 0.15f,
                        fx->color, fx->text, 0, 0, 0);
        }

        if (fx->shader) {
            CG_DrawPic(sx - size * 0.5f + mx, sy - size + my, size, size, fx->shader);
        }

        trap_R_SetColor(NULL);
    }
}

/*
==================
CG_DamagePlum
// Address: 0x10013660
[QL] Floating damage number. Allocates a shared floating-effect record (FE_DAMAGE_NUMBER)
carrying the damage as text; CG_DrawFloatingEffects projects and floats it. Replaces the
old LE_DAMAGEPLUM local-entity / number-sprite path. (The binary also filters by weapon via
a cvar weapon-flag mask; that filter is not reproduced here.)
==================
*/
void CG_DamagePlum(int damage, int weapon, vec3_t org) {
    floatingEffect_t* fx;

    (void)weapon;

    fx = CG_AllocFloatingEffect();
    if (!fx) {
        return;
    }

    fx->type = FE_DAMAGE_NUMBER;
    fx->lifeRate = 1000.0f;  // 0x447a0000
    fx->fadeStart = 600.0f;  // 0x44160000
    fx->doFade = 1;

    // colour style based on cg_damagePlumColorStyle (binary DAT_10b71fec)
    switch (cg_damagePlumColorStyle.integer) {
    case 2:  // damage tier colours
        if (damage >= 76) {
            fx->color[0] = 1.0f; fx->color[1] = 0.0f; fx->color[2] = 0.0f;
        } else if (damage >= 51) {
            fx->color[0] = 1.0f; fx->color[1] = 0.5f; fx->color[2] = 0.0f;
        } else if (damage >= 26) {
            fx->color[0] = 1.0f; fx->color[1] = 1.0f; fx->color[2] = 0.0f;
        } else {
            fx->color[0] = 0.25f; fx->color[1] = 0.5f; fx->color[2] = 1.0f;
        }
        fx->color[3] = 1.0f;
        break;
    default:  // style 1: white (binary calls CG_DamageColor(damage/100); not ported)
        fx->color[0] = fx->color[1] = fx->color[2] = fx->color[3] = 1.0f;
        break;
    }

    VectorCopy(org, fx->origin);
    // random world scatter (+/-10 on each axis)
    fx->origin[0] += crandom() * 10.0f;
    fx->origin[1] += crandom() * 10.0f;
    fx->origin[2] += crandom() * 10.0f;

    // screen drift: rises (screen -Y) and settles quadratically, small X jitter
    fx->velX = crandom() * 50.0f;           // 0x19: +/-50
    fx->velY = -120.0f - random() * 20.0f;  // 0x1a: -120 .. -140
    fx->accY = 150.0f;                      // 0x1c: * life^2

    Com_sprintf(fx->text, sizeof(fx->text), "%i", damage);
}

/*
====================
CG_MakeExplosion
====================
*/
localEntity_t* CG_MakeExplosion(vec3_t origin, vec3_t dir, qhandle_t hModel, qhandle_t shader, int msec, qboolean isSprite) {
    float ang;
    localEntity_t* ex;
    int offset;
    vec3_t tmpVec, newOrigin;

    if (msec <= 0) {
        CG_Error("CG_MakeExplosion: msec = %i", msec);
    }

    // skew the time a bit so they aren't all in sync
    offset = rand() & 63;

    ex = CG_AllocLocalEntity();
    if (isSprite) {
        ex->leType = LE_SPRITE_EXPLOSION;

        // randomly rotate sprite orientation
        ex->refEntity.rotation = rand() % 360;
        VectorScale(dir, 16, tmpVec);
        VectorAdd(tmpVec, origin, newOrigin);
    } else {
        ex->leType = LE_EXPLOSION;
        VectorCopy(origin, newOrigin);

        // set axis with random rotate
        if (!dir) {
            AxisClear(ex->refEntity.axis);
        } else {
            ang = rand() % 360;
            VectorCopy(dir, ex->refEntity.axis[0]);
            RotateAroundDirection(ex->refEntity.axis, ang);
        }
    }

    ex->startTime = cg.time - offset;
    ex->endTime = ex->startTime + msec;

    // bias the time so all shader effects start correctly
    ex->refEntity.shaderTime = ex->startTime / 1000.0f;

    ex->refEntity.hModel = hModel;
    ex->refEntity.customShader = shader;

    // set origin
    VectorCopy(newOrigin, ex->refEntity.origin);
    VectorCopy(newOrigin, ex->refEntity.oldorigin);

    ex->color[0] = ex->color[1] = ex->color[2] = 1.0;

    return ex;
}

/*
=================
CG_Bleed

This is the spurt of blood when a character gets hit
=================
*/
void CG_Bleed(vec3_t origin, int entityNum) {
    localEntity_t* ex;

    if (!cg_blood.integer) {
        return;
    }

    ex = CG_AllocLocalEntity();
    ex->leType = LE_EXPLOSION;

    ex->startTime = cg.time;
    ex->endTime = ex->startTime + 500;

    VectorCopy(origin, ex->refEntity.origin);
    ex->refEntity.reType = RT_SPRITE;
    ex->refEntity.rotation = rand() % 360;
    ex->refEntity.radius = 24;

    // [QL] randomly pick from 4 blood spray shaders
    ex->refEntity.customShader = cgs.media.bloodSprayShaders[rand() & 3];

    // don't show player's own blood in view
    if (entityNum == cg.snap->ps.clientNum) {
        ex->refEntity.renderfx |= RF_THIRD_PERSON;
    }
}

/*
==================
CG_BloodSplatEffect

QL binary: blood splat at impact point, used instead of CG_Bleed for hit effects.
Binary calls this in a loop of 2 for each hit.
==================
*/
void CG_BloodSplatEffect(vec3_t origin, int entityNum) {
    // For now, delegate to CG_Bleed
    CG_Bleed(origin, entityNum);
}

/*
==================
CG_SpawnParticleEffect

QL binary: spawns impact spark particles.
==================
*/
void CG_SpawnParticleEffect(const vec3_t origin, const vec3_t vel, float size, float r, float g, float b,
                            float a, float lifetime, int startTime, int type, qhandle_t shader) {
    int i, count;

    if (!shader || lifetime <= 0.0f || size <= 0.0f) {
        return;
    }

    // type 1 is the impact spark burst: a handful of small sprites thrown out
    // from the impact point. type 0 is a single puff carried along the supplied
    // velocity, which is what the wallbang debris path wants.
    count = (type == PARTICLE_FX_SPARKS) ? 6 : 1;

    for (i = 0; i < count; i++) {
        localEntity_t* le;
        vec3_t partVel;

        if (count == 1) {
            VectorCopy(vel, partVel);
        } else {
            // Spread the burst around the supplied velocity. The caller passes
            // a pure +Z vector scaled by cg_impactSparksVelocity, so the
            // horizontal jitter is what turns it into a spray rather than a
            // column, and the vertical jitter keeps the sprites from moving in
            // lockstep.
            partVel[0] = vel[0] + crandom() * 96.0f;
            partVel[1] = vel[1] + crandom() * 96.0f;
            partVel[2] = vel[2] * (0.5f + random() * 0.75f);
        }

        le = CG_SmokePuff(origin, partVel,
                          size * (0.6f + random() * 0.6f),
                          r, g, b, a,
                          lifetime * (0.7f + random() * 0.6f),
                          startTime, 0, LEF_PUFF_DONT_SCALE, shader);

        // Sparks arc rather than travel in a straight line, so give them
        // gravity. CG_SmokePuff leaves the trajectory linear.
        if (le && type == PARTICLE_FX_SPARKS) {
            le->pos.trType = TR_GRAVITY;
        }
    }
}

/*
==================
CG_LaunchGib
==================
*/
void CG_LaunchGib(vec3_t origin, vec3_t velocity, qhandle_t hModel) {
    localEntity_t* le;
    refEntity_t* re;

    le = CG_AllocLocalEntity();
    re = &le->refEntity;

    le->leType = LE_FRAGMENT;
    le->startTime = cg.time;
    le->endTime = le->startTime + 5000 + random() * 3000;

    VectorCopy(origin, re->origin);
    AxisCopy(axisDefault, re->axis);
    re->hModel = hModel;

    le->pos.trType = TR_GRAVITY;
    VectorCopy(origin, le->pos.trBase);
    VectorCopy(velocity, le->pos.trDelta);
    le->pos.trTime = cg.time;

    le->bounceFactor = 0.6f;

    le->leBounceSoundType = LEBS_BLOOD;
    le->leMarkType = LEMT_BLOOD;
}

/*
===================
CG_GibPlayer

Generated a bunch of gibs launching out from the bodies location
===================
*/
#define GIB_VELOCITY 250
#define GIB_JUMP 250
void CG_GibPlayer(vec3_t playerOrigin) {
    vec3_t origin, velocity;

    if (!cg_blood.integer) {
        return;
    }

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    if (rand() & 1) {
        CG_LaunchGib(origin, velocity, cgs.media.gibSkull);
    } else {
        CG_LaunchGib(origin, velocity, cgs.media.gibBrain);
    }

    // allow gibs to be turned off for speed
    if (!cg_gibs.integer) {
        return;
    }

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibAbdomen);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibArm);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibChest);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibFist);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibFoot);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibForearm);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibIntestine);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibLeg);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * GIB_VELOCITY;
    velocity[1] = crandom() * GIB_VELOCITY;
    velocity[2] = GIB_JUMP + crandom() * GIB_VELOCITY;
    CG_LaunchGib(origin, velocity, cgs.media.gibLeg);
}

/*
==================
CG_LaunchExplode
==================
*/
void CG_LaunchExplode(vec3_t origin, vec3_t velocity, qhandle_t hModel) {
    localEntity_t* le;
    refEntity_t* re;

    le = CG_AllocLocalEntity();
    re = &le->refEntity;

    le->leType = LE_FRAGMENT;
    le->startTime = cg.time;
    le->endTime = le->startTime + 10000 + random() * 6000;

    VectorCopy(origin, re->origin);
    AxisCopy(axisDefault, re->axis);
    re->hModel = hModel;

    le->pos.trType = TR_GRAVITY;
    VectorCopy(origin, le->pos.trBase);
    VectorCopy(velocity, le->pos.trDelta);
    le->pos.trTime = cg.time;

    le->bounceFactor = 0.1f;

    le->leBounceSoundType = LEBS_BRASS;
    le->leMarkType = LEMT_NONE;
}

#define EXP_VELOCITY 100
#define EXP_JUMP 150
/*
===================
CG_BigExplode

Generated a bunch of gibs launching out from the bodies location
===================
*/
void CG_BigExplode(vec3_t playerOrigin) {
    vec3_t origin, velocity;

    if (!cg_blood.integer) {
        return;
    }

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * EXP_VELOCITY;
    velocity[1] = crandom() * EXP_VELOCITY;
    velocity[2] = EXP_JUMP + crandom() * EXP_VELOCITY;
    CG_LaunchExplode(origin, velocity, cgs.media.smoke2);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * EXP_VELOCITY;
    velocity[1] = crandom() * EXP_VELOCITY;
    velocity[2] = EXP_JUMP + crandom() * EXP_VELOCITY;
    CG_LaunchExplode(origin, velocity, cgs.media.smoke2);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * EXP_VELOCITY * 1.5;
    velocity[1] = crandom() * EXP_VELOCITY * 1.5;
    velocity[2] = EXP_JUMP + crandom() * EXP_VELOCITY;
    CG_LaunchExplode(origin, velocity, cgs.media.smoke2);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * EXP_VELOCITY * 2.0;
    velocity[1] = crandom() * EXP_VELOCITY * 2.0;
    velocity[2] = EXP_JUMP + crandom() * EXP_VELOCITY;
    CG_LaunchExplode(origin, velocity, cgs.media.smoke2);

    VectorCopy(playerOrigin, origin);
    velocity[0] = crandom() * EXP_VELOCITY * 2.5;
    velocity[1] = crandom() * EXP_VELOCITY * 2.5;
    velocity[2] = EXP_JUMP + crandom() * EXP_VELOCITY;
    CG_LaunchExplode(origin, velocity, cgs.media.smoke2);
}
