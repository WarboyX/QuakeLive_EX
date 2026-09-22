#!/bin/sh
# [QL] E109. One launcher for every Linux flavour - .deb, AppImage and Flatpak.
#
# The three install the same payload to three different prefixes, so the
# launcher works out its own location rather than being generated per flavour
# with a path baked in. One script, one behaviour, one place a bug can be.
#
# WHY fs_basepath HAS TO BE SET AT ALL. Sys_DefaultInstallPath() is the
# directory the binary sits in, which is right when someone unzips an archive
# and wrong for every packaged layout: on a .deb the binary is in /usr/lib and
# the game data is beside it, but the process is started from /usr/bin through
# a symlink, and the engine would look wherever the caller happened to be.
#
# WHAT IS NOT HERE. Nothing copies, finds or mentions pak00.pk3. The engine
# locates the player's own Steam install by itself (Sys_SteamPath, E109) and
# reads it in place. pak00 is not redistributable, so a launcher that "helpfully"
# copied it would be the one part of this that must never exist.
set -eu

# Resolve symlinks so /usr/bin/quakelive -> /usr/lib/quakelive/... lands in the
# real directory. readlink -f is not POSIX but is present on every Linux this
# targets; the loop is the fallback for anything where it is not.
self=$0
if command -v readlink >/dev/null 2>&1 && readlink -f "$self" >/dev/null 2>&1; then
    self=$(readlink -f "$self")
else
    while [ -L "$self" ]; do
        link=$(ls -ld -- "$self" | sed 's/.* -> //')
        case $link in
            /*) self=$link ;;
            *)  self=$(dirname "$self")/$link ;;
        esac
    done
fi
HERE=$(cd -- "$(dirname -- "$self")" && pwd)

# An AppImage mounts read-only and sets APPDIR; the payload is under it.
if [ -n "${APPDIR:-}" ] && [ -d "$APPDIR/usr/lib/quakelive" ]; then
    BASE=$APPDIR/usr/lib/quakelive
elif [ -d "$HERE/../lib/quakelive" ]; then
    BASE=$(cd -- "$HERE/../lib/quakelive" && pwd)
else
    BASE=$HERE
fi

BIN=$BASE/quakelive.x86_64
if [ ! -x "$BIN" ]; then
    echo "quakelive: cannot find the game binary under $BASE" >&2
    exit 1
fi

# fs_homepath is left alone deliberately - the engine's own default (~/.quakelive,
# or the Flatpak's private home) is already the right writable location, and it
# is where game modules are extracted out of iobin.pk3 at run time. That is what
# lets the installed tree stay read-only, which /usr and a Flatpak both require.
exec "$BIN" +set fs_basepath "$BASE" "$@"
