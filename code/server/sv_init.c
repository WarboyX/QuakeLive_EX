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
===============
SV_SendConfigstring

Creates and sends the server command necessary to update the CS index for the
given client
===============
*/
static void SV_SendConfigstring(client_t* client, int index) {
    int maxChunkSize = MAX_STRING_CHARS - 24;
    int len;

    len = strlen(sv.configstrings[index]);

    if (len >= maxChunkSize) {
        int sent = 0;
        int remaining = len;
        char* cmd;
        char buf[MAX_STRING_CHARS];

        while (remaining > 0) {
            if (sent == 0) {
                cmd = "bcs0";
            } else if (remaining < maxChunkSize) {
                cmd = "bcs2";
            } else {
                cmd = "bcs1";
            }
            Q_strncpyz(buf, &sv.configstrings[index][sent],
                       maxChunkSize);

            SV_SendServerCommand(client, "%s %i \"%s\"\n", cmd,
                                 index, buf);

            sent += (maxChunkSize - 1);
            remaining -= (maxChunkSize - 1);
        }
    } else {
        // standard cs, just send it
        SV_SendServerCommand(client, "cs %i \"%s\"\n", index,
                             sv.configstrings[index]);
    }
}

/*
===============
SV_UpdateConfigstrings

Called when a client goes from CS_PRIMED to CS_ACTIVE.  Updates all
Configstring indexes that have changed while the client was in CS_PRIMED
===============
*/
void SV_UpdateConfigstrings(client_t* client) {
    int index;

    /*
    [QL] Only resend configstrings that actually changed while the client was in
    CS_PRIMED. The gamestate already contained all of them at the time it was
    sent, so resending everything would overflow the 64-slot reliable buffer.

    Changed ones alone can overflow it too. A client loading into a server that
    is filling with bots is in CS_PRIMED for several seconds, and every bot that
    arrives in that window dirties CS_PLAYERS + n - so on a 64-slot server the
    backlog can be larger than the whole ring, and firing it all at the moment
    the client goes active cycles commands out from under it before cgame has
    run a frame. That is the client-side half of "CL_GetServerCommand: a
    reliable command was cycled out", and on a listen server it takes the server
    down with it.

    So the backlog is drained rather than flushed. Send until the unacknowledged
    window is half full, leave the rest flagged, and SV_SendClientMessages calls
    back once a frame until it is empty. Configstrings hold absolute values, not
    deltas, so arriving over a few frames costs nothing but the ordering between
    two indices - which nothing depends on.
    */
    for (index = 0; index < MAX_CONFIGSTRINGS; index++) {
        if (!client->csUpdated[index]) {
            continue;
        }
        if (client->reliableSequence - client->reliableAcknowledge >= MAX_RELIABLE_COMMANDS / 2) {
            return;   // resume next frame
        }
        client->csUpdated[index] = qfalse;

        // do not always send server info to all clients
        if (index == CS_SERVERINFO && client->gentity &&
            (client->gentity->r.svFlags & SVF_NOSERVERINFO)) {
            continue;
        }
        SV_SendConfigstring(client, index);
    }
}

/*
===============
SV_SetConfigstring

===============
*/
void SV_SetConfigstring(int index, const char* val) {
    int i;
    client_t* client;

    if (index < 0 || index >= MAX_CONFIGSTRINGS) {
        Com_Error(ERR_DROP, "SV_SetConfigstring: bad index %i", index);
    }

    if (!val) {
        val = "";
    }

    // don't bother broadcasting an update if no change
    if (!strcmp(val, sv.configstrings[index])) {
        return;
    }

    // change the string in sv
    Z_Free(sv.configstrings[index]);
    sv.configstrings[index] = CopyString(val);

    // send it to all the clients if we aren't
    // spawning a new server
    if (sv.state == SS_GAME || sv.restarting) {
        // send the data to all relevant clients
        for (i = 0, client = svs.clients; i < sv_maxclients->integer; i++, client++) {
            if (client->state < CS_PRIMED) {
                continue;
            }
            // do not always send server info to all clients
            if (index == CS_SERVERINFO && client->gentity && (client->gentity->r.svFlags & SVF_NOSERVERINFO)) {
                continue;
            }

            // [QL] PRIMED clients got all configstrings in the gamestate;
            // just mark the index so SV_UpdateConfigstrings resends only
            // the ones that actually changed during the PRIMED window.
            if (client->state == CS_PRIMED) {
                client->csUpdated[index] = qtrue;
                continue;
            }

            SV_SendConfigstring(client, index);
        }
    }
}

/*
===============
SV_GetConfigstring

===============
*/
void SV_GetConfigstring(int index, char* buffer, int bufferSize) {
    if (bufferSize < 1) {
        Com_Error(ERR_DROP, "SV_GetConfigstring: bufferSize == %i", bufferSize);
    }
    if (index < 0 || index >= MAX_CONFIGSTRINGS) {
        Com_Error(ERR_DROP, "SV_GetConfigstring: bad index %i", index);
    }
    if (!sv.configstrings[index]) {
        buffer[0] = 0;
        return;
    }

    Q_strncpyz(buffer, sv.configstrings[index], bufferSize);
}

/*
===============
SV_SetUserinfo

===============
*/
void SV_SetUserinfo(int index, const char* val) {
    if (index < 0 || index >= sv_maxclients->integer) {
        Com_Error(ERR_DROP, "SV_SetUserinfo: bad index %i", index);
    }

    if (!val) {
        val = "";
    }

    Q_strncpyz(svs.clients[index].userinfo, val, sizeof(svs.clients[index].userinfo));
    Q_strncpyz(svs.clients[index].name, Info_ValueForKey(val, "name"), sizeof(svs.clients[index].name));
}

/*
===============
SV_GetUserinfo

===============
*/
void SV_GetUserinfo(int index, char* buffer, int bufferSize) {
    if (bufferSize < 1) {
        Com_Error(ERR_DROP, "SV_GetUserinfo: bufferSize == %i", bufferSize);
    }
    if (index < 0 || index >= sv_maxclients->integer) {
        Com_Error(ERR_DROP, "SV_GetUserinfo: bad index %i", index);
    }
    Q_strncpyz(buffer, svs.clients[index].userinfo, bufferSize);
}

/*
================
SV_CreateBaseline

Entity baselines are used to compress non-delta messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
static void SV_CreateBaseline(void) {
    sharedEntity_t* svent;
    int entnum;

    for (entnum = 1; entnum < sv.num_entities; entnum++) {
        svent = SV_GentityNum(entnum);
        if (!svent->r.linked) {
            continue;
        }
        svent->s.number = entnum;

        //
        // take current state as baseline
        //
        sv.svEntities[entnum].baseline = svent->s;
    }
}

/*
===============
SV_BoundMaxClients

===============
*/
static void SV_BoundMaxClients(int minimum) {
    // get the current maxclients value
    Cvar_Get("sv_maxclients", "8", 0);

    sv_maxclients->modified = qfalse;

    if (sv_maxclients->integer < minimum) {
        Cvar_Set("sv_maxclients", va("%i", minimum));
    } else if (sv_maxclients->integer > MAX_CLIENTS) {
        Cvar_Set("sv_maxclients", va("%i", MAX_CLIENTS));
    }
}

/*
===============
SV_Startup

Called when a host starts a map when it wasn't running
one before.  Successive map or map_restart commands will
NOT cause this to be called, unless the game is exited to
the menu system first.
===============
*/
/*
===============
SV_SnapshotBackup

[QL] How many frames of snapshot entities the ring has to hold per client.

Stock code asked whether the server was dedicated and used PACKET_BACKUP if it
was, or 4 if it was not, on the reasoning that "we don't need nearly as many
when playing locally". That reasoning is about how many clients the server is
feeding, not about where it is running, and a listen server full of bots feeds
just as many as a dedicated one: the ring is shared, and every client's frame
advances first_entity by the entities in it.

At 64 slots the short ring covered only a handful of frames of traffic, so any
hitch - a scoreboard request rebuilds and sends three ~1500-byte reliable
commands for 63 players - let it wrap past a frame a client was still deltaing
against. SV_WriteSnapshotToClient then logs "Delta request from out of date
entities" and sends a full uncompressed snapshot instead, which is a bandwidth
spike and a visible stutter. 94 of those in one match, 45 of them immediately
after a score request.

So the question is the client count. A small local game keeps the small ring
and the memory that goes with it; anything big enough to be a server gets the
full PACKET_BACKUP, and Com_InitHunkMemory raises the hunk floor to match.
===============
*/
#define SV_SMALL_GAME_CLIENTS 8

int SV_SnapshotBackup(void) {
    if (sv_maxclients->integer > SV_SMALL_GAME_CLIENTS) {
        return PACKET_BACKUP;
    }
    return 4;
}

static void SV_Startup(void) {
    if (svs.initialized) {
        Com_Error(ERR_FATAL, "SV_Startup: svs.initialized");
    }
    SV_BoundMaxClients(1);

    svs.clients = Z_Malloc(sizeof(client_t) * sv_maxclients->integer);
    svs.numSnapshotEntities = sv_maxclients->integer * SV_SnapshotBackup() * MAX_SNAPSHOT_ENTITIES;

    // [QL] The snapshot entity ring below is the biggest thing a server ever
    // puts on the hunk and it scales with sv_maxclients, so on a large server
    // it can exhaust the hunk on its own and take the map load down with a bare
    // "Hunk_Alloc failed on <n>". Com_InitHunkMemory sizes the hunk from
    // sv_maxclients when that was set on the command line; it cannot when
    // sv_maxclients comes from a config, which is exactly when this bites.
    // Report the shortfall and the fix rather than leaving an opaque failure.
    {
        int needBytes = (int)(svs.numSnapshotEntities * sizeof(entityState_t));
        int haveBytes = Hunk_MemoryRemaining();

        if (needBytes > haveBytes) {
            int haveMegs = Cvar_VariableIntegerValue("com_hunkMegs");
            int needMegs = (haveMegs - (haveBytes / (1024 * 1024))) +
                           (needBytes / (1024 * 1024)) + 16;

            Com_Printf("^1----------------------------------------------------------\n");
            Com_Printf("^1sv_maxclients %i needs %i MB for snapshot entities, but only\n",
                       sv_maxclients->integer, needBytes / (1024 * 1024));
            Com_Printf("^1%i MB of hunk remains. The map load is about to fail.\n",
                       haveBytes / (1024 * 1024));
            Com_Printf("^1Restart with: +set com_hunkMegs %i\n", needMegs);
            Com_Printf("^1(com_hunkMegs is latched - it must be on the command line.)\n");
            Com_Printf("^1----------------------------------------------------------\n");
        }
    }

    svs.initialized = qtrue;

    // Don't respect sv_killserver unless a server is actually running
    if (sv_killserver->integer) {
        Cvar_Set("sv_killserver", "0");
    }

    Cvar_Set("sv_running", "1");

    // Join the ipv6 multicast group now that a map is running so clients can scan for us on the local network.
    NET_JoinMulticast6();
}

/*
==================
SV_ChangeMaxClients
==================
*/
void SV_ChangeMaxClients(void) {
    int oldMaxClients;
    int i;
    client_t* oldClients;
    int count;

    // get the highest client number in use
    count = 0;
    for (i = 0; i < sv_maxclients->integer; i++) {
        if (svs.clients[i].state >= CS_CONNECTED) {
            if (i > count)
                count = i;
        }
    }
    count++;

    oldMaxClients = sv_maxclients->integer;
    // never go below the highest client number in use
    SV_BoundMaxClients(count);
    // if still the same
    if (sv_maxclients->integer == oldMaxClients) {
        return;
    }

    oldClients = Hunk_AllocateTempMemory(count * sizeof(client_t));
    // copy the clients to hunk memory
    for (i = 0; i < count; i++) {
        if (svs.clients[i].state >= CS_CONNECTED) {
            oldClients[i] = svs.clients[i];
        } else {
            Com_Memset(&oldClients[i], 0, sizeof(client_t));
        }
    }

    // free old clients arrays
    Z_Free(svs.clients);

    // allocate new clients
    svs.clients = Z_Malloc(sv_maxclients->integer * sizeof(client_t));
    Com_Memset(svs.clients, 0, sv_maxclients->integer * sizeof(client_t));

    // copy the clients over
    for (i = 0; i < count; i++) {
        if (oldClients[i].state >= CS_CONNECTED) {
            svs.clients[i] = oldClients[i];
        }
    }

    // free the old clients on the hunk
    Hunk_FreeTempMemory(oldClients);

    // allocate new snapshot entities
    svs.numSnapshotEntities = sv_maxclients->integer * SV_SnapshotBackup() * MAX_SNAPSHOT_ENTITIES;
}

/*
================
SV_ClearServer
================
*/
static void SV_ClearServer(void) {
    int i;

    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        if (sv.configstrings[i]) {
            Z_Free(sv.configstrings[i]);
        }
    }
    Com_Memset(&sv, 0, sizeof(sv));
}

/*
================
SV_LoadAltEntityString

[QL] Entity override. When sv_altEntDir names a directory, the server looks for
"<sv_altEntDir>/<mapname>.ent" and, if present, spawns the level from that file
instead of the entity lump inside the .bsp. This lets an operator retune item
placement, spawn points or gametype entities on a stock map without shipping a
modified .bsp that every client would then have to download.

The override is server-side only: entities drive spawning, not collision, so
clients are unaffected and pure-server checks still see the original .bsp.

Called once per SV_SpawnServer, after CM_LoadMap and before SV_InitGameProgs.
================
*/
static char* sv_altEntityString = NULL;

void SV_LoadAltEntityString(const char* mapname) {
    char path[MAX_QPATH];
    char* buffer;
    long len;

    // Drop whatever the previous map used.
    if (sv_altEntityString) {
        Z_Free(sv_altEntityString);
        sv_altEntityString = NULL;
    }

    if (!sv_altEntDir || !sv_altEntDir->string[0]) {
        return;
    }

    Com_sprintf(path, sizeof(path), "%s/%s.ent", sv_altEntDir->string, mapname);

    len = FS_ReadFile(path, (void**)&buffer);
    if (len <= 0 || !buffer) {
        // Not an error: only some maps are expected to carry an override.
        if (buffer) {
            FS_FreeFile(buffer);
        }
        return;
    }

    // FS_ReadFile's buffer is freed on the next FS_Restart, so keep our own
    // copy for the lifetime of the map. Null-terminated for the token parser.
    sv_altEntityString = Z_Malloc(len + 1);
    Com_Memcpy(sv_altEntityString, buffer, len);
    sv_altEntityString[len] = '\0';
    FS_FreeFile(buffer);

    Com_Printf("Using entity override %s (%li bytes)\n", path, len);
}

/*
================
SV_AltEntityString

Returns the loaded override for this map, or NULL to use the .bsp's own lump.
================
*/
char* SV_AltEntityString(void) {
    return sv_altEntityString;
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
This is NOT called for map_restart
================
*/
void SV_SpawnServer(char* server, qboolean killBots) {
    int i;
    int checksum;
    qboolean isBot;
    char systemInfo[16384];
    const char* p;

    /* [QL] The map that is ending still has its counters; report before the
       game module goes away and sv is wiped below. The game module writes its
       own half from G_ShutdownGame, which SV_ShutdownGameProgs is about to
       call, so the two halves land together in the log. */
    if (sv_mapEndReport && sv_mapEndReport->integer && sv.state == SS_GAME) {
        SV_SnapStats_f();
    }

    // shut down the existing game if it is running
    SV_ShutdownGameProgs();

    Com_Printf("------ Server Initialization ------\n");
    Com_Printf("Server: %s\n", server);

    // if not running a dedicated server CL_MapLoading will connect the client to the server
    // also print some status stuff
    CL_MapLoading();

    // make sure all the client stuff is unloaded
    CL_ShutdownAll(qfalse);

    // clear the whole hunk because we're (re)loading the server
    Hunk_Clear();

    // clear collision map data
    CM_ClearMap();

    // init client structures and svs.numSnapshotEntities
    if (!Cvar_VariableValue("sv_running")) {
        SV_Startup();
    } else {
        // check for maxclients change
        if (sv_maxclients->modified) {
            SV_ChangeMaxClients();
        }
    }

    // clear pak references
    FS_ClearPakReferences(0);

    // allocate the snapshot entities on the hunk
    svs.snapshotEntities = Hunk_Alloc(sizeof(entityState_t) * svs.numSnapshotEntities, h_high);
    svs.nextSnapshotEntities = 0;

    // toggle the server bit so clients can detect that a
    // server has changed
    svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

    // set nextmap to the same map, but it may be overriden
    // by the game startup or another console command
    Cvar_Set("nextmap", "map_restart 0");
    //	Cvar_Set( "nextmap", va("map %s", server) );

    for (i = 0; i < sv_maxclients->integer; i++) {
        // save when the server started for each client already connected
        if (svs.clients[i].state >= CS_CONNECTED) {
            svs.clients[i].oldServerTime = sv.time;
        }
    }

    // wipe the entire per-level structure
    SV_ClearServer();
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        sv.configstrings[i] = CopyString("");
    }

    // make sure we are not paused
    Cvar_Set("cl_paused", "0");

    // get a new checksum feed and restart the file system
    sv.checksumFeed = (((unsigned int)rand() << 16) ^ (unsigned int)rand()) ^ Com_Milliseconds();
    FS_Restart(sv.checksumFeed);

    CM_LoadMap(va("maps/%s.bsp", server), qfalse, &checksum);

    // [QL] pick up an entity override for this map, if the operator configured
    // one. Must come after CM_LoadMap (which owns the BSP's own entity lump)
    // and before SV_InitGameProgs (which starts parsing entities).
    SV_LoadAltEntityString(server);

    // set serverinfo visible name
    Cvar_Set("mapname", server);

    Cvar_Set("sv_mapChecksum", va("%i", checksum));

    // serverid should be different each time
    sv.serverId = com_frameTime;
    sv.restartedServerId = sv.serverId;  // I suppose the init here is just to be safe
    sv.checksumFeedServerId = sv.serverId;
    Cvar_Set("sv_serverid", va("%i", sv.serverId));

    // clear physics interaction links
    SV_ClearWorld();

    // media configstring setting should be done during
    // the loading stage, so connected clients don't have
    // to load during actual gameplay
    sv.state = SS_LOADING;

    // load and spawn all other entities
    SV_InitGameProgs();

    // don't allow a map_restart if game is modified
    sv_gametype->modified = qfalse;

    // run a few frames to allow everything to settle
    for (i = 0; i < 3; i++) {
        SV_GameRunFrame(sv.time);
        SV_BotFrame(sv.time);
        sv.time += 100;
        svs.time += 100;
    }

    // create a baseline for more efficient communications
    SV_CreateBaseline();

    for (i = 0; i < sv_maxclients->integer; i++) {
        // send the new gamestate to all connected clients
        if (svs.clients[i].state >= CS_CONNECTED) {
            const char* denied;

            if (svs.clients[i].netchan.remoteAddress.type == NA_BOT) {
                if (killBots) {
                    SV_DropClient(&svs.clients[i], "");
                    continue;
                }
                isBot = qtrue;
            } else {
                isBot = qfalse;
            }

            // connect the client again
            denied = SV_GameClientConnect(i, qfalse, isBot);  // firstTime = qfalse
            if (denied) {
                // this generally shouldn't happen, because the client
                // was connected before the level change
                SV_DropClient(&svs.clients[i], denied);
            } else {
                if (!isBot) {
                    // when we get the next packet from a connected client,
                    // the new gamestate will be sent
                    svs.clients[i].state = CS_CONNECTED;
                } else {
                    client_t* client;
                    sharedEntity_t* ent;

                    client = &svs.clients[i];
                    client->state = CS_ACTIVE;
                    ent = SV_GentityNum(i);
                    ent->s.number = i;
                    client->gentity = ent;

                    client->deltaMessage = -1;
                    client->nextSnapshotTime = 0;  // generate a snapshot immediately
                }
            }
        }
    }

    // run another frame to allow things to look at all the players
    SV_GameRunFrame(sv.time);
    SV_BotFrame(sv.time);
    sv.time += 100;
    svs.time += 100;

    if (sv_pure->integer) {
        // the server sends these to the clients so they will only
        // load pk3s also loaded at the server
        p = FS_LoadedPakChecksums();
        Cvar_Set("sv_paks", p);
        if (strlen(p) == 0) {
            Com_Printf("WARNING: sv_pure set but no PK3 files loaded\n");
        }
        p = FS_LoadedPakNames();
        Cvar_Set("sv_pakNames", p);
    } else {
        Cvar_Set("sv_paks", "");
        Cvar_Set("sv_pakNames", "");
    }
    // the server sends these to the clients so they can figure
    // out which pk3s should be auto-downloaded
    p = FS_ReferencedPakChecksums();
    Cvar_Set("sv_referencedPaks", p);
    p = FS_ReferencedPakNames();
    Cvar_Set("sv_referencedPakNames", p);

    // save systeminfo and serverinfo strings
    Q_strncpyz(systemInfo, Cvar_InfoString_Big(CVAR_SYSTEMINFO), sizeof(systemInfo));
    cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
    SV_SetConfigstring(CS_SYSTEMINFO, systemInfo);

    /*
    [QL] Big buffer: this game's serverinfo does not fit in MAX_INFO_STRING.

    The game module alone flags 45 cvars CVAR_SERVERINFO and the engine adds
    more, which overruns the 1024-byte builder. Info_SetValueForKey then drops
    every key that will not fit and prints "Info string length exceeded" - once
    per key, on every rebuild, which is every time a serverinfo cvar changes:

        Info string length exceeded          (x hundreds, between kills)

    The dropped keys are the tail of the list, so g_levelStartTime was being cut
    mid-key and everything after it never reached the client at all. The client
    reads about thirty keys out of here - gametype, teamsize, the shotgun and
    pmove values, the round timers - so losing the tail silently changes how the
    client behaves.

    Info_ValueForKey handles up to BIG_INFO_STRING, and CS_SYSTEMINFO already
    goes out this way, so the configstring path can simply use the big builder.

    SVC_Status keeps the small one on purpose: that reply goes to master servers
    and the browser in a single out-of-band datagram, and is capped there.
    */
    SV_SetConfigstring(CS_SERVERINFO, Cvar_InfoString_Big(CVAR_SERVERINFO));
    cvar_modifiedFlags &= ~CVAR_SERVERINFO;

    // any media configstring setting now should issue a warning
    // and any configstring changes should be reliably transmitted
    // to all clients
    sv.state = SS_GAME;

    Hunk_SetMark();

#ifndef DEDICATED
    if (com_dedicated->integer) {
        // restart renderer in order to show console for dedicated servers
        // launched through the regular binary
        CL_StartHunkUsers(qtrue);
    }
#endif

    Com_Printf("-----------------------------------\n");
}

/*
===============
SV_Init

Only called at main exe startup, not for each game
===============
*/
void SV_Init(void) {
    SV_AddOperatorCommands();

    // serverinfo vars
    Cvar_Get("dmflags", "0", CVAR_SERVERINFO);
    Cvar_Get("fraglimit", "20", CVAR_SERVERINFO);
    Cvar_Get("timelimit", "0", CVAR_SERVERINFO);
    sv_gametype = Cvar_Get("g_gametype", "0", CVAR_SERVERINFO | CVAR_LATCH);
    Cvar_Get("sv_keywords", "", CVAR_SERVERINFO);
    // [QL] Master servers the browser queries and dedicated servers heartbeat
    // to. Quake Live's own master is long gone, so there is no useful default
    // to ship - point these at a dpmaster you control (or one that accepts the
    // "QuakeLive" gamename) and both directions start working.
    Cvar_Get("sv_master1", "", CVAR_ARCHIVE);
    Cvar_Get("sv_master2", "", CVAR_ARCHIVE);
    Cvar_Get("sv_master3", "", CVAR_ARCHIVE);
    Cvar_Get("sv_master4", "", CVAR_ARCHIVE);
    Cvar_Get("sv_master5", "", CVAR_ARCHIVE);
    sv_mapname = Cvar_Get("mapname", "nomap", CVAR_SERVERINFO | CVAR_ROM);
    sv_privateClients = Cvar_Get("sv_privateClients", "0", CVAR_SERVERINFO);
    sv_hostname = Cvar_Get("sv_hostname", "noname", CVAR_SERVERINFO | CVAR_ARCHIVE);
    sv_maxclients = Cvar_Get("sv_maxclients", "8", CVAR_SERVERINFO | CVAR_LATCH);
    // [QL] MAX_CLIENTS is not a soft limit, and nothing was enforcing it.
    //
    // The server allocates svs.clients from this value, but the game module's
    // client array is a fixed gclient_t g_clients[MAX_CLIENTS] and every loop
    // in it runs to g_maxclients.integer. Set sv_maxclients above 64 and the
    // game walks off the end of that array as soon as enough clients exist to
    // reach past slot 63 - which is why it presented as "the server hard
    // crashes after a certain number of bots" rather than as a bad cvar.
    // botlib is the same shape: botchatstates, botgoalstates, botmovestates
    // and botweaponstates are all [MAX_CLIENTS + 1].
    Cvar_CheckRange(sv_maxclients, 1, MAX_CLIENTS, qtrue);

    sv_minRate = Cvar_Get("sv_minRate", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
    sv_maxRate = Cvar_Get("sv_maxRate", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
    sv_dlRate = Cvar_Get("sv_dlRate", "100", CVAR_ARCHIVE | CVAR_SERVERINFO);
    sv_minPing = Cvar_Get("sv_minPing", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
    sv_maxPing = Cvar_Get("sv_maxPing", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
    sv_serverType = Cvar_Get("sv_serverType", "0", CVAR_ARCHIVE);  // [QL] 1 = LAN-only
    sv_floodProtect = Cvar_Get("sv_floodProtect", "1", CVAR_ARCHIVE | CVAR_SERVERINFO);

    // systeminfo
    Cvar_Get("sv_cheats", "1", CVAR_SYSTEMINFO | CVAR_ROM);
    sv_serverid = Cvar_Get("sv_serverid", "0", CVAR_SYSTEMINFO | CVAR_ROM);
    sv_pure = Cvar_Get("sv_pure", "1", CVAR_SYSTEMINFO);

    Cvar_Get("sv_paks", "", CVAR_SYSTEMINFO | CVAR_ROM);
    Cvar_Get("sv_pakNames", "", CVAR_SYSTEMINFO | CVAR_ROM);
    Cvar_Get("sv_referencedPaks", "", CVAR_SYSTEMINFO | CVAR_ROM);
    Cvar_Get("sv_referencedPakNames", "", CVAR_SYSTEMINFO | CVAR_ROM);

    // server vars
    sv_rconPassword = Cvar_Get("rconPassword", "", CVAR_TEMP);
    sv_privatePassword = Cvar_Get("sv_privatePassword", "", CVAR_TEMP);
    sv_fps = Cvar_Get("sv_fps", "40", CVAR_TEMP);
    sv_timeout = Cvar_Get("sv_timeout", "40", CVAR_TEMP);
    sv_zombietime = Cvar_Get("sv_zombietime", "2", CVAR_TEMP);
    Cvar_Get("nextmap", "", CVAR_TEMP);

    sv_allowDownload = Cvar_Get("sv_allowDownload", "0", CVAR_SERVERINFO);
    Cvar_Get("sv_dlURL", "", CVAR_SERVERINFO | CVAR_ARCHIVE);

    sv_reconnectlimit = Cvar_Get("sv_reconnectlimit", "3", 0);
    sv_showloss = Cvar_Get("sv_showloss", "0", 0);
    sv_padPackets = Cvar_Get("sv_padPackets", "0", 0);
    sv_killserver = Cvar_Get("sv_killserver", "0", 0);
    sv_mapChecksum = Cvar_Get("sv_mapChecksum", "", CVAR_ROM);
    sv_lanForceRate = Cvar_Get("sv_lanForceRate", "1", CVAR_ARCHIVE);
    sv_banFile = Cvar_Get("sv_banFile", "serverbans.dat", CVAR_ARCHIVE);
    sv_altEntDir = Cvar_Get("sv_altEntDir", "", CVAR_ARCHIVE);
    /* [QL] Write the snapshot and spawn numbers into the log at every map
       change, so a log handed over after the fact contains them without anyone
       having had to type snapstats while the match was still running.

       Not CVAR_ARCHIVE: this is a value we choose, and an archived default is
       written into a config on first run and then that config wins forever, so
       changing the default later would do nothing. That trap has cost this
       branch two rounds already (r_dlightMode, con_scale). */
    sv_mapEndReport = Cvar_Get("sv_mapEndReport", "1", 0);
    /* [QL] Off by default: a line a minute is right while testing and wrong on
       a server nobody is watching. autoexec.cfg turns it on. */
    sv_snapStatsInterval = Cvar_Get("sv_snapStatsInterval", "0", 0);

    // Load the bans the operator saved in a previous session so they are in
    // force before the first client can connect. Queued rather than called
    // directly: SV_AddOperatorCommands (which registers "rehashbans") runs
    // above, but sv_banFile only exists as of this line.
    Cbuf_AddText("rehashbans\n");

    // initialize bot cvars so they are listed and can be set before loading the botlib
    SV_BotInitCvars();

    // init the botlib here because we need the pre-compiler in the UI
    SV_BotInitBotLib();

}

/*
==================
SV_FinalMessage

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down.  The messages are sent immediately,
not just stuck on the outgoing message list, because the server is going
to totally exit after returning from this function.
==================
*/
void SV_FinalMessage(char* message) {
    int i, j;
    client_t* cl;

    // send it twice, ignoring rate
    for (j = 0; j < 2; j++) {
        for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
            if (cl->state >= CS_CONNECTED) {
                // don't send a disconnect to a local client
                if (cl->netchan.remoteAddress.type != NA_LOOPBACK) {
                    SV_SendServerCommand(cl, "print \"%s\n\"\n", message);
                    SV_SendServerCommand(cl, "disconnect \"%s\"", message);
                }
                // force a snapshot to be sent
                cl->nextSnapshotTime = 0;
                SV_SendClientSnapshot(cl);
            }
        }
    }
}

/*
================
SV_Shutdown

Called when each game quits,
before Sys_Quit or Sys_Error
================
*/
void SV_Shutdown(char* finalmsg) {
    if (!com_sv_running || !com_sv_running->integer) {
        return;
    }

    Com_Printf("----- Server Shutdown (%s) -----\n", finalmsg);

    NET_LeaveMulticast6();

    if (svs.clients && !com_errorEntered) {
        SV_FinalMessage(finalmsg);
    }

    // [QL] tell the masters we are going away before we stop answering them
    SV_MasterShutdown();

    SV_RemoveOperatorCommands();
    SV_ShutdownGameProgs();

    // free current level
    SV_ClearServer();

    // free server static data
    if (svs.clients) {
        int index;

        for (index = 0; index < sv_maxclients->integer; index++)
            SV_FreeClient(&svs.clients[index]);

        Z_Free(svs.clients);
    }
    Com_Memset(&svs, 0, sizeof(svs));

    Cvar_Set("sv_running", "0");

    Com_Printf("---------------------------\n");

    // disconnect any local clients
    if (sv_killserver->integer != 2)
        CL_Disconnect(qfalse);
}
