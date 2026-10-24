#!/usr/bin/env python3
"""tools/cell_dump/cell_diff.py -- compare map-cell dumps written by BANISHED_CELL_DUMP.

usage:
  cell_diff.py BEFORE.txt AFTER.txt [--at X,Y] [--radius N]
      every cell whose fields differ between the two dumps (both from the same map);
      --at limits the listing to cells within --radius (default 3) of X,Y.
  cell_diff.py DUMP.txt --suspects
      cells a "red square" can come from in one dump: footprintCount 0xFF (a wrap below 0),
      above 2, or non-zero with no road and no building within 2 cells.

Fields (src/diag/cell_trace.cpp): count = footprintCount (+0x28), terrain = terrainType
(+0x29), road = +0x2A (255 = none), roadState = +0x2B, growth = +0x2C, floor = floorCount
(+0x2D), zone = +0x26, blocker / occupant = the entity's description name or '-'.
"""
import argparse
import sys

FIELDS = ["count", "terrain", "road", "roadState", "growth", "floor", "zone", "blocker", "occupant"]


def load(path):
    header = ""
    cells = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                if not header:
                    header = line.strip()
                continue
            parts = line.split()
            if len(parts) < 12:
                continue
            index, x, y = int(parts[0]), int(parts[1]), int(parts[2])
            cells[index] = (x, y, dict(zip(FIELDS, parts[3:12])))
    return header, cells


def near(x, y, at, radius):
    return at is None or (abs(x - at[0]) <= radius and abs(y - at[1]) <= radius)


def diff(before_path, after_path, at, radius):
    header_a, a = load(before_path)
    header_b, b = load(after_path)
    print("before:", header_a)
    print("after: ", header_b)
    changed = 0
    for index in sorted(set(a) | set(b)):
        if index not in a or index not in b:
            print(f"cell {index}: present in one dump only")
            changed += 1
            continue
        x, y, fa = a[index]
        _, _, fb = b[index]
        if fa == fb or not near(x, y, at, radius):
            continue
        changes = ", ".join(f"{k} {fa[k]} -> {fb[k]}" for k in FIELDS if fa[k] != fb[k])
        print(f"cell {index} ({x},{y}): {changes}")
        changed += 1
    print(f"{changed} cell(s) differ")


def blocker_nearby(cells, x, y, size):
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            nx, ny = x + dx, y + dy
            if 0 <= nx < size and 0 <= ny < size:
                cell = cells.get(ny * size + nx)
                if cell and cell[2]["blocker"] != "-":
                    return True
    return False


def suspects(path, at, radius):
    header, cells = load(path)
    print(header)
    size = int(header.split("size=")[1].split()[0]) if "size=" in header else 0
    found = 0
    for index in sorted(cells):
        x, y, f = cells[index]
        if not near(x, y, at, radius):
            continue
        count = int(f["count"])
        road = int(f["road"])
        terrain = int(f["terrain"])
        reason = None
        if count == 255:
            reason = "count 0xFF (wrapped below 0)"
        elif count > 2:
            reason = f"count {count}"
        elif (count > 0 and road == 255 and terrain not in (0, 1) and f["blocker"] == "-"
              and not blocker_nearby(cells, x, y, size)):
            # A building's '.' cells hold count 1 with no blocker of their own, but sit
            # next to the building's blocked cells; one with no blocker within 2 is orphaned.
            reason = f"count {count} with no road and no building within 2 cells"
        if reason:
            print(f"cell {index} ({x},{y}): {reason}; terrain {terrain} road {road} "
                  f"blocker {f['blocker']} occupant {f['occupant']}")
            found += 1
    print(f"{found} suspect cell(s)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dumps", nargs="+")
    parser.add_argument("--suspects", action="store_true")
    parser.add_argument("--at", help="X,Y cell to centre on")
    parser.add_argument("--radius", type=int, default=3)
    args = parser.parse_args()
    at = tuple(int(v) for v in args.at.split(",")) if args.at else None
    if args.suspects:
        for path in args.dumps:
            suspects(path, at, args.radius)
        return 0
    if len(args.dumps) != 2:
        parser.error("give two dumps to compare, or --suspects")
    diff(args.dumps[0], args.dumps[1], at, args.radius)
    return 0


if __name__ == "__main__":
    sys.exit(main())
