## Quake Live Ex

An open-source client and server for **Quake Live**, built on ioquake3 — 273 commits on top of **tjone270/ioquakelive**.

**The goal** is a build functionally equivalent to Quake Live 1069 that then goes past it, without splitting the playerbase to do it. `com_gamename`, protocol 91 and `baseqz` are untouched, so **stock Quake Live clients still see these servers and still join them**. "Ex" is for extended.

**You need to own the game.** `pak00.pk3` from a Steam install has to be present. It is not redistributed here and never will be — the engine is GPL, the assets are id's, and that line does not get blurred.

**The real enemy is silence.** A cvar registered and read by nothing. A shader name the pak does not contain. A menu that fails to parse and leaves you looking at the wrong items. All of them produce no error and nothing on screen — so much of the work is making failures say something, then fixing what they said.

**The approach.** Treat the cause, not the symptom: the snapshot fix was not a bigger buffer, it was finding out why the buffer filled. Measure before and after on the same map instead of asserting an improvement. Put anything new behind a cvar that restores stock behaviour, so it can be tested against itself live. And never ship a change that is right here and wrong on a stock client — the improved shotgun is written, tested, and off by default for that reason.

**Where that got us.** 93% of deaths on thunderstruck were telefrags rather than kills; trinity went **159 → 24** telefrags against **492 → 1842** weapon deaths. 1,350,347 snapshot entities were being dropped silently in one map; peak is now 128 of 256 with zero drops at 64 players. Plus a Vulkan renderer, smarter bots, a Freeze Tag that actually freezes people, and menus that do what they say.

*Honest part:* those numbers are measured. Plenty else is read-correct and unproven in a live match; `TRACKER.md` says which is which.
