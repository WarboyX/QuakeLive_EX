### C16. Score tracker never shows the player's score — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

The two-bar tracker top-left read 39/38 while the player was 40th with 1, and
0/0 while the player was on -1. Four samples, always the top two players, never
the viewer — so this was never a negative-number bug, which is how it was first
reported.

**The HUD that loads is not the one I was reading.** `cg_hudFiles` defaults to
`ui/hud.txt`, which loads `ui/hud.menu`, and that asks for
`CG_1ST_PLACE_SCORE` and `CG_2ND_PLACE_SCORE` with **no ownerdrawflags at all**
— both are plain `visible 1`. The four-item
first-place/not-first-place arrangement I traced through `comp_hud.menu` is a
different HUD and never applied. So the earlier round spent ruling out
`ownerDrawVisible`, `PERS_RANK` and the flag gating was looking at the wrong
file the whole time; all three were fine.

Both owner-draws were mapped straight to `CG_DrawRedScore` / `CG_DrawBlueScore`,
which read `cgs.scores1` / `cgs.scores2`. In a team game those are the team
scores and it is correct; in a free-for-all they are 1st and 2nd place.

`CG_DrawPlaceScore` now serves both: team gametypes keep red and blue, and
free-for-all draws the leader on the top line and **your** score on the bottom,
swapping the bottom line to the runner-up when you are the one leading. That is
the same information `comp_hud.menu` builds from four gated items, expressed as
two.

This follows the reported intent rather than a binary trace — the behaviour was
stated as "one should be the client's player, one should be the top fragger".

# ioquakelive — issue and feature tracker

Working notes for the port. Open items first, resolved at the bottom so we do not
re-litigate settled ground.

Status key: **OPEN** · **IN PROGRESS** · **NEEDS INFO** · **BLOCKED** · **DONE**

---

## Progress

| Area | Progress | Notes |
|---|---|---|
| **Client / UI** (U) | `███████████████░░░░░  12/16` | U18 root-caused: missing commas, not the string pool |
| **Client / cgame** (C) | `████████████████████  9/9` | C12: the scoreboard panel is an ad slot, not a levelshot |
| **Renderer** (R) | `█████░░░░░░░░░░░░░░░  2/8` | Vulkan runs and draws text |
| **Weapons** (W) | `░░░░░░░░░░░░░░░░░░░░  0/4` | W1/W3 are vanilla-only — invisible in our client |
| **Engine / server** (E) | `████████░░░░░░░░░░░░  7/14` | E11: map_restart ran GAME_INIT twice |
| **Overall** | `████████████░░░░░░░░  31/52` | by binary: 13 server · 36 client · 3 both |

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
| ● | **E11** | The match never starts, and kills score nothing | DONE (verify) |
| ● | **E12** | Nothing was written when Windows crashed | DONE (verify) |
| ● | **E13** | More than ~25 bots crashed the server | DONE (verify) |
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
| ● | **C12** | Scoreboard picture panel drew a weapon icon | DONE (verify) |
| ◐ | **C13** | Scoreboard is empty with a full server | PARTIAL |
| ◐ | **C14** | Match summary: no cursor, no voting, no winner, no arena shots | PARTIAL |
| ● | **C16** | Score tracker never shows the player's score | DONE (verify) |
| ● | **C17** | A2M instagib dropped weapons on death | DONE (verify) |
| ○ | **C18** | Scoreboard K/D, damage and accuracy wrong for everyone but you | OPEN |
| ○ | **C15** | HUD score tracker shows 1st and 2nd, not 1st and yours | OPEN |
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
| ● | **E14** | Nothing verified that the loaded module matched the shipped one | DONE (verify) |
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
| ● | **E11** | The match never starts, and kills score nothing | DONE (verify) |
| ● | **E12** | Nothing was written when Windows crashed | DONE (verify) |
| ● | **E13** | More than ~25 bots crashed the server | DONE (verify) |
| ○ | **E4** | ZMQ stats feed absent | OPEN |
| ● | **E14** | Nothing verified that the loaded module matched the shipped one | DONE (verify) |
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
| ● | **C12** | Scoreboard picture panel drew a weapon icon | DONE (verify) |
| ◐ | **C13** | Scoreboard is empty with a full server | PARTIAL |
| ◐ | **C14** | Match summary: no cursor, no voting, no winner, no arena shots | PARTIAL |
| ● | **C16** | Score tracker never shows the player's score | DONE (verify) |
| ● | **C17** | A2M instagib dropped weapons on death | DONE (verify) |
| ○ | **C18** | Scoreboard K/D, damage and accuracy wrong for everyone but you | OPEN |
| ○ | **C15** | HUD score tracker shows 1st and 2nd, not 1st and yours | OPEN |
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

### C12. Scoreboard picture panel drew a weapon icon — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

Reported as "the level image is not displayed on the scoreboard". It is not a
level image: Quake Live's `ingame_scoreboard_*.menu` puts `ownerdraw UI_ADVERT`
in that panel, with `style WINDOW_STYLE_SHADER` and
`defaultContent "textures/ad_content/ad2x1.jpg"`.

Our owner-draw table had `case UI_ADVERT` calling `CG_DrawWeaponIcon`, so the
advertisement slot painted a weapon. It is now a no-op, which lets the menu
system paint the item's own `defaultContent` — the same thing Quake Live shows
without a live ad, and this build has no ad server (`RE_Get_Advertisements`
reports none in both renderers).

### C13. Scoreboard is empty with a full server — PARTIAL
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

With 40 players the in-game scoreboard and the end-of-match summary both draw
no rows at all, and the client's own stats are wrong (a player with one frag was
credited with fourteen kills).

A scoreboard is **one reliable command**. `trap_SendServerCommand` lands it in
`client->reliableCommands[][MAX_STRING_CHARS]` and it is truncated there,
silently, at 1024 bytes. Every emitter declared its buffer at or above that
(`char string[1400]` in FFA, `[1024]` in the other seven) and then guarded on
`sizeof(that buffer)` — so the guard could not fire before the truncation did,
and the command name plus three leading numbers were never counted at all.

Truncation does not just drop the tail players. **The entry count is in the
header**, so the client is told to read N players' worth of fields out of a
string that stops mid-entry: every field past the cut is read from whatever
follows, or from nothing. That is a scoreboard with garbage in it, which is why
the local player's kills were wrong as well as the rows being missing.

Fixed: a shared `MAX_SCOREBOARD_PAYLOAD` (`MAX_STRING_CHARS - 64`) that all
eight emitters guard on, so the count and the entries always agree, and
`G_ScoreboardTruncated` says once per level when players are being dropped
instead of dropping them quietly.

**Chunking is now built, for every emitter that can overflow.** Each one flushes
a chunk whenever the next entry would cross the budget. The first chunk keeps
the original verb's shape and reports only the entries *it* carries, so a stock
Steam client still gets a correct — if short — scoreboard; the rest go out as
`<verb>2 <startIndex> <count> ...`, which a stock client has no handler for and
drops. Done for `scores_ffa`, `scores_tdm`, `scores_ca`, `scores_ctf`,
`scores_ft`, `scores_rr`, `scores_race` and the broadcast `smscores`.
(`scores_duel` is two players and `scores_ad` is a fixed score history, neither
of which can overflow; `tinfo` is capped at `TEAM_MAXOVERLAY`.)

On the client each parser's per-entry read is now a `CG_ParseScoreEntry_*`
function and one `CG_ParseScoresCont(parser)` handles every continuation, so the
first message and its continuations cannot read a row differently — which is how
the parsers and emitters drifted apart in the first place.

`MAX_SCOREBOARD_PAYLOAD` only ever described the FFA header. `scores_tdm` leads
with twenty-eight team totals and `scores_ctf` with thirty-four, some of them
possession times in milliseconds, so those four build their header first and take
the budget from `G_ScoreboardBudget(strlen(header))`. Hiding the opposing team's
totals moved out of the player loop at the same time — it ran once per player and
the viewer does not change between iterations.

`G_ScoreboardTruncated` now reports whenever the sent/total pair changes rather
than once per level, so the log shows the point at which players start being
dropped ("20 of 21", "20 of 22") instead of one line naming whatever the count
happened to be the first time it overflowed. Only `tinfo`, `castats` and the
end-of-match `smscores` per-weapon rows still reach it.

One thing to watch: a full FFA scoreboard is now three or four reliable commands
instead of one, against a 64-slot ring. Both broadcast sites are intermission
only and the client throttles its own `score` request to one every two seconds,
so nothing in normal play multiplies this — but a client that spams `score` costs
four times what it used to.

### E24. Every freeze was destroyed on the frame it happened — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

With the round machine finally running, players froze — and were thawed again
instantly:

```
Biker was railed by Major
Biker was auto-thawed.
Wrack was railed by Klesk
Wrack was auto-thawed.
```

`G_Damage`, right after `targ->die()`:

```c
if (g_freezeAutoThawTime.integer &&
    level.time < targ->client->respawnTime + g_freezeAutoThawTime.integer) {
    Freeze_InstaKill(targ, 1);
```

The comparison is the wrong way round — true for the whole auto-thaw window and
false once it has passed, when the intent is to destroy the body *after* the
window elapses. `targ->die()` has just frozen the player, so `respawnTime` is
around `level.time` and the test passed on the very frame of the freeze. Every
freeze was destroyed immediately, which is why the mode still looked like
ordinary deaths.

Now measured from `ps.freezetime` — the stamp `Freeze_PlayerFrozen` sets — so it
agrees with the per-frame timeout in `Freeze_ClientThawCheck` instead of running
a second clock off `respawnTime`. On the freezing frame the elapsed time is 0, so
it no longer fires there.

`Freeze_Think` also warns once per level, without needing `g_debugFreeze`, if the
round state is still not `RS_PLAYING` well after the countdowns should have
finished. A declined freeze is indistinguishable from a death, and that cost two
rounds of "still not working" with nothing in the log to work from.

### E25. Freeze Tag timings set to the intended rules — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

The rules the mode is meant to run, now that freezing works:

| | before | now |
|---|---|---|
| `g_freezeThawTime` — teammate must stand over you | 2000 | **7000** |
| `g_freezeAutoThawTime` — melt on your own, in a round | 120000 | **0 (off)** |
| `g_freezeWarmupThawTime` — warmup self-thaw fuse | *did not exist* | **5000** |

- **A live round has no auto-thaw at all.** The only way out of the ice is a
  teammate, which is the mode. At 120000 a statue nobody reached melted by
  itself after two minutes and a round could resolve with no thawing done.
- **Thawing takes real commitment.** `Freeze_ClientThawCheck` counts down only
  while a teammate is in range with line of sight, and lets the timer decay back
  up when they leave, so this is continuous presence rather than a total. Two
  seconds meant you thawed people in passing; seven leaves the thawer exposed,
  which is the trade the mode is built on.
- **Warmup freezes too, on a short fuse.** Warmup is practice, so a shot should
  still put you in a statue — it just should not park you there.
  `Freeze_ClientThawCheck` already had a warmup branch that bleeds the timer with
  no teammate needed, so the fuse is just where that timer starts.
  `Freeze_PlayerFrozen` no longer requires `RS_PLAYING` during warmup, and
  `maxThaw` follows the same value so the thaw-progress display measures against
  the right number.

### C28. Frozen player presentation — DONE (verify)
**Lives in:** our **client** (cgame) + `pak01` · **Seen by:** our client only

Three things reported about a frozen player, all fixed:

- **The ice looked wrong.** `sprites/frozen` was painted flat over the model as a
  `customShader`, which washed the whole character out to a pale blue silhouette.
  It is now drawn the way the *quad shell* is — the model rendered a second time
  with an additive `tcGen environment` shader — so it reads as a coating catching
  the light instead of a repaint, and the team colours underneath stay legible.
  `powerups/freezeshell` is **our own shader**, shipped in
  `content/pak01/scripts/freeze.shader`, referencing Quake Live's
  `textures/effects/icemap.jpg` by name. Only the name — no asset content, same
  rule as the pak manifest. Falls back to `frozenShader` if pak01 is missing.

- **The statue kept running.** `pm_type` stops a player moving but not their
  animation, so someone shot mid-sprint stayed frozen in a running stride.
  `Freeze_PlayerFrozen` now parks both halves on `LEGS_IDLE` / `TORSO_STAND` with
  `ANIM_TOGGLEBIT` flipped, so the client restarts the animation rather than
  lerping out of the run.

- **The scoreboard kept popping up.** It shows itself automatically while dead,
  and a statue sits at zero health for its whole life. `PM_FREEZE` is its own
  `pm_type`, but for a frame or two around the death `pm_type` is still `PM_DEAD`
  while `PW_FREEZE` is already set — the "sometimes" in the report. The powerup
  is the reliable test, so that is what the auto-show now checks.

`g_freezeThawTime` is **3000** — the value asked for after playing it.

### E26. Bot filler exhausts the slots in Freeze Tag — DONE (root cause found)
**Lives in:** our **server** (qagame) · **Seen by:** server console

```
Unable to add bot. All player slots are in use.
Start server with more 'open' slots (or check setting of sv_maxclients cvar).
```

Chased through three wrong answers before the slot table settled it. Sixteen bots
on a sixteen-slot server, and five of them on `team 3` — TEAM_SPECTATOR:

```
   0 connected bot team 3 "Bones"     3 connected bot team 1 "Lucy"
   1 connected bot team 3 "Visor"     4 connected bot team 2 "Stripe"
   2 connected bot team 3 "Gorre"    14 connected bot team 3 "Phobos"
                                     15 connected bot team 3 "Crash"
```

`G_ReadSessionData` (g_session.c) runs from `ClientConnect` immediately after
`G_InitSessionData`, and this line overwrote the team `PickTeam` had just chosen:

```c
if (g_teamSpawnAsSpec.integer && g_gametype.integer >= GT_TEAM && level.warmupTime) {
    sessionTeam = TEAM_SPECTATOR;
}
```

For a human that is the whole point — reconnect during warmup and pick your own
side. A bot has no menu, so it stays a spectator forever.
`G_CheckMinimumPlayers` counts *per team*, so a spectator bot counts toward
neither: the shortfall never closes, another bot goes in every second, and it
runs until all sixteen slots are bots. A human trying to connect then finds the
server full and the refusal spams the console — exactly as reported.

`level.warmupTime` is `-1` while the server idles pre-game, which is nonzero, so
this was live the whole time the server sat waiting for players. Fixed by
exempting bots (`SVF_BOT`, which `G_AddBot` sets before calling `ClientConnect`).

**Guard added on top**, because the *shape* of this failure recurs with any
counting bug: every number `G_CheckMinimumPlayers` works from is per team, and a
bot the per-team counts cannot see is a bot the filler tries to replace once a
second forever. `G_FillBots` now enforces two limits that do not depend on the
counting being right — leave `BOT_RESERVED_SLOTS` (1) free so a person can always
get in, and when there is no room, kick a bot sitting in spectator rather than
asking for a slot that does not exist (only when out of room, so an admin's
deliberately-spectating bot survives on a server with space). Duel now routes
through `G_FillBots` too, for the same guard.

**Earlier fixes on the way, all real but none of them this:**

- `G_CountBotPlayersWithQueued` skipped `CON_CONNECTING` bots and counted only
  queued entries already due, so a bot inside its spawn delay was counted zero
  times while holding a slot. Its queue loop also had no team filter.
- `G_AddBot` returned on a refused `ClientConnect` without calling
  `trap_BotFreeClient`, leaking the slot `trap_BotAllocateClient` had already
  moved to `CS_ACTIVE`.
- `SV_DropClient` parks a departing human in `CS_ZOMBIE` for `sv_zombietime` so
  the final reliable message can be retransmitted, and `SV_BotAllocateClient`
  only looked for `CS_FREE`. On a nearly full server that alone refused the
  replacement bot. It now takes a zombie when nothing is free; live clients
  untouched.

**Diagnostics kept** on both sides — the game's slot table and the server's own
`client_t` states. They are what found this, and together they show whether the
game and the server disagree about who holds what. Refusals are throttled to one
a second.

### E27. Warmup countdown allowed firing — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

Players could freeze each other during the countdown to match start, carrying a
freeze into a round they had no say in.

Warmup is two different things and they want opposite rules. The open-ended
practice period before a match — `level.warmupTime == -1`, nobody counting down —
is practice: shooting should work and a hit should freeze, on the short
`g_freezeWarmupThawTime` fuse (an earlier request, unchanged). The countdown once
the match has been called is not practice.

`level.warmupTime > 0` is that countdown, `RS_COUNTDOWN` the round-based
equivalent — the same pair `g_combat.c` already tests to shorten the respawn
during a countdown. `ClientThink_real` now sets `PMF_RESPAWNED` for the duration,
which is the lockout `PM_Weapon` already honours ("don't allow attack until all
buttons are up") and lives in `pm_flags`, so it is networked and the client
predicts the same result — the gun stays quiet on both sides instead of flashing
locally for a shot the server discards. Re-set each frame because `PmoveSingle`
clears it on release.

Movement is deliberately untouched: `GT_AD` locks players in place during its
countdown, Freeze Tag does not, and that was not what was asked for.

### E28. "I can unfreeze enemy players" — explained, not a team-check hole
**Lives in:** our **server** (qagame) · **Seen by:** every client

The warmup branch of `Freeze_ClientThawCheck` does not check teams, because in
warmup nobody thaws anybody — the statue expires on its own fuse. A frozen enemy
coming back five seconds after you walked over is indistinguishable from having
thawed them.

The live-round path tests `sess.sessionTeam` in the box scan *and* again in the
re-resolve (`Freeze_ThawerStillEligible`), so a cross-team thaw cannot start or
complete there. No third path clears `PW_FREEZE`: frozen players are damage-immune
(`g_combat.c` step 22 sets `take = 0`), so shooting a statue cannot respawn it
either.

With firing disabled during the countdown (E27), the warmup branch stops being
reachable at match start. `g_debugFreeze 1` now names a warmup self-thaw when it
happens, so this is attributable rather than inferred.

### C29. Thaw fails intermittently — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

Reported as thawing working about half the time.

Two causes, both in `Freeze_ClientThawCheck`, and the line-of-sight one is the
one that matches the report.

**The LOS trace was at foot level.** With `g_freezeThawThroughSurface 0` the
binary traces `ps.origin` → `ps.origin`, a point trace against `CONTENTS_SOLID`,
and rejects the teammate unless it is completely clear. Those origins sit 24
units off the floor, and the two players are inside a 96-unit radius, so on flat
ground the line is clear and thawing works. Everywhere else it does not: a stair
tread, a ramp, a jump-pad lip, the edge of whatever you are standing on, or the
statue having died a step further down a slope all put brush between two players
who are looking straight at each other. The thaw then silently never starts, and
one step sideways fixes it — which is what "half the time" was.

`Freeze_ThawVisible` now tests at chest height first and falls back to the
original foot-level line only if that is blocked (crouched, or a low ceiling);
either passing is enough. `startsolid`/`allsolid` counts as visible — it means
the trace began inside a brush, which says nothing about what is between the two
players, and blocking on it would strand a statue for the rest of the round.

**The thawer lock was never released.** `ps.thawClientNum` was set on the first
frame a teammate was found and `thawClientNum_valid` was only cleared on a frame
with no teammate at all. If the locked teammate walked off, got frozen or died
while somebody else was still in range, the re-resolve loop searched for a client
number that was no longer there, found nothing, and left the stale lock in place
— so the thaw ran to completion crediting the assist to a player who was not
performing it, and on frames where the lookup *did* find them the eligibility
filters were never re-applied. The lookup now re-checks eligibility
(`Freeze_ThawerStillEligible`) and drops the lock when it cannot be honoured,
re-locking onto whoever the scan actually found this frame.

`g_debugFreeze 1` now also prints the rejected-for-line-of-sight case by name.

**Third cause, found after the LOS fix did not settle it** — reported back as
thawing still taking a long time.

Thaw progress did not pause when contact broke, it *reversed*, at the same rate
it advanced: `ps.thawtime += msec` on any frame with no valid thawer. The scan
demands the teammate be inside a 96-unit box *and* pass the line-of-sight trace
every single frame, which is not a stable enough test to hang that on — a
teammate circling the statue, clipping a pillar or drifting to the edge of the
radius gives progress back as fast as they earn it, so three seconds of standing
there completes nothing. Halved contact means net zero.

**Settled with a grace window rather than a rate.** Two different things look
identical for one frame: a thawer who has stepped behind a pillar or drifted to
the edge of the radius, and one who has given up and walked off. Both earlier
attempts treated them the same and were wrong in opposite directions — undoing a
frame of progress per frame meant no thaw ever finished on uneven ground, and
refilling at a quarter rate fixed that but made leaving almost free, so a statue
could be chipped at across a whole round.

The distinction is *time*, so time is what is measured. `lastThawContactTime` is
stamped on every frame with a valid thawer; inside `g_freezeThawGrace` (default
500 ms) the progress simply holds, and past it it snaps back to full. A real
reset when someone walks away, and a corner or a bad trace costs nothing.
`g_debugFreeze 1` names the reset and how long contact had been lost.

**Fourth: the reach was too small.** `g_freezeThawRadius` is a real cvar —
`Freeze_ClientThawCheck` reads it every frame to size the `trap_EntitiesInBox`
scan, so it was always tunable and is not one of the 186 dead ones. The default
was just mean: 96 units is a box of ±96 around the statue, and since
`EntitiesInBox` tests bounding boxes rather than origins the effective reach was
about 111 — roughly a player and a half. That does not survive either player
drifting, which is the same brittleness that made the LOS test and the progress
decay feel broken. Raised to **160** (about 175 effective): still a commitment
with someone shooting at you, but circling the statue or backing off a step no
longer drops the thaw.

Verify: thaw a teammate on a staircase and on a ramp; `g_debugThawTime 1` prints
the countdown to the thawer every frame if the rate needs measuring.

### C30. Rail colour by gametype — DONE (verify)
**Lives in:** our **client** (cgame) · **Seen by:** our client only

The stock rail takes its colour from the shooter's own `color1`/`color2`
userinfo. In a team mode that is backwards — the one thing a beam could tell
you, whose side fired it, is replaced by a colour that player picked for
themselves. In a free-for-all it is fine, and it is the player's own choice, so
it stays there.

QL's own `cg_forceTeamRailColor1`/`2` do not fill the team gap: they are a
viewer-side friend/foe override with two arbitrary colours, and
`CG_GetRailColorFloat` returns early for `viewer == owner`, so your own rail is
never touched by them.

`cg_railColorMode` (cgame, default 1):

| value | team gametypes | FFA / Duel / Race |
|-------|----------------|-------------------|
| `0` | player's own colour | player's own colour |
| `1` | **team colours** | player's own colour, or **yellow** if they never picked one |
| `2` | **team colours** | **a distinct colour per player** |
| `3` | white | white |

**Yellow for an unset colour.** `CG_ColorFromString` gives white for anything
outside 1..7, which is where an unset or missing `color1` lands — and 7 is white
too, so a real choice of white and the no-choice default are indistinguishable.
Yellow is the better thing to do with it either way: a white rail is the hardest
to pick out against a bright skybox, and it is what a player gets by doing
nothing, so it is what most rails on a public server end up being.

The per-player colour is a hash of the client slot, not a random draw. Two
constraints rule a draw out: every client has to agree on what colour a given
player's rail is, or the same shot is a different colour on each screen and the
colour stops meaning anything; and it has to hold still for the match, or the
beam strobes and is worse than the one colour the player chose. `clientNum *
0.618` and take the fraction as a hue — the golden-ratio trick, so neighbouring
slots land far apart (orange, blue-green, purple, yellow) instead of walking a
rainbow ramp.

Applied in `CG_GametypeRailColor`, called from the top of `CG_GetRailColorFloat`
and `CG_GetRailColorByte`, plus the railgun model tint and the muzzle blast — so
the core beam, the ring sprites, the impact mark, the gun in your hands and its
flash all agree. `>= GT_TEAM` is the team test; `GT_FFA`, `GT_DUEL` and
`GT_RACE` sit below it in QL's enum and everything above is a team mode.

Note on "let the player pick their colour": that already exists and always has —
it is the `color1`/`color2` userinfo, which is what mode 0 and mode 1-in-FFA
fall through to. QL's own player-options menu lives in `pak00.pk3`, which we
cannot ship or edit; `/color1 <n>` and `/color2 <n>` at the console set the same
values the menu would.

### C31. Team overlay carried no data — DONE (verify)
**Lives in:** our **client** (cgame) + **server** (qagame) · **Seen by:** our client only

Every overlay row read `name / unknown / 0 / (red no-entry icon) / 0`.

Matching QL's flat `tinfo` fixed the parse errors that were dropping clients on
join, but it also removed everything the overlay draws. Location, health, armour
and weapon are Quake 3 `tinfo` fields; QL's client gets teammate state from the
snapshot instead and ours does not, so `ci->location` was 0 (`CS_LOCATIONS + 0`
is empty, hence "unknown"), health and armour were 0, and `ci->curWeapon` 0 has
no icon — which is what the red circle was, `cgs.media.deferShader` standing in
for a missing weapon icon.

Fixed with a companion command rather than by breaking `tinfo` again. `tinfo`
keeps the exact shape a stock Quake Live client expects; `tinfo2` is ours, six
ints per player — client number, location, health, armour, weapon, **frozen**.

**Correction: "a client that does not know the verb ignores it" was wrong.** A
stock Quake Live client prints `Unknown client game command: tinfo2` for every
one it receives, and this goes out twice a second per player — a vanilla client
on our server gets a console full of nothing else. Any command stock QL has no
handler for has to be *gated*, not just assumed harmless.

So the server now knows who can parse what. Our engine registers `iqlclient` as a
`CVAR_ROM` userinfo cvar (`cl_main.c`), which arrives with the connect packet;
`ClientUserinfoChanged` reads it into `pers.extendedClient`, and
`TeamplayInfoMessage` returns before the `tinfo2` block for anyone without it.
`CVAR_ROM` because it identifies the binary — a user setting it by hand would
only be lying to the server about what their client can parse.

A stock client loses the overlay detail and nothing else: `tinfo` is the
QL-shaped message and still goes to everyone. That is the right trade, and
`pers.extendedClient` is the gate for any future extension command. Roughly 200 bytes at
`TEAM_MAXOVERLAY` 8, well inside the 1024-byte reliable limit, with the same
`MAX_SCOREBOARD_PAYLOAD` guard.

Frozen teammates show as an ice-blue row with **`FROZEN` in the location
column** — the only text field on the row, so it is where a word belongs, and
where a frozen player is matters far less than that they are frozen and need
fetching.

That column now reads **FROZEN → real location → ALIVE**. It answers a question
with two useful states in Freeze Tag: is that teammate a statue, or still
playing. Where they are is the bonus, and most maps do not offer it.

The first attempt at this did not work, and for an instructive reason: index 0 is
the sentinel, not an empty string. `G_LinkLocations` writes the literal
`"unknown"` into `CS_LOCATIONS + 0` (g_target.c) and hands that index to every
player the map has no `target_location` for, so testing the *returned string* for
empty never fired — it came back with six characters in it every time. The test
that works is `ci->location > 0`.

Health and armour keep their real numbers (0 0 for a statue) rather than being
replaced by the word. The row stays ice blue rather than going through
`CG_GetColorForHealth`, which would paint a statue critical-red off its zero
health — and red is already what a *dying* teammate looks like.

**No weapon icon on those rows.** There is nothing to draw: `PM_Weapon` sets
`ps.weapon` to `WP_NONE` while health is <= 0, so a statue's `curWeapon` is
`WP_NONE` and the icon fell through to `deferShader` — the red no-entry circle.
It also sat badly: the icon is placed three characters into a field sized for
`"%3i %3i"`, landing in the gap between the two numbers.

### C32. Frozen players kept animating — DONE (verify)
**Lives in:** our **client** (cgame) · **Seen by:** our client only

Parking a frozen player on `LEGS_IDLE`/`TORSO_STAND` server-side stopped the run
cycle but not the motion — idle is still an animation, so the statue breathed and
swayed. There is no way to say "one frame" from the server: an animation is a
range in the model's `animation.cfg` and the client lerps through it.

`CG_PlayerAnimation` now collapses `oldFrame` onto `frame` with zero backlerp for
any entity carrying `PW_FREEZE`. That holds the exact pose the player was hit in —
which is better than a reset to a neutral stance — and stops the interpolation
dead.

### C33. Matching Quake Live's freeze visuals — IN PROGRESS
**Lives in:** our **client** (cgame) · **Seen by:** our client only

Side by side with the Steam client, ours had **erased the player**: a pale
white-pink silhouette with no team colour and no skin. Quake Live's frozen player
stays completely readable — you can see the blue armour and the model detail
through angular, semi-transparent ice.

The layering was already right: `CG_AddRefEntityWithPowerups` draws the model
normally and *then* adds a second pass with `customShader`, the same as the quad
shell. The fault was entirely in the shader — two `GL_ONE GL_ONE` stages of a
bright environment map add roughly twice the map's brightness on top of the
model, which saturates to white.

**Second miss: the shell has to stand off the model.** QL's ice *hovers* around
the player rather than clinging to the surface, which is what makes it read as a
block of ice with someone inside instead of a shiny skin. A second pass of the
same mesh is skin-tight by definition, so the geometry has to be pushed outward,
and the only way a shader moves geometry in idTech3 is `deformVertexes`:

```
deformVertexes wave <div> <func> <base> <amplitude> <phase> <freq>
```

moves each vertex along its own normal. The **base** is the constant push — the
standoff — with the amplitude a slow breathing on top so a statue is not inert,
and `div` spreading the phase by vertex position so the surface flexes rather
than inflating as one rigid ball. Verified reachable on player models:
`RB_CalcDeformVertexes` offsets along `tess.normal`, and `RB_DeformTessGeometry`
runs in the generic stage iterator that MD3s use — the VBO fast path that skips
deforms is world geometry only.

Three variants ship in `content/pak01/scripts/freeze.shader`, selected by
**`cg_freezeShell`** (default 1), sampling the two axes still open — how far the
shell stands off, and whether it reads as glass or as glow:

**Third: white, and no pulsing.** `textures/effects/icemap.jpg` carries its own
blue and `rgbGen` only *scales* what a texture already has — there is no way to
desaturate one in the fixed pipeline. So the coat is built on `$whiteimage`, the
renderer's built-in 1×1 white, and the shape comes from `alphaGen
lightingSpecular` instead of from the map: opacity follows the light, which is
what reads as facets. The blue map is dropped rather than tinted down.

Ice does not breathe, either, which takes some care because the deform is spelled
as a wave. Amplitude 0 with frequency 0 is the constant case —
`RB_CalcDeformVertexes` has a `frequency == 0` branch that evaluates the waveform
once and pushes every vertex by that fixed amount — so the shell holds its shape.
Same reason the sheen is `rgbGen const` rather than `rgbGen wave`, and there is no
`tcMod` anywhere.

**Fourth: the glow is a second shell.** One shader gets one `deformVertexes`, so a
halo standing further off the model than the coat cannot be another stage — it has
to be its own shader and its own `trap_R_AddRefEntityToScene`.
`powerups/freezeglow` sits at 14 units with `blendfunc GL_SRC_ALPHA GL_ONE`,
additive weighted by alpha so `lightingSpecular` concentrates it where light
catches the hull and lets it fall away elsewhere; a flat additive shell that size
reads as a pale silhouette rather than a glow. Faint on purpose — it is drawn
three times per player (legs, torso, head) and accumulates where the parts
overlap.

**Fifth: too big, and the halo blew out.** All standoffs halved, and the halo
dropped from `rgbGen const 0.55` to a twelfth of that — it is drawn once per model
part (legs, torso, head), so it accumulates wherever those overlap and a value
that looks mild on one surface saturates a whole player.

**Sixth: the halo was the wrong idea entirely.** A glow around the silhouette
reads as a powerup, not as ice — style 1 or 2 with the effect *off* looked best.
Replaced with animated overlays: a stage just outside the coat that moves the
texture while the shell itself stays still.

**Seventh: the shell shrinks as the thaw progresses.** A shader cannot read game
state, but the server already publishes it — `Freeze_ClientThawCheck` buckets
`ps.thawtime` into thirds and writes the bucket into the low bits of `generic1`,
`BG_PlayerStateToEntityState` copies it into the entity state and `msg.c` networks
it. Each style ships three coats at decreasing standoff and cgame picks by
`generic1 & 3`, so the ice closes in while a teammate works on the statue instead
of holding one size until it pops.

That needed a server fix too: the bits only ever *accumulated*. `bit1` was set
once `thawtime` fell below a third and nothing cleared it until the timer climbed
back above two thirds, so a statue whose thawer walked away stayed drawn at its
thinnest all the way back up — the progress went one way. Both bits are now
cleared before the bucket is written.

| `cg_freezeShellStyle` (the coat) | |
|---|---|
| `1` | **blue, close** (default) — QL's ice environment map, 2 → 0.5 units |
| `2` | **white, close** — flat white, 2 → 0.5 units |
| `3` | **blue, wide** — as 1, 5 → 1.2 units |
| `4` | **white, wide** — as 2, 5 → 1.2 units |

| `cg_freezeShellEffect` (animated overlay) | |
|---|---|
| `0` | **off** (default) — static coat only |
| `1` | slow swirl — the environment map rotating in place |
| `2` | turbulent shimmer — `tcMod turb`, so the reflection ripples |
| `3` | animated frames — a two-frame `animMap` over QL's `envmapblue`/`envmapblue2` pair |
| `4` | crawling frost — `tcGen base` + scroll, fixed to the body rather than the view |

Twenty combinations; every shader registers at load so neither cvar needs a
`vid_restart`.

All three are registered at load, so switching the cvar takes effect without a
`vid_restart`, and a missing one is reported rather than silently drawing nothing
(`RE_RegisterShader` returns 0 for a name the pak lacks).

**Still not done — the damage layer.** From `docs/pak-manifest.txt`, QL swaps
blood for ice on a frozen target and shatters with a ball sprite:

| name | almost certainly |
|------|------------------|
| `gfx/damage/ice_spurt.png` | blood spurt replacement on a frozen player |
| `gfx/damage/ice_stain.png` | blood stain / impact mark replacement |
| `gfx/misc/iceball.png` | shatter / gib particle |
| `icons/thaw.png` | thaw medal icon (the assist award reuses the generic one) |

Three registrations plus a branch in the missile-hit path. Confirmed again that
there are **no ice meshes** in the pak — the whole effect is shader and sprite
over the ordinary player model, so anything model-shaped is the wrong direction.

### E31. Joining mid-round demoted you to spectator, permanently — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

Reported as: leave and reconnect and you are stuck in spectator, no join-team
buttons, and it survives the round ending *and* a map change. The scoreboard
showed two bots stuck there next to the player.

`Freeze_ClientBegin`'s round-active branch puts a player who arrives while a
round is live into a follow, so they watch until the next round. It did that
through `Cmd_FollowCycle_f`, whose first act is:

```c
if (ent->client->sess.sessionTeam != TEAM_SPECTATOR) {
    SetTeam(ent, "spectator");
}
```

That is right for the console command — asking to follow *is* giving up — and
wrong here: the branch is explicitly guarded on the player still having a team.
So joining mid-round moved you to `TEAM_SPECTATOR` for good. `SetTeam` writes the
session, which is why it survived the round ending, a map change and
reconnecting, and the round restart only respawns players who still have a team,
so nothing ever brought you back. Bots joining mid-round demoted themselves
identically — that is where the spectator bots came from, and the earlier
`g_teamSpawnAsSpec` theory was wrong (that cvar defaults to 0 and never fired).

Split into `G_FollowCycle(ent, dir, keepTeam)`; `Cmd_FollowCycle_f` passes
`qfalse`, `Freeze_ClientBegin` calls `G_FollowCycleKeepTeam`. Mid-round joiners
get the camera without the consequence and play the next round.

**Also made a silent refusal audible.** `SetTeam`'s `g_maxGameClients` override
turned a join into a spectate without a word, which from outside looks exactly
like the join being ignored — the same shape, and just as hard to tell from a
bug. Every other refusal in that function prints; this one now does too.

**Hardened the session record while in here.** `G_ReadSessionData` sscanf'd into
three uninitialised locals and assigned them unconditionally; a short or missing
record left `sess.sessionTeam` holding whatever was on the stack, and
`G_WriteSessionData` then persisted it. It now parses into an array, requires all
13 fields and an in-range team, and otherwise keeps what `G_InitSessionData`
chose. Not the cause of this bug, but the same failure shape.

**Noted, not changed:** `g_teamAutoJoin` and `g_teamForceBalance` are registered
`CVAR_ARCHIVE` on defaults we choose, which is the trap in CLAUDE.md — a stale
server config pins them and changing the shipped default does nothing. Left alone
so this build carries only the fix under test.

### E32. Spurious kamikaze explosions on busy maps — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

Reported on a CTF castle map: players dying to `MOD_KAMIKAZE` with nothing on
screen to explain it, and movement feeling broken. One cause for both.

`G_FreeEntity` calls `G_StartKamikaze` on **every** freed entity — temp entities,
missiles, `info_null`, movers, triggers — so that function is the only thing
standing between an ordinary entity being freed and a kamikaze going off. Its
non-client guard was:

```c
} else if (!ent->activator) {
    return;
}
```

`activator` is an ordinary field. `G_UseTargets` stamps it on every entity a
trigger fires, movers carry it, and plenty of map logic sets it. So on a map with
real trigger and mover traffic — a castle, with doors and plats and teleporters —
routine entity frees were detonating kamikazes. Which is also why it looked
map-specific: quiet maps never accumulate enough of them to notice.

The real detonator has a name. `player_die` spawns it as classname
`"kamikaze timer"` with `think = G_FreeEntity`, so the free *is* the detonation,
and `GibEntity` already finds the same entity by that name. The guard tests for
it now.

**And that is very likely the movement report too.** `KamikazeShockWave` does not
add knockback, it *overwrites* velocity:

```c
ent->client->ps.velocity[0] = dir[0] * push;   // push = 400
ent->client->ps.velocity[1] = dir[1] * push;
ent->client->ps.velocity[2] = 100;
```

Whatever you were doing is discarded and replaced with a 400-unit shove away from
the blast plus 100 up. With explosions firing off routine entity frees, that is
players being yanked around at random — and the per-entity `kamikazeShockTime`
cooldown of 3 s makes it intermittent rather than constant, which is what
"movement seems broken" describes. Unconfirmed until retested: if movement is
still wrong once the explosions stop, it is a separate problem and worth its own
entry.

### E33. a2m-instagib-ctf.cfg — DONE
**Lives in:** content · **Seen by:** server admins

Instagib CTF, following the same chain as the other A2M modes (`exec
a2m-common.cfg`, railgun via `g_startingWeapons 64`, everything else locked out
via `g_disableLoadout 32638`). Takes `capturelimit`/`timelimit` and the team
cvars from `ctf.cfg` rather than instagib's `fraglimit`, since CTF has no
fraglimit and a capture is worth more than a frag. Passes
`content/serverconfigs/check-configs.py` — 9 mode configs now.

### E34. Japanese-castle CTF map — several symptoms, one fix so far
**Lives in:** our **server** (qagame) mostly · **Seen by:** every client

Reported together: stuttering, doors misbehaving and crushing, wrong spawn bases,
invisible players, buggy stairs, and a crash switching to the map "infinity".
Recording what is settled and what each remaining one needs, rather than
guessing at six things at once.

**Settled — spurious kamikaze explosions (E32).** Has a direct mechanism for the
stuttering *and* the doors. Each bogus explosion spawns an entity whose think runs
`trap_EntitiesInBox` over the map and applies radius damage every frame for the
kamikaze duration; a door-heavy map frees enough activator-carrying entities to
have many running at once, which is a server frame-time problem, i.e. stuttering
for everyone. And `Blocked_Door` calls `G_FreeEntity` on any non-client blocker —
a gib, a dropped weapon, a missile — so a door crushing debris *was itself* a
detonation trigger, going off at the doorway. `KamikazeShockWave` then overwrites
velocity outright (400 out, 100 up) and `KamikazeRadiusDamage` does 400 damage,
which is being shoved into the door you are standing in and taking damage for it.
Retest before anything else here.

**Settled — silent CTF spawn fallback.** `SelectCTFSpawnPoint` falls back to
`SelectSpawnPoint(vec3_origin, ...)` when the map has no
`team_CTF_redspawn`/`bluespawn` for that team — and that picker is *team-agnostic*,
choosing any `info_player_deathmatch`. So both sides spawn across the map, which
is exactly "blue player standing in the red castle". It did that without a word.
It now warns once per team per level and names the entity the map is missing.
`TEAM_BEGIN` uses a different set again (`team_CTF_redplayer`/`blueplayer`) that
maps routinely omit, and the warning distinguishes them.

**Not settled — invisible players.** The screenshot shows shadows and a floating
name with no model, and that combination is informative: `CG_Player` returns
early when `ci->legsModel` is 0, so nothing would draw at all. A name means the
model handle is *not* zero and the models did load. So this is not the
registration failure it looks like, and the candidates are elsewhere —
`RF_THIRD_PERSON` being applied to the wrong entity, a forced-model/skin
resolving to something invisible, or a renderer-side failure.

Discriminator: `CG_Player` already prints
`WARNING: client N (name) has no player model loaded and is drawing nothing` with
the model and skin names. If that line is in the console it is a load failure; if
it is absent, the model loaded and something is hiding it. Worth checking
`com_hunkMegs` either way — it is `CVAR_ARCHIVE` on a default we choose, the trap
in CLAUDE.md, so a stale config can pin it low and starve model loading on a big
map.

**Update — the stutter is on every CTF map, not just this one.** That makes it
gametype-wide, and CTF is the mode with the most entity churn: flags dropped,
returned and respawned constantly, and flag stands that fire targets (which is
`G_UseTargets`, which is what stamps `activator`). Every one of those frees was a
kamikaze candidate before E32, so "various degrees across CTF maps" fits how much
churn each map generates. Retest before looking further. If it survives the fix,
the next suspects are `CheckTeamStatus` — `Team_GetLocation` is O(clients ×
locations) with a `trap_InPVS` per candidate, once a second — and the reliable
command traffic from `tinfo`/`tinfo2`, which is two per client per second now.

**Not settled — stairs.** `STEPSIZE` is 18 units; a castle with taller steps is
map design rather than a bug. But stutter and a velocity overwrite both look like
bad stair movement, so retest after the kamikaze fix.

**Not settled — crash on "infinity".** Needs the server console output or
`crashlog.txt`. Nothing to go on otherwise.

### E35. Player rendering review — findings
**Lives in:** our **client** (cgame + renderervk) · **Seen by:** our client only

Requested review of the player render and entity paths before testing. Three
findings, two acted on, plus one correction to something I said earlier.

**Correction first.** I claimed a floating name proved `ci->legsModel` was
non-zero, because `CG_Player` returns early when it is 0. That was wrong: the
name comes from `CG_DrawCrosshairNames`, which reads `cg.crosshairClientNum` from
a **trace** (cg_draw.c) and looks up `clientinfo[].name`. It never touches the
model. So a name over an invisible player says nothing either way.

**1. Alpha-zero team tint hides players silently — fixed.**
`CG_PlayerTeamSkins` runs for every player in every gametype and writes
`shaderRGBA` from packed `0xRRGGBBAA` cvars. There was a guard for a colour that
is *entirely* zero (falls back to white) but not for one with real RGB and no
alpha — and that is the shape a plausible config value takes: write the colour
the natural way as six hex digits and `0x2a8000` parses to `0x002a8000`, alpha
`00`, a fully transparent player with nothing said anywhere.

Every one of these cvars is `CVAR_ARCHIVE` — the trap in CLAUDE.md — so such a
value lives in the user's config and follows them across builds and reinstalls,
long after the shipped default changed. It also fits "*most* players are
invisible" exactly: team and enemy colours are separate cvars, so one bad value
hides one side only.

`CG_ValidTeamSkinColor` now treats alpha 0 as "no value", falls back to the
shipped default, and names the cvar once.

**2. Dropped scene entities were developer-only — fixed.**
`RE_AddRefEntityToScene` drops everything past `MAX_REFENTITIES` (1023) with a
`PRINT_DEVELOPER` line, i.e. invisible unless someone has already guessed the
cause. The symptom is things missing from the scene with no explanation. Now a
rate-limited `PRINT_WARNING` with a running count. It matters more than it did: a
frozen player costs three refEntities per model part rather than one (model, ice
coat, animated overlay), and every powerup shell is another pass.

**3. Snapshot entity drops are already instrumented — no change.**
`SV_AddEntToSnapshot` warns every 10 s with a count once `MAX_SNAPSHOT_ENTITIES`
(256) is hit, which is the server-side version of the same failure. Worth knowing
the ordering: `SV_AddEntitiesVisibleFromPoint` walks entities in number order and
clients occupy 0..maxclients-1, so players are added first and are the *last*
thing to be dropped. That is ordering luck rather than design, but it does mean
snapshot overflow shows up as missing items before missing players.

**Not a finding, but where to look next if the tint is not it:** `CG_Player`
gates on `cent->currentState.number == cg.snap->ps.clientNum` for
`RF_THIRD_PERSON`. That is correct while following (the followed player *should*
be hidden in first person), but it is the one remaining path that hides a
specific player rather than all of them.

### E36. Codebase audit: incomplete calls, stubs, silent failures — DONE
**Lives in:** **both** binaries + renderervk + tooling · **Seen by:** our client only

**The system, not the sweep.** A one-off audit goes stale the day after it is
written, so the deliverable is `tools/stub-report.py` plus
`docs/stub-manifest.txt`, the same shape as `tools/dead-cvars.py` for the cvar
version of this problem. `package-release.sh` runs it and **fails the build on an
unclassified stub**.

The point is not that every empty function is a bug — most are correct. The point
is that each one should have been *looked at once* and the answer written down,
so a genuinely missing implementation is on a list instead of in the game, and a
new empty function added next month shows up as UNCLASSIFIED rather than blending
into the seventy-odd that were already there.

Four verdicts: **BY-DESIGN** (empty is correct), **UPSTREAM** (empty in Quake 3 /
ioquake3 / Quake3e too, and filling it would diverge from the base for nothing),
**WIRED-ELSEWHERE** (behaviour exists, in the function the reason names), **GAP**
(genuinely missing — the list to work from).

Current state, 76 empty bodies outside vendored trees:

| verdict | count | |
|---|---|---|
| BY-DESIGN | 56 | 44 of them are `code/null/` — the headless drivers the dedicated server links instead of a client, renderer, sound and input layer. Implementing any would be *wrong*. The rest are marker entities (`SP_team_CTF_*`, `SP_misc_teleporter_dest`, `SP_info_player_intermission`), dispatch-table no-ops (`RB_SurfaceSkip`) and the passive console. |
| UPSTREAM | 18 | `CG_RunMenuScript`, `CG_AddParticleShrapnel`, `Weapon_Gauntlet`'s siblings, `QAL_Shutdown`, `S_AL_SoundList`, the `Sys_GLimp*` hooks, `UI_Shutdown`, `Item_StopCapture`. |
| WIRED-ELSEWHERE | 2 | `Weapon_Gauntlet` (damage is in `CheckGauntletAttack`), `DuelScoreboardMessage_impl` (duel goes out as `scores_duel`). |
| GAP | 0 | |

**The one GAP it found, now fixed.** `CL_PostProcessRestart_f` was registered as
the `postprocess_restart` console command and did nothing — typing it was
silently ignored, which is worse than not having the command, because the name
promises something.

There turns out to be nothing distinct for it to do on **either** renderer, and
for different reasons. On Vulkan, `vk_update_post_process_pipelines()` runs from
the renderer-cvar-modified block in `tr_cmds.c`, so bloom, HDR and capture rebuild
the moment anything in `CVG_RENDERER` changes — no window in which a manual
restart would help. On OpenGL2 the post cvars split two ways: `r_toneMap`,
`r_cameraExposure` and the `r_forceToneMap*` cheats are read per frame in the
backend so they apply on the next frame, and `r_hdr` is `CVAR_LATCH` — the engine
already has a mechanism for applying it, `vid_restart`, and that is what latching
means. A half-measure between the two would be a third path to maintain for no
gain.

So it reports rather than pretending. Keeping the command leaves any existing
bind working; removing it would turn a silent no-op into "unknown command", which
is not an improvement for someone who has it bound.

**A note on the tool itself.** The first cut reported `UI_AdjustFrom640` as empty:
it treated any line starting with `*` as a block-comment continuation, which is
also what a pointer dereference looks like (`*x = *x * scale;`). Fixed with real
comment stripping. A scanner with false positives is worse than no scanner,
because it trains you to skim the output.

**Separately — unchecked asset registration.** 276 raw `trap_R_RegisterShader` /
`trap_R_RegisterModel` calls in cgame against 12 that use the checked
`CG_RegisterShaderOr` / `CG_RegisterModelOr` helpers. Both return **0** for
anything they cannot load and every caller treats 0 as "draw nothing", so a
missing, misspelled or unloaded-pak asset is an invisible object with no message
anywhere — the shape behind the invented Freeze Tag asset names, a missing menu
shader and a weapon icon. Auditing 276 call sites is the wrong move; the only
place that knows the name is the registration function, so **`RE_RegisterShader`,
`RE_RegisterShaderNoMip` and `RE_RegisterModel` now print the name they failed
on**, covering cgame, ui and the game module at once.

**`developer` defaults to 1 on this branch** (`common.c`) so those lines and the
`MAX_REFENTITIES` drop are visible without having to guess the cause and re-run.
`CVAR_TEMP`, so it never reaches a config. **RELEASE: set it back to `"0"` before
shipping** — that default is the only switch. The `MAX_REFENTITIES` warning is
back to `PRINT_DEVELOPER` to match.

### E37. Doors never fully open or close, travel percentage jumps — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

Reported on the castle map: doors stuck part-open, rapidly shifting between
percentages of travel. Testing narrowed it precisely — it happens with the
**vanilla Steam client** (so not our rendering or interpolation) and **not** on
the **vanilla Steam server** binary (so it is our `qagame` diverging from QL's).

**The cause, found once the report narrowed to "closing works, opening does
not".** Ordinary doors are triggered by `Touch_PlatCenterTrigger`, and its guard
was:

```c
if (ent->parent->moverState != MOVER_POS2) {     // "not fully open"
    Use_BinaryMover(ent->parent, ent, other);
}
```

Stock Quake 3 guards on `!= MOVER_1TO2` — *not currently opening*. The touch
fires **every frame** a player stands in the trigger, and `Use_BinaryMover` is a
toggle: called while the mover is `MOVER_1TO2` it takes the "only partway up
before reversing" branch and turns the door around. So walking up to a closed
door opened it, and then every subsequent frame in the trigger reversed it —
`1TO2` to `2TO1`, then back to `1TO2` the next frame, flipping at server frame
rate. **That is the percentage jumping**, and it stops the moment you leave the
trigger, which is why the close afterwards looked clean.

With the stock guard the four states behave correctly:

| state | | |
|---|---|---|
| `MOVER_POS1` | closed | open it |
| `MOVER_1TO2` | already opening | **leave it alone** — the fix |
| `MOVER_POS2` | fully open | `Use_BinaryMover` refreshes the auto-close wait, so standing in a doorway holds it open |
| `MOVER_2TO1` | closing | reverse to opening, which is what walking into a closing door should do |

The old guard also broke the `POS2` case: skipping the call there meant the wait
timer was never refreshed, so a door would close on a player standing in the
doorway.

**Also fixed on the way (first attempt at this, correct but not the cause):**
`SetMoverState` had been changed to walk `ent->teamchain` itself, reasoning that
setting every part `STATIONARY` at once stops `G_MoverTeam` re-firing `reached()`
on the slaves. That problem does not exist — `G_MoverTeam`'s success loop already
skips anything that is not `TR_LINEAR_STOP` — and it made `Reached_BinaryMover`
(the *per part* latch, called by `G_MoverTeam` for each part whose own
`trTime + trDuration` has elapsed) drag every part after it to `POS1`/`POS2`
early, then re-latch the whole team as the others reached in turn. Reverted to
the stock split: `SetMoverState` latches one part, `MatchTeam` iterates.

**Checked and cleared**, so they do not get re-suspected:

- `G_MoverTeam`'s blocked branch reads
  `part->s.pos.trTime += level.time - (level.time - level.frametime)`, which
  looks like a botched rewrite of stock's `level.time - level.previousTime`. It
  is algebraically identical: `G_RunFrame` assigns
  `level.frametime = levelTime - level.time` *before* `level.time = levelTime`.
- `Use_BinaryMover` matches stock exactly, including the partial-travel reversal
  arithmetic and the `+50` ms offset on the open.
- `BG_EvaluateTrajectory`'s `TR_LINEAR_STOP` case does clamp negative
  `deltaTime` to 0, so the `+50` open offset holds the door at `pos1` for the
  first 50 ms rather than running it backwards.
- `Touch_DoorTrigger` (the key-door variant) already had the right shape.
- `Blocked_Door` reversing on every blocked frame is stock behaviour; the
  spurious kamikaze shockwaves that were shoving players into doorways are E32.

### C27. Voice chat verbs are unhandled — DONE
**Lives in:** our **client** (cgame) · **Seen by:** our client only

```
Unknown client game command: vchat
Unknown client game command: vtell
```

`G_VoiceTo` sends `vchat`, `vtchat` and `vtell`; there was no cgame handler, and
bots taunt constantly — a full Freeze Tag console was more of that line than
anything else, which pushes the lines you actually need off the top while
diagnosing something else.

Consumed silently now, like `pstats`. Playing them properly needs the voice-chat
script files that map an id to a sound and a line of text (Quake 3 answers these
with `CG_VoiceChat`); swallowing them loses nothing that was working, and the
server side is unchanged, so an implementation drops straight in later.

### C26. Scoreboard player list sat over its column headers — DONE (verify), now tunable
**Update.** One element height overshot and left a visible gap, and I cannot see
the result from here to judge it. The shift is `cg_scoreboardListOffset`, default
8 — raise to push the rows down, lower to bring them up, 0 for the menu's own
geometry. Not `CVAR_ARCHIVE`: it is our layout value, not the user's, and
archiving a shipped default stops the default applying (CLAUDE.md).

Also, the left-hand team's scroll bar now draws on the *outside* edge rather than
between the two lists, so they mirror each other. That needed a new
`WINDOW_LB_LEFTSCROLL` window flag — there is no menu keyword for it — honoured
both by `Item_ListBox_Paint` and by the `Item_ListBox_OverLB` hit test, so clicks
land on the bar where it is drawn. cgame sets it on `FEEDER_REDTEAM_LIST`.


**Lives in:** our **client** (cgame / ui) · **Seen by:** our client only

`Item_ListBox_Paint` draws the first row at `rect.y + 1`. Quake Live's scoreboard
menus put the PLAYER / SCORE / K/D / THAWS labels *inside* the top of the list
rect — in `ingame_scoreboard_ft.menu` the labels are at `y 170` and the list is
`rect 73 165 284 130` — so row one was painted across the header band.

Quake Live's own list box evidently starts its content below that. Changing
`Item_ListBox_Paint` would move every list in the game (server browser, demo
list, map list), so `CG_OffsetScoreboardList` nudges only the four scoreboard
feeders: top down by one element height, height down by the same, leaving the
bottom edge and the scroll bar where the menu put them. Applied once per menu —
`Menus_FindByName` returns the same `menuDef` each call, so a gametype change
would otherwise stack a second offset.

One element height is the natural "one row for the header" reading and is tuned
by eye against a screenshot, not measured against Quake Live. If it is still off,
the shift is the one number to change.

### C25. Team scoreboards drew one row and then blanks — DONE (verify)
**Lives in:** our **client** (cgame) · **Seen by:** our client only

Headers read "3 Players" and "2 Players" with one row rendered, or none.

`scores_ft` writes **17** fields per player. `CG_ParseScoreEntry_Ft` read
**18**. This was my own regression: an earlier pass correctly added the missing
`sp->tks` read but left the `i++; // unknown field` that had been standing in
for it.

One field of drift per entry compounds. From the second player on, `sp->client`
was reading somebody's damage figure or alive flag; the range clamp turned that
into client 0, `cgs.clientinfo[0]` is not the player that row belongs to, and
`CG_FeederItemText` returns "" when `infoValid` is false — so the list drew one
plausible row and then blanks. The team headers come from a different count,
which is why they still said three.

**Checked the rest mechanically rather than by eye, and Freeze Tag was the only
one wrong:**

| verb | emitter | parser |
|---|---|---|
| `scores_ffa` | 18 | 18 |
| `smscores` | 8 | 8 |
| `scores_tdm` | 15 | 15 |
| `scores_ca` | 16 | 16 |
| `scores_ctf` | 17 | 17 |
| `scores_ft` | 17 | ~~18~~ 17 |
| `scores_rr` | 19 | 19 |
| `scores_race` | 4 | 4 |
| `tinfo` | 1 | 1 |

**`tools/check-score-fields.py`** now does this count on every emitter/parser
pair and exits non-zero on a mismatch. Run it after touching either side. This
class of bug has cost four rounds — the FFA WEAP column, the Freeze Tag tail,
this over-read, and `tinfo`'s six-fields-for-one — and not one of them produced
an error at runtime. A parser reads whatever is next in the argv list: a drift
of one shows up as a wrong number, a blank row, or `bad client number: 85`,
never as "the message is malformed". Counting is mechanical, so it should not be
done by eye.

### E23. Serverinfo overflowed 1024 bytes and dropped keys — DONE (verify)
**Lives in:** our **server** (engine) · **Seen by:** every client

The dedicated console, hundreds of lines between kills:

```
Info string length exceeded
Info string length exceeded
```

`Cvar_InfoString` builds into a `MAX_INFO_STRING` (1024) buffer. The game module
alone flags **45 cvars `CVAR_SERVERINFO`** and the engine adds more, so the
string overruns; `Info_SetValueForKey` then drops every key that will not fit and
prints, once per key, on every rebuild — and `SV_Frame` rebuilds whenever a
serverinfo cvar changes.

The drops are the tail of the list, which is why an earlier `InitGame` line was
cut mid-key at `g_levelStartTi`. **The client reads about thirty keys out of
CS_SERVERINFO** — gametype, teamsize, the shotgun and pmove values, the round
timers — so anything past the cut silently never arrived.

`Info_ValueForKey` already handles `BIG_INFO_STRING`, and `CS_SYSTEMINFO`
already goes out that way, so the configstring path now uses
`Cvar_InfoString_Big`. `SVC_Status` deliberately keeps the small builder: that
reply goes to master servers and the browser in one out-of-band datagram and is
capped there. The three ui buffers that copy CS_SERVERINFO were `MAX_INFO_STRING`
and would have truncated it themselves; they are `BIG_INFO_STRING` now.

### E22. Nobody froze in Freeze Tag — DONE (verify), second cause found
**Update.** Wiring `Freeze_PlayerFrozen` into `player_die` was necessary but not
sufficient — it still declined every freeze, because **the Freeze Tag round
state machine was never started.**

`G_InitGame`'s round-based switch had `case GT_RR: RR_InitRoundState()` and a
bare `default:`. `GT_FREEZE` had no case, so `level.roundState` stayed
zero-initialised: `eCurrent` RS_WARMUP, `tNext` 0. `Freeze_GetRoundState` only
promotes `eNext` into `eCurrent` when `tNext` is non-zero, and all five
`Freeze_RoundStateTransition` call sites are reachable only once the machine is
already turning. It never turned.

So `eCurrent` sat at RS_WARMUP for the whole match — through warmup, through
"MATCH IN PROGRESS", through every kill — and `Freeze_PlayerFrozen` refuses
unless it is RS_PLAYING. A declined freeze is an ordinary death, which is why it
read as the freeze code being wrong rather than never reached.

`Freeze_InitRoundState` now mirrors RR's: park in RS_WARMUP while warmup runs,
otherwise drop into the countdown. `Freeze_Think` also kicks RS_WARMUP into
RS_COUNTDOWN once warmup ends, since it returns early during warmup and nothing
else would move it.

`g_debugFreeze 1` traces every state transition and names the reason whenever a
freeze is declined — same approach as `g_debugWarmup`.

### E22-original. Nobody froze in Freeze Tag — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

Players shot in Freeze Tag just died and respawned. No statue, no thawing.

`Freeze_PlayerFrozen` — the function that actually freezes somebody — was
**defined in `g_gametype_ft.c` and called from nowhere in the tree.**
`player_die`'s Freeze Tag fork did this instead:

```c
self->client->ps.pm_type = PM_FREEZE;
self->client->ps.thawtime = g_freezeThawTime.integer;
```

which is not enough to freeze anyone. What marks a frozen player is
`ps.powerups[PW_FREEZE]`, and everything downstream gates on it: pmove treats a
zero-health player as dead without it (`bg_pmove.c:2275`), `ClientThink_real`
keeps `PM_FREEZE` instead of `PM_DEAD` on it (`g_active.c:962`),
`Freeze_ClientThawCheck` counts down against it, and `Freeze_DeathFinalize`
refuses to respawn without it. None of that was ever reached.

Same shape as the unassigned ice handles (C23) and the registered-but-unread
cvars (E8): written, never wired. The fork now calls `Freeze_PlayerFrozen`, and
falls back to `PM_DEAD` when it declines — it refuses outside a live round
(`RS_PLAYING`), so a warmup death is still an ordinary respawn.

`Freeze_PlayerFrozen` also now sets the networked `s.powerups` freeze bit, which
`g_combat.c`'s other freeze path already set. Without it the playerState says
frozen but nothing on the wire tells other clients, so the body would draw as an
ordinary corpse.

### E21. Joining any team gametype dropped the client — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client, stock included

```
CG_ParseTeamInfo: bad client number: 85
CG_ParseTeamInfo: bad client number: 90
```

`tinfo` in Quake Live is a flat list — a count, then that many client numbers.
Teammate health, armour, location, weapon and powerups reach the client through
the snapshot, not through this message. `CG_ParseTeamInfo` (binary `0x100487b0`)
reads it that way and its own comment in `cg_servercmds.c` says so explicitly.

`TeamplayInfoMessage` was still the **stock Quake 3 emitter**, writing six fields
per player. So the client read every field as a client number: argv(3) a
location, argv(4) a health value, argv(5) armour. 85 and 90 are health — which
is why the numbers in the error looked like nothing in particular.

Fatal, via `CG_Error`, on joining any team gametype. Freeze Tag is where it was
reported because that is what was being tested; TDM, CA, CTF and the rest were
equally broken. A stock Steam client would also expect the flat list, so the
emitter is the side that was wrong, and the parser was right all along.

### E20. Freeze Tag killed the server on the first frag — DONE (verify)
**Lives in:** our **server** (qagame) · **Seen by:** every client

```
Kill: 3 0 10: Keel killed Daemia by MOD_RAILGUN
----- Server Shutdown (Received signal 11) -----
```

Instagib Freeze Tag on `trinity`, twelve bots, dead on the first kill of the
match.

Both Freeze Tag thaw paths — `Freeze_InstaKill` and the `do_thaw` tail of the
thaw-progress function — called **`Kamikaze_DeathActivate(player)`**. The port
matched that name to Quake 3's function of the same name, which is the *think
function of a temporary "kamikaze timer" entity*:

```c
void Kamikaze_DeathActivate(gentity_t *ent) {
    G_StartKamikaze(ent);
    G_FreeEntity(ent);
}
```

It detonates and then frees **that** entity. Handed a player instead,
`G_FreeEntity` memsets the client's `gentity` and clears `inuse` while
`svs.clients[n].gentity` still points at it and `ent->client` is left NULL.
`neverFree` is set only on the body queue (`InitBodyQue`), so nothing stops it.
The next thing to touch that client reads freed memory — and in instagib every
kill goes through `Freeze_InstaKill`, so that is the first frag of the match.

The two are unrelated functions that happen to share a name: the binary's
finalizer at `0x10046f60` is a *thaw* finalizer, and its own comments in this
file said so ("respawns iff still frozen (PW_FREEZE != 0)", "fires
EV_THAW_PLAYER + ClientSpawn"). `Freeze_DeathFinalize` now does that — clear
`PW_FREEZE` and the freeze timers, restore `takedamage`, fire `EV_THAW_PLAYER`,
`ClientSpawn` — and never frees a player. `Kamikaze_DeathActivate` stays where it
belongs, as the kamikaze timer's think in `g_combat.c`.

This is the tracker's own E7/C3 note ("the reimpl body is the stale Q3 stand-in,
must be rewritten") and the outside review's C3. Both described it as blocking
thaw-respawn. It was considerably worse than that: the mode could not survive one
kill.

### C23. Freeze Tag had no ice at all — DONE (verify)
**Lives in:** our **client** (cgame) · **Seen by:** our client only

Five `cgs.media` handles were declared in `cg_local.h` and read by the effect
code but **assigned nowhere**, so all five were zero:

| handle | read by | what was drawn |
|---|---|---|
| `freezeModel` | `CG_FreezeEffect` (`cg_effects.c:220`) | `RT_MODEL` with `hModel` 0 |
| `iceWhiteModel` / `iceBlueModel` | `CG_ThawPlayer` (`cg_effects.c:291`) | seven shards, `hModel` 0 |
| `freezeShader` | `FE_FREEZE` effect (`cg_players.c:2583`) | billboard with shader 0 |
| `frozenFlagShader` | nothing yet | — |

A zero handle renders nothing and reports nothing, so Freeze Tag had no ice
overlay on frozen players and no thaw effect, with no error anywhere.

The mirror image was there too: `iceShardModel` and `iceShardShader1..3` were
registered and **read by nothing**. The port registered one set of names and
wrote the effects against another, and the two halves never met. That is the
same shape as the "registered cvar is not an implemented feature" trap in
CLAUDE.md, one layer down — a registered *asset* is not a drawn one.

Fixed by assigning all five. `RE_RegisterShader` and `RE_RegisterModel` both
return 0 for an asset they cannot find, so `CG_RegisterModelOr` /
`CG_RegisterShaderOr` name the miss on the console and fall back to an asset
this build is known to have.

**The dedicated Quake Live ice model names are not identified** — `pak00.pk3` is
not readable from the build environment, so `models/freeze/ice.md3`,
`ice_white.md3`, `ice_blue.md3` and `sprites/frozenflag` are the names tried
first, not names that have been confirmed. If the console prints

```
WARNING: model 'models/freeze/ice.md3' not found, falling back
```

then that name is wrong and wants correcting — but the effect renders either
way, which is the point. `sprites/frozen` (the `freezeShader` fallback) is
already registered elsewhere in the same block and is known good.

### C24. SCR_DrawDemoRecording used sprintf — DONE (verify)
**Lives in:** our **client** (engine) · **Seen by:** our client only

`sprintf` into a fixed 1024-byte stack buffer. `clc.demoName` is `MAX_QPATH`, so
it cannot actually overrun today and the reported severity was overstated — but
an unbounded write into a fixed buffer is not worth leaving in place. Now
`Com_sprintf` with `sizeof(string)`.

### C19. The scoreboard could not be scrolled — DONE (verify)
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

*"It has a scrollbar on the right side, but there's no way to scroll and find
where someone not in the top players is."*

The scoreboard list box shows ten rows (`rect ... 180` over `elementheight 18`).
The scroll bar beside it is painted from `startPos` and nothing moves it: while
`+scores` is held the cgame does not hold the key catcher, so there is no cursor
in front of the bar to drag, and `scrollScoresUp` / `scrollScoresDown` — the only
things that could move it — are not bound to anything by default. So on a full
server everyone below tenth place was unreachable.

Three parts:

- **The commands now move the list box that is on screen.** They were hardcoded
  to `menuScoreboard`, which at intermission is the wrong menu — the end-of-match
  summary is `menuEndScoreboard`. And they only ever sent an arrow key, one row
  at a time. `Menu_ScrollFeederKey` forwards any key to a feeder's list box, so
  `pageScoresUp` / `pageScoresDown` (a page) and `scrollScoresTop` /
  `scrollScoresBottom` (the ends) join them. `Item_ListBox_HandleKey` already
  understood all of these; `Menu_ScrollFeeder` just never offered them.

- **The client routes the keys without anyone binding them.**
  `CL_ScoreboardScrollKey` gives the wheel, the page keys and home/end to the
  scoreboard while `cg_scoreboardActive` is set — the cgame writes that cvar
  every frame it draws the scoreboard, so the wheel goes back to changing
  weapons the moment TAB is released. A key bound to a `+` command is left
  alone: swallowing the press while the release still fires would leave the
  action stuck on.

- **The view follows the local player's row.** `CG_SetScoreSelection` records
  the local player as the selection, but `Menu_SetFeederSelection` only touches
  `startPos` when the index is zero, so on a list longer than the window the
  selected row could sit off screen with nothing to say so.
  `Menu_ShowFeederIndex` scrolls just far enough to bring it into view and
  leaves `startPos` alone when the row is already visible.

  This runs every frame the scoreboard is drawn, not once when it opens: the
  list re-sorts as scores change, so a row index recorded at open drifts within
  seconds and the view is left on a page the player is no longer on. Scrolling
  by hand takes the view back (`cg.scoreboardScrolled`) until the board is
  closed and reopened. `Menu_SetFeederCursor` is the highlight half of the same
  job — `Menu_SetFeederSelection` cannot be used per-frame because its
  index-zero `startPos` reset would stop a first-place player scrolling away.

- **The board refreshes while it is held.** `CG_ScoresDown_f` sends `score` on
  the key press and nothing after it, so holding TAB showed whatever had arrived
  at the moment it opened — visibly stale within a second on a sixty-player
  instagib server. `CG_RefreshScoreboard` re-requests on the same two-second
  throttle the key press already used, so it costs no more than tapping TAB.

### E16. Clients dropped for "Server command overflow" at match start — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

A sixty-player server dropped three clients — including the human — seconds
after `map_restart`. The pending-command dump is almost entirely the same two
configstrings, rewritten over and over with different values:

```
cmd 1388: cs 659 "Klesk"
cmd 1389: cs 659 "Grunt"
cmd 1393: cs 659 "Klesk"
cmd 1396: cs 659 "Phobos"
```

659 and 660 are `CS_SCORES1PLAYER` / `CS_SCORES2PLAYER`, the leader and
runner-up names on the HUD. `CalculateRanks` writes them, and it runs on every
frag; each write is a reliable command **broadcast to every client**, against a
64-slot ring. A client that stops acknowledging for a moment — loading the map
after a restart, which is exactly when this fired — overflows and is dropped.

Two causes, both fixed:

- **`SortRanks` returned 0 for tied players.** `qsort` is not stable and the
  array it sorts changes every time somebody connects, so the order among a
  field of tied players was arbitrary and different on each call. At match start
  everyone is tied on zero while sixty bots are still connecting, so the
  "leader" changed several times a second and each change broadcast two
  configstrings for a lead that had not moved. Ties now break by client number,
  which also makes `PERS_RANK` stable.

- **Nothing bounded the broadcast rate.** The score configstrings moved out of
  `CalculateRanks` into `G_UpdateScoreConfigstrings`, behind
  `G_ScheduleScoreConfigstrings`, which writes at most once every 250 ms and
  otherwise sets a pending flag that `G_RunFrame` flushes. Intermission bypasses
  the limit, since the final figures have to be right and nothing follows them.
  Four updates a second is more than the eye follows on a HUD, and it caps what
  `CalculateRanks` can cost no matter how fast frags arrive.

Note the scoreboard chunking (C13) pushed in the same direction — a full FFA
scoreboard is now three or four reliable commands instead of one. That is per
request and throttled client-side to one every two seconds, so it is a much
smaller contributor than the per-frag broadcast was, but it is the reason to
keep an eye on this queue.

### E18. The engine ran in the machine's locale, so floats used a decimal comma — DONE (verify, not reproducible here)
**Lives in:** our **client** and our **server** (both come out of one binary) · **Seen by:** every client

Console lines like `CL_InitCGame: 0,53 seconds` looked like a font quirk. They
were not. The give-away is in the bot userinfo configstrings going out over the
wire:

```
cs 549 "\n\Keel\t\0\model\keel\...\skill\ 2,00\tt\0\tl\0"
```

Everything this engine parses and prints assumes a `.` decimal point — cvar
values, shader scripts, configs, `atof`, `sprintf("%f")`. On a machine whose
locale uses a decimal comma the whole engine switches over: floats are written
with commas, and anything reading them back under a C locale gets the integer
part and silently drops the fraction.

It also corrupts configs. The startup warning

```
Warning: cvar "cg_stereoSeparation" given initial values: "0" and "0,4"
```

is a value that was written into `q3config.cfg` by an earlier run under the
comma locale and read back as a cvar default. **Anyone who has run an affected
build should check their config for comma decimals** — those values are being
read as truncated integers.

The C runtime starts in the `"C"` locale, so this is not the default state: SDL
calls `setlocale(LC_ALL, "")` during init on several platforms and takes the
process with it. `setlocale(LC_NUMERIC, "C")` is now asserted at the top of
`main()` — which covers the dedicated server and the game module too, since they
share the process — and again after each `SDL_Init(SDL_INIT_VIDEO)`, in both
`GLimp_Init` and `VKimp_Init`. `LC_NUMERIC` only; the rest of the locale is
nobody's business here.

Not reproducible in the build container, which has no comma locale installed, so
this was shipped verified by inspection only.

**Confirmed fixed in play.** The same server log that showed
`\skill\ 2,00` before now reads `\skill\ 3.00` on build `ffedf1d`.

### E19. Console noise at startup — PARTLY DONE
**Lives in:** our **client** (cgame / renderer) · **Seen by:** our client only

Reported as a batch of warnings on the console. Sorted by what they actually are:

- **`unknown general shader parameter 'novlcollapse'`, forty-odd lines** —
  already fixed, in `645b80e`. The build in the screenshots was stamped
  `16:15:06` and that commit landed at `16:28`; the later screenshots show none.
  Nothing to do, and a good argument for the build stamps.

- **`Failed to load sound new_high_score.ogg!`** — real, fixed. `cg_main.c`
  registered the bare filename with no directory, straight from the binary's
  path string. `S_RegisterSound` takes the name as given and there is nothing at
  the root of the pak. Every other voice line there lives under `sound/vo`, and
  the ui module already asks for this one by that path. Now matches.

- **`CM_AddFacetBevels... invalid bevel`** and **`reused image ... with mixed
  flags`** — already `Com_DPrintf` / `PRINT_DEVELOPER`. They only appear because
  `developer 1` is set; that is what asking for developer output means. Left
  alone.

- **`R_FindImageFile could not find 'gfx/misc/console01.tga' in shader
  'console'`** and **`Can't read sound music/sonic5.wav`** — both are Quake Live
  assets referenced by Quake Live content (the `console` shader, and the map's
  worldspawn music key) that Quake Live does not ship. Not ours to add, and the
  fallbacks are harmless. Could be silenced by shipping our own `console` shader
  in `pak01`; not done, because that means drawing console art.

### E17. Returning after a drop mid-match leaves you spectating — OPEN
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

Reported straight after E16: reconnecting during a live match came back as a
spectator rather than a player.

Not yet reproduced, and the obvious path does not explain it. For FFA,
`G_InitSessionData` gives `TEAM_FREE` when `g_autoJoin` is set (it is, in
`common.cfg`) unless `g_maxGameClients` is non-zero and the server is at that
cap. `g_maxGameClients` ships as `"0"` — but it is **`CVAR_ARCHIVE`**.

**Confirmed from the server's own InitGame line:** `\g_maxGameClients\60\`
alongside `\bot_minplayers\60\` and `\sv_maxclients\64\`. Sixty bots fill
all sixty playing slots, so a returning player is at the cap and
`G_InitSessionData` puts them in spectator. Either lower `bot_minplayers` below
`g_maxGameClients`, or raise `g_maxGameClients` — and note it is `CVAR_ARCHIVE`,
so whatever it is set to now was written into the server config and will win
over the shipped default until it is changed explicitly.

There is a second, definite defect underneath it either way: **a human sitting
in spectator never displaces a bot.** `G_CheckMinimumPlayers` counts humans with
`G_CountHumanPlayers(TEAM_FREE)`, so someone who has been put in spectator is
not counted, `humanplayers + botplayers` stays equal to `bot_minplayers`, and no
bot is ever removed to make room. The logs show bots taking the vacated slots
immediately (`ClientDisconnect: 11` then `ClientBegin: 47`, `ClientBegin: 48`),
so on a bot-filled server the seat is gone before the player is back. Not
changed yet — trimming bots for anyone spectating would also trim them for
people who mean to spectate, and that needs deciding rather than assuming.

### C20. Chat ran into the next console line — DONE (verify)
**Lives in:** our **client** (cgame) · **Seen by:** our client only

```
Hossman: You have to run out of ammo sometime, camper.Demona: Camping AGAIN
stripe?Phobos was railed by Anarki
```

`CG_AddChat` printed its console copy with `CG_Printf("%s", text)`. The server's
chat payload carries no trailing newline — `G_SayTo` sends the text and nothing
else — so every chat line ran straight into whatever printed next. Only the
console copy gets the newline; `cg.currentChatLine.text` is what the chat overlay
draws and has to stay exactly as sent.

(The trailing "commas" in the same screenshot are full stops. The obituary
strings end in `.` and print with `\n`; at that font size the two are hard to
tell apart. The decimal commas in E18 were real — those were confirmed in a
plain-text log, not read off a screenshot.)

### C21. Players invisible on enclosed maps — INSTRUMENTED, cause not yet confirmed
**Lives in:** our **client** (cgame) · **Seen by:** our client only

Reported as bots and players turning invisible, more often on maps with corridors
than on open ones like Longest Yard.

Not reproduced yet. The suspicion is the deferred loader rather than culling: a
client arriving mid-match is deferred, and `CG_SetDeferredClientInfo` hands it
another client's model handles to draw with until `CG_LoadOneDeferredPlayer`
gets round to it. The final fallback loop takes the first `infoValid` client
without checking whether that client is itself deferred, so handles can be
copied from a client that has nothing loaded. A `legsModel` of 0 is then added
to the scene and renders nothing, with no error anywhere — which is exactly what
an invisible player looks like. The map-shape correlation fits: on an enclosed
map players enter the PVS suddenly and at close range, so a client is far more
likely to be drawn during the window before its models are loaded.

`CG_Player` now says so instead of drawing nothing:

```
WARNING: client 47 (Bones) has no player model loaded and is drawing nothing -
model 'bones', skin 'default', still deferred
```

Once per client, reset in `CG_LoadClientInfo` when real handles arrive. If that
line appears when a player goes invisible, the deferred chain is the cause and
the fix is to make the fallback loop skip deferred clients. If it does not
appear, the models are loaded and it is a culling problem, which is a different
search.

### C22. Client crashed instead of going to intermission — OPEN
**Lives in:** our **client** · **Seen by:** unknown

Reported: "client crashed when winning conditions were met, instead of going to
intermission."

No diagnosis yet, and guessing at it is not worth the round trip when the build
already writes `crashlog.txt` on Windows (E12) with the faulting module and
offset. That file is the next step.

Worth noting what changed near that path recently, so it can be ruled in or out
quickly: `CG_TrackLocalPlayerOnScoreboard` now runs every frame the scoreboard
is drawn (including the intermission board), `CG_EventHandling` takes the key
catcher on `CS_INTERMISSION`, and `BeginIntermission` sends every client a
chunked scoreboard — which at sixty players is three or four reliable commands
each rather than one.

### C14. Match summary: no cursor, no voting, no winner, no arena shots — PARTIAL
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

**Fixed — the mouse.** `CG_EventHandling`'s `CGAME_EVENT_SCOREBOARD` branch was
empty, and nothing anywhere in cgame ever set `KEYCATCH_CGAME`; the only calls
were `Key_SetCatcher(0)`. The cgame never asked the engine for mouse or key
input, so there was no cursor and nothing was clickable — the "Vote for Next
Arena" panels were drawn and could not be reached. `CG_MouseEvent` and
`CG_KeyEvent` were both already written and correct; nothing was ever routed to
them. `CG_EventHandling` now takes and releases the catcher, and entering
intermission (`CS_INTERMISSION`) calls it with the cursor centred.

That is why there was no voting. Whether the vote then works is untested.

**Fixed — the cursor, in two goes.** With the catcher taken, the mouse moved and
hovered *audibly* — items play a sound on mouse-enter — but nothing was drawn,
because the ui module draws its own cursor and cgame never drew one.
`CG_DrawIntermission` now draws it while `cgs.eventHandling` is active.

The first attempt still drew nothing, because it guarded on `cgs.media.cursor`
and that handle was **0**: `CG_RegisterGraphics` registered
`"menu/art/3_cursor2"`, which is Quake 3's path and does not exist in Quake
Live. `RegisterShaderNoMip` returns 0 for a shader that resolved to the default,
so the guard silently skipped the draw every frame. Quake Live's menus declare
`ui/assets/3_cursor3` in their assets block, which `CG_ParseMenu` already parses
into `cgDC.Assets.cursor` — that is now preferred, with `cgs.media.cursor`
(re-pointed at the QL path, Q3's kept as a fallback) behind it.

Worth noting as a class: a dozen other `menu/art/...` registrations remain in
cgame. Any of them that Quake Live does not ship are silently 0 in the same way.

**Still open:**

- the winner's model draws **legs only** — no torso, no head. A player model is
  three md3s joined on tags; legs-only means the torso lerp or the tag lookup is
  failing on whatever entity the summary builds, not that the model is missing.
- the three arena vote panels are solid white. `CG_DrawVoteMapShot` registers
  `levelshots/<mapname>` and falls back to `levelshots/preview/default`; white
  rather than blank suggests it is drawing *something*, so the fallback or the
  panel's own background is what is visible.

The empty scoreboard on that screen was C13.

### C18. Scoreboard K/D, damage and accuracy only ever showed the current life — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

In a 60-player instagib FFA the local player's row is self-consistent — 102
score, 90/0, 7.2k damage, which is 80 damage per kill and exactly one railgun
hit each. Every bot's row is not: scores of 65 to 97 alongside **1 to 5 kills**,
damage equal to 72 × kills, and **0 deaths** for all but one of them.

Nobody goes 97-0 in instagib on Longest Yard. Either the kill and death counters
are losing almost everything for bots, or the scores are inflated; the 0 deaths
says the counters.

**Found — the WEAP column.** `CG_ParseScores_Ffa` read field 17 into
`sp->powerUps` where the emitter writes `bestWeaponAccuracy`. `score_t` has a
`bestWeaponAccuracy` field and the WEAP column renders from it
(`cg_main.c:2541`), so it was never filled and drew **0% for every player,
including the local one** — which is exactly what the screenshots show. The
same field also overwrote `cgs.clientinfo[].powerups` with an accuracy
percentage.

FFA was the only parser with this; the CA and CTF ones fill
`bestWeaponAccuracy` correctly. The function's own doc comment listed the 17th
field as "powerups", which is presumably how the parser and the emitter drifted
apart — the parser followed the comment rather than the code that writes the
message. Both corrected.

**Full audit of every scoreboard verb**, emitter format against parser reads,
after being asked to check them all rather than the one that was reported:

| verb | emitter fields | parser reads | result |
|---|---|---|---|
| `scores_ffa` | 18 | 18 | field 17 was misread — **fixed** |
| `smscores` | 8 | 6 + 2 skipped | captures and alive were skipped as "unknown" — **fixed** |
| `scores_tdm` | 15 | 15 | aligned |
| `scores_ca` | 16 | 16 | aligned |
| `scores_ctf` | 17 | 17 | aligned |
| `scores_ft` | 17 | **16** | one field short — **fixed** |
| `scores_rr` | 19 | 19 | aligned |
| `scores_race` | 4 | 4 | aligned |

**Freeze Tag was a whole shifted tail.** The emitter writes
`... GAUNTLET, ASSIST_COUNT, numTeamKills, numTeamKilled, totalDamageDealt,
alive` and the parser read one fewer, missing `numTeamKills`. So team-kill
deaths showed team kills, damage showed team-kill deaths, and `alive` was
handed the damage figure — which made every player read as alive. (In Freeze
Tag the assist count *is* the thaw count: thawing a teammate awards an assist.)

`smscores`, the compact end-of-match FFA form, skipped two fields with
`i++; // unknown`. They are `PERS_CAPTURES` and the alive flag, and `score_t`
has a field for each.

That accounts for the accuracy column and two more gametypes. The field
alignment was never the cause of the K/D and damage numbers, though — those
fields do line up between emitter and parser.

**Root cause — `ClientSpawn` wipes `expandedStats` on every respawn.**

```c
Com_Memset(client, 0, sizeof(*client));
```

Q3 clears the whole `gclient_t` on spawn and restores by hand everything that
has to outlive a life: `pers`, `sess`, `ps.persistant[]`, the ping,
`accuracy_hits` / `accuracy_shots`. `expandedStats` is a Quake Live addition —
kills, deaths, suicides, team kills, and the per-weapon shot, hit and damage
arrays, 812 bytes of match totals — and it was never added to that list. Every
counter behind the K/D, DMG and WEAP columns was reset each time the player
respawned.

That explains the whole shape of the report:

- **Score was right and everything beside it was wrong.** `PERS_SCORE` lives in
  `persistant[]` and is restored; the columns next to it only ever showed the
  current life.
- **Every player read 0 deaths.** `numDeaths` is incremented at the moment of
  death and wiped by the respawn immediately after, so a death could never be
  displayed at all — not for bots, not for anyone.
- **Damage was exactly 72 × kills.** Both counters covered the same single life,
  so the ratio came out as one railgun hit per kill every time.
- **The one row that looked right belonged to a player who had not died.**
  A 35-0 local player showed 35 kills and 2.8k damage because nothing had
  cleared them yet. That is why this first looked like a bots-only fault.

Fixed by saving and restoring `expandedStats` across the memset alongside
`pers` and `sess`. `killStreak` is not exempted: `STAT_AddPlayerDeathStat`
already zeroes it on death, which is where a streak is meant to end.

A correction to the previous round of this entry, which listed "`expandedStats`
is zeroed only on connect and on team change, not on respawn" among the things
ruled out. `STAT_InitClient` is indeed the only *named* reset and has no callers
— but `ClientSpawn`'s blanket memset was the reset, and it was not looked for.
The clue that settled it was the 0 deaths being universal rather than
bot-specific: no gate in the stat path can produce that, only a wipe timed to
the respawn.

Still standing from that round:

- `STAT_AddPlayerDeathStat` and `STAT_AddDamageStat` are called unconditionally
  from `player_die` and `G_Damage`, and both read correctly.
- `OnSameTeam` returns qfalse below `GT_TEAM`, so the FFA case is not being
  treated as friendly fire and skipped.

Two things to look at next. Bots on this map die to `<world>` constantly
(`MOD_TRIGGER_HURT`, `MOD_FALLING` — the earlier logs are full of it); the
killer side is correctly skipped when `attacker->client` is NULL, but the victim
side should still count and apparently does not. And 72 damage per bot kill
against 80 for the player is its own discrepancy — 72 is the *clamped* victim-side
figure, so the two sides of `STAT_AddDamageStat` may be feeding the wrong field.

**Time on server** is reported as suspect in the same breath. `(level.time -
cl->pers.enterTime) / 60000` is minutes, and the column read 7 on a 7-minute
match, so it may be correct and simply coarse; not investigated.

### C17. A2M instagib dropped weapons on death — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

Instagib gives everyone the same weapon and infinite ammo, so a dropped one is
worth nothing to pick up — it just litters the map with railguns and gives the
pickup sound and the item timers something to report that does not matter. All
three A2M modes are instagib and all had it.

`TossClientItems` now skips the weapon when `g_instaGib` (or `DF_INSTAGIB`) is
set. Gated on the mode rather than on a new cvar: a server handing out a fixed
loadout with no ammo pickups has already decided this, and a cvar nobody sets is
a cvar nobody reads (E8). Powerups still drop — those are still worth taking.

### C16. Score tracker never shows the player's score — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

**This is C15, not a negative-number bug, and the screenshots settle it.** Four
samples: tracker 12/11 with the player 10th on 8; 10/9 with the player 7th on 7;
and 0/0 with the player on **-1** while everyone else sat on 0. In every case
the two bars are 1st and 2nd place. The player's score is never shown at all —
it only looked like a negative-handling fault because when scores are low the
leader's numbers happen to look plausible.

So `CG_DrawPlayerScore` and the `%i` formatting are fine, and so is the wire
format. What is wrong is that the `CG_PLAYER_SCORE` half of the pair never
draws.

Checked and ruled out this round: `cgDC.ownerDrawVisible` **is** wired
(`cg_main.c`), `ui_shared.c` **does** consult it before painting, and
`CG_OwnerDrawVisible`'s logic reads correctly — an item flagged
`IF_PLYR_IS_FIRST_PLACE` falls through every branch and hits the closing
`return qfalse` when the player is not first.

Which leaves **which HUD file is actually loaded**. `comp_hud.menu` is the one
with the four-item gated layout; Quake Live also ships `hud.menu`, `hud2`,
`hud3` and others, selected by `cg_hudfiles`, and `hud3.menu` lays these out
differently. If the loaded HUD asks for `CG_1STPLACE` and `CG_2NDPLACE`
unconditionally then the rendering is faithful and the layout is the thing to
change. That is the next thing to check and it has not been checked.

### C15. HUD score tracker shows 1st and 2nd, not 1st and yours — OPEN
**Lives in:** our **client** (cgame / ui / client engine) · **Seen by:** our client only

The two-bar tracker top-left read 39 / 38 while the player was 40th with 1. It
should read 39 / 1.

Quake Live's `comp_hud.menu` draws it as four items, gated in pairs:

```
OpponentInFirst      CG_1STPLACE      CG_SHOW_IF_PLYR_IS_NOT_FIRST_PLACE
OpponentTrailingMe   CG_2NDPLACE      CG_SHOW_IF_PLYR_IS_FIRST_PLACE
MyScoreWhenTrailing  CG_PLAYER_SCORE  CG_SHOW_IF_PLYR_IS_NOT_FIRST_PLACE
MyScoreWhenInFirst   CG_PLAYER_SCORE  CG_SHOW_IF_PLYR_IS_FIRST_PLACE
```

So a trailing player should get 1STPLACE over PLAYER_SCORE. Seeing 1st and 2nd
means the `IF_PLYR_IS_FIRST_PLACE` half is drawing when it should not.
`CG_OwnerDrawVisible` tests `PERS_RANK == 0` for first place and returns qfalse
by default, both of which look right, so the fault is not located yet —
`PERS_RANK` itself, or which HUD file is actually loaded (`hud3.menu` lays these
out differently from `comp_hud.menu`), are the two things to check next.

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

### R9. Quad glow dimmer under Vulkan than OpenGL — DONE (verify, second pass)
**Lives in:** our **client** (renderer) · **Seen by:** our client only, Vulkan only

Reported after switching to Vulkan: the quad glow is less intense, both on the
powered-up player and on the powerup lying on the floor.

Both are plain dynamic lights — `cg_players.c:1948` for the carrier and
`cg_ents.c:473` for the item, each `radius 200 + (rand() & 31)`, blue. Quake3e's
`RE_AddLightToScene` does

```c
intensity *= r_dlightScale->value;   /* tr_scene.c:272, :323 */
...
dl->radius = intensity;
```

and ships `r_dlightScale` at **0.5**, so every dynamic light in the Vulkan
renderer had half the radius. `renderergl2` has no such cvar and runs at full
radius, so the same scene really was dimmer under Vulkan — not a perceptual
difference. The gate is `r_dlightMode != 0`, and `USE_PMLIGHT` is defined with
`r_dlightMode` defaulting to 1 on x86_64, so it was always active.

Default is now 1, matching the OpenGL renderer. It is `CVAR_ARCHIVE_ND`, so
unlike the `CVAR_ARCHIVE` trap in CLAUDE.md the new default does apply — an
archived copy only exists if somebody set the cvar deliberately.

**Second pass — the radius was only half of it.** With `r_dlightScale` corrected
the glow was still dim, because `r_dlightMode` was the bigger cause:

```c
/* tr_mesh.c:350 */
if ( r_dlightMode->integer >= 2 && ( !personalModel || ... ) ) {
```

Mode 1 applies per-pixel dynamic lights to **world surfaces only**. MD3 models
are gated on `>= 2`. Every glow a player actually sees — on another player, on a
powerup lying on the floor — is a light falling on an MD3, so at mode 1 those
models were lit by the ambient grid alone while the floor around them lit up
correctly. The OpenGL renderer lights models through the legacy path regardless,
hence the difference. Default is now 2.

Its flag was plain `CVAR_ARCHIVE`, which is the trap in CLAUDE.md: written to the
user's config even at its default, so anyone who had already run a build would
keep the old value forever and a changed default would do nothing for them. Now
`CVAR_ARCHIVE_ND`, which only writes when the value differs from the default.
Anyone whose config already contains an explicit `r_dlightMode` line still needs
to remove it or set `r_dlightMode 2` by hand.

Note this is a different question from R8, which is about dynamic lights being
weak in *both* renderers. R8 stays open.

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

**Fixed — the fill rate, and the earlier revert was on a wrong attribution.**
`G_CheckMinimumPlayers` is throttled to once a second and added one bot per
call, so `bot_minplayers 40` took forty seconds and looked stalled. Raising it
to four per tick was blamed for crashing dedicated servers while filling; the
crash was `G_Alloc` pool exhaustion at bot 26 (E13), which a faster fill only
reached sooner. It is now `bot_fillRate`, default 1, clamped 1..16 — a setting
rather than a number, so reproducing anything that only happens at a high player
count is one cvar away.

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

### E13. More than ~25 bots crashed the server — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

`qconsole.log` named it on the first capture:

```
Forcing disconnect on active client: 26
ERROR: G_Alloc: failed on allocation of 9032 bytes
```

9032 is `sizeof(bot_state_t)`. `BotAISetupClient` took one per client slot that
had ever held a bot, out of the game module's `G_Alloc` pool — a fixed **256KB**
with no free, shared with every entity key and value string on the map
(`G_NewString`), the arena list (167 entries on a full install), the bot list
and the unlagged history rings. Twenty-five bots is ~226KB of that pool before
anything else, so it ran out mid-match.

It also meant bot capacity depended on the *map*: a level with more entity
strings left less room, and nothing said so.

Two halves:

- Bot states now live in a static `bot_state_t[MAX_CLIENTS]` in `ai_main.c`.
  Bots are bounded by `MAX_CLIENTS` by definition, so the storage may as well be
  — 578KB of BSS, reused across maps and reconnects, and the largest and most
  variable consumer is out of the pool entirely.
- `POOLSIZE` raised from Quake 3's 256KB to 1MB, because the ceiling was still
  map-dependent for everything else, and the failure now reports how much was in
  use rather than naming one allocation.

**The second capture showed the same crash, from a `qagame` that predates the
fix.** The error line read `G_Alloc: failed on allocation of 9032 bytes` with no
`(N of M already in use)` suffix — that suffix was added by the same commit as
the fix, and the shipped `iobin.pk3` was checked to confirm it carries the new
string. `sizeof(bot_state_t)` was also confirmed to be exactly 9032 by static
assert, so the fix targets the right allocation, and in the new module that
allocation cannot happen at all.

**Which module ran is not the same question as which build was installed, and
conflating the two was a mistake.** Game modules are extracted from the *first*
matching pak in the search path, and the homepath is searched before the
basepath — so a stale `iobin.pk3` left in `%APPDATA%\quakelive\baseq3` wins over
the one just installed next to the executable, for as long as it is there, and
the only symptom is that a fix "did not work".

`FS_ExtractGamecode` now says so: if more than one `iobin.pk3` is loaded it
names every copy and which one modules come from. Not resolved automatically —
which one an admin wants is genuinely ambiguous, and choosing for them would be
a worse surprise than saying what is going on.

The failure message and `gamemem` also carry the qagame build date now.

**Also seen in that log, unexplained:** *"Forcing disconnect on active client:
N"* is logged for every bot added, on slots that have never held a client.
`ClientConnect` logs it when `ent->inuse` is already set and then calls
`ClientDisconnect`, which is a safe no-op on a slot with no `ent->client` — so
this is noise rather than damage, but something is setting `inuse` on fresh
client slots and it is worth knowing what.

### E14. Nothing verified that the loaded module matched the shipped one — DONE (verify)
**Lives in:** **both** of our binaries · **Seen by:** every client

Two rounds of a real bug hunt went into "the fix is in the build and the crash
says otherwise", and neither the game nor the log could settle it. The extracted
module in the homepath is what actually gets loaded, and nothing ever compared
it against the `iobin.pk3` it was supposed to come from — the log said
*"Extracted ..."* whether or not the bytes were the ones in the pak, so a stale
module was indistinguishable from a fix that had not worked.

`FS_ExtractGamecode` now checksums both sides on every start and says which case
it is:

```
Verified 'uix86_64.so' (364984 bytes, checksum 0xe0de03e9) against 'baseq3/iobin.pk3'
MISMATCH: '<homepath>/uix86_64.so' on disk is 364984 bytes / checksum 0x4413021b,
          'baseq3/iobin.pk3' has 364984 bytes / checksum 0xe0de03e9 - replacing it
```

The replacement is the repair, and it is read back and re-checksummed
afterwards — a write that reports success and produces different bytes is
exactly the failure this exists to catch, so it is not taken on trust. If the
readback disagrees the engine refuses to continue rather than loading a module
that is not the one that shipped, and a target that cannot be opened for writing
now says *why* that matters instead of just failing.

Verified by corrupting an extracted module by hand: clean start reports
`Verified`, corrupted start reports `MISMATCH` with both checksums and replaces
it, next start reports `Verified` again.

**Confirmed in the field on the first run.** It caught a stale `qagame`:

```
MISMATCH: 'C:\Users\...\quakelive\baseq3\qagamex86_64.dll' on disk is 1192502 bytes
          / checksum 0xed0bf242, 'baseq3/iobin.pk3' has 1192536 bytes / checksum
          0x39a7771f - replacing it
```

34 bytes apart, not corrupt — that is a *different build* of the same source
(the `__DATE__`/`__TIME__` strings and one changed format string account for the
difference). So a module from an earlier build genuinely was being loaded while a
newer `iobin.pk3` sat next to it, which is what the previous two rounds were
arguing about.

**How it got stale is still not established.** The old code removed and rewrote
the file unconditionally and errors out if the write fails, so none of the
obvious paths explain it. What has changed is that it can no longer persist
quietly: every start now compares and repairs.

The archives also carry `checksums.txt` — sha256 of every shipped file, so an
install can be checked against the release it came from without running the
game (`sha256sum -c checksums.txt`). The engine covers modules against the pak;
this covers the files on disk against the build.

### E11. The match never starts, and kills score nothing — DONE (verify)
**Lives in:** our **server** (qagame / server engine) · **Seen by:** every client

Reported twice from different angles: the countdown runs, the announcer talks
about frags and leaderboard position, and the round never begins — and
separately, TDM/FFA kills add no points. One fault. `AddScore` opens with
`if (level.warmupTime) return;`, which is correct, so "no score" was never a
scoring bug: it was evidence that warmup had not ended.

`g_debugWarmup` traced it end to end, and the log is unambiguous:

```
warmup countdown elapsed at 77750, issuing map_restart 0
==== ShutdownGame ====
InitGame: ...
warmup -1 -> 0    (level.time 77750)      <- match live
------- Game Initialization -------
InitGame: ...
warmup -1 -> -1   (level.time 77750)      <- straight back to warmup
Warmup:
```

**`map_restart` initialised the game twice.** `SV_RestartGameProgs` called
`SV_InitGameProgs`, which ends in `SV_InitGameVM(qfalse)` and so had already run
`GAME_INIT` — and then ran `SV_InitGameVM(qtrue)` on top of it.

`g_restarted` is a one-shot flag. The countdown sets it, `map_restart` fires, and
`SP_worldspawn` reads it to decide whether to come up IN_PROGRESS or in
PRE_GAME, clearing it as it goes. The first init consumed it and went live; the
second saw 0 and went back to warmup, taking every client's ready flag with it.
Hence the loop: countdown, announcer, warmup again, and no scoring throughout.

ioquake3 has the same two-function split and does not have this problem, because
its `VM_Restart` does not call `GAME_INIT` — only `SV_InitGameVM` does. The
native-DLL loader in this tree folded the two together. Split into
`SV_LoadGameDll` (load only) and `SV_InitGameProgs` (load + init once), so a
restart initialises exactly once, with `restart=qtrue`.

**Not a bug, from the same log:** 57 `MOD_TELEFRAG` kills in a chain at the
restart. That is 40 players on `longestyard`, which has about ten spawn points —
`SelectSpawnPoint` has nowhere free to put them and they spawn on top of each
other. Quake doing what it does when the player count far exceeds the spawns.

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

### E15. Raising MAX_CLIENTS above 64 — DEFERRED
**Lives in:** **both** of our binaries · **Seen by:** every client

Asked for (64 -> 100) and set aside for a future version. Recording the cost so
it does not have to be re-derived.

It is not a constant bump. `MAX_CLIENTS` sets the width of every client-number
field in the network protocol and the layout of the configstring table, so a
server built at 100 and a **stock Steam Quake Live client** disagree about where
things are in every snapshot — the "Seen by: stock QL only" trap. It also moves
`MAX_GENTITIES` pressure, grows the bot-state array from 578KB to ~903KB
(E13), and the scoreboard message is already capped near 20 players per
reliable command (C13), so more players would not be visible without the
chunking sketched there first.

Doable as a deliberate protocol fork. Not doable as a one-line change.

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
