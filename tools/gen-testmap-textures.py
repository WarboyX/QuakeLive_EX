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
                # Matches the height-field encoders, which is what makes this
                # panel comparable with every other sheet - measured, not
                # assumed: below a dome's centre both give a negative green.
                # Both were the inverted convention until the green-channel bug
                # was found, and the two agreeing is why it survived.
                hz = math.sqrt(max(0.0, 1.0 - r2 / (radius * radius)))
                row += encode(nx, ny, hz, hz)
        rows.append(bytes(row))
    return rows


def rocknoise(octaves=4, seed=1337):
    """Value-noise height field differentiated into normals - the realistic case,
    where the dome sheet is the diagnostic one."""
    rnd = random.Random(seed)
    height = [[0.0] * W for _ in range(H)]
    amp, freq = 1.0, 4
    for _ in range(octaves):
        grid = wrap_grid(rnd, freq)
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
            # -hy for the same reason as normals_from_height below
            row += encode(-hx * scale, -hy * scale, 1.0)
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
            # -hy, not +hy. For a height field h(u,v) the tangent-space normal
            # is (-dh/du, -dh/dv, 1); this had +dh/dv, which is green inverted.
            # It survived because every texture this generator makes shared the
            # error, so they all agreed with each other - and disagreed with
            # R_GenerateNormalMap, the C path that derives a normal map from a
            # diffuse at run time and does use -dL/dy. Two surfaces lit by the
            # same shader shaded opposite ways depending on which made them.
            row += encode(-hx * scale, -hy * scale, 1.0, height[y][x])
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


def wrap_grid(rnd, freq):
    """A (freq+1)^2 lattice whose last row and column REPEAT the first.

    This is the whole reason the noise tiles, and getting it wrong is invisible
    in the sheet and obvious on a wall. A lattice of freq+1 independent randoms
    interpolates fine everywhere in the middle and then, at x=W-1, is heading
    for a value that has nothing to do with x=0 - so every texture with grain
    over it has one vertical and one horizontal discontinuity, no matter how
    neatly its blocks or cells divide 256. That was the "line break between
    them" along the corridor wall: not the masonry, the grain on it.
    """
    g = [[rnd.random() for _ in range(freq)] for _ in range(freq)]
    for row in g:
        row.append(row[0])
    g.append(list(g[0]))
    return g


def fbm(seed, octaves=5, freq0=4):
    rnd = random.Random(seed)
    out = [[0.0] * W for _ in range(H)]
    amp, freq = 1.0, freq0
    for _ in range(octaves):
        g = wrap_grid(rnd, freq)
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


# ---------------------------------------------------------------------------
# Parallax showcases. Everything above was built to test whether the SHADING is
# right; these are built to make the DEPTH unmistakable, which wants the
# opposite properties:
#
#   deep, not subtle          parallax offset is proportional to height
#   vertical sides            a sloped wall self-occludes barely at all
#   a flat-ish albedo         so what you see is shape and not paint
#   a regular grid            so a wrong offset reads as a warp, not as noise
#
# And they are meant to be seen at a GRAZING angle, which is where parallax does
# its work - head on it is nearly a no-op by construction. A floor is the best
# showcase in any game, because you never see one any other way.
# ---------------------------------------------------------------------------

def waffle(cell=64, wall=12, seed=3):
    """Deep square wells with vertical sides - the strongest parallax case."""
    grain = fbm(seed, octaves=3, freq0=16)
    height = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            ix, iy = x % cell, y % cell
            d = min(min(ix, cell - 1 - ix), min(iy, cell - 1 - iy))
            # plateau at the rim, a short bevel, then floor - the bevel is two
            # texels so the sides read as vertical without aliasing to nothing
            if d < wall - 2:
                height[y][x] = 0.95 + grain[y][x] * 0.04
            elif d < wall:
                height[y][x] = 0.95 - 0.85 * (d - (wall - 2)) / 2.0
            else:
                height[y][x] = 0.10 + grain[y][x] * 0.05

    def rgb(x, y, h):
        v = 150 if h > 0.5 else 96          # rim lighter than the well, albedo only
        v += grain[y][x] * 26
        return (v, v * 0.98, v * 0.93)
    return height, rgb


def grooves(period=32, seed=11):
    """Deep parallel channels. Directional on purpose: walk along them and the
    parallax barely moves, walk across and it swims. If both look the same the
    offset is not following the view."""
    grain = fbm(seed, octaves=3, freq0=20)
    height = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            t = (x % period) / float(period)
            # square channel with a two-texel bevel each side
            if t < 0.12 or t > 0.88:
                height[y][x] = 0.08
            elif t < 0.20:
                height[y][x] = 0.08 + 0.84 * (t - 0.12) / 0.08
            elif t > 0.80:
                height[y][x] = 0.08 + 0.84 * (0.88 - t) / 0.08
            else:
                height[y][x] = 0.92 + grain[y][x] * 0.05

    def rgb(x, y, h):
        v = 128 + grain[y][x] * 30
        return (v * 0.95, v, v * 1.02)
    return height, rgb


def steps(n=8, seed=5):
    """A terrace of n equal steps. Quantitative: at the right parallax depth the
    risers line up with the treads and the staircase looks solid; too deep and it
    shears, too shallow and it flattens. Nothing else here can be read as a
    number rather than an impression."""
    grain = fbm(seed, octaves=2, freq0=12)
    height = [[0.0] * W for _ in range(H)]
    for y in range(H):
        k = int(y / float(H) * n)
        for x in range(W):
            height[y][x] = 0.06 + (k / float(n - 1)) * 0.88 + grain[y][x] * 0.02

    def rgb(x, y, h):
        k = int(y / float(H) * n)
        v = 104 + (k % 2) * 34              # alternating treads, so steps are countable
        return (v, v * 0.99, v * 0.95)
    return height, rgb


def studs(cell=64, radius=22, seed=9):
    """Hemispheres standing proud. The dome sheet's realistic cousin - it has a
    diffuse, so it answers 'does this look like relief' where domes_n answers
    'is the basis correct'."""
    grain = fbm(seed, octaves=3, freq0=18)
    height = [[0.0] * W for _ in range(H)]
    on = [[False] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            dx = (x % cell) - cell / 2.0 + 0.5
            dy = (y % cell) - cell / 2.0 + 0.5
            r2 = dx * dx + dy * dy
            if r2 < radius * radius:
                height[y][x] = 0.30 + 0.66 * math.sqrt(1.0 - r2 / (radius * radius))
                on[y][x] = True
            else:
                height[y][x] = 0.18 + grain[y][x] * 0.06

    def rgb(x, y, h):
        v = (150 if on[y][x] else 104) + grain[y][x] * 22
        return (v * 1.02, v, v * 0.94)
    return height, rgb


# ---------------------------------------------------------------------------
# Stone. The abstract sheets above prove the effect is there; these ask the
# question the whole line of work started from - does a low-polygon rock read as
# rock. They want deep, irregular relief with hard crevices, because that is
# what stone has and what a flat photograph of stone cannot imply.
# ---------------------------------------------------------------------------

def stoneblock(seed=31):
    """Irregular masonry with deeply recessed joints. Blocks vary in size, which
    is what separates stonework from brick and what makes the joints read as
    depth rather than as a grid."""
    grain = fbm(seed, octaves=5, freq0=20)
    face = fbm(seed + 3, octaves=4, freq0=8)
    rnd = random.Random(seed)

    def partition(total, choices):
        """Random sizes that sum to EXACTLY total, so the sheet tiles.

        Accumulating until you pass the edge and truncating is the obvious way
        and it is why a wall shows a hard line where the texture wraps: the last
        course is a different height from the first and the joint does not meet
        itself. Nothing in the texture says so - you only see it on a surface
        big enough to repeat."""
        out, acc = [], 0
        while total - acc > max(choices):
            c = rnd.choice(choices)
            out.append(c)
            acc += c
        out.append(total - acc)       # the remainder, kept whole
        return out

    heights = partition(H, (38, 46, 56, 64))
    rows_y, acc = [0], 0
    for h in heights:
        acc += h
        rows_y.append(acc)
    cuts = {}
    for i in range(len(rows_y) - 1):
        xs, acc = [0], 0
        for wdt in partition(W, (44, 58, 72, 90)):
            acc += wdt
            xs.append(acc)
        cuts[i] = xs
    JOINT = 6
    height = [[0.0] * W for _ in range(H)]
    ids = [[None] * W for _ in range(H)]
    for y in range(H):
        ri = 0
        while ri + 1 < len(rows_y) and rows_y[ri + 1] <= y:
            ri += 1
        dy = min(y - rows_y[ri], (rows_y[ri + 1] if ri + 1 < len(rows_y) else H) - 1 - y)
        xs = cuts.get(ri, [0, W])
        ci = 0
        for x in range(W):
            while ci + 1 < len(xs) - 1 and xs[ci + 1] <= x:
                ci += 1
            dx = min(x - xs[ci], (xs[ci + 1] if ci + 1 < len(xs) else W) - 1 - x)
            d = min(dx, dy)
            if d < JOINT - 2:
                height[y][x] = 0.06 + grain[y][x] * 0.05      # deep joint
            elif d < JOINT + 3:
                height[y][x] = 0.06 + 0.80 * (d - (JOINT - 2)) / 5.0
            else:
                # each block slightly domed and independently weathered
                height[y][x] = 0.86 + face[y][x] * 0.10 + grain[y][x] * 0.04
                ids[y][x] = (ri, ci)

    def rgb(x, y, h):
        if ids[y][x] is None:
            v = 74 + grain[y][x] * 22                          # mortar, dark
            return (v, v * 0.99, v * 0.95)
        v = 112 + face[y][x] * 44 + grain[y][x] * 18
        return (v * 1.02, v, v * 0.92)
    return height, rgb


def cobble(cell=64, seed=47):
    """Rounded stones with deep gaps between them. The best floor for this:
    every gap is a hole the view ray has to climb out of.

    cell must divide W and H exactly, and the alternating row offset needs an
    even number of rows, or the sheet does not tile and every surface wide
    enough to repeat shows a seam."""
    grain = fbm(seed, octaves=4, freq0=22)
    jitter = fbm(seed + 7, octaves=2, freq0=W // cell + 1)
    height = [[0.0] * W for _ in range(H)]
    on = [[False] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            # offset alternate rows, and wobble each stone off its cell centre
            row = y // cell
            ox = (cell // 2) if row % 2 else 0
            cx = ((x + ox) % cell) - cell / 2.0
            cy = (y % cell) - cell / 2.0
            jx = (jitter[y][x] - 0.5) * 7.0
            r = math.sqrt((cx - jx) ** 2 + (cy + jx) ** 2)
            rad = cell * 0.44 * (0.82 + grain[y][x] * 0.34)
            if r < rad:
                height[y][x] = 0.26 + 0.70 * math.sqrt(max(0.0, 1.0 - (r / rad) ** 2))
                on[y][x] = True
            else:
                height[y][x] = 0.05 + grain[y][x] * 0.06       # deep gap

    def rgb(x, y, h):
        if on[y][x]:
            v = 104 + grain[y][x] * 52
            return (v * 1.01, v, v * 0.95)
        v = 58 + grain[y][x] * 20
        return (v, v * 0.98, v * 0.94)
    return height, rgb


def boulder(seed=71):
    """Lumpy rock with sharp crevices - the surface a seven-polygon block has to
    wear if it is going to read as a boulder. Ridged noise, so the creases are
    creases and not smooth valleys."""
    a = fbm(seed, octaves=6, freq0=3)
    b = fbm(seed + 11, octaves=5, freq0=9)
    height = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            # 1 - |2v-1| turns a smooth field into one with creases at its zeros
            ridge = 1.0 - abs(2.0 * b[y][x] - 1.0)
            height[y][x] = 0.12 + 0.62 * a[y][x] + 0.26 * (ridge ** 2)

    def rgb(x, y, h):
        v = 88 + h * 74 + b[y][x] * 20
        return (v * 1.0, v * 0.99, v * 0.94)
    return height, rgb


def gravel(seed=83):
    """Small chips. Fine, dense, shallow - the case where parallax should be
    almost invisible and the normal map does the work. Worth having so the
    others can be judged against something that is not meant to pop."""
    g = fbm(seed, octaves=6, freq0=40)
    height = [[0.10 + 0.55 * (v ** 1.6) for v in row] for row in g]

    def rgb(x, y, h):
        v = 96 + g[y][x] * 66
        return (v, v * 0.99, v * 0.96)
    return height, rgb


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("writing to content/testmaps/textures/")
    write_tga("domes_n.tga", domes())
    write_tga("rocknoise_n.tga", rocknoise())
    write_tga("flatgrey.tga", flat(128))

    for name, fn, scale in (("brick",   brick,   90.0),
                            ("rock",    rock,    70.0),
                            ("plate",   plate,  110.0),
                            ("waffle",  waffle, 140.0),
                            ("grooves", grooves,140.0),
                            ("steps",   steps,   90.0),
                            ("studs",   studs,  120.0),
                            ("stoneblock", stoneblock, 120.0),
                            ("cobble",  cobble, 130.0),
                            ("boulder", boulder, 95.0),
                            ("gravel",  gravel,  80.0)):
        h, rgb = fn()
        write_tga("%s_d.tga" % name, diffuse_from(h, rgb))
        write_tga("%s_n.tga" % name, normals_from_height(h, scale))
