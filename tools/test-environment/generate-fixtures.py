#!/usr/bin/env python3
"""Generate authored headless BSP/AAS/bot fixtures, with no game-data downloads.

This deliberately small collision arena is not a production map/compiler.
Its three convex navigation cells exercise real AAS walking and bot combat.
Binary layouts come from this repository's qfiles.h and aasfile.h.
"""
import argparse
import ctypes
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[2]


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def lump_file(magic, version, lumps, checksum=None):
    header = magic + pack("i", version)
    if checksum is not None:
        header += pack("I", checksum)
    offset = len(header) + 8 * len(lumps)
    data = b""
    for blob in lumps:
        header += pack("ii", offset, len(blob))
        data += blob
        padding = (-len(blob)) % 4
        data += b"\0" * padding
        offset += len(blob) + padding
    return header + data


def bsp():
    lumps = [b""] * 18
    entities = [{"classname": "worldspawn", "message": "Authored bot runtime fixture"}]
    for x in (-320, 320):
        for y in (-120, 120):
            entities.append({"classname": "info_player_deathmatch", "origin": f"{x} {y} 48",
                             "angle": "0" if x < 0 else "180"})
    for x in (-380, 0, 380):
        entities.append({"classname": "item_health", "origin": f"{x} 160 24"})
        entities.append({"classname": "weapon_machinegun", "origin": f"{x} -160 24"})
    for team, x in (("red", -360), ("blue", 360)):
        entities.append({"classname": f"team_CTF_{team}flag", "origin": f"{x} 0 24"})
        for y in (-120, 120):
            for suffix in ("spawn", "player"):
                entities.append({"classname": f"team_CTF_{team}{suffix}", "origin": f"{x} {y} 48",
                                 "angle": "0" if x < 0 else "180"})
    lumps[0] = ("\n".join("{\n" + "\n".join(f'"{k}" "{v}"' for k, v in ent.items()) + "\n}"
                               for ent in entities) + "\n\0").encode()
    lumps[1] = pack("64sii", b"textures/runtime/solid", 0, 1 | 0x10000)
    planes, sides, brushes = [], [], []

    def plane(normal, dist):
        number = len(planes)
        planes.extend((pack("4f", *normal, dist), pack("4f", *[-n for n in normal], -dist)))
        return number

    split = plane((1, 0, 0), 0)
    boxes = [((-512, -256, -16), (512, 256, 0)), ((-512, -256, 160), (512, 256, 176)),
             ((-512, -256, 0), (-496, 256, 160)), ((496, -256, 0), (512, 256, 160)),
             ((-496, -256, 0), (496, -240, 160)), ((-496, 240, 0), (496, 256, 160))]
    for low, high in boxes:
        first = len(sides)
        for axis in range(3):
            normal = [0, 0, 0]; normal[axis] = -1
            sides.append(pack("ii", plane(normal, -low[axis]), 0))
            normal[axis] = 1
            sides.append(pack("ii", plane(normal, high[axis]), 0))
        brushes.append(pack("iii", first, 6, 0))
    lumps[2] = b"".join(planes)
    lumps[3] = pack("9i", split, -1, -2, -512, -256, -16, 512, 256, 176)
    leaf = pack("12i", 0, 0, -512, -256, -16, 512, 256, 176, 0, 0, 0, len(brushes))
    lumps[4] = leaf * 2
    lumps[6] = pack("6i", *range(6))
    lumps[7] = pack("6f4i", -512, -256, -16, 512, 256, 176, 0, 0, 0, 6)
    lumps[8] = b"".join(brushes)
    lumps[9] = b"".join(sides)
    lumps[16] = pack("ii", 1, 1) + b"\xff"
    return lump_file(b"IBSP", 47, lumps)


def aas(checksum):
    lumps = [b""] * 14
    lumps[0] = pack("ii6f", 2, 0, -15, -15, -24, 15, 15, 32) + pack("ii6f", 4, 0, -15, -15, -24, 15, 15, 16)
    vertices, planes, edges, edgeindex, faces, faceindex = [], [], [pack("ii", 0, 0)], [], [[0] * 6], []
    areas = [pack("3i9f", *([0] * 12))]
    settings = [pack("7i", *([0] * 7))]
    shared = {}

    def plane(normal, dist):
        number = len(planes)
        axis = next(i for i, v in enumerate(normal) if v)
        planes.extend((pack("4fi", *normal, dist, axis), pack("4fi", *[-n for n in normal], -dist, axis)))
        return number

    boundaries = (-481, -128, 128, 481)
    for number in range(1, 4):
        low, high = (boundaries[number - 1], -225, 24), (boundaries[number], 225, 128)
        first = len(faceindex)
        for axis in range(3):
            for side in (0, 1):
                normal = [0, 0, 0]; normal[axis] = 1 if side == 0 else -1
                u, v = (axis + 1) % 3, (axis + 2) % 3
                if side: u, v = v, u
                corners = []
                for a, b in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    point = list(low)
                    point[axis] = high[axis] if side else low[axis]
                    point[u] = high[u] if a else low[u]
                    point[v] = high[v] if b else low[v]
                    corners.append(tuple(point))
                key = tuple(sorted(corners))
                if key in shared:
                    face = shared[key]
                    faces[face][1] = 0
                    faces[face][5] = number
                    faceindex.append(-face)
                    continue
                face = len(faces); shared[key] = face
                pnum = plane(normal, sum(a * b for a, b in zip(normal, corners[0])))
                firstedge = len(edgeindex)
                ids = []
                for point in corners:
                    ids.append(len(vertices)); vertices.append(pack("3f", *point))
                for i in range(4):
                    edgeindex.append(len(edges)); edges.append(pack("ii", ids[i], ids[(i + 1) % 4]))
                flags = 5 if axis == 2 and side == 0 else 1
                faces.append([pnum, flags, 4, firstedge, number, 0])
                faceindex.append(face)
        center = [(a + b) / 2 for a, b in zip(low, high)]
        areas.append(pack("3i9f", number, 6, first, *low, *high, *center))
        settings.append(pack("7i", 0, 1, 2 | 4, 1, number - 1, 2 if number == 2 else 1, (1, 2, 4)[number - 1]))
    reach = [pack("3i6fiH2x", *([0] * 11))]
    for src, dst, boundary in ((1, 2, -128), (2, 1, -128), (2, 3, 128), (3, 2, 128)):
        sign = 1 if dst > src else -1
        face = next(i for i, f in enumerate(faces) if {f[4], f[5]} == {src, dst})
        reach.append(pack("3i6fiH2x", dst, face, 0, boundary - sign * 8, 0, 24,
                          boundary + sign * 8, 0, 24, 2, 15))
    nodes = [pack("3i", 0, 0, 0)]
    # Six exterior bounds, then two x splits: positive leaves are solid 0.
    checks = [((1, 0, 0), -481, True), ((1, 0, 0), 481, False),
              ((0, 1, 0), -225, True), ((0, 1, 0), 225, False),
              ((0, 0, 1), 24, True), ((0, 0, 1), 128, False)]
    for i, (normal, dist, front_inside) in enumerate(checks, 1):
        children = (i + 1, 0) if front_inside else (0, i + 1)
        nodes.append(pack("3i", plane(normal, dist), *children))
    nodes.append(pack("3i", plane((1, 0, 0), -128), 8, -1))
    nodes.append(pack("3i", plane((1, 0, 0), 128), -3, -2))
    lumps[1] = b"".join(vertices); lumps[2] = b"".join(planes); lumps[3] = b"".join(edges)
    lumps[4] = pack(f"{len(edgeindex)}i", *edgeindex)
    lumps[5] = b"".join(pack("6i", *f) for f in faces)
    lumps[6] = pack(f"{len(faceindex)}i", *faceindex)
    lumps[7] = b"".join(areas); lumps[8] = b"".join(settings); lumps[9] = b"".join(reach)
    lumps[10] = b"".join(nodes)
    lumps[11] = pack("5i", *([0] * 5)); lumps[12] = pack("i", 0)
    lumps[13] = pack("8i", 0, 0, 0, 0, 3, 3, 0, 0)
    return lump_file(b"EAAS", 4, lumps, checksum)


def botfiles():
    strings = {0: '"Fixture"', 1: '"it"', 3: '"fixture_w.c"', 21: '"fixture_chat.c"',
               22: '"Fixture"', 40: '"fixture_i.c"'}
    special = {2: "0.8", 5: "180.0", 6: "0.1", 7: "0.95", 8: "0.95", 16: "0.9", 23: "1200",
               36: "0.0", 37: "0.0", 38: "0.0", 39: "0.0", 41: "1.0", 44: "0.0", 46: "1.0", 47: "1.0", 48: "0.0"}
    character = "\n".join("skill %d {\n%s\n}" % (skill, "\n".join(f"{i} {strings.get(i, special.get(i, '0.0' if 24 <= i <= 35 else '0.5'))}" for i in range(49))) for skill in (1, 4, 5))
    return {
        "botfiles/bots/fixture_c.c": character,
        "botfiles/bots/default_c.c": character,
        "botfiles/fixture_chat.c": 'chat "Fixture" { }\n',
        "botfiles/syn.c": "// authored empty synonyms\n",
        "botfiles/rnd.c": "// authored empty random chat\n",
        "botfiles/match.c": "// authored empty chat match templates\n",
        "botfiles/rchat.c": "// authored empty reply chat\n",
        "botfiles/weapons.c": '''projectileinfo { name "hitscan" damage 7 damagetype 1 }
weaponinfo { number 1 name "Gauntlet" weaponindex 4 projectile "hitscan" numprojectiles 1 speed 0 ammoindex 0 ammoamount 0 reload 0.4 }
weaponinfo { number 2 name "Machinegun" weaponindex 6 projectile "hitscan" numprojectiles 1 speed 0 ammoindex 19 ammoamount 1 reload 0.1 }
''',
        "botfiles/fixture_w.c": '''weight "Gauntlet" { switch (4) { case 0: return 0; default: return 10; } }
weight "Machinegun" { switch (6) { case 0: return 0; default: switch (19) { case 0: return 0; default: return 100; } } }
''',
        "botfiles/items.c": '''iteminfo "item_health" { name "Health" modelindex 6 type 4 index 0 respawntime 35 mins {-15, -15, -15} maxs {15, 15, 15} }
iteminfo "weapon_machinegun" { name "Machinegun" modelindex 11 type 1 index 2 respawntime 5 mins {-15, -15, -15} maxs {15, 15, 15} }
iteminfo "team_CTF_redflag" { name "Red flag" modelindex 35 type 8 index 0 respawntime 0 mins {-15, -15, -15} maxs {15, 15, 15} }
iteminfo "team_CTF_blueflag" { name "Blue flag" modelindex 36 type 8 index 0 respawntime 0 mins {-15, -15, -15} maxs {15, 15, 15} }
''',
        "botfiles/fixture_i.c": '\n'.join(f'weight "{name}" {{ return 50; }}' for name in ("item_health", "weapon_machinegun", "team_CTF_redflag", "team_CTF_blueflag")),
        "scripts/bots.txt": '\n'.join('{ name "%s" model "visor/default" aifile "bots/fixture_c.c" }' % name for name in ("FixtureA", "FixtureB", "FixtureC", "FixtureD")),
        "scripts/arenas.txt": '{ map "ql_fixture" longname "Authored runtime fixture" type "ffa team ctf ca ad ft rr" }\n',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    data = bsp()
    with tempfile.TemporaryDirectory() as directory:
        library = Path(directory) / "checksum.so"
        subprocess.run(["cc", "-shared", "-fPIC", '-DARCH_STRING="x86_64"',
                        str(REPO / "code/qcommon/md4.c"), "-o", str(library)], check=True)
        checksum_fn = ctypes.CDLL(str(library)).Com_BlockChecksum
        checksum_fn.argtypes = [ctypes.c_void_p, ctypes.c_int]; checksum_fn.restype = ctypes.c_uint
        checksum = checksum_fn(data, len(data))
    files = botfiles()
    files["maps/ql_fixture.bsp"] = data
    files["maps/ql_fixture.aas"] = aas(checksum)
    files["FIXTURE-NOTICE.txt"] = "Authored headless test fixtures; not original Quake Live assets.\n"
    with zipfile.ZipFile(args.destination / "zz_runtime_fixture.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in files.items(): archive.writestr(name, content)
    with zipfile.ZipFile(args.destination / "pak00.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("default.cfg", "// Authored dedicated-server fixture defaults, not the game's original assets.\nset sv_fps 40\n")
        archive.writestr("FIXTURE-NOTICE.txt", "Authored startup stub only; not original Quake Live pak00.\n")
    (args.destination / "fixture-manifest.json").write_text(json.dumps({"map": "ql_fixture", "bsp_checksum": checksum,
                                                                       "navigation_cells": 3, "source": "authored fixtures"}, indent=2) + "\n")
    print(f"Generated authored BSP/AAS/bot fixtures in {args.destination}")


if __name__ == "__main__":
    main()
