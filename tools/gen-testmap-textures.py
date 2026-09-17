#!/usr/bin/env python3
"""
[QL] R25. Generates the test textures for content/testmaps/. Ours, committed,
shipped in the test pk3 - nothing here touches pak00.

    python3 tools/gen-testmap-textures.py

The dome sheet is the important one. A hemisphere's normal map is unambiguous in
both axes at once, which makes it the only asset here that can fail a bad
tangent basis in a way you can see rather than measure:

  * domes that read as CRATERS  -> the green channel convention is inverted
    (OpenGL +Y up vs DirectX +Y down). Nothing to do with handedness.
  * domes lit on the side AWAY from the light -> the tangent is mirrored.

Those are two different faults with two different fixes, and a sawtooth or a
photograph of brick cannot tell them apart. The diffuse that goes with it is
flat grey on purpose: any shading you see is the normal map and nothing else.
"""
import os, struct, math, random

W = H = 256
OUT = os.path.join(os.path.dirname(__file__), "..", "content", "testmaps", "textures")


def write_tga(name, rows):
    """
    24-bit uncompressed TGA. Two conversions, and getting either wrong fails
    quietly in a way that looks like a shader bug.

    Bottom-up, so the rows are written reversed.

    And BGR, not RGB - tr_image_tga.c reads blue, green, red in that order for
    a 24-bit file. Producers here work in RGB and the swap happens once, here.
    On a diffuse this merely turns red brick blue, which at least announces
    itself. On a NORMAL MAP it swaps x with z, so a flat (128,128,255) decodes
    to (1,0,0) - a normal lying flat in the tangent plane with no z at all - and
    every authored map is silently meaningless. That shipped once.
    """
    # 3 bytes a texel is BGR, 4 is BGRA - and alpha is not decoration here, it
    # is the height field the parallax march reads. A normal map without it
    # cannot self-occlude, which is the difference between shading that looks
    # painted on and detail that hides what is behind it.
    n = len(rows[0]) // W
    data = b"".join(bytes(r[(i // n) * n + (2 - i % n if i % n < 3 else 3)]
                          for i in range(len(r)))
                    for r in reversed(rows))
    hdr = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, W, H, 8 * n,
                      8 if n == 4 else 0)
    path = os.path.join(OUT, name)
    open(path, "wb").write(hdr + data)
    print("  %-18s %7d bytes" % (name, len(hdr) + len(data)))


def encode(nx, ny, nz, h=None):
    """Unit normal to RGB, and the height to A when one is given. Rounds -
    truncating puts 127 where 128 belongs and tilts every flat texel toward
    -u/-v, which is a bias over the whole sheet."""
    m = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    out = [
        max(0, min(255, int((nx / m * 0.5 + 0.5) * 255.0 + 0.5))),
        max(0, min(255, int((ny / m * 0.5 + 0.5) * 255.0 + 0.5))),
        max(0, min(255, int((nz / m * 0.5 + 0.5) * 255.0 + 0.5))),
    ]
    if h is not None:
        out.append(max(0, min(255, int(h * 255.0 + 0.5))))
    return bytes(out)


def domes(cell=64, radius=26):
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            # offset from this cell's centre
            dx = (x % cell) - cell / 2.0 + 0.5
            dy = (y % cell) - cell / 2.0 + 0.5
            r2 = dx * dx + dy * dy
            if r2 >= radius * radius:
                row += encode(0.0, 0.0, 1.0, 0.0)     # flat between the domes
            else:
                nx = dx / radius
                ny = dy / radius
                # OpenGL convention: +Y is up in texture space, and v runs down,
                # so the y component is negated relative to the pixel axis.
                hz = math.sqrt(max(0.0, 1.0 - r2 / (radius * radius)))
                row += encode(nx, -ny, hz, hz)
        rows.append(bytes(row))
    return rows


def rocknoise(octaves=4, seed=1337):
    """Value-noise height field differentiated into normals - the realistic case,
    where the dome sheet is the diagnostic one."""
    rnd = random.Random(seed)
    height = [[0.0] * W for _ in range(H)]
    amp, freq = 1.0, 4
    for _ in range(octaves):
        grid = [[rnd.random() for _ in range(freq + 1)] for _ in range(freq + 1)]
        for y in range(H):
            gy = y / H * freq
            y0 = int(gy); fy = gy - y0
            fy = fy * fy * (3 - 2 * fy)
            for x in range(W):
                gx = x / W * freq
                x0 = int(gx); fx = gx - x0
                fx = fx * fx * (3 - 2 * fx)
                a = grid[y0][x0] + (grid[y0][x0 + 1] - grid[y0][x0]) * fx
                b = grid[y0 + 1][x0] + (grid[y0 + 1][x0 + 1] - grid[y0 + 1][x0]) * fx
                height[y][x] += (a + (b - a) * fy) * amp
        amp *= 0.5
        freq *= 2
    scale = 24.0
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            # wrap, so the sheet tiles
            hx = height[y][(x + 1) % W] - height[y][(x - 1) % W]
            hy = height[(y + 1) % H][x] - height[(y - 1) % H][x]
            row += encode(-hx * scale, hy * scale, 1.0)
        rows.append(bytes(row))
    return rows


def flat(v=128):
    return [bytes((v, v, v)) * W for _ in range(H)]


# ---------------------------------------------------------------------------
# Materials: a diffuse and a normal map generated from ONE height field, so the
# two genuinely agree.
#
# That agreement is the whole argument for authoring maps over deriving them,
# and it is worth stating plainly. R20 derives normals from a texture's
# luminance, which assumes light and dark mean high and low. On Quake Live's art
# that assumption is weak, because its diffuse already has lighting painted into
# it - so the derived normals encode the light the artist painted, and then the
# engine lights them again. Here the albedo carries colour and dirt, the height
# field carries shape, and only the height field becomes the normal.
# ---------------------------------------------------------------------------

def _clampb(v):
    return max(0, min(255, int(v + 0.5)))


def normals_from_height(height, scale):
    """Central differences, wrapping, so the sheet tiles. Alpha carries the
    height itself, which is what the parallax march steps through."""
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            hx = height[y][(x + 1) % W] - height[y][(x - 1) % W]
            hy = height[(y + 1) % H][x] - height[(y - 1) % H][x]
            row += encode(-hx * scale, hy * scale, 1.0, height[y][x])
        rows.append(bytes(row))
    return rows


def diffuse_from(height, rgb_fn):
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            r, g, b = rgb_fn(x, y, height[y][x])
            row += bytes((_clampb(r), _clampb(g), _clampb(b)))
        rows.append(bytes(row))
    return rows


def fbm(seed, octaves=5, freq0=4):
    rnd = random.Random(seed)
    out = [[0.0] * W for _ in range(H)]
    amp, freq = 1.0, freq0
    for _ in range(octaves):
        g = [[rnd.random() for _ in range(freq + 1)] for _ in range(freq + 1)]
        for y in range(H):
            gy = y / H * freq; y0 = int(gy); fy = gy - y0
            fy = fy * fy * (3 - 2 * fy)
            for x in range(W):
                gx = x / W * freq; x0 = int(gx); fx = gx - x0
                fx = fx * fx * (3 - 2 * fx)
                a = g[y0][x0] + (g[y0][x0 + 1] - g[y0][x0]) * fx
                b = g[y0 + 1][x0] + (g[y0 + 1][x0 + 1] - g[y0 + 1][x0]) * fx
                out[y][x] += (a + (b - a) * fy) * amp
        amp *= 0.5; freq *= 2
    lo = min(map(min, out)); hi = max(map(max, out)); rng = (hi - lo) or 1.0
    return [[(v - lo) / rng for v in row] for row in out]


def brick(seed=7):
    """Running-bond brick. Bricks proud, mortar recessed, per-brick colour."""
    BH, BW, M = 32, 64, 5
    rnd = random.Random(seed)
    tint = {}
    height = [[0.0] * W for _ in range(H)]
    ids = [[None] * W for _ in range(H)]
    grain = fbm(seed + 1, octaves=4, freq0=16)
    for y in range(H):
        rowi = y // BH
        off = (rowi % 2) * (BW // 2)
        for x in range(W):
            bx = ((x + off) % W) // BW
            iny = y % BH
            inx = (x + off) % BW
            mortar = iny < M or inx < M
            ids[y][x] = None if mortar else (rowi, bx)
            if mortar:
                height[y][x] = 0.12 + grain[y][x] * 0.06
            else:
                # slight dome across each brick so it is not a flat slab
                fx = (inx - M) / float(BW - M) - 0.5
                fy = (iny - M) / float(BH - M) - 0.5
                height[y][x] = 0.78 - (fx * fx + fy * fy) * 0.25 + grain[y][x] * 0.14
    for k in set(v for row in ids for v in row if v):
        tint[k] = 0.80 + rnd.random() * 0.40

    def rgb(x, y, h):
        k = ids[y][x]
        if k is None:
            v = 96 + grain[y][x] * 40            # mortar: pale grey, no shading
            return (v, v * 0.98, v * 0.94)
        t = tint[k]
        base = (150 * t, 88 * t, 66 * t)         # albedo only
        d = 0.85 + grain[y][x] * 0.30            # dirt, not light
        return (base[0] * d, base[1] * d, base[2] * d)
    return height, rgb


def rock(seed=99):
    h = fbm(seed, octaves=6, freq0=3)
    h = [[v ** 1.4 for v in row] for row in h]   # sharpen the crevices
    detail = fbm(seed + 5, octaves=3, freq0=32)

    def rgb(x, y, hv):
        v = 96 + hv * 70 + detail[y][x] * 26
        return (v * 1.02, v * 0.99, v * 0.93)
    return h, rgb


def plate(seed=21):
    """Panel seams with bevels and corner rivets - the metal case."""
    P, SEAM, BEV, RIV = 128, 4, 7, 5
    grain = fbm(seed, octaves=4, freq0=24)
    height = [[0.0] * W for _ in range(H)]
    rivet = [[False] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            ix, iy = x % P, y % P
            dx = min(ix, P - 1 - ix); dy = min(iy, P - 1 - iy)
            d = min(dx, dy)
            if d < SEAM:
                height[y][x] = 0.10
            elif d < SEAM + BEV:
                height[y][x] = 0.10 + 0.72 * (d - SEAM) / float(BEV)
            else:
                height[y][x] = 0.82 + grain[y][x] * 0.05
            # rivets, inset from each corner
            for cx in (SEAM + BEV + 6, P - SEAM - BEV - 6):
                for cy in (SEAM + BEV + 6, P - SEAM - BEV - 6):
                    r2 = (ix - cx) ** 2 + (iy - cy) ** 2
                    if r2 < RIV * RIV:
                        height[y][x] = 0.82 + 0.30 * math.sqrt(1 - r2 / float(RIV * RIV))
                        rivet[y][x] = True

    def rgb(x, y, hv):
        v = 118 + grain[y][x] * 34
        if rivet[y][x]:
            v += 14
        return (v * 0.97, v, v * 1.05)
    return height, rgb


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("writing to content/testmaps/textures/")
    write_tga("domes_n.tga", domes())
    write_tga("rocknoise_n.tga", rocknoise())
    write_tga("flatgrey.tga", flat(128))

    for name, fn, scale in (("brick", brick, 90.0),
                            ("rock",  rock,  70.0),
                            ("plate", plate, 110.0)):
        h, rgb = fn()
        write_tga("%s_d.tga" % name, diffuse_from(h, rgb))
        write_tga("%s_n.tga" % name, normals_from_height(h, scale))
