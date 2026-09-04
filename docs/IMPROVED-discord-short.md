## Quake Live Ex

273 commits on top of **tjone270/ioquakelive**. Requires a legitimate `pak00.pk3` — nothing from Quake Live is redistributed.

**Spawning turned out to be the largest fault in the game.** Every selector fell back to the same map entity forever, so everyone who died appeared on one spot and telefragged whoever was standing there — **93% of deaths on thunderstruck, 98% on citycrossings**. It now sorts points into clear, recently used and occupied with a least-recently-used fallback, uses team pads when a map is short on deathmatch spawns, steps aside instead of telefragging, and gives 1.5s of spawn protection with a firing lockout so it cannot be used to camp. Telefrags no longer count as a death.
Trinity, same map before and after: **159 → 24** telefrags against **492 → 1842** weapon deaths.

**A Vulkan renderer**, vendored from Quake3e and made to run against this tree — SDL windowing written from scratch, and a fallback to OpenGL that works. Anisotropic filtering to 16x, dither, look presets, and dynamic lights that reach world surfaces for the first time.

**Snapshots.** 1,350,347 entities were being discarded silently in a single map. Peak is now **128 of 256 with zero drops at 64 players**, and `snapstats` reports composition, drops, and whether the tick or the client `rate` clamp is the real limit.

**Bots** got a tactical layer — weapon-aware engagement range (everything was fought at 140 units), missile dodging, group pushes, retreating that stops oscillating, item awareness. `bot_tactics 0` restores stock behaviour.

**Freeze Tag** shipped as a supported gametype and never froze anyone. Fixed, ice shell and all.

Plus persistent bans, five overflow fixes, Windows crash reporting, a server browser, a scrollable scoreboard, text fields you can type into, and console text that scales.

*The honest part:* the spawn and snapshot numbers are measured. The ban system, spawn protection and bot layer are read-correct but not proven in a live match.

Detail in `IMPROVED.md`, tracked issues in `TRACKER.md`.
