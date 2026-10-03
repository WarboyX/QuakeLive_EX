# Lua modding — Garry's Mod–style scope (R30)

Asked for: mod support "in a similar way to Garry's Mod: models, scripts, game
logic, etc." This document is the deep scope. `TRACKER.md` R30 points here.

Effort is given in **test rounds** (a build you run and report on), not time.
Writing the code is not what limits progress. What limits it is how often something has to
be seen on a real client, a real monitor, or a stock Steam client.

**Status: parked.** This stays a scope here. If it is built, it is built as a
separate client, **QLEX-CE** (Quake Live EX — Custom Edition), in its own repo.

What the split changes:
- **QLEX keeps its promise.** This repo stays a drop-in for stock Quake Live:
  protocol 91, stock clients welcome, nothing here depends on Lua.
- **CE is free of the protocol-91 ceilings (§4).** It can extend
  `entityState_t`, raise the entity and model limits, and add `svc_` ops
  outright. The handshake-gated extension in §5.4 phase 2 becomes optional.
  The cost is that CE servers and CE clients only talk to each other unless CE
  keeps a stock-compatible mode. Decide that when the repo starts (it replaces
  D1).
- **Tiers A/B could still come back here.** Server-only Lua (L1–L4) is useful
  on ordinary QLEX servers and stock-safe. Whether it lives in QLEX, CE, or
  both is a choice for later.
- **Keeping the two in step.** CE forks from QLEX and takes QLEX's renderer,
  menu and fix work by regular merges. CE-only code should stay in new files
  and behind clearly marked hooks, so those merges stay cheap.

---

## 1. The lineage, and where it stops helping

The lineage is real, though slightly different from "HL2 ← GoldSrc ← Quake 2":
GoldSrc grew out of the **Quake 1** engine (with some Quake 2 code folded in),
and Source grew out of GoldSrc. Quake 3, our base, is Quake 2's successor.

What the lineage gives us — the same shape as Source, already in the tree:

| Concept | Source / GMod | Ours |
|---|---|---|
| Entities with classnames spawned from map keys | `ents.Create`, SENTs | `g_spawn.c` spawn table (53 classnames), `gentity_t` |
| Per-entity callbacks | `ENT:Think/Touch/Use/OnTakeDamage` | `think`, `framethink`, `touch`, `use`, `pain`, `die` function pointers in `gentity_t` |
| Server / client split | SERVER / CLIENT realms | `qagame` / `cgame` |
| Shared predicted code | shared realm, `SetupMove` | `bg_pmove.c`, `bg_misc.c` compiled into both |
| Networked strings | string tables | configstrings (1024 slots, 716 used) |
| Server→client messages | `net` library | reliable server commands (`trap_SendServerCommand`) |
| Model formats | MDL | MD3, MDR, **IQM** (skeletal, Blender exporter) — all three already load in renderervk |

So the *architecture* will feel familiar to anyone who has written a GMod
addon. What GMod actually leans on, though, is what **Source added** on top of
that lineage, and that is exactly what we do not have:

1. **Rigid-body physics** (VPhysics, Havok). Quake 3 has trajectories and
   bounce, nothing more. Props, the physgun, welds and ragdolls all come from
   this.
2. **Client code sent by the server.** GMod's `AddCSLuaFile` ships Lua to every
   client. Ours would only ever reach *our* client — stock Steam Quake Live will
   never run it.
3. **An extensible network format.** Source entity classes declare their own
   networked variables. We are locked to protocol 91 so stock clients can
   connect: `entityState_t` is a fixed set of fields, `modelindex` is 8 bits,
   `eType` is 8 bits.
4. **A UI toolkit scripts can build with** (VGUI/Derma). Ours is the `.menu` script
   system, built for fixed menus, not for scripts building panels at runtime.

The rest of this scope is mostly about those four.

---

## 2. The decision that shapes everything: stock clients

Every feature lands in one of three tiers, and the tier is the CLAUDE.md
"Seen by" question asked up front:

| Tier | What the mod does | Seen by | Example |
|---|---|---|---|
| **A — server logic** | Lua in qagame changing rules, scores, spawning, damage, votes, chat | every client, stock included | a gamemode, an admin plugin, a custom round system |
| **B — server logic + content** | A plus new models, sounds and textures, carried on entities stock cgame already draws (`ET_GENERAL`, movers, missiles) | every client that has the files | props, custom pickups, moving set pieces, physics objects driven by the server |
| **C — client logic** | Lua in cgame: HUD, custom drawing, effects, predicted weapons, predicted movement, menus | **our client only** | a custom HUD, a new weapon with its own viewmodel and prediction, a spawn menu |

GMod is mostly tier C. Tier A and B are worth a great deal on their own and are
safe for the existing player base; tier C is what makes it *Garry's Mod* rather
than *a scriptable Quake Live server*.

**Key finding for tier B:** stock cgame's `CG_General` draws any
`ET_GENERAL` entity with whatever model `modelindex` names, and
`TR_INTERPOLATE` lets the server move it freely each snapshot. That means a
server-simulated physics prop (§5.7) is visible and correctly placed on a stock
client with no client code at all — it just needs the model file.

**What a stock client does on a server that uses tier C** is a per-server
choice (decision D1): let them in with the tier A/B view (no custom HUD, custom
weapons look like their base weapon), or refuse them with a message naming
our client.

---

## 3. Shape of the system

### 3.1 Runtime

**Lua 5.4** (MIT), vendored in `code/lua/`, compiled into both `qagame` and
`cgame`, so one language version in both realms.

GMod uses LuaJIT. LuaJIT is faster, but for **code downloaded from a server
onto a player's machine** it is the worse choice: its FFI has to be removed,
and its instruction-count hook does not fire inside JIT-compiled traces, so a
runaway loop in hostile code cannot be reliably stopped. 5.4's hook always fires. Game
logic at Quake's scale (≤1024 entities, 40 Hz server) is well within 5.4's
reach; anything heavy stays in C and is exposed as a function. (Decision D3.)

### 3.2 Addons

Modelled directly on GMod's layout so its documentation and habits transfer:

```
baseq3/addons/<name>.pk3          (or a loose folder while developing)
  addon.txt                       name, version, author, realms, base gametype
  lua/autorun/                    shared, runs in both realms
  lua/autorun/server/             server only
  lua/autorun/client/             client only (tier C)
  lua/entities/<class>/           init.lua (server), cl_init.lua (client), shared.lua
  lua/weapons/<class>/            same split
  lua/gamemodes/<name>/           gamemode, may derive from another
  models/ sound/ textures/ scripts/   ordinary Quake content
```

- **Loading:** at map start from a `lua_addons` server cvar.
- **Reloading:** `lua_restart` reloads without a map change. Optionally, changes to loose
  files are reloaded on save, like GMod's auto-refresh.
- **Errors:** every hook call is wrapped. An error prints file:line and a
  traceback to the console and disables *that hook*, never the server.
- **Profiling:** a `lua_timings` view in the same style as `r_rtTimings`.

### 3.3 Realms

- **Server Lua** — runs in `qagame`. Owns all game state. Tier A and B.
- **Client Lua** — runs in `cgame` of our client. Draws, predicts, builds UI.
  Gets its files from the server's addons (§5.3), so it is untrusted code
  (§6).
- **Shared** — files that run in both. This is how a weapon or movement change
  is predicted correctly: the same Lua runs in `qagame` and in our `cgame`'s
  prediction, exactly as `bg_pmove.c` is compiled into both today.

---

## 4. Hard limits from protocol 91

None of these can be raised without dropping stock clients. Each one has a
way around it for our clients (§5.4) and a ceiling for theirs.

| Limit | Value | Bites when |
|---|---|---|
| `MAX_GENTITIES` | 1024 (10 bits) | prop-heavy sandbox maps; GMod servers routinely exceed this |
| `MAX_MODELS` / `modelindex` | 256 / 8 bits on the wire | map + items + player models + addon models share one table |
| `MAX_SOUNDS` | 256 | as above |
| `MAX_CONFIGSTRINGS` | 1024, 716 used | ~300 free for networked globals |
| Weapons | `MAX_WEAPONS` 16, `STAT_WEAPONS` a 16-bit mask, 15 used | **one** free weapon slot for stock clients |
| `eType` | 8 bits, 13 types + events | no new entity *types* stock cgame understands |
| Entity fields | fixed `entityState_t` | no per-class networked variables |
| Reliable commands | 64-slot buffer, 1024-char strings | a chatty `net` library will overflow it |
| Server→client ops | fixed `svc_*` set | a new op crashes a stock client's parser |

Clip models are axis-aligned boxes unless the entity is a brush model, so a
rotated prop collides with players as its unrotated box (§5.7).

---

## 5. Subsystems

### 5.1 Server API and hooks (tier A)

The previous R30 scope, now the first stage of this one. `hook.Add`-style
registration with GMod event names where the event matches, minqlx names for
admin functions where they line up, so ports from either are translations.

- **Lifecycle:** `Initialize`, `Think` (per frame), `ShutDown`, map change.
- **Players:** `PlayerConnect`, `PlayerInitialSpawn`, `PlayerSpawn`,
  `PlayerDisconnected`, `PlayerSay`, `PlayerDeath`, `EntityTakeDamage`,
  `PlayerCanPickupItem`, `PlayerChangedTeam`, `PlayerUserInfoChanged`.
- **Commands:** `concommand.Add` for client and server commands, `!chat`
  commands, votes.
- **Quake Live events:** round start and end (CA, FT, AD, RR), capture,
  obituary, match state, warmup, timeout.
- **Functions:** cvars (with the CLAUDE.md archive rule enforced: scripts may
  not set `CVAR_ARCHIVE` on values they choose), print and centre-print, read
  and write player state, kick, team change, trace, find entities in a box or sphere.

Our server code calls these hooks from the C functions that already handle
those events (`ClientConnect`, `G_Damage`, `player_die`, the `g_gametype_*.c`
round machines). The work is wiring, not redesign.

### 5.2 Scripted entities and gamemodes (tier A/B)

**Entities (SENT).**
- `lua/entities/<class>/` defines a classname. Maps can then place it from the
  editor like any built-in entity, because `G_CallSpawn` falls through to Lua
  where it now prints "doesn't have a spawn function".
- Lua tables are backed by real `gentity_t` slots, so the 1024 limit is
  shared with everything else.
- Callbacks map one-to-one onto the existing function pointers.
- Entity properties map onto `entityState_t` fields: model, skin, frame, trajectory,
  `constantLight`, `loopSound`, events. Stock clients see exactly what they
  would see from a built-in entity.

**Gamemodes.**
- A gamemode **declares a base gametype** (FFA, TDM, CA, CTF, …). That is
  what `g_gametype` reports, so stock clients' HUD and scoreboard behave
  sensibly.
- The gamemode overrides scoring, spawning, rounds, win conditions and
  loadouts on top of that base.
- Gamemodes may derive from each other (GMod's `DeriveGamemode`). Our existing
  C gametypes become the bases they derive from.

### 5.3 Content distribution

**Ours to build:**
- **Packaging.** Addons are pk3s. The server lists what it runs. Pure-server checks
  (`sv_pure`, already present) cover Lua files too, so a client cannot swap in
  a modified client script.
- **Our client downloads by HTTP** from `sv_dlURL`. The curl download path
  exists (`cl_curl.c`, `cl_parse.c` reads `sv_dlURL`). Needs: a size and progress prompt, a per-server cache
  folder, and keeping downloaded pk3s out of the global search path once you
  leave that server, so one server's addons never leak into another.

**Stock clients get content from the Workshop.**
- `CS_STEAM_WORKSHOP_IDS` (715) already exists for this.
- Stock QL subscribes and downloads through Steam.

**Gap:**
- Our client loads Workshop folders that are already on disk
  (`FS_AddWorkshopPaks`), but nothing in our client reads
  `CS_STEAM_WORKSHOP_IDS` or asks Steam for a missing item.
- Closing that needs the Steamworks UGC API in our client, the same
  library question as the stubbed `SV_ValidateSteamAuth`. Until then,
  HTTP covers our client and the Workshop covers stock clients.

### 5.4 Networking (tier C)

GMod's `net.Start/net.Send` and networked variables (`SetNW*`, `NetworkVar`).

- **Handshake.**
  - Our client advertises a capability key in userinfo (e.g. `qlex` with a
    protocol extension number).
  - The server only ever sends extension traffic to clients that sent it, and
    stock clients never see it.
- **Phase 1 — over existing channels:**
  - `net` messages ride reliable server commands with a reserved prefix.
  - Networked globals use the ~300 free configstrings.
  - Works today, and needs no engine change.
  - Bounded by the 64-command buffer, so rate-limited and batched per frame.
- **Phase 2 — a real extension:**
  - A new `svc_` op, sent only to capable clients.
  - Carries per-entity networked variables, delta-compressed per snapshot.
  - Raises the entity and model ceilings for our clients, e.g. a 16-bit model
    index in the extension block, with the stock `modelindex` set to a
    fallback model.
  - This is where GMod-scale prop counts become possible for our client while
    stock clients see a capped subset.
  - Needs engine changes in `msg.c`, `sv_snapshot.c` and `cl_parse.c`, with
    the stock-client path untouched and tested both ways.

### 5.5 Client API (tier C)

Exposed from the cgame traps that already exist (98 of them):

- **2D drawing:** `HUDPaint`, with the existing traps behind it (`DrawStretchPic`,
  `Font_DrawString`, `SetColor`).
- **3D scene:** `ENT:Draw` (add a model, poly, light or ripple to the scene),
  and `PostDrawOpaque`, which uses the ref-entity and poly traps
  (`AddRefEntityToScene`, `AddPolysToScene`, `AddLightToScene`).
- **Assets and sound:** model and shader registration, `LerpTag` for attachment points, and
  sounds.
- **Input:** key and mouse hooks; a player's own binds are never overridden.
- **Hiding stock HUD elements:** GMod's `HUDShouldDraw`. Our HUD is drawn by
  C in `cg_draw.c`, so each element needs a named gate.
- **Our own render features:** RT shadows, AO and water are reachable only
  through cvars a script may *read*, not set (§6).

### 5.6 Weapons (SWEP) and movement

**Server-only weapons (tier A/B).**
- Fire logic, damage and projectiles are written in Lua, and every client sees the effects.
- Stock clients see the weapon as a **declared base weapon**: its model, its
  muzzle flash, its slot.
- Fire-rate or ammo behaviour that `bg_pmove`'s `PM_Weapon` predicts will
  mispredict on stock clients, and on ours too unless the weapon is shared.

**Shared weapons (tier C).**
- The weapon's think runs in both realms, as `PM_Weapon` does, so our
  client predicts it exactly.
- The client side adds the viewmodel, crosshair, HUD and effects.

**The slot problem:**
- The protocol has 16 weapon slots and 15 are taken, so stock clients have one free.
- Lua weapons therefore **multiplex**: the server keeps the real
  inventory, and each Lua weapon borrows a base slot for the wire.
- Our client receives the true weapon id through §5.4. Stock clients see the
  base weapon.
- Design consequence: on a server mixing stock and our clients, two Lua
  weapons sharing a base slot are indistinguishable to stock players.

**Movement:**
- Movement hooks (`SetupMove`/`Move`) must be shared Lua, and run inside
  `bg_pmove` on both sides.
- This is the shotgun trap from CLAUDE.md in its purest form: a movement
  script on a server with stock clients mispredicts for every one of them.
- The API marks movement hooks **our-client-only**, and the server refuses to
  load them while `sv_luaStockClients` (D1) admits stock clients.

### 5.7 Physics

Nothing exists; this is the largest single piece and the one most tied to
"feels like Garry's Mod".

- **Library: Jolt Physics** (MIT, C++17).
  - Built into `qagame` on both platforms; mingw g++ handles it.
  - Bullet is the older alternative. Jolt is faster and more actively
    maintained.
- **World collision.**
  - World collision comes from the BSP: brushes as convex hulls, patches as
    triangle meshes, built at map load.
  - The same walk over level geometry that the light field's BVH already does
    on the client.
- **Props.**
  - Each prop is a `gentity_t` with `TR_INTERPOLATE` position and angles, set
    from Jolt every server frame.
  - **Seen correctly by stock clients**, as tier B.
  - Their interpolation is snapshot-to-snapshot, so fast props look as smooth
    as other players do, not smoother.
- **Players versus props.**
  - Quake clip models are axis-aligned boxes, so a player walks against a
    prop's unrotated bounding box.
  - Acceptable for crates; wrong for a long tilted plank.
  - The fix for our client is to let `pmove` trace against Jolt shapes. That
    is server-side for the server's movement, and client-side only with the
    shape data sent through §5.4.
  - Stock clients mispredict against tilted props and snap to the server's
    answer.
- **Ragdolls.**
  - Need a skeleton: **IQM** player models (or a fitted capsule chain for
    MD3s).
  - Jolt drives the bones, and the result is sent as a pose.
  - Our client only. Stock clients keep the normal death animation.
- **GMod's tools**: physgun, toolgun, weld, rope, thrusters.
  - Lua on top of Jolt constraints, once the above exists.
  - They are part of the Sandbox gamemode (§5.9), not the engine.

### 5.8 UI (tier C)

GMod's Derma: scripts build panels at runtime.

- Our `.menu` system is static and not suitable.
- Build a small retained-mode toolkit in **client Lua itself**, on
  `HUDPaint`-style drawing plus mouse and keyboard input from cgame: frame,
  button, label, text entry, list, scroll, slider, checkbox, tabs, model
  preview.
- About a dozen widgets cover a spawn menu and most addon settings screens.
- It runs in cgame, so it works in-game. Main-menu addon screens would need
  the same in `ui`, which is a later, separate step.

### 5.9 The Sandbox gamemode

What players picture when they say "Garry's Mod": the spawn menu, props,
physgun, toolgun, undo, prop limits per player, and saves. It is an addon, the
first serious consumer of everything above, and the proof the API is
complete. It needs §5.1–5.8.

### 5.10 Bots

botlib's AAS navigation does not know about props or Lua entities. Phase 1
gives Lua the bot hooks Quake Live already has (goals, chat). Bots that
navigate around physics props are out of scope.

---

## 6. Security

Tier C means **code from any server runs on a player's machine**. GMod has had
real exploits through this path. The sandbox is not optional and is the first
thing built in the client realm, before any API is exposed.

- **Libraries removed:** `os`, `io`, `package.loadlib`, `require` of C modules,
  `debug` (except `traceback`), `load` of bytecode (text only), `collectgarbage`
  options that change the collector.
- **Files:**
  - Reads are only through the engine filesystem, inside the server's addons.
  - Writes are only to a per-server `data/<server>/` folder, size-capped.
- **No network** of any kind from client Lua.
- **Commands:**
  - No raw console command execution.
  - Cvars are read-only except a script's own `lua_`-prefixed ones.
  - No `bind`, `exec`, `connect`, `rcon`, `writeconfig`, `quit` or `cl_`
    changes, and nothing archived.
- **Limits:**
  - An instruction budget per hook call.
  - A memory cap per Lua state.
  - Exceeding either kills the client Lua state and tells the player, and the
    game keeps running.
- **Isolation:** client Lua state is created per server connection and destroyed on
  disconnect. Nothing persists between servers except the size-capped data
  folder.
- **Consent:** player control over whether downloaded client Lua runs at all
  (D2).

Server Lua is trusted the way a server config is: the admin installed it.
It still runs without `os.execute`, `io.popen` and `package.loadlib`, so a
downloaded addon cannot run programs on the server host either.

---

## 7. Stages and test rounds

Each stage is usable on its own and ships behind cvars that default to off
until it is proven.

| # | Stage | Tier | Builds for you to test | Seen by |
|---|---|---|---|---|
| L1 | Runtime, sandbox, addon loader, `lua_restart`, errors, timings; server hooks and admin API (§5.1) | A | 2 | every client |
| L2 | Scripted entities: map classnames, spawn from Lua, callbacks, models, sounds, trajectories (§5.2) | A/B | 2–3 | every client with the files |
| L3 | Gamemodes with base gametype and derivation (§5.2) | A | 2 | every client |
| L4 | Addon packaging, pure checks, HTTP download with per-server cache; Workshop ID list for stock clients (§5.3) | B | 2 | every client |
| L5 | Client realm + sandbox, capability handshake, `net` over existing channels, networked globals (§5.4 phase 1, §6) | C | 3–4 | our client |
| L6 | Client hooks: `HUDPaint`, `HUDShouldDraw`, `ENT:Draw`, input (§5.5) | C | 2–3 | our client |
| L7 | Weapons: server-only, then shared and predicted, with slot multiplexing (§5.6) | B/C | 3–4 | stock: base weapon; ours: full |
| L8 | Physics: Jolt, BSP collision, props (§5.7) | B | 3–5 | every client |
| L9 | Lua UI toolkit (§5.8) | C | 2–3 | our client |
| L10 | Protocol extension: per-entity networked vars, raised entity/model ceilings for our client (§5.4 phase 2) | C | 3–4 | our client; stock path regression-tested |
| L11 | Ragdolls on IQM, player-vs-prop prediction (§5.7) | C | 3+ | our client |
| L12 | Sandbox gamemode: spawn menu, physgun, toolgun, undo, limits (§5.9) | B/C | 4+ | mixed |

L1–L4 is a fully useful **scriptable server** that every Quake Live player
can join. L5–L9 makes it **GMod-like for our client**. L10–L12 is the
**full Garry's Mod experience**.

**How each stage is tested:**
- **In the harness:**
  - A dedicated server plus bots, the setup the sanitizer run already uses.
  - One example addon per stage.
- **Needs you:**
  - Anything tier C, physics feel, and every stock-client check. The harness
    has no Steam client.

---

## 8. Decisions needed

**D1 — stock clients on a server running client Lua.**
- Options: admit them with the tier A/B view, or refuse with a message.
- Suggested: a server cvar (`sv_luaStockClients`, default admit), so it is the
  admin's choice.

**D2 — downloaded client Lua on the player's side.**
- Options: GMod-style automatic, ask once per server, or never.
- Suggested: a player cvar with all three values, defaulting to **ask once per
  server**.
- The sandbox makes automatic defensible. Asking makes the first time
  visible.

**D3 — runtime.**
- Lua 5.4 (suggested, for the sandbox guarantees in §3.1) or LuaJIT (faster,
  matches GMod, weaker runaway protection).

**D4 — GMod syntax extensions.**
- Garry's Lua accepts `!=`, `&&`, `||`, `!`, `//` and `/* */` comments, and
  `continue`.
- Supporting them means patching the Lua parser.
- It would let GMod-written code *parse*, but GMod's API is Source-specific, so
  addons still need porting.
- Suggested: **no**. Revisit if porting turns out to be common.

**D5 — physics library.**
- Jolt (suggested) or Bullet.

**D6 — where to start.**
- L1 as the first build, unless you want the client realm proven earlier, in
  which case a thin L5 (sandbox + `HUDPaint` only) can go right after L1.
