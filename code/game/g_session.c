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
#define SESSION_FIELD_COUNT 13

void G_ReadSessionData(gclient_t* client) {
    char s[MAX_STRING_CHARS];
    const char* var;
    int f[SESSION_FIELD_COUNT];
    int parsed;

    var = va("session%i", (int)(client - level.clients));
    trap_Cvar_VariableStringBuffer(var, s, sizeof(s));

    /*
    [QL] Parse into locals and commit nothing unless the whole record parsed.

    This used to sscanf straight into client->sess plus three uninitialised
    locals - sessionTeam, spectatorState, teamLeader - and then assign all three
    unconditionally. sscanf only writes the fields it manages to convert and
    leaves the rest alone, so an empty or short session string left those locals
    holding whatever was on the stack, and client->sess.sessionTeam was assigned
    from it regardless.

    A garbage team is bad enough on its own. What made it stick is that
    G_WriteSessionData writes sess back out on every map change, so the bad value
    is persisted and re-read next time: a client parked in spectator by a stack
    value stays there across a round, across a map change, and across
    reconnecting, because the record it is reading is the one it wrote. Bots are
    affected identically - they go through the same connect path - which is why
    two of them were sitting in the spectator list next to the player.

    Reading the fields into an array and requiring all of them keeps a partial
    record from being half-applied, and a missing record now leaves whatever
    G_InitSessionData just chose in place rather than overwriting it with noise.

    NOTE: every field is an int; spectatorTime in particular must be read with
    %i. Reading it as %ld makes sscanf store 8 bytes into a 4-byte member on
    LP64 (Linux/macOS), corrupting the rest of clientSession_t.
    */
    memset(f, 0, sizeof(f));
    parsed = sscanf(s, "%i %i %i %i %i %i %i %i %i %i %i %i %i",
                    &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6],
                    &f[7], &f[8], &f[9], &f[10], &f[11], &f[12]);

    if (parsed != SESSION_FIELD_COUNT) {
        if (parsed > 0 || s[0]) {
            G_Printf(S_COLOR_YELLOW "G_ReadSessionData: client %i has a bad session record "
                     "(%i of %i fields), keeping current session\n",
                     (int)(client - level.clients), parsed, SESSION_FIELD_COUNT);
        }
        return;
    }

    if (f[0] < TEAM_FREE || f[0] >= TEAM_NUM_TEAMS) {
        G_Printf(S_COLOR_YELLOW "G_ReadSessionData: client %i has an out-of-range team (%i), "
                 "keeping current session\n", (int)(client - level.clients), f[0]);
        return;
    }

    client->sess.spectatorTime   = f[1];
    client->sess.spectatorClient = f[3];
    client->sess.weaponPrimary   = f[4];
    client->sess.wins            = f[5];
    client->sess.losses          = f[6];
    client->sess.privileges      = f[8];
    client->sess.specOnly        = f[9];
    client->sess.playQueue       = f[10];
    client->sess.muted           = f[11];
    client->sess.prevScore       = f[12];

    /*
    [QL] force spectator on reconnect only when g_teamSpawnAsSpec is set,
    this is a team game and warmup is running (binary reads the cvar's
    integer directly, not level.newSession)

    Never for bots. This is what filled a 16-slot Freeze Tag server with bots and
    then refused every human connection.

    G_ReadSessionData runs from ClientConnect immediately after
    G_InitSessionData, so this line overwrites the team PickTeam has just chosen.
    For a human that is the point - you come back during warmup and pick your own
    side. A bot has no menu to pick from, so it simply stays a spectator forever,
    and G_CheckMinimumPlayers counts per team, so a spectator bot counts toward
    neither. The shortfall never closes, the filler adds another bot every second,
    and it keeps going until all sixteen slots are bots - five of them spectators
    in the reported table - at which point a human trying to connect finds the
    server full and the refusal spams the console.

    level.warmupTime is -1 while the server idles pre-game, which is nonzero, so
    this was live the entire time the server sat waiting for players.

    G_AddBot sets SVF_BOT on the entity before calling ClientConnect, so the flag
    is already there by the time this runs.
    */
    if (g_teamSpawnAsSpec.integer && g_gametype.integer >= GT_TEAM && level.warmupTime &&
        !(g_entities[client - level.clients].r.svFlags & SVF_BOT)) {
        f[0] = TEAM_SPECTATOR;
    }

    client->sess.sessionTeam = (team_t)f[0];
    client->sess.spectatorState = (spectatorState_t)f[2];
    client->sess.teamLeader = (qboolean)f[7];
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
    //
    // [QL] Quake Live puts every connecting player into spectator and makes
    // them pick JOIN MATCH from the menu. Quake 3 dropped you straight into
    // the game, and g_autoJoin (default on) restores that: connect, spawn,
    // play. Set g_autoJoin 0 for the Quake Live behaviour.
    //
    // Duel is deliberately exempt from the blanket case - it runs on a play
    // queue, so a third player joining has to wait rather than barge in. The
    // "fewer than two in the game" test is Quake 3's own tournament rule and
    // matches what G_ClientCmd's join path already enforces (g_cmds.c).
    if (g_gametype.integer >= GT_TEAM) {
        if (g_teamAutoJoin.integer || g_autoJoin.integer) {
            sess->sessionTeam = PickTeam(-1);
            BroadcastTeamChange(client, -1);
        } else {
            sess->sessionTeam = TEAM_SPECTATOR;
        }
    } else if (!g_autoJoin.integer) {
        sess->sessionTeam = TEAM_SPECTATOR;
    } else if (g_gametype.integer == GT_DUEL) {
        sess->sessionTeam = (level.numNonSpectatorClients >= 2) ? TEAM_SPECTATOR : TEAM_FREE;
    } else if (g_maxGameClients.integer > 0 && level.numNonSpectatorClients >= g_maxGameClients.integer) {
        sess->sessionTeam = TEAM_SPECTATOR;
    } else {
        sess->sessionTeam = TEAM_FREE;
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
