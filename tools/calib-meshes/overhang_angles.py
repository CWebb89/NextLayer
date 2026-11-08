#!/usr/bin/env python3
"""A model that tests overhang angle: six cantilevered ramps off a wall.

Each unit is a wall with a wedge hanging off its top, so the printed surface is
the wedge's underside and it is standing in air -- no bed under it, nothing to
lean on. The underside's angle away from vertical is 30, 40, 50, 60, 70 and 80
degrees, in that order along +Y, so a print that is ragged from 50 degrees up
says exactly which angle the machine stops carrying.

Usage: overhang_angles.py OUT.stl
"""

import math
import struct
import sys

ANGLES = (30.0, 40.0, 50.0, 60.0, 70.0, 80.0)
WALL_THICKNESS = 3.0
UNIT_WIDTH = 10.0
UNIT_GAP = 8.0
RAMP_RISE = 10.0
WALL_BASE = 25.0
MAX_REACH = 30.0


def facets(corners):
    a, b, c, d = corners
    return ((a, b, c), (a, c, d))


def box(tris, x0, y0, z0, x1, y1, z1):
    bottom = ((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0))
    top = ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))
    south = ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))
    north = ((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0))
    west = ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))
    east = ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1))
    for quad in (bottom, top, south, north, west, east):
        tris.extend(facets(quad))


def wedge(tris, y0, y1, reach, rise, base):
    """A right triangular prism rooted 1 mm inside the wall, its underside
    sloping down from the wall's top at x=0 to x=reach at z=base."""
    peak = base + rise
    root = -1.0
    south = ((root, y0, base), (reach, y0, base), (root, y0, peak))
    north = ((root, y1, base), (root, y1, peak), (reach, y1, base))
    tris.extend((south, north))
    underside = ((root, y0, peak), (root, y1, peak), (reach, y1, base), (reach, y0, base))
    tris.extend(facets(underside))
    tris.extend(facets(((root, y0, base), (root, y1, base), (root, y1, peak), (root, y0, peak))))
    tris.extend(facets(((reach, y0, base), (reach, y0, peak), (reach, y1, peak), (reach, y1, base))))


def build():
    tris = []
    y = 0.0
    for angle in ANGLES:
        reach = RAMP_RISE * math.tan(math.radians(angle))
        rise = RAMP_RISE
        if reach > MAX_REACH:
            reach = MAX_REACH
            rise = reach / math.tan(math.radians(angle))
        y0 = y
        y1 = y + UNIT_WIDTH
        box(tris, -WALL_THICKNESS, y0, 0.0, 0.0, y1, WALL_BASE + rise)
        wedge(tris, y0, y1, reach, rise, WALL_BASE)
        y = y1 + UNIT_GAP
    return tris


def write(path, tris):
    with open(path, "wb") as handle:
        handle.write(b"next-layer overhang angle test".ljust(80, b"\0"))
        handle.write(struct.pack("<I", len(tris)))
        for corners in tris:
            handle.write(struct.pack("<fff", 0.0, 0.0, 0.0))
            for x, y, z in corners:
                handle.write(struct.pack("<fff", x, y, z))
            handle.write(struct.pack("<H", 0))


def main():
    if len(sys.argv) != 2:
        print("usage: overhang_angles.py OUT.stl", file=sys.stderr)
        return 2
    tris = build()
    write(sys.argv[1], tris)
    print("%s: %d triangles, %d angles" % (sys.argv[1], len(tris), len(ANGLES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
