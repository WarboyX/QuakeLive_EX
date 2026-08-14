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

// this file holds commands that can be executed by the server console, but not remote clients

#include "g_local.h"

/*
==============================================================================

PACKET FILTERING


You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and you can use '*' to match any value
so you can specify an entire class C network with "addip 192.246.40.*"

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

g_filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.

TTimo NOTE: for persistence, bans are stored in g_banIPs cvar MAX_CVAR_VALUE_STRING
The size of the cvar string buffer is limiting the banning to around 20 masks
this could be improved by putting some g_banIPs2 g_banIps3 etc. maybe
still, you should rely on PB for banning instead

==============================================================================
*/

typedef struct ipFilter_s {
    unsigned mask;
    unsigned compare;
} ipFilter_t;

#define MAX_IPFILTERS 1024

static ipFilter_t ipFilters[MAX_IPFILTERS];
static int numIPFilters;

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter(char* s, ipFilter_t* f) {
    char num[128];
    int i, j;
    byte b[4];
    byte m[4];

    for (i = 0; i < 4; i++) {
        b[i] = 0;
        m[i] = 0;
    }

    for (i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') {
            if (*s == '*')  // 'match any'
            {
                // b[i] and m[i] to 0
                s++;
                if (!*s)
                    break;
                s++;
                continue;
            }
            G_Printf("Bad filter address: %s\n", s);
            return qfalse;
        }

        j = 0;
        while (*s >= '0' && *s <= '9') {
            num[j++] = *s++;
        }
        num[j] = 0;
        b[i] = atoi(num);
        m[i] = 255;

        if (!*s)
            break;
        s++;
    }

    f->mask = *(unsigned*)m;
    f->compare = *(unsigned*)b;

    return qtrue;
}

/*
=================
UpdateIPBans
=================
*/
static void UpdateIPBans(void) {
    byte b[4] = {0};
    byte m[4] = {0};
    int i, j;
    char iplist_final[MAX_CVAR_VALUE_STRING] = {0};
    char ip[64] = {0};

    *iplist_final = 0;
    for (i = 0; i < numIPFilters; i++) {
        if (ipFilters[i].compare == 0xffffffff)
            continue;

        *(unsigned*)b = ipFilters[i].compare;
        *(unsigned*)m = ipFilters[i].mask;
        *ip = 0;
        for (j = 0; j < 4; j++) {
            if (m[j] != 255)
                Q_strcat(ip, sizeof(ip), "*");
            else
                Q_strcat(ip, sizeof(ip), va("%i", b[j]));
            Q_strcat(ip, sizeof(ip), (j < 3) ? "." : " ");
        }
        if (strlen(iplist_final) + strlen(ip) < MAX_CVAR_VALUE_STRING) {
            Q_strcat(iplist_final, sizeof(iplist_final), ip);
        } else {
            Com_Printf("g_banIPs overflowed at MAX_CVAR_VALUE_STRING\n");
            break;
        }
    }

    trap_Cvar_Set("g_banIPs", iplist_final);
}

/*
=================
G_FilterPacket
=================
*/
qboolean G_FilterPacket(char* from) {
    int i;
    unsigned in;
    byte m[4] = {0};
    char* p;

    i = 0;
    p = from;
    while (*p && i < 4) {
        m[i] = 0;
        while (*p >= '0' && *p <= '9') {
            m[i] = m[i] * 10 + (*p - '0');
            p++;
        }
        if (!*p || *p == ':')
            break;
        i++, p++;
    }

    in = *(unsigned*)m;

    for (i = 0; i < numIPFilters; i++)
        if ((in & ipFilters[i].mask) == ipFilters[i].compare)
            return g_filterBan.integer != 0;

    return g_filterBan.integer == 0;
}

/*
=================
AddIP
=================
*/
static void AddIP(char* str) {
    int i;

    for (i = 0; i < numIPFilters; i++)
        if (ipFilters[i].compare == 0xffffffff)
            break;  // free spot
    if (i == numIPFilters) {
        if (numIPFilters == MAX_IPFILTERS) {
            G_Printf("IP filter list is full\n");
            return;
        }
        numIPFilters++;
    }

    if (!StringToFilter(str, &ipFilters[i]))
        ipFilters[i].compare = 0xffffffffu;

    UpdateIPBans();
}

/*
=================
G_ProcessIPBans
=================
*/
void G_ProcessIPBans(void) {
    char *s, *t;
    char str[MAX_CVAR_VALUE_STRING];

    Q_strncpyz(str, g_banIPs.string, sizeof(str));

    for (t = s = g_banIPs.string; *t; /* */) {
        s = strchr(s, ' ');
        if (!s)
            break;
        while (*s == ' ')
            *s++ = 0;
        if (*t)
            AddIP(t);
        t = s;
    }
}

/*
=================
Svcmd_AddIP_f
=================
*/
void Svcmd_AddIP_f(void) {
    char str[MAX_TOKEN_CHARS];

    if (trap_Argc() < 2) {
        G_Printf("Usage: addip <ip-mask>\n");
        return;
    }

    trap_Argv(1, str, sizeof(str));

    AddIP(str);
}

/*
=================
Svcmd_RemoveIP_f
=================
*/
void Svcmd_RemoveIP_f(void) {
    ipFilter_t f;
    int i;
    char str[MAX_TOKEN_CHARS];

    if (trap_Argc() < 2) {
        G_Printf("Usage: removeip <ip-mask>\n");
        return;
    }

    trap_Argv(1, str, sizeof(str));

    if (!StringToFilter(str, &f))
        return;

    for (i = 0; i < numIPFilters; i++) {
        if (ipFilters[i].mask == f.mask &&
            ipFilters[i].compare == f.compare) {
            ipFilters[i].compare = 0xffffffffu;
            G_Printf("Removed.\n");

            UpdateIPBans();
            return;
        }
    }

    G_Printf("Didn't find %s.\n", str);
}

/*
===================
Svcmd_EntityList_f
// Address: 0x10066920
===================
*/
void Svcmd_EntityList_f(void) {
    // [QL] entity-type names indexed by s.eType (0..14). Verified byte-for-byte
    // against the qagamex86.dll pointer/string table at 0x1008d7ec (each entry
    // padded to 20 columns). QL's ET ordering differs from Q3: index 12 is
    // ET_TEAM, 13 is ET_EVENT, 14 is ET_DEBUG_BOX.
    static const char* const etypeNames[15] = {
        "ET_GENERAL          ",
        "ET_PLAYER           ",
        "ET_ITEM             ",
        "ET_MISSILE          ",
        "ET_MOVER            ",
        "ET_BEAM             ",
        "ET_PORTAL           ",
        "ET_SPEAKER          ",
        "ET_PUSH_TRIGGER     ",
        "ET_TELEPORT_TRIGGER ",
        "ET_INVISIBLE        ",
        "ET_GRAPPLE          ",
        "ET_TEAM             ",
        "ET_EVENT            ",
        "ET_DEBUG_BOX        ",
    };
    int e;
    gentity_t* check;

    check = g_entities;
    for (e = 0; e < level.num_entities; e++, check++) {
        if (!check->inuse) {
            continue;
        }
        if (check->s.eType < 0 || check->s.eType > 14) {
            G_Printf("^1%3i:ET_UNKNOWN %i", e, check->s.eType);
        } else {
            G_Printf("%3i:%s", e, etypeNames[check->s.eType]);
        }

        if (check->classname) {
            G_Printf("%s", check->classname);
        }
        G_Printf("\n");
    }
}

gclient_t* ClientForString(const char* s) {
    gclient_t* cl;
    int i;
    int idnum;

    // numeric values are just slot numbers
    if (s[0] >= '0' && s[0] <= '9') {
        idnum = atoi(s);
        if (idnum < 0 || idnum >= level.maxclients) {
            Com_Printf("Bad client slot: %i\n", idnum);
            return NULL;
        }

        cl = &level.clients[idnum];
        if (cl->pers.connected == CON_DISCONNECTED) {
            G_Printf("Client %i is not connected\n", idnum);
            return NULL;
        }
        return cl;
    }

    // check for a name match
    for (i = 0; i < level.maxclients; i++) {
        cl = &level.clients[i];
        if (cl->pers.connected == CON_DISCONNECTED) {
            continue;
        }
        if (!Q_stricmp(cl->pers.netname, s)) {
            return cl;
        }
    }

    G_Printf("User %s is not on the server\n", s);

    return NULL;
}

/*
===================
Svcmd_ForceTeam_f

forceteam <player> <team>
===================
*/
void Svcmd_ForceTeam_f(void) {
    gclient_t* cl;
    char str[MAX_TOKEN_CHARS];

    // [QL] The binary (0x10066b50) has no argc guard: it reads arg 1, resolves
    // the client (printing its own error on failure), then reads arg 2.
    // find the player
    trap_Argv(1, str, sizeof(str));
    cl = ClientForString(str);
    if (!cl) {
        return;
    }

    // set the team
    trap_Argv(2, str, sizeof(str));
    SetTeam(&g_entities[cl - level.clients], str);
}

/*
===================
Svcmd_ForceShuffle_f
// Address: 0x1006a2b0

Shuffle teams randomly. Used by "forceshuffle" console command
and by RS_SHUFFLE state in Red Rover.

[QL] NOT byte-faithful yet - the binary algorithm is:
  1. startTeam = (rand()&1) ? TEAM_BLUE : TEAM_RED
  2. level.scoringDisabled = qtrue  (NOT teamShuffleActive); reset two score
     accumulators (globals 0x105dce84 / 0x105dce88, unidentified).
  3. If level.numPlayingClients < <threshold @0x105973ac> and gametype != GT_RR:
     print "Unable to shuffle: Not enough players.\n" and stop.
  4. Collect connected red/blue clients into players[].
  5. If warmupTime != 0 || gametype != GT_RR: print "Shuffling Teams.\n"
  6. qsort(players, count, 4, <random comparator @0x1006a290>)
  7. Alternate team per player; for the last player of an odd count, flip once.
     If already on the target team AND gametype==GT_RR: ClientSpawn + SelectSpawnWeapon
     (and ps.eFlags|=4 when roundState.eCurrent==1); otherwise
     SetTeam_Execute(ent, team, qfalse, qfalse) and count a move. Each shuffled
     ent gets ent-> (field @0x4f8) = level.time + 5000 (team-switch cooldown).
  8. If any moved: CalculateRanks(); else print "No players shuffled.\n"
  9. level.scoringDisabled = qfalse
BLOCKED: SetTeam_Execute is not declared in the reimpl (only a comment references
it in g_gametype_duel.c), and the two reset globals / the 0x4f8 gentity field /
the "No players shuffled" path are unmapped. Left as the existing approximation
to avoid an uncompilable reference; see report.
===================
*/
void Svcmd_ForceShuffle_f(void) {
    int i, count = 0;
    int players[MAX_CLIENTS];
    int team = (rand() & 1) ? TEAM_RED : TEAM_BLUE;

    level.teamShuffleActive = qtrue;

    for (i = 0; i < level.maxclients; i++) {
        if (g_clients[i].pers.connected != CON_CONNECTED) continue;
        if (g_clients[i].sess.sessionTeam != TEAM_RED &&
            g_clients[i].sess.sessionTeam != TEAM_BLUE) continue;
        players[count++] = i;
    }

    if (count < level.numNonSpectatorClients) {
        G_Printf("Unable to shuffle: Not enough players.\n");
    } else {
        G_Printf("Shuffling Teams.\n");
        // Simple alternating assignment
        for (i = 0; i < count; i++) {
            gentity_t* target = &g_entities[players[i]];
            if (target->client->sess.sessionTeam != team) {
                SetTeam(target, (team == TEAM_RED) ? "red" : "blue");
            }
            team = (team == TEAM_RED) ? TEAM_BLUE : TEAM_RED;
        }
    }

    level.teamShuffleActive = qfalse;
}

char* ConcatArgs(int start);

/*
==============================================================================

[QL] STEAM-ID ACCESS CONTROL LIST  (g_access.cpp in the QL source)

QL replaced Q3's IP/packet ban filter with a Steam-ID keyed access list, loaded
from the file named by the "g_accessFile" cvar (default "access.txt"). Each line
is "<steamId>|<keyword>", keyword in { "ban" -> -1, "mod" -> 1, "admin" -> 2 };
lines starting with '#' are comments.

The binary keeps this as a std::map<uint64_t, access_s> (an RB-tree). Here it is
a flat keyed array, which is behaviourally equivalent for lookup/insert/level.

Verified against qagamex86.dll:
  G_InitAccessList    0x100325f0  (loader; .so symbol G_InitAccessList)
  G_GetAccess         0x10032520  (per-client lookup; .so symbol G_GetAccess)
  G_ReloadAccessList  0x10032830  (reload + reapply; .so symbol G_ReloadAccessList)

NOTE (coordination / see report): these three should live in a dedicated
g_access.c. They are declared in g_local.h and are consumed by ClientConnect()
(the connect-time ban check) and G_InitSessionData() (privilege seeding); they
are defined here because Svcmd/ConsoleCommand's "reload_access" was originally
the only in-scope caller. The mis-typed g_client.c:G_GetAccessLevel(const char*)
stub that used to shadow this lookup has been removed - the binary's function is
G_GetAccess(int clientNum) (below), and that is what callers now use.
==============================================================================
*/

// [QL] access-list API (belongs in g_local.h once promoted out of this file)
void G_InitAccessList(void);
int  G_GetAccess(int clientNum);
void G_ReloadAccessList(void);

typedef struct {
    uint64_t steamId;   // key
    int      level;     // -1 banned, 0 none, 1 mod, 2 admin
    int      flag;      // access_s second field (Cmd_KickBan sets 1 for "kick", 0 for "ban")
} accessEntry_t;

#define MAX_ACCESS_ENTRIES 8192

static accessEntry_t g_accessList[MAX_ACCESS_ENTRIES];
static int           g_numAccessEntries;

// find-or-create the entry for a steamId (mirrors std::map::operator[])
static accessEntry_t* AccessList_Get(uint64_t steamId) {
    int i;
    for (i = 0; i < g_numAccessEntries; i++) {
        if (g_accessList[i].steamId == steamId) {
            return &g_accessList[i];
        }
    }
    if (g_numAccessEntries >= MAX_ACCESS_ENTRIES) {
        return &g_accessList[MAX_ACCESS_ENTRIES - 1];  // overflow: reuse last slot
    }
    g_accessList[g_numAccessEntries].steamId = steamId;
    g_accessList[g_numAccessEntries].level = 0;
    g_accessList[g_numAccessEntries].flag = 0;
    return &g_accessList[g_numAccessEntries++];
}

/*
=================
G_InitAccessList
// Address: 0x100325f0
=================
*/
void G_InitAccessList(void) {
    fileHandle_t f;
    int len;
    char buf[16384];
    char* line;

    len = trap_FS_FOpenFile(g_accessFile.string, &f, FS_READ);
    G_Printf("initializing access list...\n");
    if (!f) {
        G_Printf("^1file not found: %s\n", g_accessFile.string);
        return;
    }

    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1;  // [QL] binary malloc()s exactly; we cap to a fixed buffer
    }
    trap_FS_Read(buf, len, f);
    buf[len] = '\0';
    trap_FS_FCloseFile(f);

    g_numAccessEntries = 0;   // AccessList_RbTree_Clear()

    line = strtok(buf, "\n");
    while (line) {
        if (*line != '#') {
            char* cr;
            // %llu has to be fed an unsigned long long; uint64_t is a plain
            // unsigned long on LP64, so scan into a matching temporary rather
            // than let sscanf write through a mismatched pointer type.
            unsigned long long scannedId;
            uint64_t steamId;
            char word[64];

            cr = strchr(line, '\r');
            if (cr) {
                *cr = '\0';
            }
            // [QL] binary G_InitAccessList uses unbounded "%llu|%s"; %63s (word[64]) is a
            // deliberate defensive cap to avoid a stack overflow on a malformed access file.
            // Identical parse for every valid keyword (ban/mod/admin).
            if (sscanf(line, "%llu|%63s", &scannedId, word) != 2) {
                G_Printf("^1invalid admin access format, skipping: %s\n", line);
                line = strtok(NULL, "\n");
                continue;
            }

            steamId = (uint64_t)scannedId;

            if (!strcmp(word, "ban")) {
                AccessList_Get(steamId)->level = -1;
            } else if (!strcmp(word, "mod")) {
                AccessList_Get(steamId)->level = 1;
            } else if (!strcmp(word, "admin")) {
                AccessList_Get(steamId)->level = 2;
            }
            // unknown keyword: ignored, no entry created (matches binary)
        }
        line = strtok(NULL, "\n");
    }

    G_Printf("loaded %i steam ids into the access list\n", g_numAccessEntries);
}

/*
=================
G_GetAccess
// Address: 0x10032520

Returns the caller's access level: 3 for a localhost connection, otherwise the
level recorded in the access list (0 if not listed, -1 banned, 1 mod, 2 admin).
trap_GetSteamID is the flagged external boundary that supplies the steamId key.
=================
*/
int G_GetAccess(int clientNum) {
    uint64_t steamId;
    char userinfo[MAX_INFO_STRING];
    char* ip;
    int i;

    steamId = trap_GetSteamID(clientNum);
    trap_GetUserinfo(clientNum, userinfo, sizeof(userinfo));
    ip = Info_ValueForKey(userinfo, "ip");
    if (!strcmp(ip, "localhost")) {
        return 3;
    }
    for (i = 0; i < g_numAccessEntries; i++) {
        if (g_accessList[i].steamId == steamId) {
            return g_accessList[i].level;
        }
    }
    return 0;
}

/*
=================
G_ReloadAccessList
// Address: 0x10032830

[QL] "reload_access" console command: reload the access file and re-apply the
stored privilege level to every connected client slot. (The binary iterates
slots [0, level.numConnectedClients) exactly as written here.)
=================
*/
void G_ReloadAccessList(void) {
    int i;

    G_InitAccessList();
    for (i = 0; i < level.numConnectedClients; i++) {
        g_clients[i].sess.privileges = G_GetAccess(i);
    }
}

/*
===================
Svcmd_DumpVars_f
// Address: 0x10044670  (.so symbol: Svcmd_Dumpvars_f)

[QL] "dumpvars" debug dump of a player's playerState. Resolves the target via
ClientForString(arg 1) - NOT a raw slot index.
===================
*/
void Svcmd_DumpVars_f(void) {
    char str[MAX_TOKEN_CHARS];
    gclient_t* cl;

    trap_Argv(1, str, sizeof(str));
    cl = ClientForString(str);
    if (!cl) {
        return;
    }

    G_Printf("Data Dump: (%s)\n", cl->pers.netname);
    G_Printf("clientNum: %d\n", cl->ps.clientNum);
    G_Printf("pm_type: 0x%08x\n", cl->ps.pm_type);
    G_Printf("pm_flags: 0x%08x\n", cl->ps.pm_flags);
    G_Printf("pm_time: %d\n", cl->ps.pm_time);
    G_Printf("eFlags: %d\n", cl->ps.eFlags);
    G_Printf("origin: (%0.3f, %0.3f, %0.3f)\n",
            cl->ps.origin[0],
            cl->ps.origin[1],
            cl->ps.origin[2]);
}

/*
=================
ConsoleCommand

=================
*/
qboolean ConsoleCommand(void) {
    char cmd[MAX_TOKEN_CHARS];

    trap_Argv(0, cmd, sizeof(cmd));

    if (Q_stricmp(cmd, "entitylist") == 0) {
        Svcmd_EntityList_f();
        return qtrue;
    }
    if (Q_stricmp(cmd, "forceteam") == 0) {
        Svcmd_ForceTeam_f();
        return qtrue;
    }
    if (Q_stricmp(cmd, "game_memory") == 0) {
        // binary inlines: G_Printf("Game memory status: %i out of %i bytes
        // allocated\n", allocated, 0x400000). Svcmd_GameMem_f is the .so function.
        Svcmd_GameMem_f();
        return qtrue;
    }
    if (Q_stricmp(cmd, "addbot") == 0) {
        Svcmd_AddBot_f();
        return qtrue;
    }
    if (Q_stricmp(cmd, "botlist") == 0) {
        Svcmd_BotList_f();
        return qtrue;
    }
    // [QL] game_crash - developer-only intentional crash (writes 0x12345678 to NULL)
    if (Q_stricmp(cmd, "game_crash") == 0) {
        if (trap_Cvar_VariableValue("developer") == 1) {
            *(volatile int*)0 = 0x12345678;
        }
        return qtrue;
    }
    if (Q_stricmp(cmd, "forceshuffle") == 0) {
        Svcmd_ForceShuffle_f();
        return qtrue;
    }
    if (Q_stricmp(cmd, "dumpvars") == 0) {
        Svcmd_DumpVars_f();
        return qtrue;
    }
    // [QL] markstate - debug state snapshot, no-op in the release build
    if (Q_stricmp(cmd, "markstate") == 0) {
        return qtrue;
    }
    if (Q_stricmp(cmd, "reload_access") == 0) {
        G_ReloadAccessList();
        return qtrue;
    }
    // [QL] diffstate / dumpentities / printentitystates - debug no-ops in release
    if (Q_stricmp(cmd, "diffstate") == 0) {
        return qtrue;
    }
    if (Q_stricmp(cmd, "dumpentities") == 0) {
        return qtrue;
    }
    if (Q_stricmp(cmd, "printentitystates") == 0) {
        return qtrue;
    }

    // the following are handled only on a dedicated server
    if (g_dedicated.integer) {
        if (Q_stricmp(cmd, "say") == 0) {
            trap_SendServerCommand(-1, va("print \"server: %s\"", ConcatArgs(1)));
        }
        if (Q_stricmp(cmd, "tell") == 0) {
            char slotStr[MAX_TOKEN_CHARS];
            char* msg;
            const char* p;
            int slot;

            trap_Argv(1, slotStr, sizeof(slotStr));
            slot = atoi(slotStr);
            msg = ConcatArgs(2);
            for (p = slotStr; *p; p++) {
                if (*p < '0' || *p > '9') {
                    G_Printf("Bad slot number: %s\n", slotStr);
                    return qtrue;
                }
            }
            if (msg[0] == '\0') {
                return qtrue;
            }
            trap_SendServerCommand(slot, va("print \"server whisper: %s\"", msg));
        }

        // [QL] The binary then runs any remaining console command through the
        // privileged client-command dispatcher (ClientCommandPermissionCheck in
        // g_cmds.c) as the server (level 3, ent == NULL), which is how the
        // dedicated console runs admin commands (kick/addadmin/...). That
        // dispatcher is static to g_cmds.c, so the bridge is a coordination item
        // (see report). The command is consumed either way (returns qtrue),
        // unlike the old Q3 path which broadcast it as a "say".
        return qtrue;
    }

    return qfalse;
}
