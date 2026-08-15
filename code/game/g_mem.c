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
//
// g_mem.c
//

#include "g_local.h"

/*
[QL] Raised from Quake 3's 256KB.
                       
This pool has no free - G_InitMemory just resets the offset at map load - so it
has to hold everything the level ever asks for at once: every entity key and
value string on the map (G_NewString), the arena list, the bot list, and the
unlagged history rings. 256KB was a 1999 number for 1999 maps and a 32-player
ceiling.

What made it visible was bots. A bot_state_t is 9032 bytes and used to come from
here, so past twenty-five bots the pool ran out and the server died mid-match
with "G_Alloc: failed on allocation of 9032 bytes". Those now live in their own
static array (ai_main.c), which is the bigger half of the fix; this is the other
half, because the ceiling was still map-dependent - a level with more entity
strings left less room for everything else, and nothing said so.

1MB of BSS in the game module. The failure also now reports what was in use, so
the next time this is reached it says so instead of naming one allocation.
*/
#define POOLSIZE (1024 * 1024)

static char memoryPool[POOLSIZE];
static int allocPoint;

void* G_Alloc(int size) {
    char* p;

    if (g_debugAlloc.integer) {
        G_Printf("G_Alloc of %i bytes (%i left)\n", size, POOLSIZE - allocPoint - ((size + 31) & ~31));
    }

    if (allocPoint + size > POOLSIZE) {
        // The build date is in here deliberately. This message is the one that
        // reaches a bug report, and the first question it has to answer is
        // "is the reporter running the build that changed this" - the previous
        // round was spent on a crash that had already been fixed in the build
        // that had not been installed yet.
        G_Error("G_Alloc: failed on allocation of %i bytes (%i of %i already in use, qagame built %s %s)",
                size, allocPoint, POOLSIZE, __DATE__, __TIME__);
        return NULL;
    }

    p = &memoryPool[allocPoint];

    allocPoint += (size + 31) & ~31;

    return p;
}

void G_InitMemory(void) {
    allocPoint = 0;
}

void Svcmd_GameMem_f(void) {
    G_Printf("Game memory status: %i out of %i bytes allocated (qagame built %s %s)\n", allocPoint, POOLSIZE,
             __DATE__, __TIME__);
}
