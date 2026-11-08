#!/usr/bin/env python3
"""Write a corpus of files a slicer must refuse without crashing.

A slicer is a parser: STL, 3MF, OBJ, STEP and G-code all arrive from strangers, from
a half-finished download, or from a tool that wrote them wrong. The corpus is
generated rather than committed so the cases stay readable as the reason they
exist, and so a case can be added without a binary blob.

    python3 tools/malformed-fixtures.py --out /tmp/nl-malformed

Models are expected to be refused with a message. G-code is expected to open: a
file whose last line has no newline is what a truncated download looks like, it is
valid G-code, and the processor throws on it rather than reading it.
"""

import argparse
import struct
from pathlib import Path

CUBE = Path(__file__).resolve().parents[1] / "oracle/models/cube_20.stl"


def triangle(tris, a, b, c):
    tris.append((a, b, c))


def stl_bytes(tris, declared=None):
    out = b"next-layer malformed fixture".ljust(80, b"\0")
    out += struct.pack("<I", len(tris) if declared is None else declared)
    for corners in tris:
        out += struct.pack("<3f", 0.0, 0.0, 0.0)
        for vertex in corners:
            out += struct.pack("<3f", *vertex)
        out += struct.pack("<H", 0)
    return out


def two_triangles():
    tris = []
    triangle(tris, (0, 0, 0), (10, 0, 0), (0, 10, 0))
    triangle(tris, (0, 0, 0), (0, 10, 0), (0, 0, 10))
    return tris


def write_models(directory):
    good = CUBE.read_bytes()
    (directory / "empty.stl").write_bytes(b"")
    (directory / "header_only.stl").write_bytes(good[:80])
    (directory / "truncated_body.stl").write_bytes(good[: 80 + 4 + 96])
    (directory / "lying_facet_count.stl").write_bytes(
        good[:80] + struct.pack("<I", 2_000_000_000))
    (directory / "not_a_mesh.stl").write_bytes(b"this is not a mesh\n" * 200)
    (directory / "open_shell.stl").write_bytes(stl_bytes(two_triangles()))
    (directory / "empty.3mf").write_bytes(b"")
    (directory / "not_an_archive.3mf").write_bytes(b"PK\x03\x04" + b"junk" * 40)
    (directory / "not_a_mesh.obj").write_bytes(b"v 0 0 0\nf 1 2 3\n" * 5)
    (directory / "not_a_step.step").write_bytes(b"ISO-10303-21;\nnot really\n")


def write_gcode(directory):
    baseline = Path(__file__).resolve().parents[1] / "oracle/baseline/gcode/cube_default.gcode"
    if not baseline.is_file():
        raise SystemExit("malformed-fixtures: %s is missing; run the oracle capture" % baseline)
    full = baseline.read_bytes()
    (directory / "truncated.gcode").write_bytes(full[: len(full) // 2])
    (directory / "binary.gcode").write_bytes(bytes(range(256)) * 64)
    header = b"\n".join(full.split(b"\n")[:40])
    (directory / "lying_layer_count.gcode").write_bytes(
        header + b"\n; total layer count = 100000\nG1 X10 Y10 Z0.2 E1\nG1 X20 Y10 E2\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="directory to write the corpus into")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    write_models(args.out)
    write_gcode(args.out)
    print("%d fixture(s) in %s" % (len(list(args.out.iterdir())), args.out))


if __name__ == "__main__":
    main()
