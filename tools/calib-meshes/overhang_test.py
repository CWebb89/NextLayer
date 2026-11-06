#!/usr/bin/env python3
"""A model that cannot be printed without support: a cantilevered shelf.

A 60 mm wide bar hanging 25 mm off the bed on two posts, plus a 45 degree
overhang on the far end, so organic supports have something to grow under and a
bridge to cross.
"""

import math
import struct
import sys


def box(tris, x0, y0, z0, x1, y1, z1):
    faces = [
        ((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)),
        ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)),
        ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)),
        ((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)),
        ((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)),
        ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)),
    ]
    for a, b, c, d in faces:
        tris.append((a, b, c))
        tris.append((a, c, d))


def write(path, tris):
    with open(path, "wb") as handle:
        handle.write(b"next-layer support test".ljust(80, b"\0"))
        handle.write(struct.pack("<I", len(tris)))
        for corners in tris:
            handle.write(struct.pack("<fff", 0.0, 0.0, 0.0))
            for x, y, z in corners:
                handle.write(struct.pack("<fff", x, y, z))
            handle.write(struct.pack("<H", 0))


def main():
    tris = []
    box(tris, -3.0, -3.0, 0.0, 3.0, 3.0, 25.0)
    box(tris, 37.0, -3.0, 0.0, 43.0, 3.0, 25.0)
    box(tris, -3.0, -3.0, 25.0, 43.0, 3.0, 31.0)
    box(tris, 43.0, -3.0, 25.0, 58.0, 3.0, 31.0)
    for step in range(6):
        z0 = 31.0 + step * 2.0
        x0 = 58.0 + step * 2.0
        box(tris, x0, -3.0, z0, x0 + 2.0, 3.0, z0 + 2.0)
    write(sys.argv[1], tris)
    print("%s: %d triangles" % (sys.argv[1], len(tris)))


if __name__ == "__main__":
    main()
