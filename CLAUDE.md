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

**A menu that fails to parse does not look like one.** The parser keeps going,
so a stray brace merges the next menu into the current one and what you see is a
menu with the wrong items in it, or a button that opens nothing. Nothing says so
on screen; it is loud in the console and silent where you are looking. Run
`tools/check-menus.py` after touching any `.menu` file. It needs nothing else:
`docs/ql-menu-names.txt` is checked in, so open/close targets that live in
pak00 resolve. It checks our files only. Pass a directory of Quake Live's `ui/`
to check against the real files, and `--dump-names <dir>` to regenerate that
list after a game patch.

**`CVAR_ARCHIVE` on a shipped default means the default stops applying.** The
value is written into a config on first run and that config then wins forever,
so changing the default later does nothing. Cost two rounds already
(`r_dlightMode`, `con_scale`). Archive is for values a user sets, not values we
choose.

## Assets

Quake Live's `pak00.pk3` is **not** redistributable and must never be committed
or shipped. Its `ui/` files are for reading only.

### The pak manifest

`docs/pak-manifest.txt` is the list of every file name inside the shipped
`.pk3`s — every model, shader, texture, sound. **Names only: no asset content.**
That is not the pak and does not fall under the rule above; it is checked in so
asset names can be verified without guessing.

**Check it before registering any asset.** `RE_RegisterShader` and
`RE_RegisterModel` both return **0** for a name the pak does not contain, so a
wrong name renders nothing and reports nothing — the same silent-failure shape
as a registered cvar that nothing reads. That has already cost a round: cgame
registered three Freeze Tag ice models under invented names, and a scan showed
the pak has no ice meshes at all — the ice is a shader over the generic gib
sphere.

`docs/ql-menu-names.txt` is the same idea for menu names — what
`tools/check-menus.py` resolves `open`/`close` targets against, so the check
needs nothing but the repo. Names only, same reasoning.

Regenerate it after a game patch with `tools/dump-pak-manifest.ps1`. **The
script is unsigned, so PowerShell's execution policy refuses to run it as a
file** — pass `-ExecutionPolicy Bypass`, which applies to that one invocation
only:

```
powershell -ExecutionPolicy Bypass -File .\tools\dump-pak-manifest.ps1 -BaseQ3 "<install>\baseq3" -Output docs\pak-manifest.txt
```

Execution policy applies to script *files*, not to pasted commands, so the same
work pasted straight into a prompt runs without it.

Point it at the **basepath** `baseq3` (next to the executable), not the one in
AppData — AppData is `fs_homepath`, where the engine extracts game modules at
runtime, and holds no pak00.

Note the manifest is a grep target, not prose: a plain `ice` search also matches
`qzpract**ice**1`, `sl**ice**_12` and `vo**ice**_window`, so read the hits
rather than counting them.
