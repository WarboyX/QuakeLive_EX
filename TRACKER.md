# ioquakelive — issue and feature tracker

Working notes for the port. Open items first, resolved at the bottom so we do not
re-litigate settled ground.

Status key: **OPEN** · **IN PROGRESS** · **NEEDS INFO** · **BLOCKED** · **DONE**

---

## Progress

| Area | Progress | Notes |
|---|---|---|
| **Client / UI** (U) | `███████████████░░░░░  12/16` | U18 root-caused: missing commas, not the string pool |
| **Client / cgame** (C) | `████████████████████  8/8` | C11: deferred models never finished loading |
| **Renderer** (R) | `█████░░░░░░░░░░░░░░░  2/8` | Vulkan runs and draws text |
| **Weapons** (W) | `░░░░░░░░░░░░░░░░░░░░  0/4` | W1/W3 are vanilla-only — invisible in our client |
| **Engine / server** (E) | `██████░░░░░░░░░░░░░░  4/12` | E11: no score and no match start are one bug |
| **Overall** | `███████████░░░░░░░░░  27/49` | by binary: 11 server · 35 client · 3 both |

"DONE (verify)" counts as done — it means shipped and awaiting your confirmation,
not finished-and-proven.

## Where a fault lives, and who sees it

These are two different questions and they were collapsed into one tag at
first, which produced the wrong answer twice.

**Everything here is ours to fix.** There is no such thing as a bug we do not
own — we ship the client *and* the server binary. What varies is which of our
two binaries is at fault, and that is what decides where to go looking:

| Lives in | Meaning |
|---|---|
| **client** | our `cgame`, `ui`, or the client half of the engine |
| **server** | our `qagame`, or the server half of the engine |
| **client and server** | the fault spans both, or is in code shared between them |

Who *experiences* it is a separate question, and it is not implied by the first:

| Seen by | Meaning |
|---|---|
| our client only | the stock client does not run this code |
| every client | including anyone joining on stock Steam Quake Live |
| stock Steam Quake Live only | **correct on ours, broken for everyone else** |

That last row is the dangerous one, and it is not the same as "server side".
Plenty of server-side behaviour is seen by every client identically: the server
decides, everyone is told, everyone agrees. The trap is where the server sends
a **seed** and each client **regenerates the result locally** — that agrees only
while both ends run the same generator. Our cgame reads the relevant cvars from
serverinfo and follows the server; a stock client cannot be told about them and
keeps running stock code. Change the server and our client moves with it, both
halves stay consistent, nothing looks wrong — while a stock client draws
something that no longer matches the damage.

The shotgun is exactly this (W1, W3): a fault **in our server**, invisible **on
our client**, wrong for everybody else. Both facts have to be recorded or the
next person reads "server side" and assumes it can be tested here.

**Rule:** a server change to anything the client redraws from a seed is broken
for stock clients until proven otherwise.

---

### By binary — where to go and fix it

#### our server (qagame / server engine)

| | ID | Item | State |
|---|---|---|---|
| ○ | **W1** | Shotgun pattern shape is unverified | OPEN |
| ○ | **W2** | Middle ring angle: 30 radians or 30 degrees? | OPEN |
| ○ | **W3** | Improved shotgun basis is locked behind custom clients | OPEN |
| ○ | **W4** | HMG spread is a fixed constant | OPEN |
| ○ | **E1** | Factory subsystem absent | OPEN |
| ○ | **E7** | Instagib is split across two cvars, and one branch is dead | PARTIAL |
| ● | **E6** | Players connect as spectators | DONE (verify) |
| ○ | **E10** | Bots: pool ceiling, fill rate, and matches that never start | PARTIAL |
| ○ | **E11** | No score for kills — the same bug as the match that never starts | OPEN |
| ● | **E12** | Nothing was written when Windows crashed | DONE (verify) |
| ○ | **E4** | ZMQ stats feed absent | OPEN |

#### our client (cgame / ui / client engine)

| | ID | Item | State |
|---|---|---|---|
| ● | **U14** | Console is unreadable at high resolution | DONE (verify) |
| ● | **U1** | Advanced settings: blanks, stuck values and `???` | DONE (verify) |
| ● | **U15** | Main menu returns with no buttons | DONE (verify) |
| ● | **U9** | Server browser painted over createserver | DONE, root cause found and verified |
| ● | **U16** | Options BACK button disappears after joining a game | DONE (verify) |
| ● | **U17** | iobin.pk3 has no visible build stamp | DONE (verify) |
| ○ | **U10** | Controls menu is empty | NEEDS INFO |
| ○ | **U11** | Cosmetic layout faults | PARTIAL (value offset fixed) |
| ● | **U12** | Render options have no home in Quake Live's menus | DONE (verify) |
| ● | **U18** | Renderer row in Render Options draws blank | DONE (verify) |
| ● | **E9** | `com_maxfps` only lands on rates that divide 1000 | DONE (verify) |
| ○ | **U2** | No player-name prompt while in a match | PARTIAL |
| ● | **U3** | `ui/menudef.h` | DONE |
| ○ | **U4** | `ui/ingame.txt` is orphaned | OPEN |
| ● | **U5** | Ambient Light Scale | DONE (verify) |
| ● | **U6** | `???` on Advanced settings | DONE (verify) |
| ● | **U7** | Control mapping | DONE (verify) |
| ● | **U8** | Server browser | DONE (verify) |
| ● | **C4** | Console commands sent as chat in-game | DONE (verify), with a caveat |
| ● | **C8** | Phantom pickup sound over taken items | DONE (verify) |
| ● | **C9** | Client dies the moment the server terminates | RESOLVED (confirmed in play) |
| ● | **C10** | `+zoom` does nothing | DONE (verify) |
| ● | **C11** | Mid-match arrivals keep a stand-in model | DONE (verify) |
| ● | **C3** | Weapon viewmodel barely visible | DONE (verify) |
| ● | **C1** | Railgun draws two beams | DONE (verify, 3rd fix) |
| ● | **C2** | Quad pickup now lights the room | DONE (verify) |
| ○ | **R8** | Dynamic light glow is weak or absent | OPEN, my earlier diagnosis was wrong |
| ○ | **R1** | Only renderergl2 ships | OPEN |
| ● | **R2** | Classic look presets | DONE (verify) |
| ○ | **R4** | Getting the metallic look back | routes, in cost order — OPEN |
| ◐ | **R5** | Vulkan renderer | shader parsing no longer fatal on unknown keywords |
| ○ | **R6** | Voodoo postfilter as a real post-process pass | OPEN |
| ● | **R7** | Output dither | DONE (verify) |
| ○ | **R3** | Quake Live art is not Quake 3 art | OPEN |
| ○ | **E2** | Teammate weapon icons never draw | OPEN |

#### spanning both of our binaries

| | ID | Item | State |
|---|---|---|---|
| ○ | **E8** | 186 cvars are registered but read by nothing | OPEN, survey done |
| ● | **E3** | No master server heartbeat / server queries | DONE (verify) |
| ○ | **E5** | Steam integration absent | OPEN |

---

### By audience — who is actually affected

#### stock Steam Quake Live only — correct on ours, broken for everyone else

| | ID | Item | State |
|---|---|---|---|
| ○ | **W1** | Shotgun pattern shape is unverified | OPEN |
| ○ | **W3** | Improved shotgun basis is locked behind custom clients | OPEN |

#### every client, vanilla included

| | ID | Item | State |
|---|---|---|---|
| ○ | **W2** | Middle ring angle: 30 radians or 30 degrees? | OPEN |
| ○ | **W4** | HMG spread is a fixed constant | OPEN |
| ○ | **E8** | 186 cvars are registered but read by nothing | OPEN, survey done |
| ○ | **E1** | Factory subsystem absent | OPEN |
| ○ | **E7** | Instagib is split across two cvars, and one branch is dead | PARTIAL |
| ● | **E6** | Players connect as spectators | DONE (verify) |
| ○ | **E10** | Bots: pool ceiling, fill rate, and matches that never start | PARTIAL |
| ○ | **E11** | No score for kills — the same bug as the match that never starts | OPEN |
| ● | **E12** | Nothing was written when Windows crashed | DONE (verify) |
| ○ | **E4** | ZMQ stats feed absent | OPEN |
| ○ | **E5** | Steam integration absent | OPEN |

#### our client only

| | ID | Item | State |
|---|---|---|---|
| ● | **U14** | Console is unreadable at high resolution | DONE (verify) |
| ● | **U1** | Advanced settings: blanks, stuck values and `???` | DONE (verify) |
| ● | **U15** | Main menu returns with no buttons | DONE (verify) |
| ● | **U9** | Server browser painted over createserver | DONE, root cause found and verified |
| ● | **U16** | Options BACK button disappears after joining a game | DONE (verify) |
| ● | **U17** | iobin.pk3 has no visible build stamp | DONE (verify) |
| ○ | **U10** | Controls menu is empty | NEEDS INFO |
| ○ | **U11** | Cosmetic layout faults | PARTIAL (value offset fixed) |
| ● | **U12** | Render options have no home in Quake Live's menus | DONE (verify) |
| ● | **U18** | Renderer row in Render Options draws blank | DONE (verify) |
| ● | **E9** | `com_maxfps` only lands on rates that divide 1000 | DONE (verify) |
| ○ | **U2** | No player-name prompt while in a match | PARTIAL |
| ● | **U3** | `ui/menudef.h` | DONE |
| ○ | **U4** | `ui/ingame.txt` is orphaned | OPEN |
| ● | **U5** | Ambient Light Scale | DONE (verify) |
| ● | **U6** | `???` on Advanced settings | DONE (verify) |
| ● | **U7** | Control mapping | DONE (verify) |
| ● | **U8** | Server browser | DONE (verify) |
| ● | **C4** | Console commands sent as chat in-game | DONE (verify), with a caveat |
| ● | **C8** | Phantom pickup sound over taken items | DONE (verify) |
| ● | **C9** | Client dies the moment the server terminates | RESOLVED (confirmed in play) |
| ● | **C10** | `+zoom` does nothing | DONE (verify) |
| ● | **C11** | Mid-match arrivals keep a stand-in model | DONE (verify) |
| ● | **C3** | Weapon viewmodel barely visible | DONE (verify) |
| ● | **C1** | Railgun draws two beams | DONE (verify, 3rd fix) |
| ● | **C2** | Quad pickup now lights the room | DONE (verify) |
| ○ | **R8** | Dynamic light glow is weak or absent | OPEN, my earlier diagnosis was wrong |
| ○ | **R1** | Only renderergl2 ships | OPEN |
| ● | **R2** | Classic look presets | DONE (verify) |
| ○ | **R4** | Getting the metallic look back | routes, in cost order — OPEN |
| ◐ | **R5** | Vulkan renderer | shader parsing no longer fatal on unknown keywords |
| ○ | **R6** | Voodoo postfilter as a real post-process pass | OPEN |
| ● | **R7** | Output dither | DONE (verify) |
| ○ | **R3** | Quake Live art is not Quake 3 art | OPEN |
| ○ | **E2** | Teammate weapon icons never draw | OPEN |
| ● | **E3** | No master server heartbeat / server queries | DONE (verify) |

Only the shotgun items have actually been observed on a stock client. The rest
are classified by where the code lives, not by observation. To check one: point
a stock client at the server and repeat the test — it loads its own `cgame`/`ui`
from `pak00.pk3` and only our `qagame`, so whatever still misbehaves is in our
server and whatever stops was in our client.

## Priorities

**P0 — blocking normal use or testing**
1. **C3** viewmodel is bigger and lower than it should be. `cg_fov` ruled out;
   next move is diffing `CG_AddPlayerWeapon` against upstream `1487e89` for the
   `MatrixMultiply` reorder.
2. **R8** rocket blasts light far too weakly. My `r_dlightMode` diagnosis was
   wrong; three leads recorded, none tried.

*(U9 closed — see below. It was `ItemParse_cvarFloatList` eating the menu's
closing brace, which merged two menus into one. Run the headless parse check
in U9 after touching any `.menu` file; parse errors are silent on screen and
loud in the console.)*

**P1 — in flight**
3. **R5** Vulkan renderer, milestone 1. Text is *not* optional here — the console
   is the debugging surface, so a text-less Vulkan build cannot debug itself.
   Stub only long enough to prove the link, then do fonts immediately.

**P2 — correctness unknowns, need evidence**
4. **W1 / W2** shotgun pattern shape and the 30-radians-or-degrees constant.
5. **U10** empty Controls panel — four causes eliminated, needs a menu-list dump.
6. **U11** cosmetic overlaps on QL's Advanced pages; **U13** widescreen bias.

**P3 — absent subsystems, none started, none blocking**
7. **E1** factory · **E2** teammate weapon icons · **E3** master heartbeat ·
   **E4** ZMQ stats · **E5** Steam.

**Deliberately parked**
- **R1** restoring renderergl1 — likely moot if R5 lands, since Quake3e's Vulkan
  renderer is GL1-equivalent in output.
- **R6** Voodoo postfilter — only meaningful at `r_colorbits 16`.

---
## Weapons / gameplay

### W1. Shotgun pattern shape is unverified — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** stock Steam Quake Live only — our client follows the server's pattern cvars, a stock one cannot

The ring pattern transcribed from the binary has a **hollow centre**: its centroid
is exactly on the aim axis, but the nearest pellet is 1.75° off it and the outer
ring sits at 5.23°. On a wall the marks form a donut with nothing where you aimed.

| range | inner ring | middle ring | outer ring |
|---|---|---|---|
| 500 units | 15 u | 31 u | 46 u |
| 1000 units | 31 u | 61 u | 92 u |
| 2000 units | 61 u | 122 u | 183 u |

Average spread matches Q3 closely (mean radius 8400 vs 8570 units), so the *width*
is plausible; the hole is what is in question. `g_shotgunPattern 1` switches to
Q3's filled cone for comparison — if that suddenly feels right, the ring radii in
the transcription are wrong.

**Next step:** play-test `g_shotgunPattern 0` vs `1` and `g_shotgunSpread 1` vs
`0.7`. Note that a vanilla client will not follow either.

**Why this is `vanilla` and not `both`:** the shotgun is not resolved server side
and drawn from the result. The server picks a seed, sends it, and the client
*regenerates the same pattern locally* to place its marks. That only lines up
while both ends run the same generator. Our cgame reads the pattern cvars from
serverinfo and follows; a stock client cannot be told about them and keeps
drawing the stock pattern. So changing the server's pattern is invisible on our
client - both halves move together - and produces marks nowhere near the damage
on a stock one. The bug exists only where we cannot see it.

### W2. Middle ring angle: 30 radians or 30 degrees? — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client, vanilla included

`g_weapon.c` / `bg_misc.c`, middle ring: `angle = i * 1.0471976f + 30.0f`. The
literal is added as **radians** (≈278.87° once reduced). 30 *degrees* (π/6) would
interleave the middle ring neatly between the inner ring's points, which is what a
designed pattern would do. Affects pellet arrangement only — not cone width and
not hit registration, since both sides compute it identically.

**Next step:** re-check the disassembly at `qagamex86.dll 0x1006d450` for whether
the constant is `30.0` or `0.5235988`.

### W3. Improved shotgun basis is locked behind custom clients — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** stock Steam Quake Live only — the fix works on our client and is held back solely for stock ones

`g_shotgunBasis 1` lays the pellets out in the player's own right/up, so the
pattern stops rotating with facing (pellet 0 measured at −60°/−150°/+120° on
screen depending on yaw with the stock frame; a constant −120° with the new one).
It defaults to **0** (stock `PerpendicularVector`) because a vanilla client cannot
be told to follow it and would draw marks nowhere near the damage.

This is the fault that was reported as "aligns fine in our client, doesn't align
in the vanilla client", and it is why the server now defaults to reproducing
exactly what an unmodified client draws (`b4a3ef4`) rather than to the better
behaviour. The improvement is written, tested and switched **off**, because
turning it on breaks the client we cannot test in.

**Decision needed:** is this server meant to serve vanilla clients permanently, or
are custom clients the target? That answer also gates `g_shotgunPattern 1`.

### W4. HMG spread is a fixed constant — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client, vanilla included

`g_weapon.c`: the binary computes Heavy Machine Gun spread per shot in `FireWeapon`
(case 0xe) from a per-client field, spin-up/heat style. The formula has not been
recovered; `HMG_SPREAD 350` stands in for it.

---

## Client / UI

The port ships **one** menu file — `content/pak01/ui/main.menu`, containing `main`,
`createserver`, and (as of the server-browser work) `joinserver` and `playersetup`.
Everything else the player sees comes from their Quake Live `pak00.pk3`, parsed by
this port's re-implemented `ui` module. Most UI bugs are therefore our module
mishandling Quake Live's menu scripts, not missing menus — but a few are both.

### U14. Console is unreadable at high resolution — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

At 3840x2160 the console font is tiny and the text cramped, which makes the
console painful to use for exactly the diagnostic work it is needed for. Q3 draws
console text with `SCR_DrawSmallChar` at fixed 640x480 virtual metrics, so it
does not scale with resolution.

`con_scale` existed, defaulted to **0.5**, and was clamped to `[0.5, 1.0]` —
half-size glyphs with no way to make them bigger. Default is now **1.0**, range
opened to `[0.25, 4]`.

**Not a resolution problem.** Console text goes through `re.DrawStretchPic` and
the renderer's 2D pass runs in a 640x480 ortho projection, so glyphs already
scale with the display: 1.0 looks the same at 720p and 2160p. I first "fixed"
this with a `vidHeight / 480` auto factor, which gives 4.0 at 2160p and made the
text four times too large. Confirmed by testing: `con_scale 1` is right at
3840x2160, which is exactly what resolution independence predicts. This is a
taste setting, not a per-display one — do not reintroduce a resolution-derived
scale here.
Also visible in the same capture and worth chasing separately:
- `WARNING: CM_SrfXPlane unreachable` repeated dozens of times on load
- `WARNING: Failed to load sound mus_high_score.ogg` — falls back, harmless but noisy
- red `ERROR:` lines about unknown mesa keywords in `scripts/*.menu` parsing

### U1. Advanced settings: blanks, stuck values and `???` — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Three separate faults, all found from the Advanced screenshots.

**Blank rows and stuck values** — `ItemParse_cvarStrList` set `strDef = qfalse`.
That sent every `cvarStrList` item down the *numeric* branch of
`Item_Multi_Setting`, comparing against a `cvarValue[]` array this parser never
populates. Every entry therefore compared equal to 0, so:
- a cvar whose numeric value is 0 (or which is a non-numeric string, `atof` → 0)
  always matched entry 0 and displayed the **first** item in the list —
  "Texture Filter: None" was `r_textureMode` reading `GL_LINEAR_MIPMAP_LINEAR`
  and landing on entry 0, which is why it would not change;
- any other value matched nothing and displayed **blank** — Ambient Light Scale,
  Crosshair Style, Gun Position, Damage Num Style, Sparks Velocity.

**`???` on preset rows** — two causes. `Item_Combo_FindCvarByValue` returned
`cvarStr[i]`, the name of the linked `ITEM_TYPE_PRESET` item, where it should
return `cvarList[i]`, the preset name that `Item_Combo_HandleKey` actually stores
in the cvar. And `Menu_CheckPresetCvars` could only ever *demote* a named preset
to "Custom": if the cvar named no known preset — the state a config that has
never been through the menu starts in — nothing matched, nothing was written,
and the row painted `???` forever. It now resolves an unnamed or drifted cvar by
scanning for the preset that matches the current settings, falling back to
"Custom". It also no longer `return`s on the first mismatch, which was
abandoning every remaining preset row in the menu.

Affected rows in the screenshots: Impact Sparks, Teammate Indicators, Low Ammo
Warning, Draw Rewards, Force Team/Enemy Model and Skin, Damage Indicator, Impact
Marks, Lighting Model.

### U15. Main menu returns with no buttons — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

OPTIONS and DEMOS both do `hide mainnav` on the way out, and **nothing ever ran
`show mainnav`**. Coming back from either left `main` painting its background with
no navigation on it. My bug, from when the entries were written.

`main`'s `onOpen` now shows the group, so any return path restores it.

Second, related fault: `Menu_ShowItemByName` cleared `WINDOW_VISIBLE` when hiding
an item but left `WINDOW_HASFOCUS` set, so an item nobody could see remained the
focused one and could still be activated. That is why clicking empty space where
a button used to be still triggered it. Both flags now clear together.

Note the mouse paths themselves were fine — `Menu_OverActiveItem` and
`Menu_HandleMouseMove` both skip items without `WINDOW_VISIBLE`. The leak was
purely the stale focus flag.

### U9. Server browser painted over createserver — DONE, root cause found and verified
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only


**It was never two menus being open. It was one menu containing both.**

`ui/main.menu` line 954 ends the createserver panel with

```
cvarFloatList { "Infinite" -1 "Off" 0 "10" 10 "50" 50 }
```

The botlib tokeniser emits `-` as its own punctuation token — which is exactly
why `PC_Float_Parse` has explicit sign handling. `ItemParse_cvarFloatList` had
none, and read the value with a bare `PC_String_Parse` that also never tested
for `}`. So it took `-` as Infinite's value and left `1` to be read as the next
entry's *name*. Every pair after that shifted by one, the list overran its own
closing brace, and it carried on consuming `rect 420 170 80 15 textaligny 12
…` as name/value pairs until it hit the itemDef's `}` — swallowing that as its
terminator, and leaving the item to swallow the **menu's** closing brace at
line 955.

`io_createserver` therefore never closed. `Menu_Parse` kept going, hit
`menuDef` at line 969, reported it as an unknown *menu* keyword and carried on
— then hit `name "io_joinserver"`, which is a perfectly valid menu keyword, and
**renamed the still-open menu**. Every item of the browser was appended to the
createserver menu.

One menu, both sets of items, named `io_joinserver`. That is:
- two BACK buttons (createserver's `backbtn` and the browser's `back`);
- NEW GAME doing nothing — `open io_createserver` had no such menu to find, the
  name had been overwritten;
- the orange JOIN SERVER title over createserver's title bar, the map preview
  where the server list should be, and every "misalignment" in between.

**Fixed** in `ItemParse_cvarFloatList`: read the value token directly, treat a
`}` in the value position as the list terminator instead of a value, and rejoin
a lone `-`/`+` with the token after it. `ItemParse_cvarStrList` got the sign
rejoining too — it cannot eat a brace (it tests every token) but it silently
mis-pairs the same way.

**Verified**, not argued: a headless run of the shipped 410e3d5 build
reproduces `ERROR: ui/main.menu, line 969: unknown menu keyword menuDef` exactly;
the same run against the fixed build parses clean.

```
SDL_VIDEODRIVER=offscreen ./quakelive.x86_64 \
  +set fs_basepath <dir> +set fs_homepath <dir>/home +set s_initsound 0 +quit
```

with a `baseq3/ui/menus.txt` containing `{ loadMenu { "ui/main.menu" } }` and a
stub `baseq3/default.cfg`. Worth keeping — it parses every menu without needing
a GPU, a server, or Quake Live's assets.

This also hits **Quake Live's own menus**: `ui/ingame_callvote.menu` line 521
failed with `couldn't parse menu item keyword cvarFloatList`, the same fault
running long enough to exceed `MAX_MULTI_CVARS`. That menu should load now too.

Three diagnoses before this one were wrong, and all three shared a mistake:
reasoning about which menus were open from the outside, instead of asking
whether the file had parsed at all. `ui_debugMenus` (added while chasing this)
traces open/close/action and stays in — but the parse errors were in the console
the whole time.

#### Earlier: correction, from the actual menu files
**Correction, from the actual menu files.** I claimed Quake Live's paks define
`createserver`, `joinserver` and `playersetup`, so ours collided with theirs.
**They do not.** Grepping every file QL loads: no menu anywhere in its `ui/` is
named any of those. Its `menus.txt` has `joinserver.menu`, `createserver.menu`
and `player.menu` **commented out** — the only menus it loads are `default`,
`main`, `main_options`, `connect`, `quit_popmenu`, `demo`, `navframeBL`, `error`,
plus the `ingame_*` set. There was never a collision.

The `io_` prefix is harmless and worth keeping as insurance, but it was not a
fix, and it changed the console commands for no reason. Both menus in the
overlap were **ours**, both `fullScreen`, both visible at once.

**Untested since:** the overlap has not been re-checked since the prefix landed.
Confirm it still happens before digging further.

**Worth copying from QL's own main menu:** it never closes `main` to show a
sub-menu. It does `hide mainnav ; open main_options`, leaving `main` open as the
backdrop with a `fullScreen 0` overlay on top. Ours does `close main ; open
io_createserver` with both `fullScreen MENU_TRUE`. Matching QL's pattern — hide
the nav group, make the sub-menus non-fullscreen overlays — would make the whole
class of overlap impossible rather than relying on paired open/close.

#### Earlier, also wrong
Both being `fullScreen`, and Q3 painting every visible menu rather than
only the topmost, they drew on top of each other: doubled headers, overlapping
labels, two sets of BACK buttons, the map preview sitting where the server list
should be. One bug, many symptoms.

The menus this build defines now carry an `io_` prefix so they cannot collide.
`main` is deliberately left unprefixed: this file replaces `ui/main.menu` in the
search path, so Quake Live's own `main` never loads.

**Console commands changed:** `menu_open io_playersetup`,
`menu_open io_renderoptions`.

The first attempt — closing the sibling from the menu script — was treating a
symptom and could not have worked, since the `close` was landing on a different
menu than the `open`.

#### First attempt (did not work)
Both are `fullScreen`, and Q3's menu system paints every visible menu rather than
only the topmost, so one left open behind the other showed through it — doubled
headers, overlapping labels, two sets of BACK buttons. The navigation now closes
the sibling before opening, and `main`'s `onOpen` closes both, so returning to
main always clears whatever was underneath. My bug, introduced with U8.

### U16. Options BACK button disappears after joining a game — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Quake Live's own menus gate items on `ui_mainmenu`: `main_options`' BACK button
and `demo.menu`'s both carry `cvarTest "ui_mainmenu"` with `showCvar { "1" }`,
because in-game those panels are reached through the ingame nav and must not
offer a route back to the main menu.

`cgame` sets `ui_mainmenu` to `"0"` when a map loads (`cg_main.c`), and **nothing
ever set it back**. So OPTIONS had a BACK button on a fresh launch and lost it
for the rest of the session the moment you joined a game — gone from the screen,
and unclickable where it should be, because the item genuinely was not there.

That intermittency is the whole reason this looked like it came and went with
unrelated menu changes: it tracked *whether a map had been loaded yet*, not the
menu code. Both my earlier "fix" (a focus change) and my revert of it were
coincidence. `_UI_SetActiveMenu(UIMENU_MAIN)` now sets it back to `"1"` — the
point at which we are demonstrably out of a game.

**Lesson worth keeping:** when a symptom appears and disappears across builds,
check what *session state* differs before blaming the diff.

### U17. iobin.pk3 has no visible build stamp — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

The pak01 stamp is baked into the menu text at package time, so it identifies
the **menus** only. The game modules ship in a separate pak and can be a
different build entirely — exactly the case worth spotting when a fix appears
not to have landed. The ui module now publishes `ui_iobinBuild` from its own
compile (`PRODUCT_VERSION`) and the main menu shows it under the pak01 line.

The menu item carries no `text` on purpose: `Item_Text_Paint` falls through to
the cvar when `item->text` is NULL, so it shows the running module's build
rather than a baked string that could disagree with it.

`cgame`/`qagame` also print their build to the console at init.

### U10. Controls menu is empty — NEEDS INFO
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Not `???` rows: **no rows at all**. The tabs draw, so the menu loaded and it is
the item list inside that is missing. That is a different fault from U7 (commands
that could not be bound), so U7 is not the explanation here.

**Menu file now read.** No feeder and no exotic owner draw — the bind rows are
plain `ITEM_TYPE_BIND` items (`+forward`, `+back`, …) each carrying
`group grpControls` and **`visible 0`**. They are revealed by the menu's own
script:

```
onOpen { hide grpControls ; show move ; show header1 ; uiScript loadControls ... }
```

so the panel starts empty by design and `show move` is what fills it. The
show/hide machinery checks out — `Menu_ItemsMatchingGroup` matches on name *or*
group, and `Menu_ShowItemByName` sets `WINDOW_VISIBLE` correctly — so the fault is
further along.

**Ruled out by inspection — do not re-check these:**
- *The route.* QL's own `main.menu` uses the identical action:
  `hide mainnav ; open main_options ; open ingame_controls`. Ours is faithful.
- *`;` handling.* `Item_RunScript` skips a bare `;` and resets `bRan` per
  statement, so the chained `onOpen` runs to completion.
- *Name-vs-group matching.* 18 items are named `move`; `Menu_ItemsMatchingGroup`
  and `Menu_GetMatchingItemByNumber` both match name **or** group, identically.
- *`Menus_Activate`.* Builds the stack `itemDef_t` with `parent` set before
  running `onOpen`, matching Q3.

**Remaining suspects, in order:**
1. `uiScript loadControls` erroring or long-jumping before the `setitemcolor`
   run, though the shows precede it so this would not explain an empty panel.
2. `ingame_controls` not being *loaded* at the moment it is opened —
   `ui/ingame.txt` is loaded separately from `ui/menus.txt`, so opening it from
   the main menu may target a menu that does not exist yet. `Menus_ActivateByName`
   on a missing name fails silently.
3. The six `grpControlbutton` tab buttons that switch groups.

**Next step:** print the menu list at the moment OPTIONS is pressed and confirm
`ingame_controls` is present. Suspect 2 is cheap to test and fits the evidence:
the tab bar drew because that is `main_options`, and the panel below was empty
because the menu meant to fill it was never loaded.

### U11. Cosmetic layout faults — PARTIAL (value offset fixed)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only


**The systematic one is fixed.** Every value row carries `text ""` (see U12 for
why it cannot be omitted), and the engine then added the 8-unit label gap on
top — the gap that exists only to hold a value clear of its label. With no
label there was nothing to be clear of, so every selector, yes/no toggle, key
bind and slider bar painted 8 units right of its own rect, each one out of line
with the label sitting at its left. Game Type, Bot Skill, Time Limit, Frag
Limit, Cap Limit, and the browser's filter rows were all the same fault.

`Item_ValueOffset()` now makes that test shared. `Item_TextField_Paint` and the
owner-draw path already did it correctly; `Item_Multi_Paint`,
`Item_YesNo_Paint`, `Item_Combo_Paint`, the bind painter and all five slider
sites hardcoded the 8. All five slider sites matter together — the bar, the
thumb and the click hit-test have to agree or the slider stops tracking the
cursor.

Still open, visible in the Advanced screenshots, none investigated yet:
- "Default" painting over the "Game Settings" title on the Weapons page — looks
  like a preset name drawn at the title position.
- "Zoom Sens" slider labels read `0.01 | 1 | 1`; the maximum label appears wrong.

### U12. Render options have no home in Quake Live's menus — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`r_dither` and the rest of the renderer work has nowhere to live: the Video
Options and Post Process tabs are in Quake Live's `pak00.pk3`, which this build
neither ships nor can edit. A self-contained `renderoptions` menu carries them
instead — reachable from the main menu's RENDER entry or `\menu_open
renderoptions` anywhere, including mid-match.

Covers: `r_dither` (off / ordered / temporal), `r_colorbits` (driver default /
16-bit / 10-bit), `r_hdr`, `r_toneMap`, `r_postProcess`, `r_cubeMapping`,
`r_cubemapSize`, `r_specularMapping`, `r_normalMapping`, `r_ambientScale`, plus
one-click buttons for the classic / gloss / voodoo / defaults presets and an
APPLY that runs `vid_restart` for the latched ones.

Every value row needs `text ""` even when the label is a separate decoration
item: `Item_Multi_Paint`, `Item_YesNo_Paint` and `Item_Slider_Paint` all paint the
value at `item->textRect`, and `textRect` is only computed inside
`Item_Text_Paint`, which is skipped when `item->text` is NULL. Omitting it draws
every value at 0,0 — off the panel entirely, which is what the first cut of this
menu did. The `createserver` rows had it right; these did not.

**Open:** the proper home is still Quake Live's own tabs. If its
`ui/*.menu` files turn up (U1, U10 need them too) these rows should move there.

### U18. Renderer row in Render Options draws blank — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

The RENDER OPTIONS panel painted every row correctly except one: `Renderer:`
had a label and no value. Two theories were wrong before the data arrived.

Not the string pool — `ui_report` on a full menu load says *"String Pool is 9.3%
full, 97697 bytes out of 1048576 used"*. Not the cvar either: the row's cvar
`cl_renderer` exists and reads `opengl2`.

The row's list was empty. `cvarStrList` was written as

```
cvarStrList { "OpenGL 2" "opengl2" "Vulkan" "vulkan" }
```

and botlib's `PS_ReadString` joins adjacent quoted strings the way a C compiler
joins string literals, so all four arrived as **one** token,
`"OpenGL 2opengl2Vulkanvulkan"`. The parser took it as a display name, waited
for a value, hit `}` and returned with `count == 0`. `Item_Multi_Setting` then
matched nothing and returned `""`.

Every `cvarStrList` in Quake Live's own menus is comma-separated —
`{ "Default", "globalpreset_default", ... }` — and that is why. Ours was the
only one in this tree without commas, and the only broken row. The neighbouring
rows are `cvarFloatList`, where the numbers sit between the strings and no two
quoted strings are ever adjacent, which is why they were all fine.

Fixed in the menu (commas added), and the parser now refuses to fail silently:
an empty `cvarStrList` is a source error naming the cvar and the cause, and an
odd-length one says the last name has no value. `Item_Multi_Setting` also warns
once per value when a cvar holds something not in its row's list, separating
"unregistered cvar" from "value not listed".

Both diagnostics were what found this: the second one printed
*"multi item cvar \"cl_renderer\" is \"opengl2\", which is not one of its 0
listed values"*, and `0` was the whole answer.

### U2. No player-name prompt while in a match — PARTIAL
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

A `playersetup` menu now exists and is reachable anywhere via `\menu_open
playersetup`, but nothing in Quake Live's in-game menu opens it — that menu is
theirs and we do not ship a replacement.

**Next step:** either ship an `ingame_options.menu` override (needs QL's original
as a base) or accept the console command.

### U3. `ui/menudef.h` — DONE
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Diffed against Quake Live's copy: **identical apart from trailing whitespace on
two lines.** The concern that shipping ours could break every QL menu at once was
unfounded, so it is now in `pak01.pk3` and the build no longer depends on finding
their header. Nothing else changes, since the two agree.

### U4. `ui/ingame.txt` is orphaned — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`ui/ingame.txt` at the repo root lists nine `ui/ingame_*.menu` files. None exist in
the repo and the file is not packaged. Either write those menus or delete the file.

### U5. Ambient Light Scale — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`r_ambientScale` was `CVAR_CHEAT`: the menu could not write it while connected and
the engine reset it to 0.6 on every connect. Now `CVAR_ARCHIVE`. Reported as
"blank", which this may or may not explain — needs a re-test.

### U6. `???` on Advanced settings — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`Item_Combo_FindCvarByValue` painted `???` whenever a cvar's value was not one of
the item's presets, including when the cvar's own default is absent from the list.
It now shows the raw value; `???` is kept only for cvar-does-not-exist, so the two
faults are distinguishable from a screenshot.

### U7. Control mapping — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`g_bindings` was a fixed table of Q3's commands; `BindingIDFromName` returned −1 for
anything else, so any command QL's control menus bind that Q3 did not have could
not be bound and painted `???`. The table now grows on demand. Capacity 256.

### U8. Server browser — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

All the machinery was already implemented (`UI_BuildServerDisplayList`,
`UI_StartServerRefresh`, `FEEDER_SERVERS`, sorting, favourites, `UI_NETSOURCE`,
`UI_NETFILTER`). Only the menu was missing.

---

## Client / cgame

### C4. Console commands sent as chat in-game — DONE (verify), with a caveat
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`cl_keys.c` only prepends the implicit `\\` when `clc.state != CA_ACTIVE`. Once
connected, any console line not starting with `\\` or `/` falls through to
`if (con_autochat->integer) Cbuf_AddText("cmd say ")` and is chatted instead of
run. That is stock ioquake3 behaviour with `con_autochat` defaulting to 1, not
something this port broke — but it is a bad default for a build used to
administer servers. Default is now 0.

**Caveat, same trap as R8:** `con_autochat` is `CVAR_ARCHIVE`, so a config that
already holds a value keeps it and the new default does nothing. Existing
installs need `/con_autochat 0` once. Prefixing with `/` or `\\` always works
regardless.

**Menu row resolved.** From `ingame_options_advanced.menu:1757`, the row is
`ITEM_TYPE_YESNO` bound to **`cl_allowConsoleChat`** — Quake Live's name for the
setting. This build only ever read ioquake3's `con_autochat`, so the toggle wrote
a cvar nothing consulted. `cl_allowConsoleChat` is now registered (default 0) and
both must agree before a console line is chatted, so either turns it off and the
menu row works.

The row also carries `cvarTest "com_allowConsole"` with `disableCvar { "0" }`, so
it greys out when the console itself is disabled.

### C8. Phantom pickup sound over taken items — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Walk back over an armour spawn you already cleared and the pickup sound plays
again, with nothing granted.

`CG_TouchItem` **sets** `EF_NODRAW` to mean "already taken this prediction" and
never **reads it back**. Quake 3 got away with that because a picked-up item
was marked `SVF_NOCLIENT` and stopped being transmitted at all, so the client
never saw it again. Quake Live's item timers need the entity client-side to
count the respawn down, so `Touch_Item` clears `SVF_NOCLIENT` again and leaves
only `EF_NODRAW` set (`g_items.c`). The entity keeps arriving, prediction
re-takes it on every pass, and `EV_ITEM_PICKUP` fires locally each time.

The server correctly ignores those touches — it zeroed `r.contents` on pickup —
so nothing was granted. Sound only, which is why it read as a sound bug rather
than an item bug. Bail out on `EF_NODRAW`.

Armour is where it shows because armour and mega health are the items with
`itemTimer` set; anything else with a timer would do the same.

### C9. Client dies the moment the server terminates — RESOLVED (confirmed in play)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`VM_Free` is routinely reached from *inside* a VM call. The common case is
exactly this one: cgame calls `trap_GetServerCommand`, the engine's
`CL_GetServerCommand` sees the server's `disconnect` and raises
`Com_Error(ERR_SERVERDISCONNECT)`, and `Com_Error` tears the client down and
`longjmp`s back to `Com_Frame` — all while cgame's stack frames are still live
underneath it.

`forced_unload` only ever suppressed the "VM_Free on running vm" error. **It
did not stop `Sys_UnloadDll`.** So the module was unmapped and *then* `longjmp`
had to unwind past stack frames belonging to it. On Windows x64 `longjmp`
unwinds through SEH, which looks up unwind data in the module each frame came
from — that lookup lands in an unmapped image and the process dies instantly,
inside the unwinder, with nothing in the log tying it to the disconnect.

On Linux `longjmp` just restores the stack pointer and never consults the
module, which is why this is Windows-only and why it could not be reproduced
here.

**Fixed:** when a VM is freed with live frames, the handle is held and unmapped
at the top of `Com_Frame`, after the longjmp has completed. Everything else —
normal shutdown, `VM_Clear`, any `VM_Free` at `callLevel` 0 — still unloads
immediately, so the change only touches the pathological case.

### C10. `+zoom` does nothing — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Right click on the railgun did not zoom. `CG_ZoomDown_f` and `CG_ZoomUp_f` were
written, declared in `cg_local.h`, and registered in no command table, so the
engine looked up `+zoom`, found nothing, and did nothing. Quake Live's
`default.cfg` binds MOUSE2 to it, which is why this reads as a broken zoom
rather than as a missing command.

Registered. The same scan - every `CG_*_f` defined in cgame against every one
in the table - turned up four more: `nextframe` and `prevframe` were registered
but `testmodel`, `testgun`, `nextskin` and `prevskin` were not, so the two that
were there had nothing to step. Registered as a set.

This is the same trap as E8's 186 dead cvars, one layer over: a handler that
exists and is reachable from nothing looks exactly like a working feature until
someone presses the button.

### C3. Weapon viewmodel barely visible — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

**Not a missing tag and not a regression.** `\weaponreport` showed every hands
model loading and every `tag_weapon` resolving, and `CG_AddViewWeapon`,
`CG_CalcFov` and `CG_CalculateWeaponPosition` are byte-identical to upstream
`1487e89`. It is geometry.

`cg_fov` is a **horizontal** field of view, so a wider display does not show
more — it shows the same width over less height. At 16:9 the vertical FOV is a
quarter narrower than the 4:3 the Quake 3 viewmodel maths assumed, and Q3's own
`-0.2 * (cg_fov - 90)` term makes it worse by pushing the gun further *down* as
the FOV rises. The weapon tags sit ~11 units below the view axis, so at
3840x2160 with `cg_fov 100` (vertical half-FOV **33.8 degrees**) nothing closer
than ~16 units ahead of the eye clears the bottom edge:

| weapon | tag fwd | down | on screen past | model shown from |
|---|---|---|---|---|
| lightning | −8.3 | 11.0 | 16.4 fwd | 24.7 units along it |
| rocket | −10.4 | 11.3 | 16.9 fwd | 27.3 units along it |
| railgun | +1.8 | 12.5 | 18.6 fwd | 16.8 units along it |
| gauntlet | +10.4 | 13.4 | 20.0 fwd | 9.6 units along it |

Most tags are *behind* the eye, so the first 10–27 units of each model are off
screen. That is why it hit several weapons and hit the LG and RL hardest.

**Fixed** with `cg_gunAspect` (default on). The correction goes **forward**,
not up: raising the gun works arithmetically but slides it up the screen away
from where it belongs, while pushing it forward walks the model into the part
of the view cone that has widened enough to contain it, so it keeps its
downward angle and simply stops being cropped. Confirmed by eye — `cg_gunZ`
adjustments were rejected in testing, `cg_gunX 6.5` was not.

The shape is derived rather than fitted. The first forward distance clearing
the bottom edge is `down / tan(halfFovY)`, and `tan(halfFovY) =
tan(halfFovX) / aspect`, so it is `down * aspect / tan(halfFovX)` — exactly
**linear in aspect** and inversely proportional to `tan(halfFovX)`. Both terms
follow from that; only the constant is empirical, and it only sets how far past
grazing the edge the gun sits, which is taste rather than geometry.

    gunForward = 19.5 * (aspect / (4/3) - 1) * (tan(50 deg) / tan(cg_fov / 2))

Zero at 4:3, so nothing changes on the aspect the original maths assumed. The
fov term is exactly 1.0 at `cg_fov 100`, so the configuration this was tuned on
is bit-identical and other fields of view follow the derivation. 6.50 at 16:9,
5.42 at 16:9 / fov 110, 15.17 on 21:9; the fov factor is clamped to [0.5, 2.0].
`cg_gunAspect 0` restores the old framing, and `cg_gunX` still stacks on top.

**Lesson:** three wrong diagnoses here came from reasoning about the code path.
The one that worked came from printing the actual numbers and doing the
trigonometry.
**Affects several weapons, worst on the lightning gun (6).**

`CG_AddViewWeapon` is byte-identical to upstream `1487e89`, so this is not a
regression there. But `tag_weapon` on the hands model carries the **entire**
offset that puts the first-person gun into the lower right of the view — the
hands are never drawn, they exist only to position the weapon (`cg_local.h:522`)
— and `R_LerpTag` zeroes the orientation when the model or the tag is missing
(`tr_model.c:1237`). The call in `CG_AddPlayerWeapon` **ignored the return
value**, so a failure silently drew the gun at the eye with the view
orientation: only the barrel tip in frame.

That the severity varies per weapon is consistent with this rather than
against it — if every gun is drawn at the eye, what you see depends on each
model's own geometry, and a long thin model with its origin at the grip (the
LG) shows least.

**Measured, and the theory was wrong.** `\weaponreport` on the live build:
every weapon reports `hands ok` and `tag_weapon ok`. Nothing is missing. The
offsets are real Quake Live tag data, e.g. LG (6) at fwd -8.3, right -2.9,
up -9.0; RL (5) at -10.4 / -4.7 / -9.3; gauntlet (1) at +10.4 / 0.2 / -11.4.

So: the tags resolve, `CG_AddViewWeapon` and `CG_CalcFov` and
`CG_CalculateWeaponPosition` are all byte-identical to upstream `1487e89`, and
`cg_gunX/Y/Z` are wired (the C symbols are `cg_gun_x/y/z`, the **cvars** are
`cg_gunX/cg_gunY/cg_gunZ` — worth knowing before tuning them).

What the numbers do show is that most weapons sit *behind* the eye: the LG's
origin is 8.3 units back, so with `r_znear 4` the bulk of the model is behind
the near plane and only the forward end survives clipping — "you can barely
see the tip", literally. Whether that is wrong depends on how the models are
authored relative to `tag_weapon`, which cannot be settled without the assets.

**Next:** the remaining unknown is a constant. `cg_gunX` moves the gun forward;
finding the value that frames it correctly and baking it in as the default (or
as an fov-scaled term next to the existing `fovOffset`) closes this. That needs
one number from someone who can see the result.

#### Earlier: cg_fov ruled out
`cg_fov` reads 100, the default, and was set to 100 and other values deliberately
without fixing the framing. **My earlier read of the console capture was wrong** —
38.400208 was a transient from an experiment, not the resting value. Fov is not
the cause.

Remaining candidates, none checked:
- `cg_gunX` / `cg_gunY` / `cg_gunZ` — viewmodel offsets, all default 0.
- `cg_drawGun` — 1 normal, 2 and 3 select different positions.
- `CG_AddPlayerWeapon`'s placement, and the `MatrixMultiply` that was moved
  before the `VectorMA` chain during the earlier weapon work. That change is in
  the same function that positions the viewmodel and is the most likely thing
  this port altered.
- `CG_CalcFov`'s widescreen aspect handling (U13).

**Next step:** the `MatrixMultiply` reorder is the one thing known to have been
changed near this code. Diff `CG_AddPlayerWeapon` against upstream `1487e89`
before looking anywhere else.
`/cg_fov` reads **38.400208**, against a default of 100. The viewmodel shares the
world refdef, so a fov that low enlarges the gun and crops it lower — exactly the
reported symptom, and nothing to do with the render presets.

**Immediate fix:** `/cg_fov 100`.

**The real question is how it got there.** 38.400208 is not a value anyone types.
The Game Options page has an FOV *slider* (`10 | 100 | 130`), and sliders were
among the rows painting wrongly before U1/U12 — `Item_Slider_Paint` positions
from `item->textRect` like the others. A slider whose hit region and drawn
position disagree will write a value from wherever the click landed, and 38.4 is
roughly where a click near the left of that track maps to. Suspect that opening
or brushing the FOV row wrote it.

**Next step:** confirm by resetting `cg_fov` to 100, opening Game Options, and
checking whether it changes without a deliberate drag. If it does, the slider's
hit region is wrong — check `Rect_ContainsWidescreenPoint` against where the
slider actually paints, which ties directly to U13.
Reported after the render preset work: the first-person weapon is larger and
further down the screen, so less of it is visible. Most obvious on the lightning
gun. Explicitly *not* a brightness issue — the framing changed.

**Nothing in the render work should be able to cause this.** `r_dither`,
`r_dlightMode`, `r_ambientScale`, tonemapping and cubemapping do not move or
scale the viewmodel, and neither `classic.cfg` nor `gloss.cfg` touches a gun or
fov cvar. So either something else changed it, or it predates the preset work and
was noticed alongside it. Do not assume the presets are the cause.

**The cvars that actually control this:**
- `cg_fov` — default here is **100**. Quake 3 shipped 90. The viewmodel is drawn
  in the same refdef as the world, so a *lower* fov makes the gun look bigger and
  crop lower, which is the direction reported. Worth checking whether the config
  in use is overriding it, and what Quake Live's own default is.
- `cg_gunX` / `cg_gunY` / `cg_gunZ` — all default 0, viewmodel offsets.
- `cg_drawGun` — 1 normal; 2 and 3 select different positions, and note the
  `EV_RAILTRAIL` handler already special-cases 2 and 3 by nudging `origin2`
  along `cg.refdef.viewaxis[1]`, so those modes do shift things.

Also still worth checking `CG_CalcFov`'s widescreen aspect handling (U13).

### C1. Railgun draws two beams — DONE (verify, 3rd fix)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

**The one that mattered, at last: it was inside `CG_RailTrail` itself.**

One call draws two beams. `CG_RailTrail` allocates the `RT_RAIL_CORE` entity,
then applies the view-relative adjustment (`cg_weapons.c:408`) that nudges
`re->origin` onto the drawn muzzle — the whole point of which is that the beam
leaves the barrel instead of the eye. With `cg_oldRail != 2` (the default is
**1**, so this is every shot) it then handed `CG_RailTrailCore` the *untouched*
`start` argument, so the `RT_RAIL_RINGS` half of the same shot began at the raw
server/predicted muzzle, out along the view axis.

Two beams, two origins, one impact point — exactly as reported. The spiral path
below it already used the adjusted origin, which is what disguised this as a
branch-specific bug rather than a missing assignment.

**Fixed** (`cgame: start the rail rings beam at the same muzzle as the core`):
capture the adjusted origin into a local before allocating anything further —
`CG_AllocLocalEntity` can recycle the oldest entity and invalidate `re` — and
pass that to both halves.

The two fixes below were both real bugs and both stay in; neither was what was
on screen.

#### Second fix (real, but drew from the world origin, not the view axis)
There are *three* rail trail sources, not two:
1. `cg_predict.c:537` — predicted, local player only, from the barrel.
2. `cg_event.c:1141` — the server's `EV_RAILTRAIL`, from the transmitted muzzle.
3. `cg_weapons.c:1648` → `CG_SpawnRailTrail` — QL's own path, which is supposed
   to re-spawn the beam from the **rendered weapon flash tag** so it leaves the
   drawn barrel rather than the server's muzzle point.

`CG_SpawnRailTrail` reads `cent->pe.railgunTrailStart` as its start. That field
is declared (`cg_local.h:140`) and read (`cg_weapons.c:523`) and **written
nowhere in the codebase** — so every rail shot drew a beam from a zero vector on
top of the correct one. That is the second beam, and it happened for every
player, which is why gating only the local predicted shot did not fix it.

Made inert until the capture exists rather than drawing from the world origin;
`EV_RAILTRAIL` covers the trail meanwhile.

**Proper follow-up:** implement the capture — write `pe.railgunTrailStart` from
the weapon flash tag origin in `CG_AddPlayerWeapon` when the rail muzzle flash is
placed, then let `CG_SpawnRailTrail` draw and drop the trail from the
`EV_RAILTRAIL` handler. That restores QL's intended behaviour, where the beam
tracks the drawn weapon rather than the server's muzzle.

#### First fix (real, but also not the cause of what was visible)
Firing the rail draws one trail from the barrel and a second along the view
axis, both ending at the same impact point.

Two separate call sites draw a trail for the same shot:
- `cg_predict.c:537` — a **predicted** trail, drawn from `muzzle + right*4 - up`
  the instant you fire, so the shot feels immediate rather than waiting a round
  trip. This is the barrel one.
- `cg_event.c:1141` — the server's `EV_RAILTRAIL`, `CG_RailTrail(ci,
  es->origin2, es->pos.trBase)`, arriving a moment later. This is the view one.

Nothing suppresses either for your own shots, so both draw.

**Fixed.** `cg.predictedRailTime` records when the predicted trail was drawn, and
the `EV_RAILTRAIL` handler skips both the trail and the impact when the event is
our own shot within 1000ms of it. A dedicated field rather than reusing
`cg.lastAutoFireTime`, which doubles as the fire-rate gate. The window is
deliberately short: every other player's rail arrives through this event, and a
shot we did not predict — paused, in a timeout, spectating — still has to be
drawn here. Rail refire is 1500ms so 1000 cannot swallow a genuine second shot.

Note the predicted path drew the *impact* too, so `CG_MissileHitWall` was being
called twice for the same hit as well — the fix covers both.

### C2. Quad pickup now lights the room — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`CG_Item` adds a dlight for a powerup resting on the ground, matching the
colours `CG_PlayerPowerups` uses for a carried one — quad blue, battlesuit
green, regen red, invis white, radius 200 + rand()&31. Quake 3 only ever lit the
powered-up *player*, never the pickup. Suppressed while an item is scaling up on
respawn (`frac != 1.0`).

Whether this fully answers the reported missing glow is unverified: R8 is still
open on why explosions light so weakly, and if that turns out to be a renderer
path fault it will affect this light too.

---

## Renderer

### R8. Dynamic light glow is weak or absent — OPEN, my earlier diagnosis was wrong
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

**Correction.** I claimed `r_dlightMode 0` keeps dynamic lights off world surfaces
entirely. That is not what it does. Reading its actual uses:

- `tr_shade.c:1509` — with `r_dlightMode` non-zero, a single-pass lightall shader
  takes a *fast* dlight path folded into the main shader.
- `>= 2` gates shadowed dlights (`tr_glsl.c:1032`, `tr_image.c:2650`,
  `tr_scene.c:452`, `tr_shade.c:804`).

So `0` still lights the world, via the legacy `ProjectDlightTexture` additive
pass. Changing it alters *which* path runs, not whether lights exist. That is why
setting it to 1 by hand changed little.

**Also:** `r_dlightMode` is `CVAR_ARCHIVE | CVAR_LATCH`. Changing the default in
`tr_init.c` cannot affect anyone whose config already contains a value — the
archived one wins. The build did not "force enable" it and could not have. Any
setting we actually need on must be applied through a cfg or checked at runtime,
not just given a new default.

**Still unexplained:** rocket blasts light far too weakly, and the quad pickup
casts no glow at all.

**Leads worth taking next:**
1. The quad item may never have had a dlight. In Quake 3 it is the *powered-up
   player* that emits light (`cg_players.c:1930`), not the item on the floor.
   `CG_AddItem` adds no light for powerups. If Quake Live's pickup glows, that is
   a dlight this port has not implemented — cgame work, not a renderer setting.
2. For rockets, `CG_MakeExplosion` sets `le->light = 300` with an orange
   `lightColor`. Check `CG_AddLocalEntities` actually forwards that to
   `trap_R_AddLightToScene`, and whether the legacy dlight path scales it down.
3. Compare both dlight paths directly: `r_dlightMode 0` vs `1` vs `2` on the same
   explosion, and check `ProjectDlightTexture` is even reached.


### R1. Only renderergl2 ships — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`code/renderergl1`, the original Quake 3 renderer, was stripped from this repo.
Only `renderergl2` is built, so there is no way to get Quake 3's actual render
path — only an approximation of it.

The switching machinery is entirely intact: `cl_renderer` exists in `cl_main.c`
(default `"opengl2"`, `CVAR_ARCHIVE | CVAR_LATCH`) and falls back to its reset
string if the named DLL is missing, and `USE_RENDERER_DLOPEN=1`. Dropping
`renderergl1` back in and building `opengl1<arch>.so/.dll` would make
`cl_renderer opengl1` work with no engine changes.

**Cost:** ~15 files from ioquake3 upstream, plus adapting to this port's
`REF_API_VERSION 9` `refexport_t`, which added Quake Live's TrueType text path
(`Font_DrawString`, `TextBounds`, `GetGlyphInfo`, `SetCompositionFont`). The font
core is shared in `renderercommon` (`tr_fontstash.c`, `tr_stbtt.c`), but
`renderergl2/tr_font_gl.c` has no GL1 equivalent — that needs writing against
fixed-function GL.

**Next step:** judge `classic.cfg` first (R2). If it gets close enough, this may
not be worth doing.

### R2. Classic look presets — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`classic.cfg`, `voodoo.cfg`, `modern.cfg` in `pak01.pk3`. `classic.cfg` disables
what renderergl2 does that Quake 3 did not — `r_hdr`, `r_toneMap`,
`r_postProcess`, normal/specular/deluxe/cube mapping — and restores Quake 3's
texture handling. `r_toneMap` is the important one: environment mapping
(`TCGEN_ENVIRONMENT_MAPPED`) is implemented and working, but tonemapping
compresses exactly the highlights that read as metallic.

`classic.cfg` also sets `r_dither 2` explicitly, because before R7 the dither
lived inside the tonemap pass and turning tonemapping off silently removed it.

### R4. Getting the metallic look back — routes, in cost order — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Three ways to put gloss on surfaces. A screen-space post-process is **not** one
of them: once the frame is a 2D image the per-surface normals are gone, so a
filter cannot know which pixels are metal or which way they face. Screen space
can do the *Glide* half — dither, soften, bloom — because that genuinely is a
screen-space effect. It cannot do the reflective half.

1. **`r_cubeMapping 1` + `r_specularMapping 1`** — already in the renderer, off
   by default, needs GL 3.0. Falls back to `info_player_deathmatch` spawns as
   probes when a map has no `env.json` and no `misc_cubemap` entities, so it
   works anywhere with no authoring. Shipped as `gloss.cfg`. This is *real*
   reflection and will look glossier than Quake 3 ever did — Quake 3's effect
   was a fixed smear, not a probe.
2. **Shader overrides** — a pk3 of `scripts/*.shader` adding a `tcGen
   environment` stage to the surfaces that should be metal. This is exactly how
   Quake 3 did it: data, not code, per-surface correct, and renderer-agnostic
   (it would survive a move to Vulkan). Best fidelity per unit effort. Needs the
   Quake Live texture names to target.
3. **Screen-space reflections** — needs a normal/depth G-buffer that renderergl2
   does not currently write. Real work, and still approximate at surface edges.

### R5. Vulkan renderer — RUNS, no text yet
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`code/renderervk` is vendored from ec-/Quake3e (GPLv2, 29 files) plus two files
of ours. `code/renderervk/README.ioquakelive.md` carries the detail.
`BUILD_RENDERER_VULKAN` still defaults to 0 and no default-build rule touches
the directory, so an OpenGL build cannot be affected — `opengl2x86_64.so` is
byte-identical with the target added.

**It runs.** Verified end to end against Mesa lavapipe under Xvfb: instance,
physical device, swapchain (IMMEDIATE, 3 images, `B8G8R8A8_UNORM`), shaders, 60
frames of the main menu, clean shutdown, and a screenshot with the menu
background art in it. Select it with `\cl_renderer vulkan` then `\vid_restart`.

**Text works.** `tr_font_vk.c` is the Vulkan backend for the fontstash atlas.
Verified: the main menu renders its buttons, both build stamps and the
copyright line, antialiased and in the right colours. renderergl2 keeps the
atlas as a single-channel `GL_R8` texture read as coverage through a swizzle
(1,1,1,R); Vulkan has no swizzle to lean on without a second image view and its
own descriptor, so this expands each dirty rect to RGBA on the way in — white
with coverage in alpha, which is what that swizzle produces.

**Six rendering faults were reported; three root causes found, all fixed, none
of them confirmed in play yet.**

| | symptom | cause |
|---|---|---|
| 1 | item pads, wall teleporters, the lightning beam and grenades draw as a near-black box with a white border | material keywords |
| 2 | quad damage glow missing | material keywords (assumed) |
| 3 | in-game text is fragments of the wrong glyphs; the menu and console are fine | font atlas resize |
| 4 | `vid_restart` opens a second window and leaves the first | `Shutdown` argument |
| 5 | player models do not draw | `novlcollapse` |

**`novlcollapse`, and the rule that made it fatal.** The console said it
outright: *"unknown general shader parameter 'novlcollapse'"*, on every player
skin in the game, both flags, the race markers and a map texture. `novlcollapse`
is Quake Live's "do not collapse the vertex lighting stage" hint. renderergl2
has always known it; renderervk did not.

That alone should have cost a lighting hint. It cost the whole model, because
**both** parsers answered an unrecognised keyword with `return qfalse`, and
`ParseShader` turns that into `defaultShader` on the entire shader. One keyword
it has never heard of and the material is replaced by the black box, with
nothing on screen to say why.

That is the wrong trade for a renderer reading someone else's shader scripts,
and chasing the keywords one at a time was the wrong shape of fix. **Unknown
keywords are now skipped rather than fatal, in both renderers** — skipping a
keyword whose meaning we lack renders a shader slightly wrong, failing it
renders the shader completely wrong and takes the model with it. Each unknown
keyword is reported once, with the first shader it appeared in, instead of once
per shader: one keyword across forty player skins was forty identical lines, and
that is what the console looked like.

**`developer 1` did nothing for the renderer.** `CL_RefPrintf` handled
`PRINT_DEVELOPER` behind `#if DEBUG_RENDERER`, a macro defined nowhere in the
tree — so every developer-level line from either renderer was discarded at
compile time. That is not a debug switch, it is a deleted one, and it threw away
the two messages that answer most "why is this surface wrong" questions:
*"Couldn't find image file for shader %s"* and *"no shader for surface %s in
skin %s"*. It now goes through `Com_DPrintf`, which is gated on the cvar, which
is the gate it wanted. (This is also why asking for a `developer 1` trace last
round produced nothing — the instruction was useless as given.)

**Material keywords.** That black box with a white border is
`R_CreateDefaultImage`'s output — those surfaces were resolving to
`tr.defaultShader`, so it was never a missing texture. Quake Live's shaders
carry the renderergl2 material set: `stage`, `diffuseMap`, `normalMap`,
`bumpMap`, `specularMap`, `normalScale`, `specularScale`, `gloss`, `roughness`,
`parallaxDepth`, `vertexLit` and the rest — 18 keywords renderergl2 knows and
Quake3e's parser has never seen. An unknown keyword in `ParseStage` does not
skip a line: it returns `qfalse`, and `ParseShader` then marks the **whole
shader** default. Ordinary map surfaces carry no material keywords, which is
why the world looked right and only the effects were wrong.

This renderer has no material pipeline, so it now does what a non-material
renderer should: accept the keywords, consume their arguments, render the
diffuse. A stage explicitly declared a normal or specular map is dropped rather
than drawn — painting a normal map on as if it were colour is worse than
leaving it out.

**Font atlas resize.** A `VkImage`'s extent is fixed at creation, so a bigger
atlas means a new image — but `RE_RegisterShaderFromImage` looks the shader up
by name and returns the existing one, still pointing at the **old** image.
fontstash re-uploaded every glyph into the new image while the draw path kept
sampling the old one. The menu and console survived because their glyphs were
rasterised before the first growth past 512x512; a match needs far more glyphs,
so it was the first thing to trip it. `renderResize` now reuses the image when
the size is unchanged and re-points the shader when it is not.

**Shutdown's argument.** This tree's `refexport_t` entry is
`Shutdown(qboolean destroyWindow)`; Quake3e's takes a `refShutdownCode_t`,
where 1 is `REF_KEEP_WINDOW`. `CL_ShutdownRef` passes `qtrue`, so renderervk
read "keep the window", left `SDL_window` alive, and the engine then unloaded
the module out from under it — the next renderer opened a second window beside
the orphaned first. `CL_ShutdownRef` unloads the library immediately afterwards
regardless, so `REF_UNLOAD_DLL` is the honest translation.

**None of this could be reproduced here.** All of it needs a map, and maps live
in `pak00.pk3`, which is Quake Live's and is not ours to hold. The three causes
were found by reading, and the evidence for each is in the fix; only play will
confirm them.

Two real defects were found looking for these, neither of which is the cause:
`RE_AddRefEntityToScene` was being called through a one-argument pointer while
its definition takes two, and `REFENTITYNUM_BITS` was redefined from the tree's
10 to Quake3e's 12 *after* `tr_types.h` had defined it, leaving the two
renderers disagreeing on `MAX_REFENTITIES` (1023 vs 4095). Both fixed.

**The windowing question is answered.** The measured gap was 24 imports, of
which the six windowing ones meant "restructure `sdl_glimp.c` so the engine owns
the window" — shared code the OpenGL renderer depends on. It did not need
restructuring: the renderer keeps ownership of its window, `vk_window.c`
provides the same four `VK*imp_` entry points from inside the Vulkan module, and
`GetRefAPI` points `ri` at them after copying the engine's import table. The
vendored source still calls `ri.VKimp_Init()` and simply reaches code in its own
module. Two lines of patch instead of a restructure.

**Two things that were live bugs, now fixed:**

- Picking a renderer that cannot load wrote the choice to the config *before*
  trying it, and `CL_InitRef` then went straight to `ERR_FATAL` — unrecoverable
  without hand-editing the config. It now resets and loads the default. Verified
  against a deliberately unloadable `vulkanx86_64.so`.
- The same trap one layer down: a machine with no Vulkan driver got as far as
  loading the module and then died creating the window, on every launch
  thereafter. `VKimp_Init` now resets `cl_renderer` before erroring, and says
  why.

**Also open:** renderervk's own `r_dither` is 0–1 where this tree's is 0–2, so
the temporal setting logs *"cvar 'r_dither' out of range (max 1), setting to
1"*. The Windows build of the Vulkan target has not been exercised.

### R6. Voodoo postfilter as a real post-process pass — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`voodoo.cfg` gets the 16-bit dither but not the thing that made it look good:
Voodoo3 and later ran a filter over the dithered output on scanout — the "22-bit"
mode — blending adjacent pixels so the dither read as a smooth gradient rather
than as noise. Unlike reflection, this genuinely is reproducible in screen space:
dither-smoothing needs nothing but neighbouring pixels.

R7 landed first and shares the insertion point. The postfilter would be a second
pass in `RB_DitherToScreen`, reading neighbours instead of adding noise, and is
only worth running under `r_colorbits 16` — at 8 bits and above there is no
dither pattern coarse enough to be worth smoothing.

Note this has nothing to do with API lineage. OpenGL (1992, out of SGI's IRIS GL)
predates Glide (1996), and Glide borrowed conventions from OpenGL rather than the
reverse; Vulkan descends from AMD's Mantle and was a deliberate break from
OpenGL's state machine. Glide is a dead branch — nothing of it survives in Vulkan
to be recovered. What is recoverable is the hardware's *output stage* behaviour,
which is what this item and R7 are.

### R7. Output dither — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

The renderer had exactly one dither: a 2-phase checkerboard hardcoded to half an
8-bit step, inside `tonemap_fp.glsl`. It only ran when `r_hdr` and `r_toneMap`
were both on, so `classic.cfg` — which turns both off — removed the only dither in
the pipeline and banded *worse* than the defaults. A fixed 8-bit amplitude is also
wrong on any other framebuffer: four times more noise than a 10-bit output needs.

Now `RB_DitherToScreen` (`tr_postprocess.c`), replacing the `FBO_FastBlit` calls
at the end of `RB_SwapBuffers`, so it applies on every path. Amplitude derives
from `glConfig.colorBits` — what the driver actually handed back, not what was
requested — so a 10-bit framebuffer gets a quarter of the noise an 8-bit one does.

| value | pattern |
|---|---|
| `r_dither 0` | plain hardware blit |
| `r_dither 1` | ordered 8x8 Bayer, stable frame to frame |
| `r_dither 2` | *(default)* interleaved gradient noise, golden-ratio phase per frame off a frame counter |

`r_dither 2` puts its error in high spatial frequencies where the eye discards
it, and decorrelating successive frames lets it average out over time instead of
sitting still on a surface — worth roughly another bit at these frame rates.

`r_colorbits 30` asks for a real 10-bit framebuffer; the amplitude follows it
automatically. That is actual precision rather than simulated, and makes the
dither mostly moot on hardware and displays that support it.

### R3. Quake Live art is not Quake 3 art — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

The env-mapped metal shaders live in Quake 3's `pak0.pk3`. Quake Live retextured
everything, so no renderer setting recovers surfaces whose shaders are not
loaded. Note the load order: `paksort` is `FS_PathCmp` ascending and later paks
take precedence, so a Quake 3 `pak0.pk3` dropped into `baseq3` alongside Quake
Live's `pak00.pk3` is **shadowed by it** — it has to sort last (e.g.
`zz_q3pak0.pk3`) for its shaders to win.

**Next step:** test with Quake 3's paks to separate "renderer is wrong" from
"art is different". This decides whether R1 is worth the effort.

---

## Engine / server

### E8. 186 cvars are registered but read by nothing — OPEN, survey done
**Lives in:** our **client and server** both · **Seen by:** every client, vanilla included — game cvars are server side, cgame/ui cvars client side

Registering a cvar creates it, gives it a default, exposes it to configs and
lists it in `\cvarlist`. **None of that makes it do anything.** A cvar nothing
reads is indistinguishable from a working setting: you set it, it takes the
value, and the game ignores it.

This has already produced two real bugs. `g_spawnItemWeapons` was registered,
exposed as a gamerule, documented, set to `0` in the shipped instagib configs —
and read by no code at all, so instagib servers kept spawning weapons.
`g_instaGib` itself was half-wired the same way (E7).

`tools/dead-cvars.py` surveys this and is repeatable. Pass a directory of
Quake Live's own `ui/` files so cvars its menus consume are not miscounted:

    tools/dead-cvars.py /path/to/ql/ui

Current count: **186** — 78 game, 84 cgame, 24 ui. Not all matter; the survey
does not distinguish. Three groups:

- **Harmless.** Pure advertisement (`g_version`, `gamedate`, `sv_mapname`,
  `ui_version`) and bot debug knobs (`bot_show*`, `bot_debugVar`) that Quake
  Live registered and never used either.
- **Silently missing server features.** `g_powerupRespawn`, `g_allTalk`,
  `g_dropCmds`, `g_shuffle_*`, `g_switchTeamDelay`, `g_spawnMinDistance`,
  `g_spawnRandomRatio`, `g_kickBadUserinfo`, `g_flagPhysics`, `g_flagBounce`,
  `g_droppedFlagBonus`, `g_freezeAllowRespawn`, `g_freezeProtectedSpawnTime`,
  `g_quadHog*`, `g_flight*`, `g_grantItemOnSpawn`, `g_accuracyFlags`. An admin
  setting any of these gets no error and no effect.
- **Silently missing client features.** `cg_itemTimers` and
  `cg_specItemTimers*` (the timers the server already sends), the whole
  `cg_drawTeamOverlay*` group, `cg_projectileNudge` / `cg_autoProjectileNudge`,
  `cg_simpleItems*`, `cg_hitBeep`, `cg_raceBeep`, `cg_noTaunt`, `cg_ignore`,
  `cg_oldPlasma`, `cg_oldRocket`, `cg_thirdPersonPitch`,
  `cg_teammateCrosshairNames`, `cg_screenDamage_*`.

`g_freezeAllowRespawn` and `g_freezeProtectedSpawnTime` are worth doing first —
the shipped `a2m-instagib-freeze.cfg` runs freeze tag, and neither knob works.

**Rule this establishes:** adding a cvar is not implementing a feature. Run the
survey after adding one.

### E1. Factory subsystem absent — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client, vanilla included

Quake Live's "factory" layer (named rule presets loaded from `scripts/*.factories`,
selecting gametype plus a bundle of cvars) has no implementation. Servers configure
raw cvars instead.

### E2. Teammate weapon icons never draw — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

`clientInfo_t::curWeapon` is never written because the server never sends the
`tinfo` command. The HUD field exists and is read; nothing populates it.

### E3. No master server heartbeat / server queries — DONE (verify)
**Lives in:** our **client and server** both · **Seen by:** our client only — the heartbeat half also makes our server visible to any client's browser

**The browser could never have found anything.** `UI_StartServerRefresh` shells
out to `localservers` and `globalservers`, and neither command existed:

- `CL_LocalServers_f` was fully implemented but never registered with
  `Cmd_AddCommand`, so LAN scanning hit "unknown command";
- `CL_GlobalServers_f` was declared in `client.h` and **never defined at all**;
- there were no `sv_master` cvars anywhere in the tree.

The receive half was already complete — `CL_ServersResponsePacket` parses both
`getserversResponse` and `getserversExtResponse`, and the server answers
`getinfo`/`getstatus`. Only the request side was missing.

Both commands are now registered, `CL_GlobalServers_f` is implemented, and
`sv_master1..5` exist. Dedicated servers heartbeat to them
(`SV_MasterHeartbeat` from `SV_Frame`, `SV_MasterShutdown` on the way out).

**The master cvars ship empty on purpose.** Quake Live's own master is long
gone and there is no honest default to point at. Set them to a dpmaster that
accepts the `QuakeLive` gamename and both directions start working:

```
seta sv_master1 "your.dpmaster.host"
```

LAN discovery needs none of that and works as soon as the command exists.

`GAMENAME_FOR_MASTER` is a separate constant rather than `com_gamename`
because `PRODUCT_NAME` is `"Quake Live"` — a master filters on a single token
and the space would break it.

**Verified:** both commands execute, the LAN scan runs, and the master address
resolves and the request goes out. End-to-end discovery needs a running server
with a real map, so that part is yours to confirm.

**Still absent:** the Valve/Steam server query protocol (A2S). Only the Quake
master protocol is implemented.

### E7. Instagib is split across two cvars, and one branch is dead — PARTIAL
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client, vanilla included

`g_instaGib` and the `DF_INSTAGIB` dmflag were two halves of one feature keyed
off different cvars with nothing connecting them: damage resolution in
`g_combat.c` tests `g_instaGib`, while the ammo grant and the warmup-extras
suppression in `g_client.c` tested only the dmflag. `g_instaGib 1` alone gave
instagib damage with **finite ammo and a full warmup arsenal**; the dmflag
alone gave infinite ammo with ordinary damage. **Fixed** — either works now.

**Still open:** `g_combat.c`'s instagib resolution guards on
`client->ps.powerups[0] != 0` ("armored: no instagib"). `PW_NONE` is 0, so
`powerups[0]` is never set and **the branch is unreachable**. The comment says
armour, and QL reordered the powerup enum (`PW_BATTLESUIT` is 5), so this looks
like an index that did not survive the reorder. Left alone rather than guessed
at: making battlesuit confer instagib immunity is a gameplay change and needs
checking against the binary first. The shipped instagib configs turn powerup
spawning off, so it cannot bite either way meanwhile.

### E6. Players connect as spectators — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client, vanilla included

Quake Live drops every connecting player into spectator and makes them pick
JOIN MATCH from the menu. Quake 3 dropped you straight in. `g_autoJoin`
(default **1**) restores the Quake 3 behaviour; set it to 0 for Quake Live's.

Duel is exempt from the blanket case because it runs on a play queue — a third
player has to wait rather than barge in. The "fewer than two in the game" test
is Quake 3's own tournament rule and matches what the join path in `g_cmds.c`
already enforces. `g_maxGameClients` is respected for the rest.

**Server-side cvar** — it governs servers you run, not ones you join.

**Deliberately not `CVAR_ARCHIVE`.** An archived cvar is written into the
server's saved config on first run, and that config then wins on every later
launch — so the shipped default stops applying the moment one build has written
it, and a future build's default is silently overridden by a stale line in a
file nobody remembers writing. That has already cost this project two rounds
(`r_dlightMode`, `con_scale`). Without the flag, the default in `g_main.c` is
what a dedicated server actually starts with, every build, every time.

Admins who want it off put it in `server.cfg`, which is re-exec'd on every
start and is where server policy belongs. `server.cfg.example` ships in both
release archives documenting this and the `sv_master` cvars — the dedicated
server previously shipped with no configuration at all and no hint that
`sv_master` needs setting before the Internet tab can work.

**Rule for this repo:** a cvar whose *shipped default* is part of the product
should not be `CVAR_ARCHIVE`. Archive is for values a user sets, not for values
we choose.
The server does not announce itself, so it cannot appear in any public list. The
client's browser can still reach it by direct connect or LAN.

### E9. `com_maxfps` only lands on rates that divide 1000 — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Reported as "com_maxfps 210 to 240 defaults to 250, but 200 does 200". Nothing
is defaulting — the frame limiter was

```c
minMsec = 1000 / com_maxfps->integer;
```

in whole milliseconds. `1000/210` and `1000/240` are both `4`, and so is
`1000/250`. The only rates the limiter could ever produce were the ones that
divide 1000 exactly: 1000, 500, 333, 250, 200, 166, 142, 125, 111, 100. Every
value in between was accepted, archived, and quietly rounded to the next one up.

144 is the one worth calling out: it was running at 166.

Fixed by carrying the fractional millisecond across frames instead of
truncating it, so the interval alternates between the two neighbouring whole
milliseconds in the right proportion — 240 fps asks for 4.1667 ms and gets five
4 ms frames then one 5 ms frame, 25 ms per six frames, exactly 240.

Verified over 100000 frames per rate against the old expression:

| com_maxfps | before | after |
|---|---|---|
| 333 | 333.33 | 333.00 |
| 240 | 250.00 | 240.00 |
| 210 | 250.00 | 210.00 |
| 200 | 200.00 | 200.00 |
| 144 | 166.67 | 144.00 |
| 90 | 90.91 | 90.00 |
| 60 | 62.50 | 60.00 |

Above 1000 fps there is still no sub-millisecond sleep to hand out, so the cap
stays at 1 ms and the remainder is dropped rather than accumulated as a debt
that can never be paid. Sub-millisecond timing throughout is the other fix and a
much larger one — it means replacing `Com_TimeVal` and `Sys_Milliseconds`.

### E10. Bots: pool ceiling, fill rate, and matches that never start — PARTIAL
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

Three separate faults reported together.

**Fixed — the 21-bot ceiling.** `MAX_BOT_CHARACTERS` was `MAX_CLIENTS + 1`, so
bot 22 onward failed with *"couldn't load skill 2.000000 from bots/\*_c.c"*
while the console simultaneously said it had *"loaded cached skill 2.000000 from
bots/major_c.c"* — the cache hit was found only after the free-slot search had
already bailed out. The pool is now `4 * MAX_CLIENTS` and the cache lookup runs
before the bailout; `BotInterpolateCharacters` returns 0 cleanly on a full pool
instead of erroring.

**Fixed — the hard crash, and it was not the bots.** `sv_maxclients` had no
range check on it at all. The server sizes `svs.clients` from the cvar, but the
game module's client array is a fixed `gclient_t g_clients[MAX_CLIENTS]` (64)
and every loop in it ran to `g_maxclients.integer`. Setting `sv_maxclients`
above 64 and then filling with bots walks off the end of that array the moment a
client lands past slot 63 — which is why it presented as "the server hard
crashes after a certain number of bots" and looked like a bot-naming problem.
botlib is the same shape: `botchatstates`, `botgoalstates`, `botmovestates` and
`botweaponstates` are all `[MAX_CLIENTS + 1]`.

`Cvar_CheckRange(sv_maxclients, 1, MAX_CLIENTS, qtrue)` on the engine side, and
the game module clamps `level.maxclients` and says so rather than silently, in
case it is ever loaded by a server that does not. The 30 loops that read
`g_maxclients.integer` directly now read `level.maxclients`, so the clamp
actually protects something — the cvar is read once, where it is validated.

**Reverted — the fill rate.** `G_CheckMinimumPlayers` is throttled to once a
second and added one bot per call, so `bot_minplayers 40` took forty seconds and
looked stalled. Raising it to four per tick fixed the wait and **crashed
dedicated servers while filling**. Four adds in one frame is four `ClientBegin`
calls, four botlib character loads and four AAS clients appearing between two
server frames; the one-per-second path has never done it. Back to 1 until the
crash is understood — the right fix is not a bigger burst, it is not gating the
whole thing behind a one-second timer, which is a change to
`G_CheckMinimumPlayers`.

**Open — the match does not start.** The countdown runs, the announcer talks
about frags and leaderboard position, and the round never begins. `g_debugWarmup`
is shipped and traces every `SetWarmupState` transition, names the gate in
`WarmupBlocked()`, and logs countdown-elapsed and auto-forfeit. No trace has come
back yet, so this is still unlocated.

### C11. Mid-match arrivals keep a stand-in model — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

"Sometimes when a bot joins it doesn't load their model, sometimes it does."

A client that arrives mid-match is *deferred*: `CG_SetDeferredClientInfo` copies
some other client's model as a stand-in, and the real one is meant to load
later. The only things that called `CG_LoadDeferredPlayers` were the scoreboard
draw — and then only after it had been drawn eleven times
(`cg.deferredPlayerLoading > 10`) — the local player's own info changing, and
the first snapshot. So a bot joining a match in progress kept its stand-in until
the player happened to die and look at the scoreboard. Whether the model was
right came down to whether that had happened yet, which is exactly what
"sometimes" means.

Deferring is worth keeping: a snapshot can bring eight clients in at once and
loading eight player models in one frame is a visible hitch, which is the whole
reason it exists. It just never finished. One deferred client is now loaded per
frame, which spreads the cost the way deferring intended and bounds the wait at
one frame per client.

### E11. No score for kills, and the match that never starts, are one bug — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

Reported separately — "TDM/FFA kills don't add points" — and it is the same
fault as the match that runs a countdown and never begins. `AddScore` opens
with:

```c
if ( level.warmupTime ) {
    return;
}
```

Scoring is off for as long as warmup is on, which is correct behaviour. So "no
points" is not a scoring bug at all: it says `level.warmupTime` is still
non-zero while the match looks live, and that is the match-start fault seen from
a different angle.

The path is: countdown elapses → `g_restarted 1` → `map_restart 0` →
`SP_worldspawn` sees `g_restarted` and calls `SetWarmupState(0)`. Read straight
through, that works, and `SV_MapRestart_f`'s early-outs all look satisfiable, so
where it actually stops is not established. `g_debugWarmup 1` traces every
`SetWarmupState` transition, names the gate in `CheckWarmupConditions`, and logs
countdown-elapsed and auto-forfeit — that trace is what is needed and has not
been captured yet. (`developer 1` is the wrong cvar for this and was the wrong
thing to ask for.)

### E12. Nothing was written when Windows crashed — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

`sys_unix.c` has had `Sys_ErrorDialog` writing `crashlog.txt` for as long as
this tree has existed. `sys_win32.c` had nothing, so a hard crash on the
platform this actually ships on left nothing behind — which is why "does the
server have a log file?" had no good answer.

`SetUnhandledExceptionFilter` now writes the exception code, the faulting
address, and the module plus offset within it. Module-and-offset is the part
worth having: it says whether the fault was in `quakelive.exe`,
`cgamex86_64.dll`, `qagamex86_64.dll` or a renderer, and the offset stays valid
across runs.

For the console output leading up to a crash: **`logfile 2`**, not `logfile 1`.
1 buffers and loses exactly the tail worth reading; 2 flushes after every line.
`qconsole.log` lands in `fs_homepath/baseq3`.

The >20-bot crash is still open and this is what it needs.

### E4. ZMQ stats feed absent — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client, vanilla included

Quake Live publishes match events over ZeroMQ (`zmq_stats_enable` and friends).
Nothing here implements it. Wanted by most server-stats tooling.

### E5. Steam integration absent — OPEN
**Lives in:** our **client and server** both · **Seen by:** every client, vanilla included

Auth, and `FS_CopyFromSteam` is defined but unused (it is one of the two remaining
compiler warnings in engine code).

---

## Resolved

| # | Item |
|---|---|
| R1 | LP64 session-data corruption (`%ld` vs `int` in `g_session.c`) |
| R2 | Out-of-bounds writes from network-supplied weapon indices |
| R3 | Server ban system: `banlist`/`ban`/`unban`/`rehashbans`, in-game `/ban` |
| R4 | Access-level lookup used a broken stub at connect time |
| R5 | Console command auto-completion |
| R6 | Format-string vulnerability in error paths |
| R7 | All 25 project-code compiler warnings cleared (90 → 0) |
| R8 | Hunk exhaustion at high player counts — 88.5 MB of 128 MB at 48 players; `DEF_COMHUNKMEGS` 128 → 256 with a dedicated-server floor derived from `sv_maxclients` |
| R9 | Silent snapshot-entity discards — now counted and warned |
| R10 | Reliable-command buffer overflow dropped clients with no warning |
| R11 | `sv_altEntDir` entity override |
| R12 | Team lock (`G_IsTeamLocked`) |
| R13 | `CG_SpawnParticleEffect` (impact sparks) |
| R14 | Railgun tracer and impacts invisible — shaders never registered |
| R15 | Shotgun server/client pattern divergence — the server ignored the transmitted seed |
| R16 | Shotgun pattern generation duplicated in two files; now shared in `bg_misc.c` |
| R17 | Shotgun smoke puffs spawned at the map origin — the "32 units in front of the muzzle" step had been dropped |
| R18 | Server defaults now trace bit-identically to what an unmodified cgame draws (905760/905760 components, max diff 0) |

---

## Diagnostics available

| Command | Scope | What it reports |
|---|---|---|
| `cg_debugShotgun 1` | client, in-map | Per blast: fire direction vs view axis, muzzle vs camera, resolved pattern parameters, pellet spread off the crosshair |
| `menu_open <name>` | client | Opens any loaded menu by name |
| `menu_close <name>` | client | Closes it |
| `ui_report` | client | Dumps menu/item state |
