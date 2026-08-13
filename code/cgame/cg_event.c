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
// cg_event.c -- handle entity events at snapshot or playerstate transitions

#include "cg_local.h"

// for the voice chats
#include "../../ui/menudef.h"
//==========================================================================

/*
===================
CG_PlaceString

Also called by scoreboard drawing
===================
*/
const char* CG_PlaceString(int rank) {
    static char str[64];
    char *s, *t;

    if (rank & RANK_TIED_FLAG) {
        rank &= ~RANK_TIED_FLAG;
        t = "Tied for ";
    } else {
        t = "";
    }

    if (rank == 1) {
        s = S_COLOR_BLUE "1st" S_COLOR_WHITE;  // draw in blue
    } else if (rank == 2) {
        s = S_COLOR_RED "2nd" S_COLOR_WHITE;  // draw in red
    } else if (rank == 3) {
        s = S_COLOR_YELLOW "3rd" S_COLOR_WHITE;  // draw in yellow
    } else if (rank == 11) {
        s = "11th";
    } else if (rank == 12) {
        s = "12th";
    } else if (rank == 13) {
        s = "13th";
    } else if (rank % 10 == 1) {
        s = va("%ist", rank);
    } else if (rank % 10 == 2) {
        s = va("%ind", rank);
    } else if (rank % 10 == 3) {
        s = va("%ird", rank);
    } else {
        s = va("%ith", rank);
    }

    Com_sprintf(str, sizeof(str), "%s%s", t, s);
    return str;
}

/*
=============
CG_ModToWeaponIcon
[QL] Map means of death to weapon icon shader for obituary display
=============
*/
static qhandle_t CG_ModToWeaponIcon(int mod) {
    switch (mod) {
        case MOD_SHOTGUN:                                       return cg_weapons[WP_SHOTGUN].weaponIcon;
        case MOD_GAUNTLET:                                      return cg_weapons[WP_GAUNTLET].weaponIcon;
        case MOD_MACHINEGUN:                                    return cg_weapons[WP_MACHINEGUN].weaponIcon;
        case MOD_GRENADE:
        case MOD_GRENADE_SPLASH:                                return cg_weapons[WP_GRENADE_LAUNCHER].weaponIcon;
        case MOD_ROCKET:
        case MOD_ROCKET_SPLASH:                                 return cg_weapons[WP_ROCKET_LAUNCHER].weaponIcon;
        case MOD_PLASMA:
        case MOD_PLASMA_SPLASH:                                 return cg_weapons[WP_PLASMAGUN].weaponIcon;
        case MOD_RAILGUN:
        case MOD_RAILGUN_HEADSHOT:                              return cg_weapons[WP_RAILGUN].weaponIcon;
        case MOD_LIGHTNING:
        case MOD_LIGHTNING_DISCHARGE:                           return cg_weapons[WP_LIGHTNING].weaponIcon;
        case MOD_BFG:
        case MOD_BFG_SPLASH:                                    return cg_weapons[WP_BFG].weaponIcon;
        case MOD_NAIL:                                          return cg_weapons[WP_NAILGUN].weaponIcon;
        case MOD_CHAINGUN:                                      return cg_weapons[WP_CHAINGUN].weaponIcon;
        case MOD_PROXIMITY_MINE:                                return cg_weapons[WP_PROX_LAUNCHER].weaponIcon;
        case MOD_HMG:                                           return cg_weapons[WP_HMG].weaponIcon;
        default:                                                return cgs.media.fragIconShader;
    }
}

/*
=============
CG_StripColorCodes
[QL] Strip Q3 color codes from a string (for team game obituary names)
=============
*/
static void CG_StripColorCodes(char *dst, const char *src, int dstSize) {
    int i = 0;
    while (*src && i < dstSize - 1) {
        if (*src == '^' && src[1] >= '0' && src[1] <= '7') {
            src += 2;
            continue;
        }
        if (*src >= ' ') {
            dst[i++] = *src;
        }
        src++;
    }
    dst[i] = '\0';
}

/*
=============
CG_AddObituary
[QL] Add an obituary entry to the ring buffer (binary: CG_ScorePlum at 10018f60)
=============
*/
static void CG_AddObituary(const char *victimName, const char *attackerName,
                            int victimTeam, int attackerTeam,
                            int hasAttacker, qhandle_t weaponIcon) {
    int i;

    // Shift entries if we're at capacity (controlled by cg_obituaryRowSize)
    if (cg.obituaryCount >= cg_obituaryRowSize.integer &&
        cg_obituaryRowSize.integer > 0 && cg_obituaryRowSize.integer < MAX_OBITUARIES) {
        for (i = 0; i < cg.obituaryCount - 1; i++) {
            cg.obituaries[i] = cg.obituaries[i + 1];
        }
        cg.obituaryCount--;
    }

    // Also shift if at absolute max
    if (cg.obituaryCount >= MAX_OBITUARIES) {
        for (i = 0; i < MAX_OBITUARIES - 1; i++) {
            cg.obituaries[i] = cg.obituaries[i + 1];
        }
        cg.obituaryCount = MAX_OBITUARIES - 1;
    }

    i = cg.obituaryCount;
    memset(&cg.obituaries[i], 0, sizeof(cg.obituaries[i]));
    cg.obituaries[i].active = 1;
    cg.obituaries[i].time = cg.time;
    cg.obituaries[i].hasAttacker = hasAttacker;
    cg.obituaries[i].weaponIcon = weaponIcon;
    cg.obituaries[i].victimTeam = victimTeam;
    cg.obituaries[i].attackerTeam = attackerTeam;

    if (victimName) {
        if (cgs.gametype >= GT_TEAM) {
            CG_StripColorCodes(cg.obituaries[i].victimName, victimName, sizeof(cg.obituaries[i].victimName));
        } else {
            Q_strncpyz(cg.obituaries[i].victimName, victimName, sizeof(cg.obituaries[i].victimName));
        }
    }
    if (attackerName) {
        if (cgs.gametype >= GT_TEAM) {
            CG_StripColorCodes(cg.obituaries[i].attackerName, attackerName, sizeof(cg.obituaries[i].attackerName));
        } else {
            Q_strncpyz(cg.obituaries[i].attackerName, attackerName, sizeof(cg.obituaries[i].attackerName));
        }
    }

    cg.obituaryCount++;
}

/*
=============
CG_Obituary
=============
*/
static void CG_Obituary(entityState_t* ent) {
    int mod;
    int target, attacker;
    // MOD_THAW only sets a message for the no-attacker (auto-thaw) case and
    // otherwise falls through to the attacker/target handling below, so this
    // must start out NULL rather than holding whatever was on the stack.
    char* message = NULL;
    char* message2;
    const char* targetInfo;
    const char* attackerInfo;
    char targetName[32];
    char attackerName[32];
    gender_t gender;
    clientInfo_t* ci;
    qhandle_t weaponIcon;
    int victimTeam, attackerTeam;

    target = ent->otherEntityNum;
    attacker = ent->otherEntityNum2;
    mod = ent->eventParm;

    if (target < 0 || target >= MAX_CLIENTS) {
        CG_Error("CG_Obituary: target out of range");
    }

    // [QL] Resolve weapon icon and add to obituary display buffer
    weaponIcon = CG_ModToWeaponIcon(mod);
    victimTeam = cgs.clientinfo[target].team;
    if (attacker >= 0 && attacker < MAX_CLIENTS && attacker != target) {
        attackerTeam = cgs.clientinfo[attacker].team;
        CG_AddObituary(cgs.clientinfo[target].name, cgs.clientinfo[attacker].name,
                       victimTeam, attackerTeam, 1, weaponIcon);
    } else {
        CG_AddObituary(cgs.clientinfo[target].name, NULL,
                       victimTeam, 0, 0, weaponIcon);
    }
    ci = &cgs.clientinfo[target];

    if (attacker < 0 || attacker >= MAX_CLIENTS) {
        attacker = ENTITYNUM_WORLD;
        attackerInfo = NULL;
    } else {
        attackerInfo = CG_ConfigString(CS_PLAYERS + attacker);
    }

    targetInfo = CG_ConfigString(CS_PLAYERS + target);
    if (!targetInfo) {
        return;
    }
    Q_strncpyz(targetName, Info_ValueForKey(targetInfo, "n"), sizeof(targetName) - 2);
    strcat(targetName, S_COLOR_WHITE);

    message2 = "";

    // check for single client messages

    switch (mod) {
        case MOD_SUICIDE:
            message = "suicides";
            break;
        case MOD_FALLING:
            message = "cratered";
            break;
        case MOD_CRUSH:
            message = "was squished";
            break;
        case MOD_WATER:
            message = "sank like a rock";
            break;
        case MOD_SLIME:
            message = "melted";
            break;
        case MOD_LAVA:
            message = "does a back flip into the lava";
            break;
        case MOD_TARGET_LASER:
            message = "saw the light";
            break;
        case MOD_TRIGGER_HURT:
            message = "was in the wrong place";
            break;
        // [QL] new environment/solo means-of-death
        case MOD_SWITCH_TEAMS:
            message = "switched teams";
            break;
        case MOD_THAW:
            // [QL] auto-thaw (no attacker) shows a solo message
            if (attacker == ENTITYNUM_WORLD || attacker < 0 || attacker >= MAX_CLIENTS) {
                message = "was auto-thawed";
            }
            break;
        default:
            message = NULL;
            break;
    }

    if (attacker == target) {
        gender = ci->gender;
        switch (mod) {
            case MOD_KAMIKAZE:
                message = "goes out with a bang";
                break;
            case MOD_GRENADE_SPLASH:
                if (gender == GENDER_FEMALE)
                    message = "tripped on her own grenade";
                else if (gender == GENDER_NEUTER)
                    message = "tripped on its own grenade";
                else
                    message = "tripped on his own grenade";
                break;
            case MOD_ROCKET_SPLASH:
                if (gender == GENDER_FEMALE)
                    message = "blew herself up";
                else if (gender == GENDER_NEUTER)
                    message = "blew itself up";
                else
                    message = "blew himself up";
                break;
            case MOD_PLASMA_SPLASH:
                if (gender == GENDER_FEMALE)
                    message = "melted herself";
                else if (gender == GENDER_NEUTER)
                    message = "melted itself";
                else
                    message = "melted himself";
                break;
            case MOD_BFG_SPLASH:
                message = "should have used a smaller gun";
                break;
            case MOD_PROXIMITY_MINE:
                if (gender == GENDER_FEMALE) {
                    message = "found her prox mine";
                } else if (gender == GENDER_NEUTER) {
                    message = "found its prox mine";
                } else {
                    message = "found his prox mine";
                }
                break;
            // [QL] lightning-gun water discharge self-kill
            case MOD_LIGHTNING_DISCHARGE:
                if (gender == GENDER_FEMALE)
                    message = "discharged herself";
                else if (gender == GENDER_NEUTER)
                    message = "discharged itself";
                else
                    message = "discharged himself";
                break;
            default:
                if (gender == GENDER_FEMALE)
                    message = "killed herself";
                else if (gender == GENDER_NEUTER)
                    message = "killed itself";
                else
                    message = "killed himself";
                break;
        }
    }

    if (message) {
        CG_Printf("%s %s.\n", targetName, message);
        return;
    }

    // check for kill messages from the current clientNum
    if (attacker == cg.snap->ps.clientNum) {
        char* s;

        if (cgs.gametype < GT_TEAM) {
            s = va("You fragged %s\n%s place with %i", targetName,
                   CG_PlaceString(cg.snap->ps.persistant[PERS_RANK] + 1),
                   cg.snap->ps.persistant[PERS_SCORE]);
        } else {
            s = va("You fragged %s", targetName);
        }
        CG_CenterPrint(s, SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH);
        // print the text message as well
    }

    // check for double client messages
    if (!attackerInfo) {
        attacker = ENTITYNUM_WORLD;
        strcpy(attackerName, "noname");
    } else {
        Q_strncpyz(attackerName, Info_ValueForKey(attackerInfo, "n"), sizeof(attackerName) - 2);
        strcat(attackerName, S_COLOR_WHITE);
        // check for kill messages about the current clientNum
        if (target == cg.snap->ps.clientNum) {
            Q_strncpyz(cg.killerName, attackerName, sizeof(cg.killerName));
        }
    }

    if (attacker != ENTITYNUM_WORLD) {
        switch (mod) {
            case MOD_GRAPPLE:
                message = "was caught by";
                break;
            case MOD_GAUNTLET:
                message = "was pummeled by";
                break;
            case MOD_MACHINEGUN:
                message = "was machinegunned by";
                break;
            case MOD_SHOTGUN:
                message = "was gunned down by";
                break;
            case MOD_GRENADE:
                message = "ate";
                message2 = "'s grenade";
                break;
            case MOD_GRENADE_SPLASH:
                message = "was shredded by";
                message2 = "'s shrapnel";
                break;
            case MOD_ROCKET:
                message = "ate";
                message2 = "'s rocket";
                break;
            case MOD_ROCKET_SPLASH:
                message = "almost dodged";
                message2 = "'s rocket";
                break;
            case MOD_PLASMA:
                message = "was melted by";
                message2 = "'s plasmagun";
                break;
            case MOD_PLASMA_SPLASH:
                message = "was melted by";
                message2 = "'s plasmagun";
                break;
            case MOD_RAILGUN:
            case MOD_RAILGUN_HEADSHOT:              // [QL]
                message = "was railed by";
                break;
            case MOD_LIGHTNING:
                message = "was electrocuted by";
                break;
            case MOD_BFG:
            case MOD_BFG_SPLASH:
                message = "was blasted by";
                message2 = "'s BFG";
                break;
            case MOD_NAIL:
                message = "was nailed by";
                break;
            case MOD_CHAINGUN:
                message = "got lead poisoning from";
                message2 = "'s Chaingun";
                break;
            case MOD_PROXIMITY_MINE:
                message = "was too close to";
                message2 = "'s Prox Mine";
                break;
            case MOD_KAMIKAZE:
                message = "falls to";
                message2 = "'s Kamikaze blast";
                break;
            case MOD_JUICED:
                message = "was juiced by";
                break;
            case MOD_TELEFRAG:
                message = "tried to invade";
                message2 = "'s personal space";
                break;
            // [QL] new means-of-death obituary lines
            case MOD_THAW:
                message = "was thawed by";
                break;
            case MOD_LIGHTNING_DISCHARGE:
                message = "was discharged by";
                break;
            case MOD_HMG:
                message = "was ripped up by";
                message2 = "'s HMG";
                break;
            default:
                message = "was killed by";
                break;
        }

        if (message) {
            CG_Printf("%s %s %s%s\n",
                      targetName, message, attackerName, message2);
            return;
        }
    }

    // we don't know what it was
    CG_Printf("%s died.\n", targetName);

    // QL binary: cg_followKiller.integer auto-follows the killer (vmCvar 0x10A64800)
    if (cg_followKiller.integer && target == cg.snap->ps.clientNum &&
        attacker != target && attacker >= 0 && attacker < MAX_CLIENTS) {
        trap_SendConsoleCommand(va("follow %d\n", attacker));
    }
}

//==========================================================================

/*
===============
CG_UseItem
===============
*/
static void CG_UseItem(centity_t* cent) {
    clientInfo_t* ci;
    int itemNum, clientNum;
    gitem_t* item;
    entityState_t* es;

    es = &cent->currentState;

    itemNum = (es->event & ~EV_EVENT_BITS) - EV_USE_ITEM0;
    if (itemNum < 0 || itemNum > HI_NUM_HOLDABLE) {
        itemNum = 0;
    }

    // print a message if the local player
    if (es->number == cg.snap->ps.clientNum) {
        if (!itemNum) {
            CG_CenterPrint("No item to use", SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH);
        } else {
            item = BG_FindItemForHoldable(itemNum);
            CG_CenterPrint(va("Use %s", item->pickup_name), SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH);
        }
    }

    switch (itemNum) {
        default:
        case HI_NONE:
            trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.useNothingSound);
            break;

        case HI_TELEPORTER:
            break;

        case HI_MEDKIT:
            clientNum = cent->currentState.clientNum;
            if (clientNum >= 0 && clientNum < MAX_CLIENTS) {
                ci = &cgs.clientinfo[clientNum];
                ci->medkitUsageTime = cg.time;
            }
            trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.medkitSound);
            break;

        case HI_KAMIKAZE:
            break;

        case HI_PORTAL:
            break;

        case HI_INVULNERABILITY:
            trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.useInvulnerabilitySound);
            break;
    }
}

/*
================
CG_ItemPickup

A new item was picked up this frame
================
*/
static void CG_ItemPickup(int itemNum) {
    cg.itemPickup = itemNum;
    cg.itemPickupTime = cg.time;
    cg.itemPickupBlendTime = cg.time;
    // see if it should be the grabbed weapon
    if (bg_itemlist[itemNum].giType == IT_WEAPON) {
        // select it immediately
        if (cg_autoswitch.integer && bg_itemlist[itemNum].giTag != WP_MACHINEGUN) {
            cg.weaponSelectTime = cg.time;
            cg.weaponSelect = bg_itemlist[itemNum].giTag;
        }
    }
}

/*
================
CG_WaterLevel

Returns waterlevel for entity origin
================
*/
int CG_WaterLevel(centity_t* cent) {
    vec3_t point;
    int contents, sample1, sample2, anim, waterlevel;
    int viewheight;

    anim = cent->currentState.legsAnim & ~ANIM_TOGGLEBIT;

    if (anim == LEGS_WALKCR || anim == LEGS_IDLECR) {
        viewheight = CROUCH_VIEWHEIGHT;
    } else {
        viewheight = DEFAULT_VIEWHEIGHT;
    }

    //
    // get waterlevel, accounting for ducking
    //
    waterlevel = 0;

    point[0] = cent->lerpOrigin[0];
    point[1] = cent->lerpOrigin[1];
    point[2] = cent->lerpOrigin[2] + MINS_Z + 1;
    contents = CG_PointContents(point, -1);

    if (contents & MASK_WATER) {
        sample2 = viewheight - MINS_Z;
        sample1 = sample2 / 2;
        waterlevel = 1;
        point[2] = cent->lerpOrigin[2] + MINS_Z + sample1;
        contents = CG_PointContents(point, -1);

        if (contents & MASK_WATER) {
            waterlevel = 2;
            point[2] = cent->lerpOrigin[2] + MINS_Z + sample2;
            contents = CG_PointContents(point, -1);

            if (contents & MASK_WATER) {
                waterlevel = 3;
            }
        }
    }

    return waterlevel;
}

/*
================
CG_PainEvent

Also called by playerstate transition
================
*/
void CG_PainEvent(centity_t* cent, int health) {
    char* snd;

    // don't do more than two pain sounds a second
    if (cg.time - cent->pe.painTime < 500) {
        return;
    }

    if (health < 25) {
        snd = "*pain25_1.wav";
    } else if (health < 50) {
        snd = "*pain50_1.wav";
    } else if (health < 75) {
        snd = "*pain75_1.wav";
    } else {
        snd = "*pain100_1.wav";
    }
    // play a gurp sound instead of a normal pain sound
    if (CG_WaterLevel(cent) == 3) {
        if (rand() & 1) {
            trap_S_StartSound(NULL, cent->currentState.number, CHAN_VOICE, CG_CustomSound(cent->currentState.number, "sound/player/gurp1.ogg"));
        } else {
            trap_S_StartSound(NULL, cent->currentState.number, CHAN_VOICE, CG_CustomSound(cent->currentState.number, "sound/player/gurp2.ogg"));
        }
    } else {
        trap_S_StartSound(NULL, cent->currentState.number, CHAN_VOICE, CG_CustomSound(cent->currentState.number, snd));
    }
    // save pain time for programitic twitch animation
    cent->pe.painTime = cg.time;
    cent->pe.painDirection ^= 1;
}

/*
==============
CG_EntityEvent

An entity has an event value
also called by CG_CheckPlayerstateEvents
==============
*/
#define DEBUGNAME(x)              \
    if (cg_debugEvents.integer) { \
        CG_Printf(x "\n");        \
    }
void CG_EntityEvent(centity_t* cent, vec3_t position) {
    entityState_t* es;
    int event;
    vec3_t dir;
    const char* s;
    int clientNum;
    clientInfo_t* ci;

    es = &cent->currentState;
    event = es->event & ~EV_EVENT_BITS;

    if (cg_debugEvents.integer) {
        CG_Printf("ent:%3i  event:%3i ", es->number, event);
    }

    if (!event) {
        DEBUGNAME("ZEROEVENT");
        return;
    }

    clientNum = es->clientNum;
    if (clientNum < 0 || clientNum >= MAX_CLIENTS) {
        clientNum = 0;
    }
    ci = &cgs.clientinfo[clientNum];

    switch (event) {
        //
        // movement generated events
        //
        case EV_FOOTSTEP:
            DEBUGNAME("EV_FOOTSTEP");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY,
                                  cgs.media.footsteps[ci->footsteps][rand() & 3]);
            }
            break;
        case EV_FOOTSTEP_METAL:
            DEBUGNAME("EV_FOOTSTEP_METAL");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY,
                                  cgs.media.footsteps[FOOTSTEP_METAL][rand() & 3]);
            }
            break;
        case EV_FOOTSPLASH:
            DEBUGNAME("EV_FOOTSPLASH");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY,
                                  cgs.media.footsteps[FOOTSTEP_SPLASH][rand() & 3]);
            }
            break;
        case EV_FOOTWADE:
            DEBUGNAME("EV_FOOTWADE");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY,
                                  cgs.media.footsteps[FOOTSTEP_SPLASH][rand() & 3]);
            }
            break;
        case EV_SWIM:
            DEBUGNAME("EV_SWIM");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY,
                                  cgs.media.footsteps[FOOTSTEP_SPLASH][rand() & 3]);
            }
            break;

        case EV_FALL_SHORT:
            DEBUGNAME("EV_FALL_SHORT");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.landSound);
            if (clientNum == cg.predictedPlayerState.clientNum) {
                // smooth landing z changes
                cg.landChange = -8;
                cg.landTime = cg.time;
            }
            break;
        case EV_FALL_MEDIUM:
            DEBUGNAME("EV_FALL_MEDIUM");
            // use normal pain sound
            trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, "*pain100_1.wav"));
            if (clientNum == cg.predictedPlayerState.clientNum) {
                // smooth landing z changes
                cg.landChange = -16;
                cg.landTime = cg.time;
            }
            break;
        case EV_FALL_FAR:
            DEBUGNAME("EV_FALL_FAR");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, CG_CustomSound(es->number, "*fall1.wav"));
            cent->pe.painTime = cg.time;  // don't play a pain sound right after this
            if (clientNum == cg.predictedPlayerState.clientNum) {
                // smooth landing z changes
                cg.landChange = -24;
                cg.landTime = cg.time;
            }
            break;

        // [QL] EV_STEP_4/8/12/16 removed - QL uses playerState_t for step smoothing.
        // Step smoothing is now handled in CG_PlayerStateToEntityState or CG_TransitionPlayerState.

        case EV_JUMP_PAD:
            DEBUGNAME("EV_JUMP_PAD");
            //		CG_Printf( "EV_JUMP_PAD w/effect #%i\n", es->eventParm );
            {
                vec3_t up = {0, 0, 1};

                CG_SmokePuff(cent->lerpOrigin, up,
                             32,
                             1, 1, 1, 0.33f,
                             1000,
                             cg.time, 0,
                             LEF_PUFF_DONT_SCALE,
                             cgs.media.smokePuffShader);
            }

            // boing sound at origin, jump sound on player
            trap_S_StartSound(cent->lerpOrigin, -1, CHAN_VOICE, cgs.media.jumpPadSound);
            trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, "*jump1.wav"));
            break;

        case EV_JUMP:
            DEBUGNAME("EV_JUMP");
            trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, "*jump1.wav"));
            break;
        case EV_TAUNT:
            DEBUGNAME("EV_TAUNT");
            // [QL] cg_allowTaunt gates the taunt sound (binary tests DAT_10a68eec, exact-nonzero)
            if (cg_allowTaunt.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, "*taunt.wav"));
            }
            break;

        // [QL] Voice-taunt events (0x4b-0x50). Each enqueues a voice chat for the
        // taunting client via CG_VoiceChatLocal(client, token). Binary passes the
        // client in es->number (0x0, [EBP]) NOT es->clientNum. Binary gates
        // EV_TAUNT_YES/EV_TAUNT_NO on cg_allowTaunt; the four order taunts are
        // never gated. Tokens are the literal voice-chat command names.
        case EV_TAUNT_YES:                      // 0x4b
            DEBUGNAME("EV_TAUNT_YES");
            if (cg_allowTaunt.integer) {
                CG_VoiceChatLocal(es->number, "yes");
            }
            break;
        case EV_TAUNT_NO:                       // 0x4c
            DEBUGNAME("EV_TAUNT_NO");
            if (cg_allowTaunt.integer) {
                CG_VoiceChatLocal(es->number, "no");
            }
            break;
        case EV_TAUNT_FOLLOWME:                 // 0x4d
            DEBUGNAME("EV_TAUNT_FOLLOWME");
            CG_VoiceChatLocal(es->number, "followme");
            break;
        case EV_TAUNT_GETFLAG:                  // 0x4e
            DEBUGNAME("EV_TAUNT_GETFLAG");
            CG_VoiceChatLocal(es->number, "ongetflag");
            break;
        case EV_TAUNT_GUARDBASE:                // 0x4f
            DEBUGNAME("EV_TAUNT_GUARDBASE");
            CG_VoiceChatLocal(es->number, "ondefense");
            break;
        case EV_TAUNT_PATROL:                   // 0x50
            DEBUGNAME("EV_TAUNT_PATROL");
            CG_VoiceChatLocal(es->number, "onpatrol");
            break;
        case EV_WATER_TOUCH:
            DEBUGNAME("EV_WATER_TOUCH");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.watrInSound);
            break;
        case EV_WATER_LEAVE:
            DEBUGNAME("EV_WATER_LEAVE");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.watrOutSound);
            break;
        case EV_WATER_UNDER:
            DEBUGNAME("EV_WATER_UNDER");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.watrUnSound);
            break;
        case EV_WATER_CLEAR:
            DEBUGNAME("EV_WATER_CLEAR");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, CG_CustomSound(es->number, "*gasp.wav"));
            break;

        case EV_ITEM_PICKUP:
            DEBUGNAME("EV_ITEM_PICKUP");
            {
                gitem_t* item;
                int index;

                index = es->eventParm;  // player predicted

                if (index < 1 || index >= bg_numItems) {
                    break;
                }
                item = &bg_itemlist[index];

                // powerups and team items will have a separate global sound, this one
                // will be played at prediction time
                if (item->giType == IT_POWERUP || item->giType == IT_TEAM) {
                    trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.n_healthSound);
                } else if (item->giType == IT_PERSISTANT_POWERUP) {
                    switch (item->giTag) {
                        case PW_SCOUT:
                            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.scoutSound);
                            break;
                        case PW_GUARD:
                            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.guardSound);
                            break;
                        case PW_DOUBLER:
                            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.doublerSound);
                            break;
                        case PW_AMMOREGEN:
                            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.ammoregenSound);
                            break;
                    }
                } else {
                    trap_S_StartSound(NULL, es->number, CHAN_AUTO, trap_S_RegisterSound(item->pickup_sound, qfalse));
                }

                // show icon and name on status bar
                if (es->number == cg.snap->ps.clientNum) {
                    CG_ItemPickup(index);
                }
            }
            break;

        case EV_GLOBAL_ITEM_PICKUP:
            DEBUGNAME("EV_GLOBAL_ITEM_PICKUP");
            {
                gitem_t* item;
                int index;

                index = es->eventParm;  // player predicted

                if (index < 1 || index >= bg_numItems) {
                    break;
                }
                item = &bg_itemlist[index];
                // powerup pickups are global
                if (item->pickup_sound) {
                    trap_S_StartSound(NULL, cg.snap->ps.clientNum, CHAN_AUTO, trap_S_RegisterSound(item->pickup_sound, qfalse));
                }

                // show icon and name on status bar
                if (es->number == cg.snap->ps.clientNum) {
                    CG_ItemPickup(index);
                }
            }
            break;

        //
        // weapon events
        //
        case EV_NOAMMO:
            DEBUGNAME("EV_NOAMMO");
            if (es->number == cg.snap->ps.clientNum) {
                // QL binary: cg_switchOnEmpty.integer gates auto-switch (vmCvar 0x10A673E0)
                if (cg_switchOnEmpty.integer) {
                    CG_OutOfAmmoChange();
                } else {
                    trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.noAmmoSound);
                }
            }
            break;
        case EV_CHANGE_WEAPON:
            DEBUGNAME("EV_CHANGE_WEAPON");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.selectSound);
            break;
        case EV_DROP_WEAPON:
            DEBUGNAME("EV_DROP_WEAPON");
            // [QL] auto-switch to next available weapon when local player drops
            if (es->number == cg.snap->ps.clientNum) {
                CG_NextWeapon_f();
            }
            break;
        case EV_FIRE_WEAPON:
            DEBUGNAME("EV_FIRE_WEAPON");
            CG_FireWeapon(cent);
            break;

        case EV_USE_ITEM0:
            DEBUGNAME("EV_USE_ITEM0");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM1:
            DEBUGNAME("EV_USE_ITEM1");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM2:
            DEBUGNAME("EV_USE_ITEM2");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM3:
            DEBUGNAME("EV_USE_ITEM3");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM4:
            DEBUGNAME("EV_USE_ITEM4");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM5:
            DEBUGNAME("EV_USE_ITEM5");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM6:
            DEBUGNAME("EV_USE_ITEM6");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM7:
            DEBUGNAME("EV_USE_ITEM7");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM8:
            DEBUGNAME("EV_USE_ITEM8");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM9:
            DEBUGNAME("EV_USE_ITEM9");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM10:
            DEBUGNAME("EV_USE_ITEM10");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM11:
            DEBUGNAME("EV_USE_ITEM11");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM12:
            DEBUGNAME("EV_USE_ITEM12");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM13:
            DEBUGNAME("EV_USE_ITEM13");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM14:
            DEBUGNAME("EV_USE_ITEM14");
            CG_UseItem(cent);
            break;
        case EV_USE_ITEM15:
            DEBUGNAME("EV_USE_ITEM15");
            CG_UseItem(cent);
            break;

        //=================================================================

        //
        // other events
        //
        case EV_PLAYER_TELEPORT_IN:
            DEBUGNAME("EV_PLAYER_TELEPORT_IN");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.teleInSound);
            CG_SpawnEffect(position);
            break;

        case EV_PLAYER_TELEPORT_OUT:
            DEBUGNAME("EV_PLAYER_TELEPORT_OUT");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.teleOutSound);
            CG_SpawnEffect(position);
            break;

        case EV_ITEM_POP:
            DEBUGNAME("EV_ITEM_POP");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.respawnSound);
            break;
        case EV_ITEM_RESPAWN:
            DEBUGNAME("EV_ITEM_RESPAWN");
            cent->miscTime = cg.time;  // scale up from this
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.respawnSound);
            break;

        case EV_GRENADE_BOUNCE:
            DEBUGNAME("EV_GRENADE_BOUNCE");
            if (rand() & 1) {
                trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.hgrenb1aSound);
            } else {
                trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.hgrenb2aSound);
            }
            break;

        case EV_PROXIMITY_MINE_STICK:
            DEBUGNAME("EV_PROXIMITY_MINE_STICK");
            if (es->eventParm & SURF_FLESH) {
                trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.wstbimplSound);
            } else if (es->eventParm & SURF_METALSTEPS) {
                trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.wstbimpmSound);
            } else {
                trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.wstbimpdSound);
            }
            break;

        case EV_PROXIMITY_MINE_TRIGGER:
            DEBUGNAME("EV_PROXIMITY_MINE_TRIGGER");
            trap_S_StartSound(NULL, es->number, CHAN_AUTO, cgs.media.wstbactvSound);
            break;
        case EV_KAMIKAZE:
            DEBUGNAME("EV_KAMIKAZE");
            CG_KamikazeEffect(cent->lerpOrigin);
            break;
        case EV_OBELISKEXPLODE:
            DEBUGNAME("EV_OBELISKEXPLODE");
            CG_ObeliskExplode(cent->lerpOrigin, es->eventParm);
            break;
        case EV_OBELISKPAIN:
            DEBUGNAME("EV_OBELISKPAIN");
            CG_ObeliskPain(cent->lerpOrigin);
            break;
        case EV_INVUL_IMPACT:
            DEBUGNAME("EV_INVUL_IMPACT");
            CG_InvulnerabilityImpact(cent->lerpOrigin, cent->currentState.angles);
            break;
        case EV_JUICED:
            DEBUGNAME("EV_JUICED");
            // [QL] repurposed (binary 0x47): in freeze tag this is the "player
            // frozen" ice effect; in every other gametype it gibs the player.
            // Binary gates on DAT_10a5f430 (freeze media / freeze-tag active).
            if (cgs.gametype == GT_FREEZE) {
                CG_FreezeEffect(cent->lerpOrigin);
            } else {
                CG_GibPlayer(cent->lerpOrigin);
            }
            break;
        case EV_LIGHTNINGBOLT:
            DEBUGNAME("EV_LIGHTNINGBOLT");
            CG_LightningBoltBeam(es->origin2, es->pos.trBase);
            break;
        case EV_SCOREPLUM:
            DEBUGNAME("EV_SCOREPLUM");
            // [QL] binary guards on the local player before spawning the plum
            // (CG_EntityEvent 0x40: snap ps.clientNum == es->clientNum 0xb0).
            // Score value rides es->time (0x5c).
            if (cg.snap->ps.clientNum == es->clientNum) {
                CG_ScorePlum(cent->currentState.otherEntityNum, cent->lerpOrigin, cent->currentState.time);
            }
            break;

        //
        // missile impacts
        //
        case EV_MISSILE_HIT:
            DEBUGNAME("EV_MISSILE_HIT");
            ByteToDir(es->eventParm, dir);
            CG_MissileHitPlayer(es->weapon, position, dir, es->otherEntityNum);
            break;

        case EV_MISSILE_MISS:
            DEBUGNAME("EV_MISSILE_MISS");
            ByteToDir(es->eventParm, dir);
            CG_MissileHitWall(es->weapon, 0, position, dir, IMPACTSOUND_DEFAULT);
            break;

        case EV_MISSILE_MISS_METAL:
            DEBUGNAME("EV_MISSILE_MISS_METAL");
            ByteToDir(es->eventParm, dir);
            CG_MissileHitWall(es->weapon, 0, position, dir, IMPACTSOUND_METAL);
            break;

        case EV_RAILTRAIL:
            DEBUGNAME("EV_RAILTRAIL");
            cent->currentState.weapon = WP_RAILGUN;

            // CG_RailTrail draws with cgs.media.railCoreShader / railRingsShader,
            // both of which are only loaded by CG_RegisterWeapon(WP_RAILGUN).
            // Nothing pre-registers weapons at level load, so a rail shot seen
            // before the railgun's own model or item has been drawn rendered
            // with shader handle 0 - an invisible tracer, and an invisible
            // impact once CG_MissileHitWall reached railExplosionShader.
            CG_RegisterWeapon(WP_RAILGUN);

            if (es->clientNum == cg.snap->ps.clientNum && !cg.renderingThirdPerson) {
                if (cg_drawGun.integer == 2)
                    VectorMA(es->origin2, 8, cg.refdef.viewaxis[1], es->origin2);
                else if (cg_drawGun.integer == 3)
                    VectorMA(es->origin2, 4, cg.refdef.viewaxis[1], es->origin2);
            }

            // CG_PredictRailFire already drew this shot's trail and impact
            // straight from the barrel the moment the button went down, so that
            // the local rail does not wait a round trip to appear. Drawing the
            // event's copy as well put a second beam along the view axis ending
            // at the same point. Only skip it for our own predicted shot and
            // only briefly: every other player's rail arrives this way, and a
            // shot we did not predict - paused, in a timeout, spectating - still
            // needs drawing here. Rail refire is 1500ms, so 1000 cannot swallow
            // a genuine second shot.
            if (es->clientNum == cg.snap->ps.clientNum && cg.predictedRailTime &&
                cg.time - cg.predictedRailTime < 1000) {
                break;
            }

            CG_RailTrail(ci, es->origin2, es->pos.trBase);

            // if the end was on a nomark surface, don't make an explosion
            if (es->eventParm != 255) {
                ByteToDir(es->eventParm, dir);
                CG_MissileHitWall(es->weapon, es->clientNum, position, dir, IMPACTSOUND_DEFAULT);
            }
            break;

        case EV_BULLET_HIT_WALL:
            DEBUGNAME("EV_BULLET_HIT_WALL");
            ByteToDir(es->eventParm, dir);
            CG_Bullet(es->pos.trBase, es->otherEntityNum, dir, qfalse, ENTITYNUM_WORLD);
            break;

        case EV_BULLET_HIT_FLESH:
            DEBUGNAME("EV_BULLET_HIT_FLESH");
            CG_Bullet(es->pos.trBase, es->otherEntityNum, dir, qtrue, es->eventParm);
            break;

        case EV_SHOTGUN:
            DEBUGNAME("EV_SHOTGUN");
            CG_ShotgunFire(es);
            break;

        case EV_GENERAL_SOUND:
            DEBUGNAME("EV_GENERAL_SOUND");
            if (cgs.gameSounds[es->eventParm]) {
                trap_S_StartSound(NULL, es->number, CHAN_VOICE, cgs.gameSounds[es->eventParm]);
            } else {
                s = CG_ConfigString(CS_SOUNDS + es->eventParm);
                trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, s));
            }
            break;

        case EV_GLOBAL_SOUND:  // play from the player's head so it never diminishes
            DEBUGNAME("EV_GLOBAL_SOUND");
            if (cgs.gameSounds[es->eventParm]) {
                trap_S_StartSound(NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.gameSounds[es->eventParm]);
            } else {
                s = CG_ConfigString(CS_SOUNDS + es->eventParm);
                trap_S_StartSound(NULL, cg.snap->ps.clientNum, CHAN_AUTO, CG_CustomSound(es->number, s));
            }
            break;

        case EV_GLOBAL_TEAM_SOUND:  // play from the player's head so it never diminishes
        {
            DEBUGNAME("EV_GLOBAL_TEAM_SOUND");
            switch (es->eventParm) {
                case GTS_RED_CAPTURE:  // CTF: red team captured the blue flag, 1FCTF: red team captured the neutral flag
                    if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED)
                        CG_AddBufferedSound(cgs.media.captureYourTeamSound);
                    else
                        CG_AddBufferedSound(cgs.media.captureOpponentSound);
                    break;
                case GTS_BLUE_CAPTURE:  // CTF: blue team captured the red flag, 1FCTF: blue team captured the neutral flag
                    if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE)
                        CG_AddBufferedSound(cgs.media.captureYourTeamSound);
                    else
                        CG_AddBufferedSound(cgs.media.captureOpponentSound);
                    break;
                case GTS_RED_RETURN:  // CTF: blue flag returned, 1FCTF: never used
                    if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED)
                        CG_AddBufferedSound(cgs.media.returnYourTeamSound);
                    else
                        CG_AddBufferedSound(cgs.media.returnOpponentSound);
                    //
                    CG_AddBufferedSound(cgs.media.blueFlagReturnedSound);
                    break;
                case GTS_BLUE_RETURN:  // CTF red flag returned, 1FCTF: neutral flag returned
                    if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE)
                        CG_AddBufferedSound(cgs.media.returnYourTeamSound);
                    else
                        CG_AddBufferedSound(cgs.media.returnOpponentSound);
                    //
                    CG_AddBufferedSound(cgs.media.redFlagReturnedSound);
                    break;

                case GTS_RED_TAKEN:  // CTF: red team took blue flag, 1FCTF: blue team took the neutral flag
                    // if this player picked up the flag then a sound is played in CG_CheckLocalSounds
                    if (cg.snap->ps.powerups[PW_BLUEFLAG] || cg.snap->ps.powerups[PW_NEUTRALFLAG]) {
                    } else {
                        if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE) {
                            if (cgs.gametype == GT_1FCTF)
                                CG_AddBufferedSound(cgs.media.yourTeamTookTheFlagSound);
                            else
                                CG_AddBufferedSound(cgs.media.enemyTookYourFlagSound);
                        } else if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED) {
                            if (cgs.gametype == GT_1FCTF)
                                CG_AddBufferedSound(cgs.media.enemyTookTheFlagSound);
                            else
                                CG_AddBufferedSound(cgs.media.yourTeamTookEnemyFlagSound);
                        }
                    }
                    break;
                case GTS_BLUE_TAKEN:  // CTF: blue team took the red flag, 1FCTF red team took the neutral flag
                    // if this player picked up the flag then a sound is played in CG_CheckLocalSounds
                    if (cg.snap->ps.powerups[PW_REDFLAG] || cg.snap->ps.powerups[PW_NEUTRALFLAG]) {
                    } else {
                        if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED) {
                            if (cgs.gametype == GT_1FCTF)
                                CG_AddBufferedSound(cgs.media.yourTeamTookTheFlagSound);
                            else
                                CG_AddBufferedSound(cgs.media.enemyTookYourFlagSound);
                        } else if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE) {
                            if (cgs.gametype == GT_1FCTF)
                                CG_AddBufferedSound(cgs.media.enemyTookTheFlagSound);
                            else
                                CG_AddBufferedSound(cgs.media.yourTeamTookEnemyFlagSound);
                        }
                    }
                    break;
                case GTS_REDOBELISK_ATTACKED:  // Overload: red obelisk is being attacked
                    if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED) {
                        CG_AddBufferedSound(cgs.media.yourBaseIsUnderAttackSound);
                    }
                    break;
                case GTS_BLUEOBELISK_ATTACKED:  // Overload: blue obelisk is being attacked
                    if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE) {
                        CG_AddBufferedSound(cgs.media.yourBaseIsUnderAttackSound);
                    }
                    break;
                case GTS_REDTEAM_SCORED:
                    CG_AddBufferedSound(cgs.media.redScoredSound);
                    break;
                case GTS_BLUETEAM_SCORED:
                    CG_AddBufferedSound(cgs.media.blueScoredSound);
                    break;
                case GTS_REDTEAM_TOOK_LEAD:
                    CG_AddBufferedSound(cgs.media.redLeadsSound);
                    break;
                case GTS_BLUETEAM_TOOK_LEAD:
                    CG_AddBufferedSound(cgs.media.blueLeadsSound);
                    break;
                case GTS_TEAMS_ARE_TIED:
                    CG_AddBufferedSound(cgs.media.teamsTiedSound);
                    break;
                case GTS_KAMIKAZE:
                    trap_S_StartLocalSound(cgs.media.kamikazeFarSound, CHAN_ANNOUNCER);
                    break;

                // ============================================================
                // [QL] GTS 14-24 (binary CG_EntityEvent inner switch on eventParm).
                // Binary-verified transport (see global_team_sound_t):
                //   es->eventParm   = GTS_* index (this switch)
                //   es->modelindex2 = affected/surviving/capturing team
                //   es->powerups    = DOM point id (GTS_DOMINATION_POINT_CAPTURE)
                // ============================================================
                case GTS_REDTEAM_WON:  // 14 (binary case 0xe)
                {
                    int myTeam = cgs.clientinfo[cg.clientNum].team;
                    CG_ResetAnnouncements();
                    // TODO: match-win buzzer gated on cg_teamEventSounds in
                    // the binary; that cvar is not yet in ioquakelive, so play the
                    // fanfare unconditionally for now.
                    trap_S_StartLocalSound(cgs.media.countFightSound, CHAN_ANNOUNCER);
                    if (myTeam == TEAM_RED)
                        trap_S_StartBackgroundTrack("music/win", "");
                    else if (myTeam == TEAM_BLUE)
                        trap_S_StartBackgroundTrack("music/loss", "");
                    CG_QueueAnnouncement(cgs.media.redWinsSound);
                    cg.intermissionStarted = qtrue;
                    break;
                }
                case GTS_BLUETEAM_WON:  // 15 (binary case 0xf)
                {
                    int myTeam = cgs.clientinfo[cg.clientNum].team;
                    CG_ResetAnnouncements();
                    trap_S_StartLocalSound(cgs.media.countFightSound, CHAN_ANNOUNCER);
                    if (myTeam == TEAM_BLUE)
                        trap_S_StartBackgroundTrack("music/win", "");
                    else if (myTeam == TEAM_RED)
                        trap_S_StartBackgroundTrack("music/loss", "");
                    CG_QueueAnnouncement(cgs.media.blueWinsSound);
                    cg.intermissionStarted = qtrue;
                    break;
                }
                case GTS_RED_WINS_ROUND:  // 16 (binary case 0x10)
                    CG_CenterPrint("Red Team Wins Round!", 90, 0.5f);
                    CG_QueueAnnouncement(cgs.media.redWinsRoundSound);
                    break;
                case GTS_BLUE_WINS_ROUND:  // 17 (binary case 0x11)
                    CG_CenterPrint("Blue Team Wins Round!", 90, 0.5f);
                    CG_QueueAnnouncement(cgs.media.blueWinsRoundSound);
                    break;
                case GTS_DRAW_ROUND:  // 18 (binary case 0x12)
                    CG_ResetAnnouncements();
                    CG_CenterPrint("Round Draw", 90, 0.5f);
                    CG_QueueAnnouncement(cgs.media.roundDrawSound);
                    // [QL] Red Rover elimination also lists the surviving
                    // players parsed from a server id string (cgs.nextmaps in the
                    // binary); that source is not yet mirrored into cgs, so only the
                    // common path is ported.
                    break;
                case GTS_LAST_STANDING:  // 19 (binary case 0x13)
                    // gate: cg_announcerLastStanding, and only for the surviving
                    // team (es->modelindex2 == local player's team).
                    if (cg_announcerLastStanding.integer &&
                        cgs.clientinfo[cg.clientNum].team == es->modelindex2) {
                        CG_QueueAnnouncement(cgs.media.lastStandingSound);
                    }
                    break;
                case GTS_ROUND_OVER:  // 20 (binary case 0x14)
                    CG_CenterPrint("Round Over", 90, 0.5f);
                    // [QL] Red Rover elimination shows "<name> Wins!" / survivor list;
                    // requires the parsed id source not yet in cgs.
                    // TODO: round-over announcer sfx not yet in cgs.media
                    break;
                case GTS_DOMINATION_POINT_CAPTURE:  // 23 (binary case 0x17)
                    if (es->modelindex2 == 0) {
                        // round-start "fight" sound
                        trap_S_StartLocalSound(cgs.media.countFightSound, CHAN_ANNOUNCER);
                    } else {
                        // es->modelindex2 = capturing team; es->powerups = DOM point id.
                        // TODO: "captured point <name>" CenterPrint needs
                        // BG_GetDomPointFromIdentifier, and the DOM-capture announcer
                        // sfx is not yet in cgs.media; port when DOM is wired up.
                    }
                    break;
                case GTS_SURVIVOR:  // 24 (binary case 0x18)
                    if (cgs.clientinfo[cg.clientNum].team == es->modelindex2) {
                        trap_S_StartLocalSound(cgs.media.survivorSound, CHAN_ANNOUNCER);
                    }
                    break;

                default:
                    break;
            }
            break;
        }

        case EV_PAIN:
            // local player sounds are triggered in CG_CheckLocalSounds,
            // so ignore events on the player
            DEBUGNAME("EV_PAIN");
            if (cent->currentState.number != cg.snap->ps.clientNum) {
                CG_PainEvent(cent, es->eventParm);
            }
            break;

        case EV_DEATH1:
        case EV_DEATH2:
        case EV_DEATH3:
            DEBUGNAME("EV_DEATHx");

            if (CG_WaterLevel(cent) == 3) {
                trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, "*drown.wav"));
            } else {
                trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, va("*death%i.wav", event - EV_DEATH1 + 1)));
            }

            break;

        case EV_DROWN:
            DEBUGNAME("EV_DROWN");
            trap_S_StartSound(NULL, es->number, CHAN_VOICE, CG_CustomSound(es->number, "*drown.wav"));
            break;

        case EV_OBITUARY:
            DEBUGNAME("EV_OBITUARY");
            CG_Obituary(es);
            break;

        //
        // powerup events
        //
        case EV_POWERUP_QUAD:
            DEBUGNAME("EV_POWERUP_QUAD");
            if (es->number == cg.snap->ps.clientNum) {
                cg.powerupActive = PW_QUAD;
                cg.powerupTime = cg.time;
            }
            trap_S_StartSound(NULL, es->number, CHAN_ITEM, cgs.media.quadSound);
            break;
        case EV_POWERUP_BATTLESUIT:
            DEBUGNAME("EV_POWERUP_BATTLESUIT");
            if (es->number == cg.snap->ps.clientNum) {
                cg.powerupActive = PW_BATTLESUIT;
                cg.powerupTime = cg.time;
            }
            trap_S_StartSound(NULL, es->number, CHAN_ITEM, cgs.media.protectSound);
            break;
        case EV_POWERUP_REGEN:
            DEBUGNAME("EV_POWERUP_REGEN");
            if (es->number == cg.snap->ps.clientNum) {
                cg.powerupActive = PW_REGEN;
                cg.powerupTime = cg.time;
            }
            trap_S_StartSound(NULL, es->number, CHAN_ITEM, cgs.media.regenSound);
            break;
        case EV_POWERUP_ARMORREGEN:
            DEBUGNAME("EV_POWERUP_ARMORREGEN");
            if (es->number == cg.snap->ps.clientNum) {
                cg.powerupActive = PW_AMMOREGEN;
                cg.powerupTime = cg.time;
            }
            trap_S_StartSound(NULL, es->number, CHAN_ITEM, cgs.media.armorRegenSound);
            break;

        case EV_GIB_PLAYER:
            DEBUGNAME("EV_GIB_PLAYER");
            // don't play gib sound when using the kamikaze because it interferes
            // with the kamikaze sound, downside is that the gib sound will also
            // not be played when someone is gibbed while just carrying the kamikaze
            if (!(es->eFlags & EF_KAMIKAZE)) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.gibSound);
            }
            CG_GibPlayer(cent->lerpOrigin);
            break;

        // [QL] EV_STOPLOOPINGSOUND removed from QL event enum.

        case EV_DEBUG_LINE:
            DEBUGNAME("EV_DEBUG_LINE");
            CG_Beam(cent);
            break;

        // ================================================================
        // [QL] New events - ported from cgamex86.dll binary analysis
        // ================================================================
        case EV_FOOTSTEP_SNOW:
            DEBUGNAME("EV_FOOTSTEP_SNOW");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.footsteps[FOOTSTEP_SNOW][rand() & 3]);
            }
            break;
        case EV_FOOTSTEP_WOOD:
            DEBUGNAME("EV_FOOTSTEP_WOOD");
            if (cg_footsteps.integer) {
                trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.footsteps[FOOTSTEP_WOOD][rand() & 3]);
            }
            break;
        case EV_ITEM_PICKUP_SPEC:
            DEBUGNAME("EV_ITEM_PICKUP_SPEC");
            // [QL] spectator item pickup notification - updates tracking table.
            // Binary CG_ItemPickupSpec (0x53) keys the table on the picking-up
            // client in es->otherEntityNum (0x94), stores the item index from
            // es->modelindex (0xa8), the amount from es->modelindex2 (0xac) and
            // the origin from es->pos.trBase (0x18) - NOT eventParm/generic1.
            {
                int slot, freeSlot = -1;
                // search for existing entry or free slot
                for (slot = 0; slot < MAX_SPEC_PICKUPS; slot++) {
                    if (cg.specPickups[slot].active &&
                        cg.specPickups[slot].clientNum == es->otherEntityNum) {
                        freeSlot = slot;
                        break;
                    }
                    if (!cg.specPickups[slot].active && freeSlot == -1) {
                        freeSlot = slot;
                    }
                }
                if (freeSlot >= 0) {
                    cg.specPickups[freeSlot].active = qtrue;
                    cg.specPickups[freeSlot].clientNum = es->otherEntityNum;
                    cg.specPickups[freeSlot].itemIndex = es->modelindex;
                    cg.specPickups[freeSlot].amount = es->modelindex2;
                    cg.specPickups[freeSlot].time = cg.time;
                    VectorCopy(es->pos.trBase, cg.specPickups[freeSlot].origin);
                }
            }
            break;
        case EV_OVERTIME:
            DEBUGNAME("EV_OVERTIME");
            // [QL] overtime announcement
            trap_S_StartSound(NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.overtimeSound);
            CG_CenterPrint(va("Overtime! %d seconds added", cgs.timelimit_overtime), 90, 0.5f);
            break;
        case EV_GAMEOVER:
            DEBUGNAME("EV_GAMEOVER");
            // [QL] game over - play win/loss music based on outcome
            if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR) {
                trap_S_StartBackgroundTrack("music/win", "");
            } else if (cgs.gametype >= GT_TEAM) {
                if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED) {
                    trap_S_StartBackgroundTrack(
                        cgs.scores1 >= cgs.scores2 ? "music/win" : "music/loss", "");
                } else {
                    trap_S_StartBackgroundTrack(
                        cgs.scores2 >= cgs.scores1 ? "music/win" : "music/loss", "");
                }
            } else {
                // FFA/duel - compare local player score against top score
                trap_S_StartBackgroundTrack(
                    cg.snap->ps.persistant[PERS_SCORE] >= cgs.scores1 ? "music/win" : "music/loss",
                    "");
            }
            break;
        case EV_MISSILE_MISS_DMGTHROUGH:
            DEBUGNAME("EV_MISSILE_MISS_DMGTHROUGH");
            // [QL] wallbang impact -> dedicated damage-through effect (binary 0x56
            // calls CG_MissileHitWall_DmgThrough, not the plain CG_MissileHitWall)
            ByteToDir(es->eventParm, dir);
            CG_MissileHitWall_DmgThrough(position, dir, es->weapon);
            break;
        case EV_THAW_PLAYER:
            DEBUGNAME("EV_THAW_PLAYER");
            // [QL] freeze-tag thaw complete (binary 0x57): spawn the ice-shard
            // burst. The shard spawning lives in cg_effects.c CG_ThawPlayer.
            CG_ThawPlayer(cent->lerpOrigin);
            break;
        case EV_THAW_TICK:
            DEBUGNAME("EV_THAW_TICK");
            trap_S_StartSound(NULL, es->number, CHAN_BODY, cgs.media.thawTickSound);
            break;
        case EV_SHOTGUN_KILL:
            DEBUGNAME("EV_SHOTGUN_KILL");
            // [QL] delayed shotgun-kill blood burst (binary 0x59). The blood-puff
            // logic (count = es->generic1/5, at es->otherEntityNum skin) lives in
            // cg_weapons.c CG_ShotgunKillEffect.
            CG_ShotgunKillEffect(cent);
            break;
        case EV_POI:
            DEBUGNAME("EV_POI");
            // [QL] point of interest marker - floating 3D indicator for team modes
            // es->powerups = duration in ms, es->generic1 = team
            if (cg.numPoiPics < MAX_POI_PICS) {
                int idx = cg.numPoiPics;
                VectorCopy(es->pos.trBase, cg.poiPics[idx].origin);
                cg.poiPics[idx].startTime = cg.time;
                cg.poiPics[idx].length = es->powerups;
                cg.poiPics[idx].team = es->generic1;
                cg.numPoiPics++;
            }
            break;
        case EV_LIGHTNING_DISCHARGE:
            DEBUGNAME("EV_LIGHTNING_DISCHARGE");
            // [QL] LG water-discharge electric burst (binary 0x5c). Intensity is
            // taken from the event parameter; the sprite effect lives in
            // cg_effects.c CG_LightningDischargeEffect.
            CG_LightningDischargeEffect(es->eventParm);
            break;
        case EV_RACE_START:
            DEBUGNAME("EV_RACE_START");
            // [QL] race mode start - init race state for local player
            if (cg.snap->ps.clientNum == es->clientNum) {
                // [QL] binary transport (CG_EntityEvent 0x5d): startTime rides
                // es->time (0x5c), the total rides es->powerups (0xc4) and the
                // first checkpoint ent rides es->otherEntityNum2 (0x98). It does
                // NOT use otherEntityNum/eventParm here.
                cg.race.active = qtrue;
                cg.race.startTime = es->time;
                cg.race.checkpointDiff = 0;
                cg.race.hasDiff = qfalse;
                cg.race.totalCheckpoints = es->powerups;  // total checkpoint count
                cg.race.bestSplit = 0;
                cg.race.checkpointCount = 0;
                cg.race.nextCheckpointEnt = es->otherEntityNum2;  // entity number of first checkpoint
                cg.race.nextNextCheckpointEnt = -1;
                cg.race.currentCheckpointEnt = -1;
                trap_S_StartLocalSound(cgs.media.countFightSound, CHAN_ANNOUNCER);
            }
            break;
        case EV_RACE_CHECKPOINT:
            DEBUGNAME("EV_RACE_CHECKPOINT");
            // [QL] race checkpoint - show split time center print
            if (cg.snap->ps.clientNum == es->clientNum) {
                // [QL] binary transport (CG_EntityEvent 0x5e): the running count
                // is incremented locally (not sent), the next checkpoint ent rides
                // es->otherEntityNum2 (0x98), the total rides es->powerups (0xc4),
                // and the split time rides es->time (0x5c) but is only valid when
                // es->generic1 (0xe0) is nonzero. The binary has no second-level
                // lookahead. Previously read generic1 as the count, otherEntityNum
                // as the next ent, and gated the split on time.
                cg.race.checkpointCount++;
                cg.race.totalCheckpoints = es->powerups;
                cg.race.currentCheckpointEnt = cent->currentState.number;
                cg.race.nextCheckpointEnt = es->otherEntityNum2 ? es->otherEntityNum2 : -1;
                cg.race.nextNextCheckpointEnt = -1;
                if (es->generic1 != 0) {
                    cg.race.checkpointDiff = es->time;
                    cg.race.hasDiff = qtrue;
                } else {
                    cg.race.checkpointDiff = 0;
                    cg.race.hasDiff = qfalse;
                }
                // play checkpoint sound (QL uses countdown sounds based on cg_raceCountdownSounds)
                trap_S_StartLocalSound(cgs.media.count1Sound, CHAN_ANNOUNCER);
                // show split time
                {
                    int remaining = cg.race.totalCheckpoints - cg.race.checkpointCount;
                    char remainStr[64] = "";
                    if (remaining < 4 && cg_drawCheckpointRemaining.integer) {
                        Com_sprintf(remainStr, sizeof(remainStr), "^7%d remaining", remaining);
                    }
                    if (cg.race.hasDiff) {
                        const char *color;
                        int absDiff = cg.race.checkpointDiff < 0 ? -cg.race.checkpointDiff : cg.race.checkpointDiff;
                        int ms = absDiff % 1000;
                        int sec = (absDiff / 1000) % 60;
                        int min = absDiff / 60000;
                        if (cg.race.checkpointDiff < 0) color = "^2";       // ahead (green)
                        else if (cg.race.checkpointDiff == 0) color = "^7"; // tied (white)
                        else color = "^1";                                    // behind (red)
                        if (min > 0) {
                            CG_CenterPrint(va("%s%s%d:%02d.%03d\n%s",
                                color, cg.race.checkpointDiff < 0 ? "-" : "+",
                                min, sec, ms, remainStr), 120, SMALLCHAR_WIDTH);
                        } else {
                            CG_CenterPrint(va("%s%s%d.%03d\n%s",
                                color, cg.race.checkpointDiff < 0 ? "-" : "+",
                                sec, ms, remainStr), 120, SMALLCHAR_WIDTH);
                        }
                    } else {
                        CG_CenterPrint(va("Checkpoint %d\n%s", cg.race.checkpointCount, remainStr), 120, SMALLCHAR_WIDTH);
                    }
                }
            }
            break;
        case EV_RACE_FINISH:
            DEBUGNAME("EV_RACE_FINISH");
            // [QL] race finish - show final time, update personal best
            if (cg.snap->ps.clientNum == es->clientNum) {
                cg.race.active = qfalse;
                cg.race.nextCheckpointEnt = -1;
                cg.race.currentCheckpointEnt = -1;
                // [QL] binary gates the DNF branch on es->generic1 (0xe0), not
                // powerups; finish time still rides es->time (0x5c).
                if (es->generic1 == 0) {
                    // invalid finish (DNF)
                    cg.race.hasDiff = qfalse;
                    cg.race.checkpointDiff = 0;
                } else {
                    cg.race.finishTime = es->time;
                    if (cg.race.bestTime > 0) {
                        // show split vs personal best
                        int diff = cg.race.finishTime - cg.race.bestTime;
                        int absDiff = diff < 0 ? -diff : diff;
                        int ms = absDiff % 1000;
                        int sec = (absDiff / 1000) % 60;
                        int min = absDiff / 60000;
                        const char *color;
                        if (diff < 0) color = "^2";
                        else if (diff == 0) color = "^7";
                        else color = "^1";
                        if (min > 0) {
                            CG_CenterPrint(va("%s%s%d:%02d.%03d",
                                color, diff < 0 ? "-" : "+", min, sec, ms), 120, SMALLCHAR_WIDTH);
                        } else {
                            CG_CenterPrint(va("%s%s%d.%03d",
                                color, diff < 0 ? "-" : "+", sec, ms), 120, SMALLCHAR_WIDTH);
                        }
                        // update PB only if improved
                        if (cg.race.finishTime < cg.race.bestTime) {
                            cg.race.bestTime = cg.race.finishTime;
                        }
                    } else {
                        // first finish - set as PB
                        cg.race.bestTime = cg.race.finishTime;
                    }
                }
                trap_S_StartLocalSound(cgs.media.raceFinishSound, CHAN_ANNOUNCER);
            }
            break;
        case EV_DAMAGEPLUM:
            DEBUGNAME("EV_DAMAGEPLUM");
            // [QL] floating damage number - only show for the local player's hits.
            // The server (DamagePlum 0x10046680 / the shotgun flush) packs the amount
            // in s.time, the recipient clientNum in s.clientNum, and the weapon in
            // s.generic1 - NOT pos.trTime/otherEntityNum/weapon.
            if (cg_damagePlum.string[0] && cg.snap->ps.clientNum == es->clientNum) {
                CG_DamagePlum(es->time, es->generic1, cent->lerpOrigin);
            }
            break;
        case EV_AWARD:
            DEBUGNAME("EV_AWARD");
            // [QL] award notification - 10 types, uses existing reward display system.
            // Binary (CG_EntityEvent 0x61) guards on es->clientNum (0xb0), selects
            // the medal from es->generic1 (0xe0) and takes the count from
            // es->modelindex2 (0xac) - NOT otherEntityNum/weapon/generic1.
            if (cg.snap->ps.clientNum == es->clientNum) {
                qhandle_t shader = 0;
                int count = es->modelindex2;
                switch (es->generic1) {
                case 0: shader = cgs.media.medalComboKill; break;
                case 1: shader = cgs.media.medalRampage; break;
                case 2: shader = cgs.media.medalMidair; break;
                case 3: shader = cgs.media.medalRevenge; break;
                case 4: shader = cgs.media.medalPerforated; break;
                case 5: shader = cgs.media.medalHeadshot; break;
                case 6: shader = cgs.media.medalAccuracy; break;
                case 7: shader = cgs.media.medalQuadGod; break;
                case 8: shader = cgs.media.medalFirstFrag; count = 1; break;
                case 9: shader = cgs.media.medalPerfect; break;
                }
                if (shader && cg.rewardStack < (MAX_REWARDSTACK - 1)) {
                    cg.rewardSound[cg.rewardStack] = cgs.media.impressiveSound;
                    cg.rewardShader[cg.rewardStack] = shader;
                    cg.rewardCount[cg.rewardStack] = count;
                    cg.rewardStack++;
                    cg.rewardTime = cg.time;
                }
            }
            break;
        case EV_INFECTED:
            DEBUGNAME("EV_INFECTED");
            // [QL] infection mode - centerprint + announcement sound
            // Server sends with SVF_SINGLECLIENT so only infected player receives this
            CG_CenterPrint("You have been infected!", 120, SMALLCHAR_WIDTH);
            trap_S_StartLocalSound(cgs.media.infectedSound, CHAN_ANNOUNCER);
            break;
        case EV_NEW_HIGH_SCORE:
            DEBUGNAME("EV_NEW_HIGH_SCORE");
            // [QL] new high score announce (binary CG_EntityEvent 0x1001c5c3 queues
            // new_high_score.ogg via CG_QueueAnnouncement; routed through the buffered
            // announcer ring here).
            CG_AddBufferedSound(cgs.media.newHighScoreSound);
            break;

        default:
            DEBUGNAME("UNKNOWN");
            CG_Error("Unknown event: %i", event);
            break;
    }
}

/*
==============
CG_CheckEvents

==============
*/
void CG_CheckEvents(centity_t* cent) {
    // check for event-only entities
    if (cent->currentState.eType > ET_EVENTS) {
        if (cent->previousEvent) {
            return;  // already fired
        }
        // if this is a player event set the entity number of the client entity number
        if (cent->currentState.eFlags & EF_PLAYER_EVENT) {
            cent->currentState.number = cent->currentState.otherEntityNum;
        }

        cent->previousEvent = 1;

        cent->currentState.event = cent->currentState.eType - ET_EVENTS;
    } else {
        // check for events riding with another entity
        if (cent->currentState.event == cent->previousEvent) {
            return;
        }
        cent->previousEvent = cent->currentState.event;
        if ((cent->currentState.event & ~EV_EVENT_BITS) == 0) {
            return;
        }
    }

    // calculate the position at exactly the frame time
    BG_EvaluateTrajectory(&cent->currentState.pos, cg.snap->serverTime, cent->lerpOrigin);
    CG_SetEntitySoundPosition(cent);

    CG_EntityEvent(cent, cent->lerpOrigin);
}
