## Quake Live Ex

273 commits on top of **tjone270/ioquakelive**. Requires a legitimate `pak00.pk3` — nothing from Quake Live is redistributed.

**Spawning — the largest fault in the game.**
~~Every selector fell back to the same map entity forever, so everyone who died appeared on one spot and telefragged whoever was standing there~~ — **93% of deaths on thunderstruck, 98% on citycrossings.** Now: tiered selection with a least-recently-used fallback, team pads when a map is short on deathmatch spawns, stepping aside instead of telefragging, spawn protection with a firing lockout, and telefrags that no longer count as a death.
Trinity, same map before and after: **159 → 24** telefrags against **492 → 1842** weapon deaths.

**Renderer** — a Vulkan backend from Quake3e, SDL windowing written from scratch, a working fallback to OpenGL, anisotropic filtering to 16x, dither, look presets, and ~~dlights that never reached world surfaces~~ **lights that do**.

**Snapshots** — ~~1,350,347 entities discarded silently in one map~~ **peak 128 of 256, zero drops at 64 players**. `snapstats` reports composition, drops, and whether the tick or the client `rate` clamp is the real limit.

**Bots** — a tactical layer: weapon-aware engagement range (~~everything was fought at 140 units~~), missile dodging, group pushes, retreating that stops oscillating, item awareness. `bot_tactics 0` restores stock behaviour exactly.

**Freeze Tag** ~~shipped as a supported gametype and never froze anyone~~ — fixed, ice shell and all.

Plus persistent bans, five overflow fixes, Windows crash reporting, a server browser, a scrollable scoreboard, ~~text fields you couldn't type into~~, and console text that scales.

*The honest part:* the spawn and snapshot numbers are measured. The ban system, spawn protection and bot layer are read-correct but not yet proven in a live match.

Detail in `IMPROVED.md`, every tracked issue in `TRACKER.md`.
