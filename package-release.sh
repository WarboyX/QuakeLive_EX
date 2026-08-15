#!/bin/sh
# package-release.sh - build both platforms and package them, named by commit.
#
# Every archive carries the short commit hash in both the zip name and the
# folder inside it, so two builds can be downloaded and extracted side by side
# without one silently overwriting the other. Appends -dirty when the tree has
# uncommitted changes, because a build from an uncommitted tree cannot be
# reproduced from the hash alone and should not claim it can.

set -e

OUT=${OUT:-release}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

# Resolve OUT to an absolute path once: the zip calls below run from inside the
# staging directories, so a relative OUT would be interpreted against those.
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

REV=$(git rev-parse --short HEAD)
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    REV="$REV-dirty"
fi

L=build/release-linux-x86_64
W=build/release-mingw32-x86_64

# A mode config only sets what it cares about, so anything it does not mention
# survives from whatever ran before it. common.cfg resets those; this fails the
# build if a config sets something that reset block does not cover.
python3 content/serverconfigs/check-configs.py

echo "building $REV"

# Stamp the loaded pak01 with the revision. Menu fixes live in pak01.pk3 and
# module fixes in iobin.pk3, and replacing only one of the two has repeatedly
# made a fixed bug look unfixed. The main menu now shows which pak01 is
# actually loaded, so a screenshot settles it.
sed -i "s/@@PAKSTAMP@@/$REV/g" content/pak01/ui/main.menu
trap 'sed -i "s/pak01 $REV/pak01 @@PAKSTAMP@@/g" content/pak01/ui/main.menu' EXIT
# BUILD_RENDERER_VULKAN is off in the Makefile's default so an ordinary build
# cannot be affected by the port. It is on here because a renderer nobody can
# select is a renderer nobody can test - and it is genuinely inert until
# cl_renderer says otherwise: opengl2 is byte-identical either way.
make BUILD_RENDERER_VULKAN=1 -j"$JOBS"
make PLATFORM=mingw32 ARCH=x86_64 BUILD_RENDERER_VULKAN=1 -j"$JOBS"

rm -rf "$OUT/pkg"
mkdir -p "$OUT/pkg" "$OUT/stage"

LD="$OUT/pkg/quakelive-linux-x86_64-$REV"
WD="$OUT/pkg/quakelive-windows-x64-$REV"
mkdir -p "$LD/baseq3" "$WD/baseq3"

cp -p $L/quakelive.x86_64 $L/quakelive_dedicated.x86_64 $L/opengl2x86_64.so "$LD/"
cp -p $W/quakelive.x86_64.exe $W/quakelive_dedicated.x86_64.exe $W/opengl2x86_64.dll $W/SDL2.dll "$WD/"

# The Vulkan renderer sits next to the OpenGL one and is reached with
# "cl_renderer vulkan" + "vid_restart". Nothing loads it otherwise. Copied
# only if it was built, so the package still forms with the target off.
# ("if" rather than "[ ... ] && cp": set -e would take the false test as a
# failed command and abort the whole package.)
if [ -f $L/vulkanx86_64.so ]; then cp -p $L/vulkanx86_64.so "$LD/"; fi
if [ -f $W/vulkanx86_64.dll ]; then cp -p $W/vulkanx86_64.dll "$WD/"; fi

# iobin.pk3 holds every platform's game modules, so one pak serves a linux
# server and its windows clients and both pass the same sv_pure checksum.
rm -rf "$OUT/stage"; mkdir -p "$OUT/stage"
cp $L/baseq3/*.so $W/baseq3/*.dll "$OUT/stage/"
(cd "$OUT/stage" && zip -q -9 "$LD/baseq3/iobin.pk3" *.so *.dll)
cp -p "$LD/baseq3/iobin.pk3" "$WD/baseq3/iobin.pk3"
cp -p $L/baseq3/pak01.pk3 "$LD/baseq3/"
cp -p $L/baseq3/pak01.pk3 "$WD/baseq3/"
cp -p TRACKER.md "$LD/" 2>/dev/null || true
cp -p TRACKER.md "$WD/" 2>/dev/null || true

# The dedicated server otherwise ships with no configuration and no hint that
# sv_master needs setting before the Internet browser tab can work.
cp -p server.cfg.example "$LD/" 2>/dev/null || true
cp -p server.cfg.example "$WD/" 2>/dev/null || true

# Ready-to-run server configs land in baseq3/ so "+exec ffa.cfg" works without
# the admin having to move anything first.
cp -p content/serverconfigs/*.cfg "$LD/baseq3/"
cp -p content/serverconfigs/*.cfg "$WD/baseq3/"

mkdir -p "$OUT/out"
rm -f "$OUT/out"/*-"$REV".zip
(cd "$OUT/pkg" && zip -q -r -9 "$OUT/out/quakelive-linux-x86_64-$REV.zip" "quakelive-linux-x86_64-$REV")
(cd "$OUT/pkg" && zip -q -r -9 "$OUT/out/quakelive-windows-x64-$REV.zip" "quakelive-windows-x64-$REV")

rm -rf "$OUT/stage"
ls -lh "$OUT/out"/*-"$REV".zip
