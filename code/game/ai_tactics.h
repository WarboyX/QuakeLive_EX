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

/*****************************************************************************
 * name:		ai_tactics.h
 *
 * desc:		[QL] tactical layer over the Quake 3 bot AI
 *
 * The stock AI decides everything from one bot's own inventory. It has no idea
 * how many people are in the room, it holds one strafe direction until a
 * random() clears 0.935, it fights every weapon at the same 140 units, and it
 * flips between fight and retreat on whichever side of 50 BotAggression landed
 * this frame. This layer supplies the missing context and nothing else - every
 * entry point returns the stock answer when bot_tactics is 0.
 *
 *****************************************************************************/

/* [QL] CTF roles - see BotCTFPickRole. Ordered so the array in that function can
   be indexed by them directly. */
#define CTFROLE_ATTACK 0  // go for the enemy flag
#define CTFROLE_DEFEND 1  // hold our own
#define CTFROLE_ESCORT 2  // stay with whoever is carrying theirs
#define CTFROLE_ROAM 3    // fight for the middle, pick things up
#define CTFROLE_COUNT 4

#define TACTIC_FALLBACK 0  // outnumbered here and now
#define TACTIC_EVEN 1      // no reason to think either way
#define TACTIC_PUSH 2      // enough friends nearby to press

/*
bot_tactics_t itself lives in ai_main.h, next to the rest of bot_state_t, because
that is where every other per-bot field is declared and splitting the struct
across two headers would make the include order load-bearing.
*/

struct bot_state_s;

// recompute the tactical picture; called once per bot think
void BotTacticsUpdate(struct bot_state_s* bs);
// reset the picture, for a bot that just spawned or entered the game
void BotTacticsReset(struct bot_state_s* bs);
// TACTIC_*, or TACTIC_EVEN when the layer is off
int BotPosture(struct bot_state_s* bs);
// preferred engagement distance and tolerance for the weapon in hand
void BotIdealAttackRange(struct bot_state_s* bs, float* dist, float* range);
// qtrue and a horizontal direction when something is about to hit the bot
int BotDodgeDirection(struct bot_state_s* bs, vec3_t dir);
// qtrue when the enemy has been tracking the bot with a hitscan weapon
int BotEnemyTrackingMe(struct bot_state_s* bs);
// qtrue when this item goal is worth breaking off a fight for
int BotWantsItemGoal(struct bot_state_s* bs, bot_goal_t* goal);
// scale a nearby-goal search range by what the bot is short of
float BotItemSearchRange(struct bot_state_s* bs, float range);
// a goal on the nearest ally, to fall back towards rather than away
int BotRegroupGoal(struct bot_state_s* bs, bot_goal_t* goal);
// which job this bot should take in CTF, given what the rest of the team is doing
int BotCTFPickRole(struct bot_state_s* bs);
// qtrue when the team already has as many of this CTF role as the mix wants
int BotCTFRoleCrowded(struct bot_state_s* bs, int role);
// take a defensive posting when the team has nobody holding the base
int BotAutoDefendGoal(struct bot_state_s* bs);
// qtrue when there is somewhere to go that way - not blocked, not off an edge
int BotRoomToMove(struct bot_state_s* bs, vec3_t dir, float dist);
// the "bots" console command: every bot, every gametype, with its posture
void BotTacticsReport(void);
