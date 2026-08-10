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

/*
=======================================================================

  SESSION DATA

Session data is the only data that stays persistant across level loads
and tournament restarts.
=======================================================================
*/

/*
================
G_WriteClientSessionData

Called on game shutdown
================
*/
void G_WriteClientSessionData(gclient_t* client) {
    const char* s;
    const char* var;

    // [QL] 13 fields. QL field order: weaponPrimary comes before
    // wins/losses/teamLeader, and prevScore is appended. updatePlayQueue and
    // joinTime are not serialised.
    s = va("%i %i %i %i %i %i %i %i %i %i %i %i %i",
           client->sess.sessionTeam,
           client->sess.spectatorTime,
           client->sess.spectatorState,
           client->sess.spectatorClient,
           client->sess.weaponPrimary,
           client->sess.wins,
           client->sess.losses,
           client->sess.teamLeader,
           client->sess.privileges,
           client->sess.specOnly,
           client->sess.playQueue,
           client->sess.muted,
           client->sess.prevScore);

    var = va("session%i", (int)(client - level.clients));

    trap_Cvar_Set(var, s);
}

/*
================
G_ReadSessionData

Called on a reconnect
================
*/
void G_ReadSessionData(gclient_t* client) {
    char s[MAX_STRING_CHARS];
    const char* var;
    int teamLeader;
    int spectatorState;
    int sessionTeam;

    var = va("session%i", (int)(client - level.clients));
    trap_Cvar_VariableStringBuffer(var, s, sizeof(s));

    // NOTE: every field below is an int; spectatorTime in particular must be
    // read with %i. Reading it as %ld makes sscanf store 8 bytes into a 4-byte
    // member on LP64 (Linux/macOS), corrupting the rest of clientSession_t.
    sscanf(s, "%i %i %i %i %i %i %i %i %i %i %i %i %i",
           &sessionTeam,
           &client->sess.spectatorTime,
           &spectatorState,
           &client->sess.spectatorClient,
           &client->sess.weaponPrimary,
           &client->sess.wins,
           &client->sess.losses,
           &teamLeader,
           &client->sess.privileges,
           &client->sess.specOnly,
           &client->sess.playQueue,
           &client->sess.muted,
           &client->sess.prevScore);

    // [QL] force spectator on reconnect only when g_teamSpawnAsSpec is set,
    // this is a team game and warmup is running (binary reads the cvar's
    // integer directly, not level.newSession)
    if (g_teamSpawnAsSpec.integer && g_gametype.integer >= GT_TEAM && level.warmupTime) {
        sessionTeam = TEAM_SPECTATOR;
    }

    client->sess.sessionTeam = (team_t)sessionTeam;
    client->sess.spectatorState = (spectatorState_t)spectatorState;
    client->sess.teamLeader = (qboolean)teamLeader;
}

/*
================
G_InitSessionData

Called on a first-time connect
================
*/
void G_InitSessionData(gclient_t* client, char* userinfo) {
    clientSession_t* sess;

    sess = &client->sess;

    // initial team determination
    if (g_gametype.integer >= GT_TEAM) {
        if (g_teamAutoJoin.integer) {
            sess->sessionTeam = PickTeam(-1);
            BroadcastTeamChange(client, -1);
        } else {
            sess->sessionTeam = TEAM_SPECTATOR;
        }
    } else {
        sess->sessionTeam = TEAM_SPECTATOR;
    }

    sess->spectatorState = SPECTATOR_FREE;
    sess->spectatorTime = (int)time(NULL);          // [QL] wall clock, not level.time
    // [QL] binary calls G_GetAccess(clientNum), which fetches the client's
    // userinfo/ip and looks it up in the access list itself
    sess->privileges = G_GetAccess((int)(client - level.clients));
    sess->playQueue = 0;
    sess->specOnly = (g_gametype.integer == GT_DUEL) ? 1 : 0;

    G_WriteClientSessionData(client);
}

/*
==================
G_InitWorldSession

==================
*/
void G_InitWorldSession(void) {
    char s[MAX_STRING_CHARS];
    int gt;

    trap_Cvar_VariableStringBuffer("session", s, sizeof(s));
    gt = atoi(s);

    // if the gametype changed since the last session, don't use any
    // client sessions
    if (g_gametype.integer != gt) {
        level.newSession = qtrue;
        G_Printf("Gametype changed, clearing session data.\n");
    }
}

/*
==================
G_WriteSessionData

==================
*/
void G_WriteSessionData(void) {
    int i;

    trap_Cvar_Set("session", va("%i", g_gametype.integer));

    for (i = 0; i < level.maxclients; i++) {
        if (level.clients[i].pers.connected == CON_CONNECTED) {
            G_WriteClientSessionData(&level.clients[i]);
        }
    }
}
