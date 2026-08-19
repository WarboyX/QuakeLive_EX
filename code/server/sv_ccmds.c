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

#include "server.h"

/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/

/*
==================
SV_GetPlayerByHandle

Returns the player with player id or name from Cmd_Argv(1)
==================
*/
static client_t* SV_GetPlayerByHandle(void) {
    client_t* cl;
    int i;
    char* s;
    char cleanName[64];

    // make sure server is running
    if (!com_sv_running->integer) {
        return NULL;
    }

    if (Cmd_Argc() < 2) {
        Com_Printf("No player specified.\n");
        return NULL;
    }

    s = Cmd_Argv(1);

    // Check whether this is a numeric player handle
    for (i = 0; s[i] >= '0' && s[i] <= '9'; i++)
        ;

    if (!s[i]) {
        int plid = atoi(s);

        // Check for numeric playerid match
        if (plid >= 0 && plid < sv_maxclients->integer) {
            cl = &svs.clients[plid];

            if (cl->state)
                return cl;
        }
    }

    // check for a name match
    for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
        if (!cl->state) {
            continue;
        }
        if (!Q_stricmp(cl->name, s)) {
            return cl;
        }

        Q_strncpyz(cleanName, cl->name, sizeof(cleanName));
        Q_CleanStr(cleanName);
        if (!Q_stricmp(cleanName, s)) {
            return cl;
        }
    }

    Com_Printf("Player %s is not on the server\n", s);

    return NULL;
}

/*
==================
SV_GetPlayerByNum

Returns the player with idnum from Cmd_Argv(1)
==================
*/
static client_t* SV_GetPlayerByNum(void) {
    client_t* cl;
    int i;
    int idnum;
    char* s;

    // make sure server is running
    if (!com_sv_running->integer) {
        return NULL;
    }

    if (Cmd_Argc() < 2) {
        Com_Printf("No player specified.\n");
        return NULL;
    }

    s = Cmd_Argv(1);

    for (i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') {
            Com_Printf("Bad slot number: %s\n", s);
            return NULL;
        }
    }
    idnum = atoi(s);
    if (idnum < 0 || idnum >= sv_maxclients->integer) {
        Com_Printf("Bad client slot: %i\n", idnum);
        return NULL;
    }

    cl = &svs.clients[idnum];
    if (!cl->state) {
        Com_Printf("Client %i is not active\n", idnum);
        return NULL;
    }
    return cl;
}

//=========================================================

/*
==================
SV_Map_f

Restart the server on a different map
==================
*/
static void SV_Map_f(void) {
    char* cmd;
    char* map;
    qboolean killBots, cheat;
    char expanded[MAX_QPATH];
    char mapname[MAX_QPATH];

    map = Cmd_Argv(1);
    if (!map) {
        return;
    }

    // make sure the level exists before trying to change, so that
    // a typo at the server console won't end the game
    Com_sprintf(expanded, sizeof(expanded), "maps/%s.bsp", map);
    if (FS_ReadFile(expanded, NULL) == -1) {
        Com_Printf("Can't find map %s\n", expanded);
        return;
    }

    // force latched values to get set
    Cvar_Get("g_gametype", "0", CVAR_SERVERINFO | CVAR_USERINFO | CVAR_LATCH);

    cmd = Cmd_Argv(0);
    if (!Q_stricmp(cmd, "devmap")) {
        cheat = qtrue;
        killBots = qtrue;
    } else {
        cheat = qfalse;
        killBots = qfalse;
    }

    // save the map name here cause on a map restart we reload the q3config.cfg
    // and thus nuke the arguments of the map command
    Q_strncpyz(mapname, map, sizeof(mapname));

    // start up the map
    SV_SpawnServer(mapname, killBots);

    // set the cheat value
    // if the level was started with "map <levelname>", then
    // cheats will not be allowed.  If started with "devmap <levelname>"
    // then cheats will be allowed
    if (cheat) {
        Cvar_Set("sv_cheats", "1");
    } else {
        Cvar_Set("sv_cheats", "0");
    }
}

/*
================
SV_MapRestart_f

Completely restarts a level, but doesn't send a new gamestate to the clients.
This allows fair starts with variable load times.
================
*/
static void SV_MapRestart_f(void) {
    int i;
    client_t* client;
    const char* denied;
    qboolean isBot;
    int delay;

    // make sure we aren't restarting twice in the same frame
    if (com_frameTime == sv.serverId) {
        return;
    }

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (sv.restartTime) {
        return;
    }

    if (Cmd_Argc() > 1) {
        delay = atoi(Cmd_Argv(1));
    } else {
        delay = 5;
    }
    if (delay && !Cvar_VariableValue("g_doWarmup")) {
        char warmupInfo[MAX_INFO_STRING] = {0};
        int gametype;

        sv.restartTime = sv.time + delay * 1000;

        // [QL] Build info string with time and gametype keys
        Info_SetValueForKey(warmupInfo, "time", va("%i", sv.restartTime));
        gametype = (int)Cvar_VariableValue("g_gametype");
        Info_SetValueForKey(warmupInfo, "g_gametype", va("%d", gametype));
        SV_SetConfigstring(CS_WARMUP, warmupInfo);
        return;
    }

    // [QL] Skip variable-change checks when game triggered the restart (g_restarted)
    if (!Cvar_VariableValue("g_restarted")) {
        // check for changes in variables that can't just be restarted
        if (sv_maxclients->modified || sv_gametype->modified) {
            char mapname[MAX_QPATH];

            Com_Printf("variable change -- restarting.\n");
            // restart the map the slow way
            Q_strncpyz(mapname, Cvar_VariableString("mapname"), sizeof(mapname));

            SV_SpawnServer(mapname, qfalse);
            return;
        }
    }

    // toggle the server bit so clients can detect that a
    // map_restart has happened
    svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

    // generate a new serverid
    sv.serverId = com_frameTime;
    Cvar_Set("sv_serverid", va("%i", sv.serverId));

    // [QL] Record level start wall-clock time
    Cvar_Set("g_levelStartTime", va("%i", (int)time(NULL)));

    // if a map_restart occurs while a client is changing maps, we need
    // to give them the correct time so that when they finish loading
    // they don't violate the backwards time check in cl_cgame.c
    for (i = 0; i < sv_maxclients->integer; i++) {
        if (svs.clients[i].state == CS_PRIMED) {
            svs.clients[i].oldServerTime = sv.restartTime;
        }
    }

    // reset all the vm data in place without changing memory allocation
    // note that we do NOT set sv.state = SS_LOADING, so configstrings that
    // had been changed from their default values will generate broadcast updates
    sv.state = SS_LOADING;
    sv.restarting = qtrue;

    SV_RestartGameProgs();

    // run a few frames to allow everything to settle
    for (i = 0; i < 3; i++) {
        SV_GameRunFrame(sv.time);
        sv.time += 100;
        svs.time += 100;
    }

    sv.state = SS_GAME;
    sv.restarting = qfalse;

    // connect and begin all the clients
    for (i = 0; i < sv_maxclients->integer; i++) {
        client = &svs.clients[i];

        // send the new gamestate to all connected clients
        if (client->state < CS_CONNECTED) {
            continue;
        }

        if (client->netchan.remoteAddress.type == NA_BOT) {
            isBot = qtrue;
        } else {
            isBot = qfalse;
        }

        // add the map_restart command
        SV_AddServerCommand(client, "map_restart\n");

        // connect the client again, without the firstTime flag
        denied = SV_GameClientConnect(i, qfalse, isBot);
        if (denied) {
            // this generally shouldn't happen, because the client
            // was connected before the level change
            SV_DropClient(client, denied);
            Com_Printf("SV_MapRestart_f(%d): dropped client %i - denied!\n", delay, i);
            continue;
        }

        if (client->state == CS_ACTIVE)
            SV_ClientEnterWorld(client, &client->lastUsercmd);
        else {
            // If we don't reset client->lastUsercmd and are restarting during map load,
            // the client will hang because we'll use the last Usercmd from the previous map,
            // which is wrong obviously.
            SV_ClientEnterWorld(client, NULL);
        }
    }

    // run another frame to allow things to look at all the players
    SV_GameRunFrame(sv.time);
    sv.time += 100;
    svs.time += 100;
}

//===============================================================

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
static void SV_Kick_f(void) {
    client_t* cl;
    int i;

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: kick <player name>\nkick all = kick everyone\nkick allbots = kick all bots\n");
        return;
    }

    cl = SV_GetPlayerByHandle();
    if (!cl) {
        if (!Q_stricmp(Cmd_Argv(1), "all")) {
            for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
                if (!cl->state) {
                    continue;
                }
                if (cl->netchan.remoteAddress.type == NA_LOOPBACK) {
                    continue;
                }
                SV_DropClient(cl, "was kicked");
                cl->lastPacketTime = svs.time;  // in case there is a funny zombie
            }
        } else if (!Q_stricmp(Cmd_Argv(1), "allbots")) {
            for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
                if (!cl->state) {
                    continue;
                }
                if (cl->netchan.remoteAddress.type != NA_BOT) {
                    continue;
                }
                SV_DropClient(cl, "was kicked");
                cl->lastPacketTime = svs.time;  // in case there is a funny zombie
            }
        }
        return;
    }
    if (cl->netchan.remoteAddress.type == NA_LOOPBACK) {
        Com_Printf("Cannot kick host player\n");
        return;
    }

    SV_DropClient(cl, "was kicked");
    cl->lastPacketTime = svs.time;  // in case there is a funny zombie
}

/*
==================
SV_KickNum_f

Kick a user off of the server
==================
*/
static void SV_KickNum_f(void) {
    client_t* cl;

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <client number>\n", Cmd_Argv(0));
        return;
    }

    cl = SV_GetPlayerByNum();
    if (!cl) {
        return;
    }
    if (cl->netchan.remoteAddress.type == NA_LOOPBACK) {
        Com_Printf("Cannot kick host player\n");
        return;
    }

    SV_DropClient(cl, "was kicked");
    cl->lastPacketTime = svs.time;  // in case there is a funny zombie
}

/*
===============================================================================

ADDRESS BAN LIST

SV_IsBanned (sv_client.c) rejects connects and getchallenge requests against
serverBans[]. The commands below are what actually populate that list and keep
it on disk in sv_banFile, so an operator's bans survive a map change and a
server restart.

Entries are stored as an address plus a CIDR prefix length, and an entry may be
either a ban or an exception; SV_IsBanned checks exceptions first, so a narrow
exception can be carved out of a wide ban.

===============================================================================
*/

/*
==================
SV_WriteBans

Save the current ban list to sv_banFile.
==================
*/
static void SV_WriteBans(void) {
    int index;
    fileHandle_t writeto;
    char filepath[MAX_QPATH];

    if (!sv_banFile->string || !*sv_banFile->string) {
        return;
    }

    // Same path SV_RehashBans_f reads back.
    Com_sprintf(filepath, sizeof(filepath), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string);

    writeto = FS_SV_FOpenFileWrite(filepath);

    if (!writeto) {
        Com_Printf("SV_WriteBans: could not open %s for writing.\n", filepath);
        return;
    }

    for (index = 0; index < serverBansCount; index++) {
        serverBan_t* curban = &serverBans[index];
        const char* writebuf;

        writebuf = va("%d %s %d\n", curban->isexception, NET_AdrToString(curban->ip), curban->subnet);
        FS_Write(writebuf, strlen(writebuf), writeto);
    }

    FS_FCloseFile(writeto);
}

/*
==================
SV_DelBanEntryFromList

Remove one entry from the list, keeping the remaining entries contiguous.
==================
*/
static qboolean SV_DelBanEntryFromList(int index) {
    if (index < 0 || index >= serverBansCount) {
        return qtrue;
    }

    if (index < serverBansCount - 1) {
        memmove(serverBans + index, serverBans + index + 1,
                (serverBansCount - index - 1) * sizeof(*serverBans));
    }

    serverBansCount--;

    return qfalse;
}

/*
==================
SV_ParseCIDRNotation

Parse an address in "address" or "address/prefix" form. Returns qtrue on a
parse failure. A missing prefix means "this exact address": /32 for IPv4 and
/128 for IPv6.
==================
*/
static qboolean SV_ParseCIDRNotation(netadr_t* dest, int* mask, char* adrstr) {
    char* suffix;

    suffix = strchr(adrstr, '/');
    if (suffix) {
        *suffix = '\0';
        suffix++;
    }

    if (!NET_StringToAdr(adrstr, dest, NA_UNSPEC)) {
        return qtrue;
    }

    if (suffix) {
        *mask = atoi(suffix);

        if (dest->type == NA_IP) {
            if (*mask < 1 || *mask > 32) {
                return qtrue;
            }
        } else {
            if (*mask < 1 || *mask > 128) {
                return qtrue;
            }
        }
    } else if (dest->type == NA_IP) {
        *mask = 32;
    } else {
        *mask = 128;
    }

    return qfalse;
}

/*
==================
SV_AddBanToList

Add a ban (or an exception) covering the given address/prefix, then persist the
list. Called both by the address-taking console commands and by banUser /
banClient, which supply a connected client's address.
==================
*/
static void SV_AddBanToList(netadr_t ip, int mask, qboolean isexception) {
    int index;

    if (serverBansCount >= SERVER_MAXBANS) {
        Com_Printf("Error: Maximum number of bans/exceptions exceeded.\n");
        return;
    }

    // Check whether an existing entry of the same kind already covers this
    // range; if so there is nothing to add. Conversely, drop any existing
    // entries of the same kind that the new (wider) range subsumes, so the
    // list does not accumulate redundant rules.
    for (index = 0; index < serverBansCount; index++) {
        serverBan_t* curban = &serverBans[index];

        if (curban->isexception != isexception) {
            continue;
        }

        if (curban->subnet <= mask && NET_CompareBaseAdrMask(curban->ip, ip, curban->subnet)) {
            Com_Printf("Error: %s %s/%d already %s by existing %s %s/%d.\n",
                       isexception ? "Exception" : "Ban", NET_AdrToString(ip), mask,
                       isexception ? "excepted" : "banned",
                       isexception ? "exception" : "ban",
                       NET_AdrToString(curban->ip), curban->subnet);
            return;
        }
    }

    for (index = 0; index < serverBansCount;) {
        serverBan_t* curban = &serverBans[index];

        if (curban->isexception == isexception && curban->subnet >= mask &&
            NET_CompareBaseAdrMask(curban->ip, ip, mask)) {
            // subsumed by the new entry
            SV_DelBanEntryFromList(index);
            continue;
        }

        index++;
    }

    serverBans[serverBansCount].ip = ip;
    serverBans[serverBansCount].subnet = mask;
    serverBans[serverBansCount].isexception = isexception;
    serverBansCount++;

    SV_WriteBans();

    Com_Printf("Added %s: %s/%d\n", isexception ? "ban exception" : "ban",
               NET_AdrToString(ip), mask);
}

/*
==================
SV_BanClient

Ban the address a connected client is playing from, then drop them. The ban
covers exactly that address - operators who want to catch a whole range can
follow up with banaddr.
==================
*/
static void SV_BanClient(client_t* cl) {
    netadr_t ip = cl->netchan.remoteAddress;
    int mask;

    if (ip.type == NA_BOT) {
        Com_Printf("Cannot ban a bot.\n");
        return;
    }

    if (ip.type == NA_IP) {
        mask = 32;
    } else if (ip.type == NA_IP6) {
        mask = 128;
    } else {
        Com_Printf("Cannot ban %s: unsupported address type.\n", cl->name);
        return;
    }

    SV_AddBanToList(ip, mask, qfalse);

    SV_DropClient(cl, "was banned");
    cl->lastPacketTime = svs.time;  // in case there is a funny zombie
}

/*
==================
SV_RehashBans_f

(Re)load the ban list from sv_banFile. Also run once at server start so that
bans are in force before the first client can connect.
==================
*/
static void SV_RehashBans_f(void) {
    int count, lineNum;
    long filelen;
    fileHandle_t readfrom;
    char *textbuf, *curpos, *maskpos, *newlinepos;
    char filepath[MAX_QPATH];

    serverBansCount = 0;

    if (!sv_banFile->string || !*sv_banFile->string) {
        return;
    }

    Com_sprintf(filepath, sizeof(filepath), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string);

    filelen = FS_SV_FOpenFileRead(filepath, &readfrom);

    if (filelen < 0 || !readfrom) {
        // No ban file yet - that is the normal state for a fresh server.
        if (readfrom) {
            FS_FCloseFile(readfrom);
        }
        return;
    }

    if (filelen < 2) {
        // Too short to hold even one entry.
        FS_FCloseFile(readfrom);
        return;
    }

    textbuf = Z_Malloc(filelen + 1);

    filelen = FS_Read(textbuf, filelen, readfrom);
    FS_FCloseFile(readfrom);

    if (filelen < 0) {
        Z_Free(textbuf);
        return;
    }

    textbuf[filelen] = '\0';

    count = 0;
    lineNum = 0;
    curpos = textbuf;

    while (*curpos && count < SERVER_MAXBANS) {
        lineNum++;

        // Terminate the current line; remember where the next one starts.
        newlinepos = strchr(curpos, '\n');
        if (newlinepos) {
            *newlinepos = '\0';
            newlinepos++;
        }

        // Written by SV_WriteBans as "<isexception> <address> <prefix>".
        if (*curpos == '0' || *curpos == '1') {
            serverBan_t* ban = &serverBans[count];

            ban->isexception = (*curpos == '1');

            curpos++;
            while (*curpos == ' ') {
                curpos++;
            }

            // Rejoin address and prefix into the "address/prefix" form
            // SV_ParseCIDRNotation accepts.
            maskpos = strrchr(curpos, ' ');
            if (maskpos) {
                *maskpos = '/';
            }

            if (SV_ParseCIDRNotation(&ban->ip, &ban->subnet, curpos)) {
                Com_Printf("Error parsing line %d in ban file %s: invalid address.\n", lineNum, filepath);
            } else {
                count++;
            }
        } else if (*curpos) {
            Com_Printf("Error parsing line %d in ban file %s: bad exception flag.\n", lineNum, filepath);
        }

        if (!newlinepos) {
            break;
        }

        curpos = newlinepos;
    }

    serverBansCount = count;

    Z_Free(textbuf);
}

/*
==================
SV_BanAddr_f

banaddr <address>[/<prefix>]   - refuse connections from that range
exceptaddr <address>[/<prefix>] - allow it back through a wider ban
==================
*/
static void SV_BanAddr_f(void) {
    netadr_t ip;
    int mask;
    qboolean isexception = (Q_stricmp(Cmd_Argv(0), "exceptaddr") == 0);

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s (ip[/subnet])\n", Cmd_Argv(0));
        return;
    }

    if (SV_ParseCIDRNotation(&ip, &mask, Cmd_Argv(1))) {
        Com_Printf("Error: Invalid address %s\n", Cmd_Argv(1));
        return;
    }

    SV_AddBanToList(ip, mask, isexception);
}

/*
==================
SV_DelBanAddr_f

bandel / exceptdel: remove an entry either by its number in "banlist" output,
or by the exact address/prefix it covers.
==================
*/
static void SV_DelBanAddr_f(void) {
    int index, todel, mask;
    netadr_t ip;
    char* banstring;
    qboolean isexception = (Q_stricmp(Cmd_Argv(0), "exceptdel") == 0);

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s (ip[/subnet] | num)\n", Cmd_Argv(0));
        return;
    }

    banstring = Cmd_Argv(1);

    if (strchr(banstring, '.') || strchr(banstring, ':')) {
        // an address rather than a list index
        if (SV_ParseCIDRNotation(&ip, &mask, banstring)) {
            Com_Printf("Error: Invalid address %s\n", banstring);
            return;
        }

        for (index = 0; index < serverBansCount; index++) {
            serverBan_t* curban = &serverBans[index];

            if (curban->isexception == isexception && curban->subnet >= mask &&
                NET_CompareBaseAdrMask(curban->ip, ip, mask)) {
                Com_Printf("Deleting %s %s/%d\n", isexception ? "exception" : "ban",
                           NET_AdrToString(curban->ip), curban->subnet);

                SV_DelBanEntryFromList(index);
                index--;
            }
        }

        SV_WriteBans();
        return;
    }

    todel = atoi(banstring);

    if (todel < 1 || todel > serverBansCount) {
        Com_Printf("Error: Invalid ban number given\n");
        return;
    }

    // banlist numbers bans and exceptions in one sequence, so walk the list
    // counting only entries of the requested kind.
    for (index = 0; index < serverBansCount; index++) {
        if (serverBans[index].isexception != isexception) {
            continue;
        }

        todel--;

        if (!todel) {
            SV_DelBanEntryFromList(index);
            SV_WriteBans();
            return;
        }
    }

    Com_Printf("Error: Invalid ban number given\n");
}

/*
==================
SV_ListBans_f
==================
*/
static void SV_ListBans_f(void) {
    int index, count;

    count = 0;
    for (index = 0; index < serverBansCount; index++) {
        serverBan_t* ban = &serverBans[index];

        if (!ban->isexception) {
            count++;
            Com_Printf("Ban #%d: %s/%d\n", count, NET_AdrToString(ban->ip), ban->subnet);
        }
    }

    count = 0;
    for (index = 0; index < serverBansCount; index++) {
        serverBan_t* ban = &serverBans[index];

        if (ban->isexception) {
            count++;
            Com_Printf("Exception #%d: %s/%d\n", count, NET_AdrToString(ban->ip), ban->subnet);
        }
    }
}

/*
==================
SV_FlushBans_f

Delete all bans and exceptions.
==================
*/
static void SV_FlushBans_f(void) {
    serverBansCount = 0;

    SV_WriteBans();

    Com_Printf("All bans and exceptions have been deleted.\n");
}

/*
==================
SV_Ban_f

Ban the address of a connected player, then drop them.
==================
*/
static void SV_Ban_f(void) {
    client_t* cl;

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: banUser <player name>\n");
        return;
    }

    cl = SV_GetPlayerByHandle();

    if (!cl) {
        return;
    }

    if (cl->netchan.remoteAddress.type == NA_LOOPBACK) {
        Com_Printf("Cannot kick host player\n");
        return;
    }

    SV_BanClient(cl);
}

/*
==================
SV_BanNum_f

Ban the address of a connected player by client number, then drop them.
==================
*/
static void SV_BanNum_f(void) {
    client_t* cl;

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: banClient <client number>\n");
        return;
    }

    cl = SV_GetPlayerByNum();
    if (!cl) {
        return;
    }
    if (cl->netchan.remoteAddress.type == NA_LOOPBACK) {
        Com_Printf("Cannot kick host player\n");
        return;
    }

    SV_BanClient(cl);
}

/*
** SV_Strlen -- skips color escape codes
*/
static int SV_Strlen(const char* str) {
    const char* s = str;
    int count = 0;

    while (*s) {
        if (Q_IsColorString(s)) {
            s += 2;
        } else {
            count++;
            s++;
        }
    }

    return count;
}

/*
================
SV_Status_f
================
*/
static void SV_Status_f(void) {
    int i, j, l;
    client_t* cl;
    playerState_t* ps;
    const char* s;
    int ping;

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    Com_Printf("map: %s\n", sv_mapname->string);

    Com_Printf("cl score ping name            address                                 rate \n");
    Com_Printf("-- ----- ---- --------------- --------------------------------------- -----\n");
    for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
        if (!cl->state)
            continue;
        Com_Printf("%2i ", i);
        ps = SV_GameClientNum(i);
        Com_Printf("%5i ", ps->persistant[PERS_SCORE]);

        if (cl->state == CS_CONNECTED)
            Com_Printf("CON ");
        else if (cl->state == CS_ZOMBIE)
            Com_Printf("ZMB ");
        else {
            ping = cl->ping < 9999 ? cl->ping : 9999;
            Com_Printf("%4i ", ping);
        }

        Com_Printf("%s", cl->name);

        l = 16 - SV_Strlen(cl->name);
        j = 0;

        do {
            Com_Printf(" ");
            j++;
        } while (j < l);

        // TTimo adding a ^7 to reset the color
        s = NET_AdrToString(cl->netchan.remoteAddress);
        Com_Printf("^7%s", s);
        l = 39 - strlen(s);
        j = 0;

        do {
            Com_Printf(" ");
            j++;
        } while (j < l);

        Com_Printf(" %5i", cl->rate);

        Com_Printf("\n");
    }
    Com_Printf("\n");
}

/*
==================
SV_ConSay_f
==================
*/
static void SV_ConSay_f(void) {
    char* p;
    char text[1024];

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (Cmd_Argc() < 2) {
        return;
    }

    strcpy(text, "console: ");
    p = Cmd_Args();

    if (*p == '"') {
        p++;
        p[strlen(p) - 1] = 0;
    }

    strcat(text, p);

    Com_Printf("%s\n", text);
    SV_SendServerCommand(NULL, "chat \"%s\"", text);
}

/*
===========
SV_Serverinfo_f

Examine the serverinfo string
===========
*/
static void SV_Serverinfo_f(void) {
    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    Com_Printf("Server info settings:\n");
    Info_Print(Cvar_InfoString_Big(CVAR_SERVERINFO));
}

/*
===========
SV_DumpUser_f

Examine all a users info strings
===========
*/
static void SV_DumpUser_f(void) {
    client_t* cl;

    // make sure server is running
    if (!com_sv_running->integer) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: dumpuser <userid>\n");
        return;
    }

    cl = SV_GetPlayerByHandle();
    if (!cl) {
        return;
    }

    Com_Printf("userinfo\n");
    Com_Printf("--------\n");
    Info_Print(cl->userinfo);
}

/*
=================
SV_KillServer
=================
*/
static void SV_KillServer_f(void) {
    SV_Shutdown("killserver");
}

//===========================================================

/*
==================
SV_CompleteMapName
==================
*/
static void SV_CompleteMapName(char* args, int argNum) {
    if (argNum == 2) {
        Field_CompleteFilename("maps", "bsp", qtrue, qfalse);
    }
}

/*
==================
SV_CompletePlayerName
==================
*/
static void SV_CompletePlayerName(char* args, int argNum) {
    if (argNum == 2) {
        char names[MAX_CLIENTS][MAX_NAME_LENGTH];
        const char* namesPtr[MAX_CLIENTS];
        client_t* cl;
        int i;
        int nameCount;
        int clientCount;

        nameCount = 0;
        clientCount = sv_maxclients->integer;

        for (i = 0, cl = svs.clients; i < clientCount; i++, cl++) {
            if (!cl->state) {
                continue;
            }
            if (i >= MAX_CLIENTS) {
                break;
            }
            Q_strncpyz(names[nameCount], cl->name, sizeof(names[nameCount]));
            Q_CleanStr(names[nameCount]);

            namesPtr[nameCount] = names[nameCount];

            nameCount++;
        }
        qsort((void*)namesPtr, nameCount, sizeof(namesPtr[0]), Com_strCompare);

        Field_CompletePlayerName(namesPtr, nameCount);
    }
}

/*
==================
[QL] Factory/Arena/MapPool stubs

These commands interact with the factory and arena system.
Currently stubbed - the full implementation requires the factory system.
==================
*/
static void SV_AutoMap_f(void) {
    Com_Printf("arena: factory/arena system not yet implemented.\n");
}

static void SV_StartRandomMap_f(void) {
    Com_Printf("startRandomMap: factory/arena system not yet implemented.\n");
}

static void SV_ReloadMapPool_f(void) {
    Com_Printf("reload_mappool: factory/arena system not yet implemented.\n");
}

static void SV_ReloadArenas_f(void) {
    Com_Printf("reload_arenas: factory/arena system not yet implemented.\n");
}

static void SV_ReloadFactories_f(void) {
    Com_Printf("reload_factories: factory/arena system not yet implemented.\n");
}

/*
==================
SV_AddOperatorCommands
==================
*/
void SV_AddOperatorCommands(void) {
    static qboolean initialized;

    if (initialized) {
        return;
    }
    initialized = qtrue;

    Cmd_AddCommand("kick", SV_Kick_f);
    Cmd_SetCommandCompletionFunc("kick", SV_CompletePlayerName);
    Cmd_AddCommand("clientkick", SV_KickNum_f);
    Cmd_AddCommand("status", SV_Status_f);
    Cmd_AddCommand("serverinfo", SV_Serverinfo_f);
    Cmd_AddCommand("dumpuser", SV_DumpUser_f);
    Cmd_AddCommand("map_restart", SV_MapRestart_f);
    Cmd_AddCommand("sectorlist", SV_SectorList_f);
    Cmd_AddCommand("map", SV_Map_f);
    Cmd_SetCommandCompletionFunc("map", SV_CompleteMapName);
    Cmd_AddCommand("devmap", SV_Map_f);
    Cmd_SetCommandCompletionFunc("devmap", SV_CompleteMapName);
    Cmd_AddCommand("killserver", SV_KillServer_f);
    if (com_dedicated->integer) {
        Cmd_AddCommand("say", SV_ConSay_f);
    }

    // Address ban list. SV_IsBanned already enforces serverBans[]; these are
    // what fill it in and keep it on disk.
    Cmd_AddCommand("rehashbans", SV_RehashBans_f);
    Cmd_AddCommand("listbans", SV_ListBans_f);
    Cmd_AddCommand("banaddr", SV_BanAddr_f);
    Cmd_AddCommand("exceptaddr", SV_BanAddr_f);
    Cmd_AddCommand("bandel", SV_DelBanAddr_f);
    Cmd_AddCommand("exceptdel", SV_DelBanAddr_f);
    Cmd_AddCommand("flushbans", SV_FlushBans_f);
    Cmd_AddCommand("banUser", SV_Ban_f);
    Cmd_SetCommandCompletionFunc("banUser", SV_CompletePlayerName);
    Cmd_AddCommand("banClient", SV_BanNum_f);

    // [QL] factory/arena/mappool commands (stubs)
    Cmd_AddCommand("arena", SV_AutoMap_f);
    Cmd_AddCommand("startRandomMap", SV_StartRandomMap_f);
    Cmd_AddCommand("reload_mappool", SV_ReloadMapPool_f);
    Cmd_AddCommand("reload_arenas", SV_ReloadArenas_f);
    Cmd_AddCommand("reload_factories", SV_ReloadFactories_f);
}

/*
==================
SV_RemoveOperatorCommands
==================
*/
void SV_RemoveOperatorCommands(void) {
#if 0
	// removing these won't let the server start again
	Cmd_RemoveCommand ("kick");
	Cmd_RemoveCommand ("clientkick");
	Cmd_RemoveCommand ("status");
	Cmd_RemoveCommand ("serverinfo");
	Cmd_RemoveCommand ("dumpuser");
	Cmd_RemoveCommand ("map_restart");
	Cmd_RemoveCommand ("sectorlist");
	Cmd_RemoveCommand ("map");
	Cmd_RemoveCommand ("devmap");
	Cmd_RemoveCommand ("killserver");
	Cmd_RemoveCommand ("say");
	Cmd_RemoveCommand ("arena");
	Cmd_RemoveCommand ("startRandomMap");
	Cmd_RemoveCommand ("reload_mappool");
	Cmd_RemoveCommand ("reload_arenas");
	Cmd_RemoveCommand ("reload_factories");
#endif
}
