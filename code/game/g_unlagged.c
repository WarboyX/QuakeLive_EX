/*
 * g_unlagged.c - hitscan lag compensation (the HAX_* system)
 *
 * QL rewinds every other player to where the firer saw them, around a
 * hitscan trace, so shots register against the target's rendered position rather
 * than its current one. Verified against qagamex86.dll (build 1069): FireWeapon
 * (0x1006f280) brackets the fire with HAX_Begin before and HAX_End after, for the
 * hitscan weapons (mask 0x60cc = MG, SG, LG, RG, CG, HMG), gated on g_lagHaxMs and
 * g_lagHaxHistory.
 *
 * Each client keeps a small ring of g_lagHaxHistory snapshots (default 4) of its
 * origin and bbox, pushed once per frame from ClientEndFrame. On a fire the ring
 * is interpolated to the firer's command time; g_lagHaxMs (default 80) caps how
 * far back the rewind can reach.
 *
 * Names are from qagamex64.so.symbols. The qagamex86.dll Ghidra project had these
 * mislabelled as Item_*SpecTimer / Item_*SpecPositions (a separate, unrelated
 * Item_InitSpecTimers exists in the symbols); trust the HAX_* symbol names.
 */
#include "g_local.h"

// Address: 0x100516d0 (HAX_Init), 0x100517c0 (HAX_Update), 0x100519e0 (HAX_Begin),
//          0x10051ac0 (HAX_End). The interpolation helper below is a separate
//          function at 0x10051850 in the DLL, inlined into HAX_Begin in the .so.

// One recorded position in a client's history ring. QL allocates these as a
// circular doubly-linked list (48 bytes each) via G_Alloc in HAX_Init.
typedef struct haxSnap_s {
    struct haxSnap_s* next;
    struct haxSnap_s* prev;
    vec3_t origin;              // s.pos.trBase at 'time'
    vec3_t mins;
    vec3_t maxs;
    int time;                   // level.time of this snapshot, 0 = empty slot
} haxSnap_t;

// The live position we saved before rewinding a client, so HAX_End can put it back.
typedef struct {
    vec3_t origin;
    vec3_t mins;
    vec3_t maxs;
    int time;                   // level.time when rewound this fire, else 0
} haxSaved_t;

static haxSnap_t* haxHistory[MAX_CLIENTS];   // ring head per client (DAT_10092ac8)
static haxSaved_t haxSaved[MAX_CLIENTS];      // saved live pos during a rewind (DAT_10091ed0)

// HAX_Init - allocate each client's snapshot ring. Called from G_InitGame when
// g_lagHaxMs and g_lagHaxHistory are both set.
void HAX_Init(void) {
    int i, j, count;
    haxSnap_t* ring;

    count = g_lagHaxHistory.integer;
    if (count < 1) {
        return;
    }
    for (i = 0; i < level.maxclients; i++) {
        ring = G_Alloc(count * sizeof(haxSnap_t));
        haxHistory[i] = ring;
        for (j = 0; j < count; j++) {
            ring[j].next = &ring[(j + 1) % count];
            ring[j].prev = &ring[(j + count - 1) % count];
            ring[j].time = 0;
        }
    }
}

// HAX_Update - push this client's current position into its ring. One step per
// frame from ClientEndFrame. Non-player entities push an empty slot.
void HAX_Update(gentity_t* ent) {
    haxSnap_t* slot;

    slot = haxHistory[ent->s.number];
    if (!slot) {
        return;
    }
    slot = slot->next;
    if (ent->s.eType == ET_PLAYER) {
        slot->time = level.time;
        VectorCopy(ent->s.pos.trBase, slot->origin);
        VectorCopy(ent->r.mins, slot->mins);
        VectorCopy(ent->r.maxs, slot->maxs);
    } else {
        slot->time = 0;
    }
    haxHistory[ent->s.number] = slot;
}

// HAX_Clear - invalidate the head slot so a respawn or disconnect doesn't leave a
// stale position for the rewind to interpolate against. QL does this inline in
// ClientSpawn / ClientDisconnect (haxHistory[clientNum]->time = 0).
void HAX_Clear(gentity_t* ent) {
    if (haxHistory[ent->s.number]) {
        haxHistory[ent->s.number]->time = 0;
    }
}

// HAX_Replay - move ent back to where it was at 'time' by interpolating the two
// ring snapshots that bracket it, then relink so the trace sees it there. The bbox
// is taken from the newer snapshot. Bails if there is no valid history. Separate
// helper at 0x10051850 in the DLL; the .so inlines it into HAX_Begin.
static void HAX_Replay(gentity_t* ent, int time) {
    haxSnap_t* cur;
    haxSnap_t* older;
    haxSnap_t* newer;
    float frac;
    vec3_t diff;

    cur = haxHistory[ent->s.number];
    if (!cur || time > cur->time) {
        return;                     // nothing newer than 'time' to rewind from
    }
    older = cur;
    newer = cur;
    while (older->time > time) {
        haxSnap_t* prev = older->prev;
        newer = older;
        if (prev->time >= older->time) {
            break;                  // wrapped past the oldest snapshot, clamp here
        }
        older = prev;
    }
    if (older->time == 0) {
        return;                     // ran into an empty slot, not enough history
    }
    trap_UnlinkEntity(ent);
    if (older == newer || older->time == newer->time) {
        VectorCopy(older->origin, ent->r.currentOrigin);
    } else {
        // origin = older + frac * (older - newer), frac spanning older..newer
        frac = (float)(time - older->time) / (float)(older->time - newer->time);
        VectorSubtract(older->origin, newer->origin, diff);
        VectorMA(older->origin, frac, diff, ent->r.currentOrigin);
    }
    VectorCopy(newer->mins, ent->r.mins);
    VectorCopy(newer->maxs, ent->r.maxs);
    trap_LinkEntity(ent);
}

// HAX_Begin - rewind every other live client to 'startTime' (the firer's command
// time), saving their current position first. The firer's own shots aren't
// compensated if it's a bot. startTime is clamped so we never rewind further back
// than g_lagHaxMs. Paired with HAX_End.
void HAX_Begin(gentity_t* shooter, int startTime) {
    int i;
    gentity_t* ent;

    if (shooter->r.svFlags & SVF_BOT) {
        return;
    }
    if (startTime < level.time - g_lagHaxMs.integer) {
        startTime = level.time - g_lagHaxMs.integer;
    }
    for (i = 0; i < level.maxclients; i++) {
        ent = &g_entities[i];
        haxSaved[i].time = 0;
        // [QL] binary (0x100519e0) gates on inuse + pers.connected==CON_CONNECTED, ent!=shooter,
        // and ps.pm_type != PM_SPECTATOR (client+4 != 2), NOT s.eType != ET_ITEM. The live
        // position saved for restore is r.currentOrigin (0x220), NOT s.pos.trBase.
        if (ent->client && ent->client->pers.connected == CON_CONNECTED && ent != shooter &&
            ent->client->ps.pm_type != PM_SPECTATOR) {
            VectorCopy(ent->r.currentOrigin, haxSaved[i].origin);
            VectorCopy(ent->r.mins, haxSaved[i].mins);
            VectorCopy(ent->r.maxs, haxSaved[i].maxs);
            haxSaved[i].time = level.time;
            HAX_Replay(ent, startTime);
        }
    }
}

// HAX_End - put every rewound client back to its live position and relink.
// haxSaved[i].time == level.time marks who was moved this fire.
void HAX_End(gentity_t* shooter) {
    int i;
    gentity_t* ent;

    (void)shooter;
    for (i = 0; i < level.maxclients; i++) {
        if (haxSaved[i].time == level.time) {
            ent = &g_entities[i];
            VectorCopy(haxSaved[i].origin, ent->r.currentOrigin);
            VectorCopy(haxSaved[i].mins, ent->r.mins);
            VectorCopy(haxSaved[i].maxs, ent->r.maxs);
            trap_LinkEntity(ent);
        }
    }
}
