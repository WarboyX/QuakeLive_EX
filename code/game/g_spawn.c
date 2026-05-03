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

qboolean G_SpawnString(const char* key, const char* defaultString, char** out) {
    int i;

    if (!level.spawning) {
        *out = (char*)defaultString;
        //		G_Error( "G_SpawnString() called while not spawning" );
    }

    for (i = 0; i < level.numSpawnVars; i++) {
        if (!Q_stricmp(key, level.spawnVars[i][0])) {
            *out = level.spawnVars[i][1];
            return qtrue;
        }
    }

    *out = (char*)defaultString;
    return qfalse;
}

qboolean G_SpawnFloat(const char* key, const char* defaultString, float* out) {
    char* s;
    qboolean present;

    present = G_SpawnString(key, defaultString, &s);
    *out = atof(s);
    return present;
}

qboolean G_SpawnInt(const char* key, const char* defaultString, int* out) {
    char* s;
    qboolean present;

    present = G_SpawnString(key, defaultString, &s);
    *out = atoi(s);
    return present;
}

qboolean G_SpawnVector(const char* key, const char* defaultString, float* out) {
    char* s;
    qboolean present;

    present = G_SpawnString(key, defaultString, &s);
    sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]);
    return present;
}

//
// fields are needed for spawning from the entity string
//
// [QL] Type constants match the binary switch at 0x10065e00: F_VECTOR is 4,
// F_ANGLEHACK 5 (Q3's numbering, kept by the QL fields[] table), F_IGNORE=9 for
// the "light" key. The gap types (3,6,7,8) are unused by QL but kept so the
// numbering stays byte-exact.
typedef enum {
    F_INT,        // 0
    F_FLOAT,      // 1
    F_STRING,     // 2
    F_LSTRING,    // 3  not used in QL fields table
    F_VECTOR,     // 4
    F_ANGLEHACK,  // 5
    F_ENTITY,     // 6  unused
    F_ITEM,       // 7  unused
    F_CLIENT,     // 8  unused
    F_IGNORE      // 9  [QL] parsed but discarded (e.g. "light")
} fieldtype_t;

typedef struct
{
    char* name;
    size_t ofs;
    fieldtype_t type;
} field_t;

// [QL] Order and contents match the binary fields[] table at 0x100803d0 (24
// entries). QL vs Q3: adds cvar, tourPointTarget, tourPointTargetName, noise
// (as a real field rather than a manual G_SpawnString) and light (F_IGNORE).
// QL has NO "target2" field (not parsed from the entity string).
field_t fields[] = {
    {"classname", FOFS(classname), F_STRING},
    {"origin", FOFS(s.origin), F_VECTOR},
    {"model", FOFS(model), F_STRING},
    {"model2", FOFS(model2), F_STRING},
    {"spawnflags", FOFS(spawnflags), F_INT},
    {"speed", FOFS(speed), F_FLOAT},
    {"target", FOFS(target), F_STRING},
    {"targetname", FOFS(targetname), F_STRING},
    {"message", FOFS(message), F_STRING},
    {"cvar", FOFS(cvar), F_STRING},                                // [QL] target_cvar sets this cvar
    {"tourPointTarget", FOFS(tourPointTarget), F_STRING},          // [QL]
    {"tourPointTargetName", FOFS(tourPointTargetName), F_STRING},  // [QL]
    {"noise", FOFS(noise), F_STRING},                              // [QL]
    {"team", FOFS(team), F_STRING},
    {"wait", FOFS(wait), F_FLOAT},
    {"random", FOFS(random), F_FLOAT},
    {"count", FOFS(count), F_INT},
    {"health", FOFS(health), F_INT},
    {"light", 0, F_IGNORE},                                        // [QL] parsed but discarded
    {"dmg", FOFS(damage), F_INT},
    {"angles", FOFS(s.angles), F_VECTOR},
    {"angle", FOFS(s.angles), F_ANGLEHACK},
    {"targetShaderName", FOFS(targetShaderName), F_STRING},
    {"targetShaderNewName", FOFS(targetShaderNewName), F_STRING},

    {NULL}};

typedef struct {
    char* name;
    void (*spawn)(gentity_t* ent);
} spawn_t;

void SP_info_player_start(gentity_t* ent);
void SP_info_player_deathmatch(gentity_t* ent);
void SP_info_player_intermission(gentity_t* ent);

void SP_func_plat(gentity_t* ent);
void SP_func_static(gentity_t* ent);
void SP_func_rotating(gentity_t* ent);
void SP_func_bobbing(gentity_t* ent);
void SP_func_pendulum(gentity_t* ent);
void SP_func_button(gentity_t* ent);
void SP_func_door(gentity_t* ent);
void SP_func_train(gentity_t* ent);
void SP_func_timer(gentity_t* self);

void SP_trigger_always(gentity_t* ent);
void SP_trigger_multiple(gentity_t* ent);
void SP_trigger_push(gentity_t* ent);
void SP_trigger_teleport(gentity_t* ent);
void SP_trigger_hurt(gentity_t* ent);

void SP_target_remove_powerups(gentity_t* ent);
void SP_target_give(gentity_t* ent);
void SP_target_delay(gentity_t* ent);
void SP_target_speaker(gentity_t* ent);
void SP_target_print(gentity_t* ent);
void SP_target_laser(gentity_t* self);
void SP_target_score(gentity_t* ent);
void SP_target_teleporter(gentity_t* ent);
void SP_target_relay(gentity_t* ent);
void SP_target_cvar(gentity_t* ent);
void SP_target_achievement(gentity_t* ent);
void SP_target_kill(gentity_t* ent);
void SP_target_position(gentity_t* ent);
void SP_target_location(gentity_t* ent);
void SP_target_push(gentity_t* ent);

void SP_light(gentity_t* self);
void SP_info_null(gentity_t* self);
void SP_info_notnull(gentity_t* self);
void SP_info_camp(gentity_t* self);
void SP_path_corner(gentity_t* self);

void SP_misc_teleporter_dest(gentity_t* self);
void SP_misc_model(gentity_t* ent);
void SP_misc_portal_camera(gentity_t* ent);
void SP_misc_portal_surface(gentity_t* ent);

void SP_shooter_rocket(gentity_t* ent);
void SP_shooter_plasma(gentity_t* ent);
void SP_shooter_grenade(gentity_t* ent);

void SP_team_CTF_redplayer(gentity_t* ent);
void SP_team_CTF_blueplayer(gentity_t* ent);

void SP_team_CTF_redspawn(gentity_t* ent);
void SP_team_CTF_bluespawn(gentity_t* ent);

void SP_team_blueobelisk(gentity_t* ent);
void SP_team_redobelisk(gentity_t* ent);
void SP_team_neutralobelisk(gentity_t* ent);

void SP_item_botroam(gentity_t* ent) {}

// [QL] QL-specific entity types
void SP_advertisement(gentity_t *ent) {
    // [QL] In real QL, the engine's ADVERT subsystem handles ad rendering.
    // We render the brush model as a static mover entity instead.
    trap_SetBrushModel(ent, ent->model);
    if (!ent->s.modelindex) {
        G_FreeEntity(ent);
        return;
    }
    ent->s.eType = ET_MOVER;
    ent->r.svFlags = SVF_USE_CURRENT_ORIGIN;
    ent->s.pos.trType = TR_STATIONARY;
    VectorCopy(ent->s.origin, ent->s.pos.trBase);
    VectorCopy(ent->s.origin, ent->r.currentOrigin);
    trap_LinkEntity(ent);
}

// defined in g_gametype_dom.c / g_gametype_race.c
void SP_team_dom_point(gentity_t *ent);
void SP_race_point(gentity_t *ent);

spawn_t spawns[] = {
    // info entities don't do anything at all, but provide positional
    // information for things controlled by other processes
    {"info_player_start", SP_info_player_start},
    {"info_player_deathmatch", SP_info_player_deathmatch},
    {"info_player_intermission", SP_info_player_intermission},
    {"info_null", SP_info_null},
    {"info_notnull", SP_info_notnull},  // use target_position instead
    {"info_camp", SP_info_camp},

    {"func_plat", SP_func_plat},
    {"func_button", SP_func_button},
    {"func_door", SP_func_door},
    {"func_static", SP_func_static},
    {"func_rotating", SP_func_rotating},
    {"func_bobbing", SP_func_bobbing},
    {"func_pendulum", SP_func_pendulum},
    {"func_train", SP_func_train},
    {"func_group", SP_info_null},
    {"func_timer", SP_func_timer},  // rename trigger_timer?

    // Triggers are brush objects that cause an effect when contacted
    // by a living player, usually involving firing targets.
    // While almost everything could be done with
    // a single trigger class and different targets, triggered effects
    // could not be client side predicted (push and teleport).
    {"trigger_always", SP_trigger_always},
    {"trigger_multiple", SP_trigger_multiple},
    {"trigger_push", SP_trigger_push},
    {"trigger_teleport", SP_trigger_teleport},
    {"trigger_hurt", SP_trigger_hurt},

    // targets perform no action by themselves, but must be triggered
    // by another entity
    {"target_give", SP_target_give},
    {"target_remove_powerups", SP_target_remove_powerups},
    {"target_delay", SP_target_delay},
    {"target_speaker", SP_target_speaker},
    {"target_print", SP_target_print},
    {"target_laser", SP_target_laser},
    {"target_score", SP_target_score},
    {"target_teleporter", SP_target_teleporter},
    {"target_relay", SP_target_relay},
    {"target_cvar", SP_target_cvar},
    {"target_achievement", SP_target_achievement},
    {"target_kill", SP_target_kill},
    {"target_position", SP_target_position},
    {"target_location", SP_target_location},
    {"target_push", SP_target_push},

    {"light", SP_light},
    {"path_corner", SP_path_corner},

    {"misc_teleporter_dest", SP_misc_teleporter_dest},
    {"misc_model", SP_misc_model},
    {"misc_portal_surface", SP_misc_portal_surface},
    {"misc_portal_camera", SP_misc_portal_camera},

    {"shooter_rocket", SP_shooter_rocket},
    {"shooter_grenade", SP_shooter_grenade},
    {"shooter_plasma", SP_shooter_plasma},

    {"team_CTF_redplayer", SP_team_CTF_redplayer},
    {"team_CTF_blueplayer", SP_team_CTF_blueplayer},

    {"team_CTF_redspawn", SP_team_CTF_redspawn},
    {"team_CTF_bluespawn", SP_team_CTF_bluespawn},

    {"team_redobelisk", SP_team_redobelisk},
    {"team_blueobelisk", SP_team_blueobelisk},
    {"team_neutralobelisk", SP_team_neutralobelisk},

    {"item_botroam", SP_item_botroam},

    // [QL] QL-specific entity types
    {"advertisement", SP_advertisement},
    {"team_dom_point", SP_team_dom_point},
    {"race_point", SP_race_point},

    // [QL] The binary spawns[] table (0x10080560, 58 entries) also lists:
    //   {"trigger_capturezone", SP_trigger_capturezone}   (func 0x1006be60)
    //   {"info_tour_point",      SP_info_tour_point}        (func 0x1005a260)
    // Neither spawn function exists in this tree yet, so the entries are omitted.
    // Wire them up once SP_trigger_capturezone / SP_info_tour_point exist.

    {NULL, 0}};

/*
===============
G_CallSpawn

Finds the spawn function for the entity and calls it,
returning qfalse if not found
===============
*/
qboolean G_CallSpawn(gentity_t* ent) {
    spawn_t* s;
    gitem_t* item;

    if (!ent->classname) {
        G_Printf("G_CallSpawn: NULL classname\n");
        return qfalse;
    }

    // check item spawn functions
    for (item = bg_itemlist + 1; item->classname; item++) {
        if (!strcmp(item->classname, ent->classname)) {
            G_SpawnItem(ent, item);
            return qtrue;
        }
    }

    // check normal spawn functions
    for (s = spawns; s->name; s++) {
        if (!strcmp(s->name, ent->classname)) {
            // found it
            s->spawn(ent);
            return qtrue;
        }
    }
    G_Printf("%s doesn't have a spawn function\n", ent->classname);
    return qfalse;
}

/*
=============
G_NewString

Builds a copy of the string, translating \n to real linefeeds
so message texts can be multi-line
=============
*/
char* G_NewString(const char* string) {
    char *newb, *new_p;
    int i, l;

    l = strlen(string) + 1;

    newb = G_Alloc(l);

    new_p = newb;

    // turn \n into a real linefeed
    for (i = 0; i < l; i++) {
        if (string[i] == '\\' && i < l - 1) {
            i++;
            if (string[i] == 'n') {
                *new_p++ = '\n';
            } else {
                *new_p++ = '\\';
            }
        } else {
            *new_p++ = string[i];
        }
    }

    return newb;
}

/*
===============
G_ParseField

Takes a key/value pair and sets the binary values
in a gentity
===============
*/
void G_ParseField(const char* key, const char* value, gentity_t* ent) {
    field_t* f;
    byte* b;
    float v;
    vec3_t vec;

    for (f = fields; f->name; f++) {
        if (!Q_stricmp(f->name, key)) {
            // found it
            b = (byte*)ent;

            switch (f->type) {
                case F_STRING:
                    *(char**)(b + f->ofs) = G_NewString(value);
                    break;
                case F_VECTOR:
                    sscanf(value, "%f %f %f", &vec[0], &vec[1], &vec[2]);
                    ((float*)(b + f->ofs))[0] = vec[0];
                    ((float*)(b + f->ofs))[1] = vec[1];
                    ((float*)(b + f->ofs))[2] = vec[2];
                    break;
                case F_INT:
                    *(int*)(b + f->ofs) = atoi(value);
                    break;
                case F_FLOAT:
                    *(float*)(b + f->ofs) = atof(value);
                    break;
                case F_ANGLEHACK:
                    v = atof(value);
                    ((float*)(b + f->ofs))[0] = 0;
                    ((float*)(b + f->ofs))[1] = v;
                    ((float*)(b + f->ofs))[2] = 0;
                    break;
                case F_IGNORE:  // [QL] "light" key: parsed but discarded (binary type 9)
                    break;
                default:
                    break;
            }
            return;
        }
    }
}

/*
====================
G_ItemValidForGametype

[QL] binary 0x10065f90: walks bg_itemlist for classname and returns
(item->validGametypes & (1 << g_gametype)) != 0. If classname is not a
bg_itemlist entry, GT_AD (gametype 11) also treats the objective
obelisks team_redobelisk/team_blueobelisk (which spawn as entities, not items) as
valid. Only team_CTF_redflag and team_CTF_blueflag carry a non-zero validGametypes
mask in the binary (both 1<<GT_AD); every other item is 0.
====================
*/
static qboolean G_ItemValidForGametype(const char* classname) {
    const gitem_t* item;
    int gt;

    if (!classname || !classname[0]) {
        return qfalse;
    }

    gt = g_gametype.integer;

    // an item whose validGametypes mask has the current gametype's bit set is
    // valid and bypasses the notteam/notfree/gametype spawn filters.
    for (item = bg_itemlist + 1; item->classname; item++) {
        if (!strcmp(item->classname, classname)) {
            return (item->validGametypes & (1 << (gt & 0x1f))) != 0;
        }
    }

    // classname is not a bg_itemlist entry: in Attack & Defend the objective
    // obelisks are valid.
    if (gt != GT_AD) {
        return qfalse;
    }
    if (!strcmp(classname, "team_redobelisk") || !strcmp(classname, "team_blueobelisk")) {
        return qtrue;
    }
    return qfalse;
}

/*
====================
G_GametypeInList

[QL] Returns qtrue if the current gametype's short name is a whitespace
separated token in list. The binary parses the list into a bitmask (bit
1<<(gametype+1) per matched name) and tests the current gametype's bit, which
reduces to an exact tokenised match on gametypeShortNames[g_gametype].
====================
*/
static qboolean G_GametypeInList(const char* list) {
    const char* name;
    const char* p;
    int len;

    if (g_gametype.integer < GT_FFA || g_gametype.integer >= GT_MAX_GAME_TYPE) {
        return qfalse;
    }
    name = gametypeShortNames[g_gametype.integer];
    len = strlen(name);

    for (p = list; *p; ) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!Q_stricmpn(p, name, len) && (p[len] == '\0' || p[len] == ' ' || p[len] == '\t')) {
            return qtrue;
        }
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return qfalse;
}

/*
===================
G_SpawnGEntityFromSpawnVars

Spawn an entity and fill in all of the level fields from
level.spawnVars[], then call the class specific spawn function
===================
*/
void G_SpawnGEntityFromSpawnVars(void) {
    int i;
    gentity_t* ent;
    char* value;
    qboolean itemValid;

    // get the next free entity
    ent = G_Spawn();

    for (i = 0; i < level.numSpawnVars; i++) {
        G_ParseField(level.spawnVars[i][0], level.spawnVars[i][1], ent);
    }

    // [QL] gametype-specific items spawn regardless, bypassing every filter below.
    itemValid = G_ItemValidForGametype(ent->classname);

    // [QL] binary-verified entity filtering (from qagamex86.dll 0x10066080):
    // 1. notteam/notfree filter based on gametype category
    // 2. "gametype" positive filter - spawn only if current gametype is listed
    // 3. "not_gametype" negative filter - suppress if current gametype is listed
    // The item-valid override skips all of these. No notsingle/notta in QL, and
    // the binary does NOT touch areaportals on a filtered-out entity.
    if (g_gametype.integer >= GT_TEAM) {
        G_SpawnInt("notteam", "0", &i);
        if (i && !itemValid) {
            G_FreeEntity(ent);
            return;
        }
    } else {
        G_SpawnInt("notfree", "0", &i);
        if (i && !itemValid) {
            G_FreeEntity(ent);
            return;
        }
    }

    if (G_SpawnString("gametype", "", &value)) {
        if (!G_GametypeInList(value) && !itemValid) {
            G_FreeEntity(ent);
            return;
        }
    }

    if (G_SpawnString("not_gametype", "", &value)) {
        if (G_GametypeInList(value) && !itemValid) {
            G_FreeEntity(ent);
            return;
        }
    }

    // move editor origin to pos
    VectorCopy(ent->s.origin, ent->s.pos.trBase);
    VectorCopy(ent->s.origin, ent->r.currentOrigin);

    // if we didn't get a classname, don't bother spawning anything
    if (!G_CallSpawn(ent)) {
        G_FreeEntity(ent);
    }
}

/*
====================
G_AddSpawnVarToken
====================
*/
char* G_AddSpawnVarToken(const char* string) {
    int l;
    char* dest;

    l = strlen(string);
    if (level.numSpawnVarChars + l + 1 > MAX_SPAWN_VARS_CHARS) {
        G_Error("G_AddSpawnVarToken: MAX_SPAWN_CHARS");  // [QL] binary string is "MAX_SPAWN_CHARS"
    }

    dest = level.spawnVarChars + level.numSpawnVarChars;
    memcpy(dest, string, l + 1);

    level.numSpawnVarChars += l + 1;

    return dest;
}

/*
====================
G_ParseSpawnVars

Parses a brace bounded set of key / value pairs out of the
level's entity strings into level.spawnVars[]

This does not actually spawn an entity.
====================
*/
qboolean G_ParseSpawnVars(void) {
    char keyname[MAX_TOKEN_CHARS];
    char com_token[MAX_TOKEN_CHARS];

    level.numSpawnVars = 0;
    level.numSpawnVarChars = 0;

    // parse the opening brace
    if (!trap_GetEntityToken(com_token, sizeof(com_token))) {
        // end of spawn string
        return qfalse;
    }
    if (com_token[0] != '{') {
        G_Error("G_ParseSpawnVars: found %s when expecting {", com_token);
    }

    // go through all the key / value pairs
    while (1) {
        // parse key
        if (!trap_GetEntityToken(keyname, sizeof(keyname))) {
            G_Error("G_ParseSpawnVars: EOF without closing brace");
        }

        if (keyname[0] == '}') {
            break;
        }

        // parse value
        if (!trap_GetEntityToken(com_token, sizeof(com_token))) {
            G_Error("G_ParseSpawnVars: EOF without closing brace");
        }

        if (com_token[0] == '}') {
            G_Error("G_ParseSpawnVars: closing brace without data");
        }
        if (level.numSpawnVars == MAX_SPAWN_VARS) {
            G_Error("G_ParseSpawnVars: MAX_SPAWN_VARS");
        }
        level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken(keyname);
        level.spawnVars[level.numSpawnVars][1] = G_AddSpawnVarToken(com_token);
        level.numSpawnVars++;
    }

    return qtrue;
}

/*QUAKED worldspawn (0 0 0) ?

Every map should have exactly one worldspawn.
"music"		music wav file
"gravity"	800 is default gravity
"message"	Text to print during connection process
*/
void SP_worldspawn(void) {
    char* s;

    G_SpawnString("classname", "", &s);
    if (Q_stricmp(s, "worldspawn")) {
        G_Error("SP_worldspawn: The first entity isn't 'worldspawn'");
    }

    // make some data visible to connecting client
    trap_SetConfigstring(CS_GAME_VERSION, GAME_VERSION);

    trap_SetConfigstring(CS_LEVEL_START_TIME, va("%i", level.startTime));

    G_SpawnString("music", "", &s);
    trap_SetConfigstring(CS_MUSIC, s);

    G_SpawnString("message", "", &s);
    trap_SetConfigstring(CS_MESSAGE, s);  // map specific message

    // [QL] author configstrings read from worldspawn entity
    G_SpawnString("author", "", &s);
    trap_SetConfigstring(CS_AUTHOR, s);

    G_SpawnString("author2", "", &s);
    trap_SetConfigstring(CS_AUTHOR2, s);

    // [QL] disable_loadout worldspawn key. Only touched when g_loadout is on;
    // the binary never clears the cvar/configstring here. The value is a
    // whitespace separated list of weapon short names, parsed by
    // BG_ParseWeaponStringtoFlag (0x1002da40) into a weapon bitmask.
    if (g_loadout.integer) {
        int mask;
        G_SpawnString("disable_loadout", "", &s);
        mask = BG_ParseWeaponStringtoFlag(s);
        trap_SetConfigstring(CS_DISABLE_LOADOUT, va("%i", mask));
        trap_Cvar_Set("g_disableLoadout", va("%i", mask));
    }

    trap_SetConfigstring(CS_MOTD, g_motd.string);  // message of the day

    // [QL] gravity is only overridden when the map sets it
    if (G_SpawnString("gravity", "800", &s)) {
        trap_Cvar_Set("g_gravity", s);
    }

    G_SpawnString("enableDust", "0", &s);
    trap_Cvar_Set("g_enableDust", s);

    // [QL] enableBreath is published to the client as a configstring
    G_SpawnString("enableBreath", "0", &s);
    trap_SetConfigstring(CS_ENABLEBREATH, s);

    // [QL] trainingMap worldspawn key forces g_training on
    G_SpawnString("trainingMap", "0", &s);
    if (atoi(s) == 1) {
        trap_Cvar_Set("g_training", "2");
    }
    // [QL] The binary (0x1006672d) also stores a separate flag from this same
    // "trainingMap" value: level.isTraining = (strcmp(value, "0") != 0), i.e.
    // set for ANY non-"0" value (not only "1"; g_training is only forced
    // when the value is exactly "1"). That global (Ghidra 0x105dea44) has no
    // field in this tree's level_locals_t; other code approximates it with
    // g_training.integer. REPORTED: add level.isTraining and set it here.

    // [QL] "atmosphere" key: the binary (0x1006671e) publishes it to a
    // configstring - if present, the map's value; if absent, "" - using CS
    // index 0x2b3 (691). This tree's CS_ATMOSEFFECT is 689, which disagrees
    // with the binary, so the publish is omitted until the header is fixed.
    // REPORTED: set CS_ATMOSEFFECT to 691 in bg_public.h, then:
    //   if (G_SpawnString("atmosphere", "", &s)) trap_SetConfigstring(CS_ATMOSEFFECT, s);
    //   else trap_SetConfigstring(CS_ATMOSEFFECT, "");

    g_entities[ENTITYNUM_WORLD].s.number = ENTITYNUM_WORLD;
    g_entities[ENTITYNUM_WORLD].r.ownerNum = ENTITYNUM_NONE;
    g_entities[ENTITYNUM_WORLD].classname = "worldspawn";

    g_entities[ENTITYNUM_NONE].s.number = ENTITYNUM_NONE;
    g_entities[ENTITYNUM_NONE].r.ownerNum = ENTITYNUM_NONE;
    g_entities[ENTITYNUM_NONE].classname = "nothing";

    // [QL] QL boots into PRE_GAME. Only a restart drops straight into
    // IN_PROGRESS, everything else stays in warmup.
    SetWarmupState(-1);
    if (g_restarted.integer) {
        trap_Cvar_Set("g_restarted", "0");
        SetWarmupState(0);
    } else if (g_doWarmup.integer) {
        SetWarmupState(-1);
        G_LogPrintf("Warmup:\n");   // [QL] binary 0x10057480 logs to the game log file, not console
    }

    // [QL] The binary (0x1006687b..0x1006691e) finishes SP_worldspawn by seeding
    // two per-level randomised spawn delays, each = base + frac(rand) * range
    // where frac(rand) = (rand() & 0x7fff) / 32767.0 and negative cvar values
    // clamp to 0. These write two level globals (Ghidra 0x105dea10, 0x105dea14;
    // the first is read by FinishSpawningItem). The base/range operands are cvar
    // values (likely g_spawnDelay_key/g_spawnDelayRandom_key and
    // g_spawnDelay_powerup/g_spawnDelayRandom_powerup). REPORTED: needs two new
    // level_locals_t fields + FinishSpawningItem wiring; omitted here.
}

/*
==============
G_SpawnEntitiesFromString

Parses textual entity definitions out of an entstring and spawns gentities.
==============
*/
void G_SpawnEntitiesFromString(void) {
    // allow calls to G_Spawn*()
    level.spawning = qtrue;
    level.numSpawnVars = 0;

    // the worldspawn is not an actual entity, but it still
    // has a "spawn" function to perform any global setup
    // needed by a level (setting configstrings or cvars, etc)
    if (!G_ParseSpawnVars()) {
        G_Error("SpawnEntities: no entities");
    }
    SP_worldspawn();

    // parse ents
    while (G_ParseSpawnVars()) {
        G_SpawnGEntityFromSpawnVars();
    }

    level.spawning = qfalse;  // any future calls to G_Spawn*() will be errors
}
