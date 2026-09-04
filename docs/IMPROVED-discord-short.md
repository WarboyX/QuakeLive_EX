## Quake Live Ex

An open-source client and server for **Quake Live**, built on ioquake3. It plays on the same servers as the stock client — protocol 91, `com_gamename` and `baseqz` are untouched, so **stock clients still see and join servers running this**, and you can switch back any time.

**Why run it instead of the stock client**

**A Vulkan renderer.** Better frame times and far more consistent pacing than the OpenGL path, and `com_maxfps` finally lands on rates that do not divide 1000 instead of snapping to the nearest one that does.

**The Quake 3 look, back.** Classic, voodoo, gloss and modern presets, output dither, anisotropic filtering to 16x, and a render menu that exposes all of it instead of hiding switches that do nothing.

**Servers built for 64 players.** Spawn selection, memory sizing and snapshot delivery were all rebuilt for full servers, and `snapstats` tells you exactly what yours is spending its bandwidth on.

**It stays up, and says why when it doesn't.** Map votes, mid-round drops, map restarts, high bot counts and score caps have all been chased down and fixed, and a Windows crash writes a log naming the address it touched instead of just vanishing — which is more than a closed binary can ever give you.

**Bots that fight properly.** They engage at the range their weapon works at, dodge rockets, push as a group and fall back when outnumbered.

**It runs where you want.** Linux, Windows and macOS including Apple Silicon, with dedicated servers, ready-made configs and a real ban system.

**And it is open source**, so a bug can be fixed rather than lived with.

**You need to own the game.** `pak00.pk3` from a Steam install has to be present — it is not redistributed here and never will be.

*Honest part:* this is a work in progress, and the bots in particular are new and barely tested. `TRACKER.md` has every known issue.
