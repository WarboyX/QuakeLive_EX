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

REV=$(git rev-parse --short HEAD)
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    REV="$REV-dirty"
fi

L=build/release-linux-x86_64
W=build/release-mingw32-x86_64

echo "building $REV"
make -j"$JOBS"
make PLATFORM=mingw32 ARCH=x86_64 -j"$JOBS"

rm -rf "$OUT/pkg"
mkdir -p "$OUT/pkg" "$OUT/stage"

LD="$OUT/pkg/quakelive-linux-x86_64-$REV"
WD="$OUT/pkg/quakelive-windows-x64-$REV"
mkdir -p "$LD/baseq3" "$WD/baseq3"

cp -p $L/quakelive.x86_64 $L/quakelive_dedicated.x86_64 $L/opengl2x86_64.so "$LD/"
cp -p $W/quakelive.x86_64.exe $W/quakelive_dedicated.x86_64.exe $W/opengl2x86_64.dll $W/SDL2.dll "$WD/"

# iobin.pk3 holds every platform's game modules, so one pak serves a linux
# server and its windows clients and both pass the same sv_pure checksum.
rm -rf "$OUT/stage"; mkdir -p "$OUT/stage"
cp $L/baseq3/*.so $W/baseq3/*.dll "$OUT/stage/"
(cd "$OUT/stage" && zip -q -9 "$OLDPWD/$LD/baseq3/iobin.pk3" *.so *.dll)
cp -p "$LD/baseq3/iobin.pk3" "$WD/baseq3/iobin.pk3"
cp -p $L/baseq3/pak01.pk3 "$LD/baseq3/"
cp -p $L/baseq3/pak01.pk3 "$WD/baseq3/"
cp -p TRACKER.md "$LD/" 2>/dev/null || true
cp -p TRACKER.md "$WD/" 2>/dev/null || true

mkdir -p "$OUT/out"
rm -f "$OUT/out"/*-"$REV".zip
(cd "$OUT/pkg" && zip -q -r -9 "$OLDPWD/$OUT/out/quakelive-linux-x86_64-$REV.zip" "quakelive-linux-x86_64-$REV")
(cd "$OUT/pkg" && zip -q -r -9 "$OLDPWD/$OUT/out/quakelive-windows-x64-$REV.zip" "quakelive-windows-x64-$REV")

rm -rf "$OUT/stage"
ls -lh "$OUT/out"/*-"$REV".zip
