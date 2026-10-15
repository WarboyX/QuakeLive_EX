# Deferred review findings

Baseline: 387d68a61ed3c5d3a36cb5f956e722208354dafc.
Saved at the user’s request; these two changes are not included in the bot work.

1. **Failed Linux affinity read leaves no restoration mask.** `Sys_CpuPlaceMainThread` can change affinity after the initial `sched_getaffinity` fails. With no saved original mask, turning placement off cannot restore the previous CPUs. The exact-source syscall fixture observed one placement write and no restoration write. Proposed fix: do not change affinity until its original mask has been captured; permit retry after a failed read.
2. **Leading empty command-line segment consumes a command slot.** `Com_ParseCommandLine` counts the segment before the first `+`, even when empty. A line with 128 actual commands retains 127 and ignores one; 130 retains 127 and ignores three. Proposed fix: stop counting the initial empty segment, with regression cases for leading `+`, a nonempty initial segment, quotes, and the capacity boundary.

Both have reproduction files in QuakeLive_EX-387d68a6-review.zip. They remain deferred rather than silently fixed in a feature build.
