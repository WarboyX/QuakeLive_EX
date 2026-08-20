#!/bin/sh
# build-macos.sh - build and package the macOS client. RUN THIS ON THE MAC.
#
# Builds arm64 and x86_64 by default and puts both in one archive, so the same
# download runs on an M-series machine and on an Intel Mac.
#
# Why this is a separate script instead of a third target in package-release.sh
# -----------------------------------------------------------------------------
# package-release.sh cross-compiles Windows from Linux because mingw-w64 is a
# complete, freely redistributable toolchain. There is no equivalent for macOS.
# Building for Darwin needs Apple's SDK, and Apple licenses that SDK for use on
# Apple hardware - it cannot be installed into a Linux build container. osxcross
# exists and the Makefile already expects it, but osxcross is built *from* an SDK
# extracted out of your own Xcode install, so the chain still starts on a Mac.
#
# So the Mac build happens on the Mac. This script makes that one command and
# produces an archive shaped like the other two.
#
# Prerequisites (once)
# --------------------
#   xcode-select --install          # clang, headers, make, python3
#
# and SDL2, for which the choice matters if you want both architectures:
#
#   brew install sdl2               # single-arch: only the host arch's CLIENT
#                                   # builds - see "SDL2 and the second arch"
#   or the universal SDL2.framework from libsdl.org, into /Library/Frameworks,
#   which builds both clients.
#
# Optional, only for the Vulkan renderer:
#   brew install --cask vulkan-sdk  # MoltenVK; without it, pass VULKAN=0
#
# Usage
# -----
#   ./build-macos.sh                     # arm64 + x86_64
#   ARCHES=arm64 ./build-macos.sh        # M-series only
#   ARCHES=x86_64 ./build-macos.sh       # Intel only
#   VULKAN=0 ./build-macos.sh            # OpenGL only
#
# SDL2 and the second arch
# ------------------------
# Only the *client* links SDL. The dedicated server and the three game modules
# do not, so they build for both architectures unconditionally. The client is
# built per-arch in a second pass that is allowed to fail: a Homebrew SDL2 is
# built for whichever arch that Homebrew is (arm64 under /opt/homebrew, x86_64
# under /usr/local), so on an M-series Mac with plain `brew install sdl2` the
# x86_64 client has nothing to link against. That is a missing dependency, not a
# broken tree, so the script says so and keeps everything else rather than
# throwing the build away. Install the universal SDL2.framework to get both.
#
# THE sv_pure TRAP - read this before you try to join a server
# ------------------------------------------------------------
# iobin.pk3 holds *every* platform's game modules in one pak, so a Linux server
# and its Windows clients agree on one checksum. A Mac client looks for
# cgamearm64.dylib / cgamex86_64.dylib inside that same pak - qcommon/files.c
# builds the name as cgame{ARCH_STRING}{DLL_EXT}.
#
# This script alone can only build the Mac share of that. The pak it writes
# contains dylibs and nothing else, which is correct for playing locally and
# WRONG for joining a server running the released pak - the checksums differ, so
# sv_pure rejects it. To get one pak that serves every platform:
#
#   1. on the Mac:    ./build-macos.sh        -> release/modules/*.dylib
#   2. copy release/modules/ to the Linux box as content/modules/
#   3. on Linux:      ./package-release.sh    -> one pak, .so + .dll + .dylib
#   4. copy that iobin.pk3 back into the Mac archive's baseq3/ (or copy
#      content/modules/ to the Mac and re-run this, which folds in whatever it
#      finds there the same way).
#
# Both scripts print the exact module list that went into the pak. If the lists
# do not match, the platforms cannot play together, and that printout is the
# only place it is visible before someone fails to connect.

set -e

OUT=${OUT:-release}
ARCHES=${ARCHES:-arm64 x86_64}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}
EXTRA_MODULES=${EXTRA_MODULES:-content/modules}

if [ "$(uname -s)" != "Darwin" ]; then
    echo "build-macos.sh: this has to run on macOS." >&2
    echo "  Apple's SDK is licensed for Apple hardware, so there is no Linux" >&2
    echo "  cross-toolchain to fall back to. See the header of this file." >&2
    exit 1
fi

# MoltenVK is what makes Vulkan work on macOS - a Vulkan implementation layered
# over Metal, not a driver. SDL_Vulkan_LoadLibrary finds it if the Vulkan SDK is
# installed; if it is not, the renderer builds fine and then has nothing to load
# at runtime, so decide here rather than there.
if [ -z "$VULKAN" ]; then
    if [ -e "$HOME/VulkanSDK" ] || [ -e /usr/local/lib/libMoltenVK.dylib ] \
       || [ -e /opt/homebrew/lib/libMoltenVK.dylib ] || [ -n "$VULKAN_SDK" ]; then
        VULKAN=1
    else
        VULKAN=0
        echo "note: no MoltenVK found - building without the Vulkan renderer."
        echo "      (brew install --cask vulkan-sdk, then VULKAN=1 ./build-macos.sh)"
    fi
fi

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

REV=$(git rev-parse --short HEAD)
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    REV="$REV-dirty"
fi

# Same two gates package-release.sh runs. A Mac build is still a build: a config
# that leaks a cvar, or an unclassified empty function, is exactly as wrong here.
python3 content/serverconfigs/check-configs.py
python3 tools/stub-report.py

echo "building $REV for macos [$ARCHES] (vulkan=$VULKAN)"

# Stamp the loaded pak01 with the revision - see package-release.sh for why both
# pak01 and iobin carry a stamp. BSD sed needs the empty -i argument.
sed -i '' "s/@@PAKSTAMP@@/$REV/g" content/pak01/ui/main.menu
trap 'sed -i "" "s/pak01 $REV/pak01 @@PAKSTAMP@@/g" content/pak01/ui/main.menu' EXIT

MD="$OUT/pkg/quakelive-macos-$REV"
rm -rf "$MD" "$OUT/stage" "$OUT/modules"
mkdir -p "$MD/baseq3" "$OUT/stage" "$OUT/modules"

CLIENTS=""
NOCLIENT=""

for A in $ARCHES; do
    B="build/release-darwin-$A"
    echo
    echo "--- $A ---"

    # Pass 1: everything that does not link SDL. Always builds.
    make PLATFORM=darwin ARCH="$A" BUILD_CLIENT=0 BUILD_SERVER=1 \
         BUILD_GAME_SO=1 BUILD_RENDERER_VULKAN="$VULKAN" -j"$JOBS"

    # Pass 2: the client. Allowed to fail - see "SDL2 and the second arch".
    set +e
    make PLATFORM=darwin ARCH="$A" BUILD_CLIENT=1 BUILD_SERVER=0 \
         BUILD_GAME_SO=0 BUILD_BASEGAME=0 BUILD_RENDERER_VULKAN="$VULKAN" -j"$JOBS"
    CLIENT_RC=$?
    set -e

    cp -p "$B/quakelive_dedicated.$A" "$MD/"
    cp -p "$B"/baseq3/*.dylib "$MD/baseq3/" 2>/dev/null || true
    cp -p "$B"/baseq3/*.dylib "$OUT/stage/"
    cp -p "$B"/baseq3/*.dylib "$OUT/modules/"

    if [ "$CLIENT_RC" -eq 0 ] && [ -f "$B/quakelive.$A" ]; then
        cp -p "$B/quakelive.$A" "$MD/"
        [ -f "$B/opengl2$A.dylib" ] && cp -p "$B/opengl2$A.dylib" "$MD/"
        [ -f "$B/vulkan$A.dylib" ] && cp -p "$B/vulkan$A.dylib" "$MD/"
        CLIENTS="$CLIENTS $A"
    else
        NOCLIENT="$NOCLIENT $A"
    fi

    # pak01 is architecture-independent; whichever pass built it will do.
    [ -f "$B/baseq3/pak01.pk3" ] && cp -p "$B/baseq3/pak01.pk3" "$MD/baseq3/"
done

# The game modules live in the pak, not loose next to the binary - the engine
# extracts them from iobin.pk3 at startup (FS_ExtractGamecode).
rm -f "$MD"/baseq3/*.dylib

# Modules built elsewhere, if someone dropped them in. Without these the pak is
# Mac-only and will not pass sv_pure against a Linux/Windows server.
FOREIGN=0
if [ -d "$EXTRA_MODULES" ]; then
    for f in "$EXTRA_MODULES"/*.so "$EXTRA_MODULES"/*.dll; do
        [ -e "$f" ] || continue
        cp -p "$f" "$OUT/stage/"
        FOREIGN=1
    done
fi

(cd "$OUT/stage" && zip -q -9 "$MD/baseq3/iobin.pk3" *)
cp -p TRACKER.md "$MD/" 2>/dev/null || true
cp -p server.cfg.example "$MD/" 2>/dev/null || true
cp -p content/serverconfigs/*.cfg "$MD/baseq3/"

(
  cd "$MD"
  {
    echo "# ioquakelive $REV (macos:$ARCHES)"
    echo "# sha256 of every file in this archive"
    echo "#"
    echo "# The game verifies the modules it extracts out of iobin.pk3 by itself."
    echo "# This is for checking the archive: shasum -a 256 -c checksums.txt"
    find . -type f ! -name checksums.txt -print0 | sort -z | xargs -0 shasum -a 256
  } > checksums.txt
)

mkdir -p "$OUT/out"
rm -f "$OUT/out/quakelive-macos-$REV.zip"
(cd "$OUT/pkg" && zip -q -r -9 "$OUT/out/quakelive-macos-$REV.zip" "quakelive-macos-$REV")
rm -rf "$OUT/stage"

echo
echo "iobin.pk3 contains:"
unzip -l "$MD/baseq3/iobin.pk3" | awk 'NR>3 && NF>3 {print "  " $4}' | grep -v '^  $' || true
if [ "$FOREIGN" = "0" ]; then
    echo
    echo "  ^ dylibs only. This build can host and play locally, but it will fail"
    echo "    sv_pure against a server running the released iobin.pk3, because that"
    echo "    pak has a different checksum. Drop the .so and .dll modules into"
    echo "    $EXTRA_MODULES/ and re-run to get a pak that serves every platform."
fi

echo
echo "clients built:  ${CLIENTS:- none}"
if [ -n "$NOCLIENT" ]; then
    echo "clients SKIPPED:$NOCLIENT - no SDL2 for that architecture."
    echo "  Only the client links SDL; the dedicated server and the game modules"
    echo "  for$NOCLIENT are in this archive and are fine. A Homebrew SDL2 is"
    echo "  built for that Homebrew's own arch, so install the universal"
    echo "  SDL2.framework from libsdl.org if you want both clients."
fi

echo
echo "modules for the shared pak left in $OUT/modules/ - copy to content/modules/"
echo "on the Linux box and re-run package-release.sh."
echo
echo "Gatekeeper: the binaries are unsigned, so macOS quarantines them out of a"
echo "downloaded zip. After extracting:  xattr -dr com.apple.quarantine <folder>"
echo
ls -lh "$OUT/out/quakelive-macos-$REV.zip"
