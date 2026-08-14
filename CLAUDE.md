# Working conventions

## Branches

Default working branch: **`claude/ioquakelive-review-6756u2`**. All work goes
there and stays there. `main` does not move on its own.

**"merge into main" means, in this order:**

1. Merge the working branch into `main` and push `main`.
2. Return to `claude/ioquakelive-review-6756u2` — the merge is a release step,
   not a change of where work happens.
3. Build a fresh package from that commit and hand over both archives, for
   upload to the GitHub release page.

Nothing else moves `main`. Do not commit to it directly between merges; that
happened once by accident after a merge left `main` checked out, and three
builds went out unreviewed before it was noticed.

### Release page

The release page and asset upload have to be done by hand. This environment
cannot do either: the GitHub MCP server exposes only read-only release tools
(`list_releases`, `get_latest_release`, `get_release_by_tag`), there is no `gh`
CLI, and pushing a tag returns HTTP 403 while branch pushes succeed. Produce the
archives and the notes; the user uploads them.

`RELEASE-NOTES.md` at the repo root is the paste-ready body — keep it current
when merging.

## Builds

`./package-release.sh` builds both platforms and writes
`release/out/quakelive-{linux-x86_64,windows-x64}-<shortsha>.zip`. It stamps
`pak01` with the revision and fails the build if a server config leaks a cvar
(`content/serverconfigs/check-configs.py`).

`release/` is gitignored — build output must never be committed.

Both the main menu and the console carry build stamps (`pak01`, `iobin`, and
the cgame/qagame init lines), because menu fixes ship in `pak01.pk3` and module
fixes in `iobin.pk3`, and replacing only one of the two has repeatedly made a
fixed bug look unfixed.

## Tracking

`TRACKER.md` is the issue list. Every item records **two** things, which do not
imply each other:

- **Lives in** — our client (`cgame`/`ui`/client engine), our server
  (`qagame`/server engine), or both. Everything here is ours to fix; this says
  which binary to go to.
- **Seen by** — our client only, every client, or stock Steam Quake Live only.

The third value of "Seen by" is the dangerous one and is *not* a synonym for
"server side". Where the server sends a seed and each client regenerates the
result locally, our cgame follows the server's cvars and a stock client cannot,
so a change is invisible here and wrong for everyone else. The shotgun is
exactly this. A server change to anything the client redraws from a seed is
broken for stock clients until proven otherwise.

## Two recurring traps in this codebase

**A registered cvar is not an implemented feature.** 186 cvars are registered
and read by nothing — setting them produces no error and no effect. This has
caused real bugs twice (`g_spawnItemWeapons`, `g_instaGib`). Run
`tools/dead-cvars.py <path-to-ql-ui>` after adding one.

**`CVAR_ARCHIVE` on a shipped default means the default stops applying.** The
value is written into a config on first run and that config then wins forever,
so changing the default later does nothing. Cost two rounds already
(`r_dlightMode`, `con_scale`). Archive is for values a user sets, not values we
choose.

## Assets

Quake Live's `pak00.pk3` is **not** redistributable and must never be committed
or shipped. Its `ui/` files are for reading only.
