#!/bin/sh
# [QL] E132. Builds the local bot harness: a CTF test map with bot navigation
# data, a pak holding it and a set of botfiles, and a server tree to run it in.
# Nothing it produces is committed - it all lands in $H (default ./harness).
#
#   build-harness.sh <release.zip> <botfiles dir> <default.cfg>
#
# release.zip   quakelive-linux-x86_64-<sha>.zip from package-release.sh
# botfiles dir  a botfiles/ tree with a scripts/bots.txt beside it:
#                 - Quake Live's own, extracted from YOUR pak00.pk3 (never
#                   commit or ship them), which is what matches the real game;
#                 - or OpenArena's (GPLv2+), e.g. from github.com/everettcaleb/
#                   openarena. Its inv.h numbers items the Quake 3 way; ours
#                   are one higher from index 4 (Quake Live's item_armor_jacket)
#                   and fix-inv.py rewrites them. Without that the flags swap
#                   and one team never attacks - see TRACKER E132.
# default.cfg   Quake Live's, from pak00. The dedicated server refuses to start
#               without a pak00.pk3; this makes a local stub holding only it.
#
# A real map instead of the generated one: put its .bsp and the .aas the game
# ships for it in $H/pk/maps before the zip step (or add them to the pak after),
# and pass MAP=<name> to run-match.sh. A shipped AAS is what the bots should
# use; bspc on a Quake Live BSP builds an incomplete one.
#
# Tools (built once, anywhere):
#   map compiler  github.com/id-tech-3-tools/map-compiler  (cmake)  -> $MAPCOMPILER
#   bspc          github.com/TTimo/bspc                     (make)   -> $BSPC
set -e
zip_in=$1; botfiles=$2; defcfg=$3
[ -f "$zip_in" ] && [ -d "$botfiles" ] && [ -f "$defcfg" ] || { sed -n 2,27p "$0"; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
H=${H:-$(pwd)/harness}
MAPCOMPILER=${MAPCOMPILER:-mapcompiler}
BSPC=${BSPC:-bspc}
rm -rf "$H/fs" "$H/pk" "$H/srv"
mkdir -p "$H/fs/baseq3/maps" "$H/pk/maps" "$H/pk/scripts" "$H/srv"

python3 "$here/gen-ctf-map.py" "$H/fs/baseq3/maps/qlbot_ctf.map"
# No -meta: bspc matches brush sides to drawn surfaces, and after -meta's
# triangulation it matches none and builds an empty AAS ("leaked").
"$MAPCOMPILER" -game quakelive -fs_basepath "$H/fs" "$H/fs/baseq3/maps/qlbot_ctf.map"
"$BSPC" -bsp2aas "$H/fs/baseq3/maps/qlbot_ctf.bsp"
cp "$H/fs/baseq3/maps/qlbot_ctf.bsp" "$H/fs/baseq3/maps/qlbot_ctf.aas" "$H/pk/maps/"

cp -r "$botfiles" "$H/pk/botfiles"
cp "$botfiles/../scripts/bots.txt" "$H/pk/scripts/"
python3 "$here/fix-inv.py" "$H/pk/botfiles/inv.h"
( cd "$H/pk" && zip -qr ../zz_botharness.pk3 . )

( cd "$H/srv" && unzip -q "$zip_in" && mv quakelive-linux-x86_64-* ql )
cp "$H/zz_botharness.pk3" "$H/srv/ql/baseq3/"
mkdir -p "$H/stub" && cp "$defcfg" "$H/stub/default.cfg"
( cd "$H/stub" && zip -q "$H/srv/ql/baseq3/pak00.pk3" default.cfg )
echo "harness ready in $H - now: H=$H $here/run-match.sh baseline 900"
