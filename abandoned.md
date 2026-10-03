# This branch is abandoned

Work moved to **`development`** on 2026-10-03. This branch was the working
branch up to commit `043fd97` and is kept, unchanged apart from this note, so
existing links and clones still resolve.

`development` starts from that same commit, so nothing was lost. Do not commit
here. To switch a local clone over:

```
git fetch origin
git switch development
```

or, to rename your local branch in place:

```
git branch -m claude/ioquakelive-review-6756u2 development
git branch -u origin/development development
```

The repository itself also moved, to `https://github.com/WarboyX/QuakeLive_EX`;
the old address redirects. Update a clone's remote with:

```
git remote set-url origin https://github.com/WarboyX/QuakeLive_EX
```
