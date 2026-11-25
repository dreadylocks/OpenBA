#!/usr/bin/env python3
"""tools/ext_icons/copymove.py -- the toolbar button of the copy / move tool (programmer art, our own).

usage: python3 tools/ext_icons/copymove.py      (from the repo root; standard library only)

Writes src/ext/ui/Dialog/Sprite/Extensions/Toolbar_copymove.png: 32 x 32 RGBA, a white silhouette with
antialiased alpha and no outline, like the toolbar's other icons (Toolbar_extensions.png, tools/ext_sprites.py).
The picture: two houses, the back one drawn as an outline up and to the left of the solid front one -- a
copy. Drawn at 8x and box-filtered down for the soft edges. Then rerun tools/ext_sprites_bake.py.
"""
import os
import struct
import zlib

OUT = os.path.join("src", "ext", "ui", "Dialog", "Sprite", "Extensions", "Toolbar_copymove.png")
W = H = 32
S = 8


def house(x, y, ox, oy, size):
    """Inside a house of width `size` whose wall's top-left is (ox, oy): a square wall under a roof."""
    wall_top = oy + size * 0.45
    if ox <= x <= ox + size and wall_top <= y <= oy + size:
        return True
    # The roof: a triangle from the wall's corners (a little wider) up to the ridge.
    half = size * 0.6
    cx = ox + size * 0.5
    if oy <= y <= wall_top:
        t = (y - oy) / (wall_top - oy)
        return abs(x - cx) <= half * t
    return False


def inside(x, y):
    """The front house solid, the back house as a ring 2.5 px wide, cut away where the front house's margin is."""
    front = house(x, y, 12.0, 11.5, 17.0)
    if front:
        return True
    # Keep a clear gap round the front house so the two read apart.
    for dx in (-2.0, 0.0, 2.0):
        for dy in (-2.0, 0.0, 2.0):
            if house(x + dx, y + dy, 12.0, 11.5, 17.0):
                return False
    back_outer = house(x, y, 2.5, 3.0, 17.0)
    back_inner = house(x, y, 5.0, 6.8, 12.0)
    return back_outer and not back_inner


def png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def main():
    out = bytearray()
    for py in range(H):
        for px in range(W):
            hits = 0
            for sy in range(S):
                for sx in range(S):
                    if inside(px + (sx + 0.5) / S, py + (sy + 0.5) / S):
                        hits += 1
            a = round(255 * hits / (S * S))
            out += bytes((255, 255, 255, a))
    png(OUT, W, H, bytes(out))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
