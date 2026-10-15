#!/usr/bin/env python3
"""
[QL] E132. Generates qlbot_ctf.map: a small symmetric CTF map for headless bot
tests (tools/bot-harness/README in build-harness.sh's header).

    gen-ctf-map.py out.map          red base west, blue base east
    gen-ctf-map.py out.map swap     the same map with the bases swapped

Swap exists because it is the test that separates "the map favours one side"
from "the code favours one team": if a lopsided result follows the team when
the bases swap, it is code. That is how E132 found the item-number mismatch.

Layout (top view, x across, y up):
  red base  x -1600..-1024 | three lanes through x -1024..1024 | blue base
  lanes: north y 224..560, centre y -96..96, south y -560..-224, divided by
  two long walls that stop short of the middle (x -256..256) so lanes cross.
"""
import sys

T = "qltest/grey"


def brush(x0, y0, z0, x1, y1, z1, tex=T):
    # six axial planes, q3 "( p1 ) ( p2 ) ( p3 ) tex ..." winding = outward normal
    f = []
    def face(p1, p2, p3):
        f.append("( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 0.5 0.5 0 0 0" % (p1 + p2 + p3 + (tex,)))
    face((x0, 0, 0), (x0, 1, 0), (x0, 0, 1))      # -x
    face((x1, 0, 0), (x1, 0, 1), (x1, 1, 0))      # +x
    face((0, y0, 0), (0, y0, 1), (1, y0, 0))      # -y
    face((0, y1, 0), (1, y1, 0), (0, y1, 1))      # +y
    face((0, 0, z0), (1, 0, z0), (0, 1, z0))      # -z
    face((0, 0, z1), (0, 1, z1), (1, 0, z1))      # +z
    return "{\n" + "\n".join(f) + "\n}\n"


X, Y, Z, W = 1600, 600, 320, 16
b = []
# shell
b.append(brush(-X - W, -Y - W, -W, X + W, Y + W, 0))          # floor
b.append(brush(-X - W, -Y - W, Z, X + W, Y + W, Z + W))      # ceiling
b.append(brush(-X - W, -Y - W, 0, -X, Y + W, Z))             # west
b.append(brush(X, -Y - W, 0, X + W, Y + W, Z))               # east
b.append(brush(-X, -Y - W, 0, X, -Y, Z))                     # south
b.append(brush(-X, Y, 0, X, Y + W, Z))                       # north
# base walls at |x| = 1024 with three doorways (the lane mouths)
for sx in (-1, 1):
    x0, x1 = sorted((sx * 1024, sx * 1024 + sx * W))
    for ya, yb in ((-Y, -448), (-336, -64), (64, 336), (448, Y)):
        b.append(brush(x0, ya, 0, x1, yb, Z))
# lane dividers, broken in the middle so the lanes cross
for sx in (-1, 1):
    xa, xb = sorted((sx * 256, sx * 1024))
    for ya, yb in ((96, 224), (-224, -96)):
        b.append(brush(xa, ya, 0, xb, yb, Z))
# cover in the middle and a low step in each base (a jump the AAS must know)
b.append(brush(-64, -64, 0, 64, 64, 96))
for sx in (-1, 1):
    xa, xb = sorted((sx * 1200, sx * 1260))
    b.append(brush(xa, 200, 0, xb, 400, 40))
    b.append(brush(xa, -400, 0, xb, -200, 40))

ents = []
def ent(cls, x, y, z, **kv):
    s = '{\n"classname" "%s"\n"origin" "%d %d %d"\n' % (cls, x, y, z)
    for k, v in kv.items():
        s += '"%s" "%s"\n' % (k, v)
    ents.append(s + "}\n")

SWAP = len(sys.argv) > 2
for sx, team in (((1, "red"), (-1, "blue")) if SWAP else ((-1, "red"), (1, "blue"))):
    face = 0 if sx < 0 else 180
    ent("team_CTF_%sflag" % team, sx * 1450, 0, 24)
    for i, (dx, dy) in enumerate(((1300, 300), (1300, -300), (1500, 450), (1500, -450),
                                  (1150, 120), (1150, -120), (1550, 250), (1550, -250))):
        ent("team_CTF_%sspawn" % team, sx * dx, dy, 40, angle=face)
    for dx, dy in ((1250, 200), (1250, -200)):
        ent("team_CTF_%splayer" % team, sx * dx, dy, 40, angle=face)
    ent("weapon_shotgun", sx * 1150, 0, 24)
    ent("item_health", sx * 1450, 400, 24)
    ent("item_health", sx * 1450, -400, 24)
    ent("weapon_railgun", sx * 640, 392, 24)
    ent("weapon_lightning", sx * 640, -392, 24)
    ent("weapon_plasmagun", sx * 640, 0, 24)
    ent("ammo_rockets", sx * 400, 300, 24)
    ent("item_armor_shard", sx * 400, -300, 24)
ent("weapon_rocketlauncher", 0, 300, 24)
ent("item_armor_combat", 0, -300, 24)
ent("item_health_large", 0, 0, 120)
ent("info_player_deathmatch", -1300, 0, 40)
ent("info_player_deathmatch", 1300, 0, 40, angle=180)
for x in range(-1400, 1401, 400):
    for y in (-400, 0, 400):
        ent("light", x, y, 280, light=300)

out = ('{\n"classname" "worldspawn"\n"message" "QL bot CTF test"\n"_ambient" "40"\n'
       + "".join(b) + "}\n" + "".join(ents))
open(sys.argv[1], "w").write(out)
