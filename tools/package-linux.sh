#!/bin/bash
# [QL] E109. Linux distribution flavours, built from the staged tree that
# package-release.sh has already assembled and checked.
#
#   tools/package-linux.sh <staged-dir> <rev> <output-dir>
#
# Produces, beside the existing .zip:
#
#   quakelive-linux-x86_64-<rev>.tar.gz   universal, for people who just want files
#   quakelive_<rev>_amd64.deb             Debian/Ubuntu/Mint
#   QuakeLiveEx-<rev>-x86_64.AppImage     one file, any distro, no install
#
# The Flatpak is a manifest rather than an artefact - see
# packaging/flatpak/. Building one needs flatpak-builder and a runtime download,
# which this environment does not have.
#
# NOTHING HERE SHIPS GAME DATA. Every flavour contains our binaries, our pak01
# and our modules only. The engine finds the player's own Steam install at run
# time (Sys_SteamPath, E109) and reads pak00.pk3 in place. A package that
# bundled it would not be distributable at all.
#
# Each step is skipped with a stated reason rather than failing the release when
# its tool is missing, because the .zip is the deliverable these are additional
# to. An empty flavour list is a worse outcome than no release, but a failed
# release because dpkg-deb is absent is worse than both.
set -euo pipefail

STAGE=${1:?staged directory}
REV=${2:?revision}
OUT=${3:?output directory}

STAGE=$(cd -- "$STAGE" && pwd)
mkdir -p "$OUT"
OUT=$(cd -- "$OUT" && pwd)
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

NAME=$(basename "$STAGE")          # quakelive-linux-x86_64-<rev>
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The launcher and desktop entry travel inside every flavour, including the
# tarball - the tarball is what the Flatpak manifest consumes, and it needs both.
#
# package-release.sh normally stages these before it writes checksums.txt, so
# that every archive of a revision has identical contents. This is the fallback
# for running this script by hand against a tree that does not have them; it
# does not overwrite, so it cannot invalidate a checksum manifest written
# upstream.
for f in quakelive-launcher.sh quakelive.desktop; do
    [ -e "$STAGE/$f" ] || cp -p "$ROOT/packaging/$f" "$STAGE/"
done
chmod 755 "$STAGE/quakelive-launcher.sh"

# ---------------------------------------------------------------- tar.gz -----
# --owner/--group so the archive does not carry whatever uid built it. Files
# extracted as root-owned into a user's home is a small thing that looks like a
# broken download.
tar -czf "$OUT/$NAME.tar.gz" -C "$(dirname "$STAGE")" \
    --owner=0 --group=0 "$NAME"
echo "package-linux: $NAME.tar.gz"

# ------------------------------------------------------------------- deb -----
if command -v dpkg-deb >/dev/null 2>&1; then
    DEB=$WORK/deb
    # /usr/lib/quakelive for the payload, a symlink in /usr/bin. The FHS wants
    # architecture-dependent files that are not directly executed by the user
    # in /usr/lib, and that is exactly what this is: the user runs the launcher.
    install -d "$DEB/usr/lib/quakelive" "$DEB/usr/bin" \
               "$DEB/usr/share/applications" "$DEB/usr/share/doc/quakelive" \
               "$DEB/DEBIAN"
    cp -a "$STAGE/." "$DEB/usr/lib/quakelive/"
    rm -f "$DEB/usr/lib/quakelive/checksums.txt"
    install -m755 "$ROOT/packaging/quakelive-launcher.sh" "$DEB/usr/bin/quakelive"
    install -m644 "$ROOT/packaging/quakelive.desktop" \
        "$DEB/usr/share/applications/io.github.warboyx.QuakeLiveEx.desktop"
    install -m644 "$ROOT/TRACKER.md" "$DEB/usr/share/doc/quakelive/" 2>/dev/null || true

    # A version must start with a digit for dpkg to accept it, and a git short
    # sha frequently does not. 0.0.0+<rev> keeps the sha visible and sorts below
    # any future real version, which is what an alpha should do.
    cat > "$DEB/DEBIAN/control" <<EOF
Package: quakelive
Version: 0.0.0+$REV
Section: games
Priority: optional
Architecture: amd64
Depends: libc6, libsdl2-2.0-0, libgl1
Recommends: steam
Maintainer: WarboyX <warboyx@gmail.com>
Description: Quake Live Ex - Quake Live engine port
 An engine port for Quake Live, with a Vulkan renderer, reflective water
 and surface work not present in the stock client.
 .
 This package contains the engine and its own assets only. It does NOT
 contain Quake Live's game data, which is not redistributable. An existing
 Steam installation of Quake Live (app 282440) is required; the game finds
 it automatically and reads it in place, copying nothing.
EOF

    # Size is what a user sees in the package manager and the only field here
    # that cannot be written by hand without going stale.
    echo "Installed-Size: $(du -ks "$DEB/usr" | cut -f1)" >> "$DEB/DEBIAN/control"

    DEBFILE=$OUT/quakelive_0.0.0+${REV}_amd64.deb
    rm -f "$DEBFILE"
    # dpkg-deb wants root-owned contents; --root-owner-group does that without
    # fakeroot, which is not installed here.
    dpkg-deb --root-owner-group -Zxz -b "$DEB" "$DEBFILE" >/dev/null
    echo "package-linux: $(basename "$DEBFILE")"
else
    echo "package-linux: no dpkg-deb, skipping .deb"
fi

# -------------------------------------------------------------- AppImage -----
# An AppImage is the official type-2 runtime with a squashfs of the AppDir
# appended. appimagetool does exactly that and nothing else that matters here,
# so this does it directly rather than depending on a second downloaded tool.
RUNTIME=${APPIMAGE_RUNTIME:-$ROOT/release/appimage-runtime-x86_64}
if [ ! -f "$RUNTIME" ] && command -v curl >/dev/null 2>&1; then
    mkdir -p "$(dirname "$RUNTIME")"
    curl -sSL --max-time 120 -o "$RUNTIME.tmp" \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 \
        && mv "$RUNTIME.tmp" "$RUNTIME" || rm -f "$RUNTIME.tmp"
fi

if command -v mksquashfs >/dev/null 2>&1 && [ -f "$RUNTIME" ]; then
    APPDIR=$WORK/AppDir
    install -d "$APPDIR/usr/lib/quakelive" "$APPDIR/usr/bin" "$APPDIR/usr/share/applications"
    cp -a "$STAGE/." "$APPDIR/usr/lib/quakelive/"
    rm -f "$APPDIR/usr/lib/quakelive/checksums.txt"
    install -m755 "$ROOT/packaging/quakelive-launcher.sh" "$APPDIR/usr/bin/quakelive"

    # An AppImage needs AppRun, a .desktop and an icon at the AppDir root. The
    # desktop file is required to name an icon that exists, so a missing icon is
    # a broken AppImage rather than an ugly one.
    install -m755 "$ROOT/packaging/quakelive-launcher.sh" "$APPDIR/AppRun"
    install -m644 "$ROOT/packaging/quakelive.desktop" \
        "$APPDIR/io.github.warboyx.QuakeLiveEx.desktop"
    cp -p "$ROOT/packaging/quakelive.desktop" \
        "$APPDIR/usr/share/applications/io.github.warboyx.QuakeLiveEx.desktop"
    if [ -f "$ROOT/packaging/icon.png" ]; then
        install -m644 "$ROOT/packaging/icon.png" "$APPDIR/io.github.warboyx.QuakeLiveEx.png"
    else
        # A 1x1 PNG is not a good icon, but a desktop entry pointing at an icon
        # that does not exist makes some launchers refuse the AppImage outright.
        printf '\211PNG\r\n\032\n\000\000\000\015IHDR\000\000\000\001\000\000\000\001\010\006\000\000\000\037\025\304\211\000\000\000\013IDATx\332c\370\017\004\000\011\373\003\375\343U\362\236\000\000\000\000IEND\256B`\202' \
            > "$APPDIR/io.github.warboyx.QuakeLiveEx.png"
    fi

    APPIMG=$OUT/QuakeLiveEx-$REV-x86_64.AppImage
    rm -f "$APPIMG"
    mksquashfs "$APPDIR" "$WORK/fs.squashfs" -root-owned -noappend -quiet -comp zstd
    cat "$RUNTIME" "$WORK/fs.squashfs" > "$APPIMG"
    chmod 755 "$APPIMG"
    echo "package-linux: $(basename "$APPIMG")"
else
    echo "package-linux: no mksquashfs or no AppImage runtime, skipping .AppImage"
fi

# The manifest, with the tarball name it should consume filled in - it is the
# one field that changes every release and the one most likely to be left stale.
if [ -f "$ROOT/packaging/flatpak/io.github.warboyx.QuakeLiveEx.yml" ]; then
    sed "s/quakelive-linux-x86_64-REV\.tar\.gz/$NAME.tar.gz/" \
        "$ROOT/packaging/flatpak/io.github.warboyx.QuakeLiveEx.yml" \
        > "$OUT/io.github.warboyx.QuakeLiveEx.yml"
    echo "package-linux: io.github.warboyx.QuakeLiveEx.yml (manifest - build it with flatpak-builder)"
fi
