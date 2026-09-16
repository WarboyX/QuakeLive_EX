#!/bin/sh
# [QL] Build content/testmaps/qltest_light.map into a loadable qltest_light.pk3 (named to match the map, so /map qltest_light is the pk3 name too).
#
# Output goes to content/testmaps/out/, which is gitignored - a compiled BSP is
# build output and does not belong in the tree, the same rule release/ follows.
#
# Needs the map compiler. It builds from source with everything vendored, so
# cmake and a C++ compiler are the whole toolchain:
#
#   git clone --depth 1 https://github.com/id-tech-3-tools/map-compiler
#   cmake -S map-compiler -B bld -DCMAKE_BUILD_TYPE=Release && cmake --build bld -j
#
# Point MAPCOMPILER at the binary, and optionally QLDIR at a Quake Live install:
#
#   MAPCOMPILER=./map-compiler/build/mapcompiler ./content/testmaps/build-testmap.sh
#   MAPCOMPILER=... QLDIR="$HOME/.steam/steam/steamapps/common/Quake Live" ./content/testmaps/build-testmap.sh
#
# QLDIR is optional. Everything the R20 test panels need is ours and shipped
# here; the only thing it buys is correct UV scale on the room and the rock
# block, which use Quake Live's ct_ragnarok/rock_03. Without it q3map2 cannot
# read that image's dimensions, falls back to 64x64, and those surfaces come out
# with the texture four times too large - one WARNING line and no other sign.
# The test panels are unaffected either way.
set -e

here=$(cd "$(dirname "$0")" && pwd)
out="$here/out"
MAPCOMPILER=${MAPCOMPILER:-mapcompiler}

command -v "$MAPCOMPILER" >/dev/null 2>&1 || [ -x "$MAPCOMPILER" ] || {
	echo "build-testmap: no map compiler. Set MAPCOMPILER=<path to mapcompiler>." >&2
	echo "See the header of this script for how to build one." >&2
	exit 1
}

# q3map2 wants a real game filesystem, so stage one. scripts/shaderlist.txt is
# not optional: q3map2 loads ONLY the .shader files named in it. Leave it out
# and the test shaders are never parsed, their images never resolve, and the two
# panels that use them come out at the wrong UV scale - reported as nothing more
# than "WARNING: Couldn't find image for shader textures/qltest/noperturb". The
# engine has no such list and loads every .shader it finds, so this is a
# compile-time requirement that does not show up at run time.
fs="$out/fs"
rm -rf "$fs"
mkdir -p "$fs/baseq3/maps" "$fs/baseq3/scripts" "$fs/baseq3/textures/qltest"
cp "$here/qltest_light.map" "$fs/baseq3/maps/"
cp "$here/qltest.shader" "$here/shaderlist.txt" "$fs/baseq3/scripts/"
cp "$here/textures/tangent.tga" "$fs/baseq3/textures/qltest/"

# -fs_basepath can be given more than once and the paths stack, so the staged
# tree supplies our files and the install (when there is one) supplies Quake
# Live's. Nothing is ever written into the install.
paths="-fs_basepath $fs"
[ -n "$QLDIR" ] && paths="$paths -fs_basepath $QLDIR"

bsp="$fs/baseq3/maps/qltest_light.bsp"

# -game quakelive is what makes this a version 47 BSP. It is a stock profile in
# the compiler (src/game_quakelive.h, "47 /* bsp file version */"), not a patch
# anyone has to apply. Our loader takes 46 as well - v47 is v46 plus
# LUMP_ADVERTISEMENTS, see qfiles.h - so a -game quake3 build also loads; 47 is
# simply the format the real maps are in, so it is what the test should be in.
#
# -keeplights goes on the BSP phase, NOT the light phase. It works by stamping
# "_keepLights" "1" into worldspawn, which the light phase reads back. Passed to
# -light it is accepted in silence and does nothing.
# shellcheck disable=SC2086  # $paths is a deliberate multi-flag expansion
"$MAPCOMPILER" -game quakelive $paths -meta -keeplights "$fs/baseq3/maps/qltest_light.map"
"$MAPCOMPILER" -game quakelive $paths -vis "$bsp"
"$MAPCOMPILER" -game quakelive $paths -light -fast "$bsp"

# The .bsp has to be inside a .pk3. A pure client only reads files that live in
# one and the server side does not go through that check, so a loose .bsp gives
# a server that loads the map and spawns into it and a renderer that says
# "RE_LoadWorldMap: couldn't load maps/qltest_light.bsp" - which reads like a
# corrupt BSP and is nothing of the kind.
pk3="$out/pk3"
rm -rf "$pk3"
mkdir -p "$pk3/maps" "$pk3/scripts" "$pk3/textures/qltest"
cp "$bsp" "$pk3/maps/"
cp "$here/qltest.shader" "$here/shaderlist.txt" "$pk3/scripts/"
cp "$here/textures/tangent.tga" "$pk3/textures/qltest/"
rm -f "$out/qltest_light.pk3"
( cd "$pk3" && zip -qr "$out/qltest_light.pk3" . )

echo
echo "wrote $out/qltest_light.pk3"
echo "drop it in baseq3/ next to pak00.pk3, then:  /map qltest_light"
