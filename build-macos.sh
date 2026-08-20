#!/bin/sh
# build-macos.sh - build and package the macOS arm64 client. RUN THIS ON THE MAC.
#
# Why this is a separate script instead of a third target in package-release.sh
# -----------------------------------------------------------------------------
# package-release.sh cross-compiles Windows from Linux because mingw-w64 is a
# complete, freely redistributable toolchain. There is no equivalent for macOS.
# Building for Darwin needs Apple's SDK, and Apple licenses that SDK for use on
# Apple hardware - it cannot be installed into a Linux build container. osxcross
# exists and works, but it is built *from* an SDK you extract from your own Xcode
# install, so it still starts on a Mac.
#
# So the Mac build happens on the Mac. This script makes that one command and
# produces an archive shaped exactly like the other two.
#
# Prerequisites (once)
# --------------------
#   xcode-select --install          # clang, headers, make
#   brew install sdl2               # the Makefile's darwin block uses sdl2-config
#
# Optional, only if you want the Vulkan renderer:
#   brew install --cask vulkan-sdk  # MoltenVK; without it, pass VULKAN=0
#
# Usage
# -----
#   ./build-macos.sh                # arm64 (M1-M4), Vulkan on if MoltenVK is present
#   VULKAN=0 ./build-macos.sh       # OpenGL only
#   ARCH=x86_64 ./build-macos.sh    # Intel Mac / Rosetta target
#
# THE sv_pure TRAP - read this before you try to join a server
# ------------------------------------------------------------
# iobin.pk3 holds *every* platform's game modules in one pak, so a Linux server
# and its Windows clients agree on one checksum. A Mac client looks for
# cgamearm64.dylib / uiarm64.dylib / qagamearm64.dylib inside that same pak
# (qcommon/files.c:1205-1209 builds the name as cgame{ARCH_STRING}{DLL_EXT}).
#
# This script alone can only build the Mac third of that. The pak it writes
# contains dylibs and nothing else, which is correct for playing locally and
# WRONG for joining a server running the released pak - the checksums differ, so
# sv_pure rejects it.
#
# To get one pak that serves all three, carry the modules across:
#
#   1. on the Mac:    ./build-macos.sh
#                     -> release/modules/*.dylib
#   2. copy release/modules/ to the Linux box, into content/modules/
#   3. on Linux:      ./package-release.sh
#                     -> iobin.pk3 now has .so + .dll + .dylib, one checksum
#   4. copy that iobin.pk3 back into the Mac archive's baseq3/ (or copy
#      content/modules/ back to the Mac and re-run this script, which folds in
#      whatever it finds there the same way).
#
# Both scripts print the exact list of modules that went into the pak. If the
# three lists do not match, the platforms cannot play together, and that is the
# only place it is visible before someone fails to connect.

set -e

OUT=${OUT:-release}
ARCH=${ARCH:-arm64}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}
EXTRA_MODULES=${EXTRA_MODULES:-content/modules}

if [ "$(uname -s)" != "Darwin" ]; then
    echo "build-macos.sh: this has to run on macOS." >&2
    echo "  Apple's SDK is licensed for Apple hardware, so there is no Linux" >&2
    echo "  cross-toolchain to fall back to. See the header of this file." >&2
    exit 1
fi

# MoltenVK is what makes Vulkan work on macOS - it is a Vulkan implementation
# layered over Metal, not a driver. SDL_Vulkan_LoadLibrary finds it if the
# Vulkan SDK is installed; if it is not, the renderer builds fine and then fails
# at runtime with nothing to load, so decide here rather than there.
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

if ! command -v sdl2-config >/dev/null 2>&1 \
   && [ ! -d /Library/Frameworks/SDL2.framework ]; then
    echo "build-macos.sh: no SDL2." >&2
    echo "  brew install sdl2" >&2
    exit 1
fi

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

REV=$(git rev-parse --short HEAD)
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    REV="$REV-dirty"
fi

B=build/release-darwin-$ARCH

# Same two gates package-release.sh runs. A Mac build is still a build: a config
# that leaks a cvar or an unclassified empty function is exactly as wrong here.
python3 content/serverconfigs/check-configs.py
python3 tools/stub-report.py

echo "building $REV for macos-$ARCH (vulkan=$VULKAN)"

# Stamp the loaded pak01 with the revision - see package-release.sh for why both
# pak01 and iobin carry a stamp.
sed -i '' "s/@@PAKSTAMP@@/$REV/g" content/pak01/ui/main.menu
trap 'sed -i "" "s/pak01 $REV/pak01 @@PAKSTAMP@@/g" content/pak01/ui/main.menu' EXIT

make PLATFORM=darwin ARCH="$ARCH" BUILD_RENDERER_VULKAN="$VULKAN" -j"$JOBS"

MD="$OUT/pkg/quakelive-macos-$ARCH-$REV"
rm -rf "$MD"
mkdir -p "$MD/baseq3"

cp -p "$B/quakelive.$ARCH" "$B/quakelive_dedicated.$ARCH" "$B/opengl2$ARCH.dylib" "$MD/"
if [ -f "$B/vulkan$ARCH.dylib" ]; then cp -p "$B/vulkan$ARCH.dylib" "$MD/"; fi

# Keep the modules where they can be carried to the Linux box and folded into
# the shared pak. This directory is the handoff point in both directions.
rm -rf "$OUT/modules"; mkdir -p "$OUT/modules"
cp -p "$B"/baseq3/*.dylib "$OUT/modules/"

rm -rf "$OUT/stage"; mkdir -p "$OUT/stage"
cp -p "$B"/baseq3/*.dylib "$OUT/stage/"

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
cp -p "$B/baseq3/pak01.pk3" "$MD/baseq3/"
cp -p TRACKER.md "$MD/" 2>/dev/null || true
cp -p server.cfg.example "$MD/" 2>/dev/null || true
cp -p content/serverconfigs/*.cfg "$MD/baseq3/"

(
  cd "$MD"
  {
    echo "# ioquakelive $REV (macos-$ARCH)"
    echo "# sha256 of every file in this archive"
    echo "#"
    echo "# The game verifies the modules it extracts out of iobin.pk3 by itself."
    echo "# This is for checking the archive: shasum -a 256 -c checksums.txt"
    find . -type f ! -name checksums.txt -print0 | sort -z | xargs -0 shasum -a 256
  } > checksums.txt
)

mkdir -p "$OUT/out"
rm -f "$OUT/out/quakelive-macos-$ARCH-$REV.zip"
(cd "$OUT/pkg" && zip -q -r -9 "$OUT/out/quakelive-macos-$ARCH-$REV.zip" "quakelive-macos-$ARCH-$REV")
rm -rf "$OUT/stage"

echo
echo "iobin.pk3 contains:"
unzip -l "$MD/baseq3/iobin.pk3" | awk 'NR>3 && NF>3 {print "  " $4}' | grep -v '^  $' || true
if [ "$FOREIGN" = "0" ]; then
    echo
    echo "  ^ dylibs only. This build can host and play locally, but it will fail"
    echo "    sv_pure against a server running the released iobin.pk3, because that"
    echo "    pak has a different checksum. Drop the .so and .dll modules into"
    echo "    $EXTRA_MODULES/ and re-run to get a pak that serves all three."
fi

echo
echo "modules for the shared pak left in $OUT/modules/ - copy to content/modules/"
echo "on the Linux box and re-run package-release.sh."
echo
echo "Gatekeeper: the binaries are unsigned, so macOS quarantines them out of a"
echo "downloaded zip. After extracting:  xattr -dr com.apple.quarantine <folder>"
echo
ls -lh "$OUT/out/quakelive-macos-$ARCH-$REV.zip"
