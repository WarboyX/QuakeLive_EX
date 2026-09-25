#!/bin/sh
# [QL] E132/E133. One headless CTF match, by default 4v4 on qlbot_ctf at timescale 8,
# and its flag counts. Set bot_debugMovement / bot_debugTrack for movement data.
#
#   run-match.sh <label> <game-seconds> [console line ...]
#   e.g. run-match.sh tac0 900 "set bot_tactics 0"
#
# Needs $H/srv/ql: an unpacked release plus the local harness pak - see
# build-harness.sh, which makes both. Environment:
#   H      harness directory (default: next to this script's parent, ./harness)
#   BUILD  a build tree to test instead of the release's own game module
#          (its qagame and dedicated server are swapped in)
#   BOTS   bot names, in join order (teams alternate; default eight). They join
#          one every 0.25 s: sixty in one burst overflow each other's reliable
#          command queues with connect messages and are dropped.
#   MAP    map to load (default qlbot_ctf)
#   CFG    server config to exec (default ctf.cfg; a2m-instagib-ctf.cfg allows
#          64 players - ctf.cfg caps them at 16, and a +set on the command line
#          loses to an exec'd config)
#   TS     timescale (default 8; 2 keeps up with sixty bots on four cores)
#   QLENV  extra environment for the server, e.g. a trace switch
#
# Game time is simulated, so the counts do not depend on machine speed - but
# a CPU too busy to keep up at timescale 8 plays fewer seconds than asked.
# Run two at a time on four cores, not more.
H=${H:-$(pwd)/harness}
[ -d "$H/srv/ql" ] || { echo "run-match: no $H/srv/ql - run build-harness.sh first"; exit 1; }
label=$1; secs=$2; shift 2
TS=${TS:-8}
B=${BUILD:-$(cd "$(dirname "$0")/../.." && pwd)/build/release-linux-x86_64}/baseq3
D=$H/run-$label
rm -rf "$D" && mkdir -p "$D/home"
# the current build's qagame in a private iobin copy
cp -r "$H/srv/ql" "$D/ql"
( cd "$B" && zip -q "$D/ql/baseq3/iobin.pk3" qagamex86_64.so )
[ -n "$BUILD" ] && cp "$BUILD/quakelive_dedicated.x86_64" "$D/ql/"
mkfifo "$D/in"
( sleep 100000 > "$D/in" & echo $! > "$D/hold" )
cd "$D/ql" && env $QLENV ./quakelive_dedicated.x86_64 +set fs_basepath "$D/ql" +set fs_homepath "$D/home" \
  +set dedicated 1 +exec ${CFG:-ctf.cfg} +set bot_enable 1 +set sv_pure 0 +set g_doWarmup 0 \
  +set timelimit 0 +set capturelimit 0 +set bot_nochat 1 +devmap ${MAP:-qlbot_ctf} < "$D/in" > "$D/log" 2>&1 &
echo $! > "$D/pid"
sleep 6
for l in "$@"; do echo "$l"; done > "$D/cmds"
timeout 5 sh -c "cat \"$D/cmds\" > \"$D/in\"" || { echo "$label: server died"; tail -3 "$D/log"; exit 1; }
# one bot every 0.25 s: sixty in one burst overflow each other's reliable
# command queues with connect messages and get dropped
for n in ${BOTS:-Angelyss Dark Murielle Nekoyss Tanisha Rai Arachna Forlorna}; do
  timeout 5 sh -c "echo 'addbot $n 4' > \"$D/in\"" || break
  sleep 0.25
done
sleep 4
timeout 5 sh -c "echo timescale $TS > \"$D/in\""
sleep $(( secs / TS + 2 ))
kill "$(cat "$D/pid")" "$(cat "$D/hold")" 2>/dev/null
L=$D/log
printf '%-14s grabs %3d  caps %3d  carrier-frags %3d  returns %3d  red-caps %d  blue-caps %d\n' "$label" \
  "$(grep -c 'got the' "$L")" "$(grep -c 'captured the' "$L")" "$(grep -c "flag carrier" "$L")" \
  "$(grep -c 'returned the' "$L")" "$(grep -c 'captured the BLUE' "$L")" "$(grep -c 'captured the RED' "$L")"
