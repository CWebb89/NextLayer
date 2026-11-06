#!/usr/bin/env python3
"""Decimate a binary STL by vertex clustering, keeping the silhouette.

The calibration meshes That project ships are Draco-compressed; the reference's own CLI exports
them as STL at full resolution (the temperature tower is 212k triangles). This
collapses vertices onto a grid instead, which is enough for a test print's
geometry and keeps the file in the repository at a sane size.
"""

import struct
import sys


def read(path):
    data = open(path, "rb").read()
    count = struct.unpack("<I", data[80:84])[0]
    tris = []
    for index in range(count):
        offset = 84 + index * 50 + 12
        corners = []
        for corner in range(3):
            corners.append(struct.unpack("<fff", data[offset + corner * 12:offset + corner * 12 + 12]))
        tris.append(tuple(corners))
    return tris


def write(path, tris):
    with open(path, "wb") as handle:
        handle.write(b"next-layer calibration mesh".ljust(80, b"\0"))
        handle.write(struct.pack("<I", len(tris)))
        for corners in tris:
            handle.write(struct.pack("<fff", 0.0, 0.0, 0.0))
            for x, y, z in corners:
                handle.write(struct.pack("<fff", x, y, z))
            handle.write(struct.pack("<H", 0))


def decimate(tris, cell):
    def quantise(point):
        return tuple(round(value / cell) for value in point)

    groups = {}
    for corners in tris:
        keys = [quantise(point) for point in corners]
        if len(set(keys)) < 3:
            continue
        groups.setdefault(tuple(sorted(keys)), (keys, corners))

    merged = {}
    for keys, corners in groups.values():
        for key, point in zip(keys, corners):
            slot = merged.setdefault(key, [0.0, 0.0, 0.0, 0])
            slot[0] += point[0]
            slot[1] += point[1]
            slot[2] += point[2]
            slot[3] += 1

    averaged = {key: (slot[0] / slot[3], slot[1] / slot[3], slot[2] / slot[3])
                for key, slot in merged.items()}
    out = []
    for keys, _ in groups.values():
        out.append(tuple(averaged[key] for key in keys))
    return out


def main():
    source, target, cell = sys.argv[1], sys.argv[2], float(sys.argv[3])
    tris = read(source)
    reduced = decimate(tris, cell)
    write(target, reduced)
    height = max(point[2] for corners in reduced for point in corners)
    print("%s: %d -> %d triangles, cell %.2f, height %.1f" % (target, len(tris), len(reduced), cell, height))


if __name__ == "__main__":
    main()
