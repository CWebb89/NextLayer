#!/usr/bin/env python3
"""Generate the fixed set of test models used by the G-code oracle.

Models are synthetic on purpose: self-contained (no licensing, no network),
small, and each one exercises a different part of the slicing pipeline.
"""

import math
import struct
from pathlib import Path

MODELS = Path(__file__).resolve().parent / "models"


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normal(tri):
    a, b, c = tri
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = cross(u, v)
    length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    if length == 0:
        return (0.0, 0.0, 0.0)
    return (n[0] / length, n[1] / length, n[2] / length)


def write_stl(path, triangles):
    with open(path, "wb") as f:
        f.write(b"next-layer oracle model".ljust(80, b"\0"))
        f.write(struct.pack("<I", len(triangles)))
        for tri in triangles:
            n = normal(tri)
            f.write(struct.pack("<3f", *n))
            for vertex in tri:
                f.write(struct.pack("<3f", *vertex))
            f.write(struct.pack("<H", 0))


def quad(a, b, c, d):
    return [(a, b, c), (a, c, d)]


def cube(size=20.0):
    s = size / 2.0
    p = [
        (-s, -s, 0.0),
        (s, -s, 0.0),
        (s, s, 0.0),
        (-s, s, 0.0),
        (-s, -s, size),
        (s, -s, size),
        (s, s, size),
        (-s, s, size),
    ]
    faces = [
        (0, 3, 2, 1),
        (4, 5, 6, 7),
        (0, 1, 5, 4),
        (1, 2, 6, 5),
        (2, 3, 7, 6),
        (3, 0, 4, 7),
    ]
    tris = []
    for f in faces:
        tris += quad(p[f[0]], p[f[1]], p[f[2]], p[f[3]])
    return tris


def cylinder(radius=10.0, height=20.0, segments=96):
    ring_bottom = []
    ring_top = []
    for i in range(segments):
        angle = 2.0 * math.pi * i / segments
        x = radius * math.cos(angle)
        y = radius * math.sin(angle)
        ring_bottom.append((x, y, 0.0))
        ring_top.append((x, y, height))
    tris = []
    for i in range(segments):
        j = (i + 1) % segments
        tris += quad(ring_bottom[i], ring_bottom[j], ring_top[j], ring_top[i])
    center_bottom = (0.0, 0.0, 0.0)
    center_top = (0.0, 0.0, height)
    for i in range(segments):
        j = (i + 1) % segments
        tris.append((center_bottom, ring_bottom[j], ring_bottom[i]))
        tris.append((center_top, ring_top[i], ring_top[j]))
    return tris


def sphere(radius=12.0, rings=48, segments=96):
    grid = []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        row = []
        for s in range(segments):
            theta = 2.0 * math.pi * s / segments
            row.append(
                (
                    radius * math.sin(phi) * math.cos(theta),
                    radius * math.sin(phi) * math.sin(theta),
                    radius + radius * math.cos(phi),
                )
            )
        grid.append(row)
    tris = []
    for r in range(rings):
        for s in range(segments):
            s2 = (s + 1) % segments
            a = grid[r][s]
            b = grid[r][s2]
            c = grid[r + 1][s2]
            d = grid[r + 1][s]
            if r == 0:
                tris.append((a, d, c))
            elif r == rings - 1:
                tris.append((a, c, b))
            else:
                tris += quad(a, d, c, b)
    return tris


def bridge(span=30.0, pillar=6.0, depth=10.0, height=25.0, bar=6.0):
    tris = []
    x0 = -span / 2.0
    x1 = span / 2.0
    y0 = -depth / 2.0
    y1 = depth / 2.0
    for ax in (x0, x1 - pillar):
        bx = ax + pillar
        corners = [
            (ax, y0, 0.0),
            (bx, y0, 0.0),
            (bx, y1, 0.0),
            (ax, y1, 0.0),
            (ax, y0, height),
            (bx, y0, height),
            (bx, y1, height),
            (ax, y1, height),
        ]
        for f in [
            (0, 3, 2, 1),
            (4, 5, 6, 7),
            (0, 1, 5, 4),
            (1, 2, 6, 5),
            (2, 3, 7, 6),
            (3, 0, 4, 7),
        ]:
            tris += quad(*[corners[i] for i in f])
    bar_corners = [
        (x0, y0, height - bar),
        (x1, y0, height - bar),
        (x1, y1, height - bar),
        (x0, y1, height - bar),
        (x0, y0, height),
        (x1, y0, height),
        (x1, y1, height),
        (x0, y1, height),
    ]
    for f in [
        (0, 3, 2, 1),
        (4, 5, 6, 7),
        (0, 1, 5, 4),
        (1, 2, 6, 5),
        (2, 3, 7, 6),
        (3, 0, 4, 7),
    ]:
        tris += quad(*[bar_corners[i] for i in f])
    return tris


def main():
    MODELS.mkdir(parents=True, exist_ok=True)
    shapes = {
        "cube_20": cube(),
        "cylinder_r10_h20": cylinder(),
        "sphere_r12": sphere(),
        "bridge_30": bridge(),
    }
    for name, tris in shapes.items():
        path = MODELS / f"{name}.stl"
        write_stl(path, tris)
        print(f"{path.name}: {len(tris)} triangles, {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
