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
    """24-bit uncompressed TGA. Bottom-up, so rows are written reversed."""
    data = b"".join(reversed(rows))
    hdr = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, W, H, 24, 0)
    path = os.path.join(OUT, name)
    open(path, "wb").write(hdr + data)
    print("  %-18s %7d bytes" % (name, len(hdr) + len(data)))


def encode(nx, ny, nz):
    """Unit normal to RGB. Rounds - truncating puts 127 where 128 belongs and
    tilts every flat texel toward -u/-v, which is a bias over the whole sheet."""
    m = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return bytes((
        max(0, min(255, int((nx / m * 0.5 + 0.5) * 255.0 + 0.5))),
        max(0, min(255, int((ny / m * 0.5 + 0.5) * 255.0 + 0.5))),
        max(0, min(255, int((nz / m * 0.5 + 0.5) * 255.0 + 0.5))),
    ))


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
                row += encode(0.0, 0.0, 1.0)          # flat between the domes
            else:
                nx = dx / radius
                ny = dy / radius
                # OpenGL convention: +Y is up in texture space, and v runs down,
                # so the y component is negated relative to the pixel axis.
                row += encode(nx, -ny, math.sqrt(max(0.0, 1.0 - r2 / (radius * radius))))
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


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("writing to content/testmaps/textures/")
    write_tga("domes_n.tga", domes())
    write_tga("rocknoise_n.tga", rocknoise())
    write_tga("flatgrey.tga", flat(128))
