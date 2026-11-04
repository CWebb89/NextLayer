#!/usr/bin/env python3
"""Slice every printer the app lists, and report the ones that produce nothing.

    python3 tools/orca-catalog/validate.py                 # whole catalog, ~25 min
    python3 tools/orca-catalog/validate.py --vendor Voron

A printer preset can resolve, appear in the dropdown and carry sensible settings
while the engine writes no G-code at all and still exits 0 -- three separate
causes did exactly that while the catalog was built. Exit code alone proves
nothing, so this checks the output file and its size.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "upstream/build/src/slic3r-app-qt/next-layer-qt"
MODEL = ROOT / "oracle/models/cube_20.stl"
MINIMUM_BYTES = 100_000


def app_environment() -> dict:
    return dict(os.environ, QT_QPA_PLATFORM="offscreen")


def printer_names(binary: Path) -> list[str]:
    result = subprocess.run([str(binary), "--dump-presets"], env=app_environment(),
                            capture_output=True, timeout=900)
    names = []
    for line in result.stdout.decode(errors="replace").splitlines():
        if "extruder)" not in line:
            continue
        names.append(" ".join(line.split("  ")[0].split()))
    return names


def slice_one(binary: Path, name: str, process: str, filament: str) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        config = Path(tmp) / "config.json"
        gcode = Path(tmp) / "out.gcode"
        resolve = subprocess.run([str(binary), "--printer", name, "--process", process,
                                  "--filament", filament, "--dump-config", str(config)],
                                 env=app_environment(), capture_output=True, timeout=600)
        if not config.is_file():
            return f"no config (exit {resolve.returncode})"
        subprocess.run([str(binary), str(MODEL), "--slice", f"{config},{gcode}"],
                       env=app_environment(), capture_output=True, timeout=600)
        if not gcode.is_file():
            return "no G-code written"
        size = gcode.stat().st_size
        if size < MINIMUM_BYTES:
            return f"{size} bytes of G-code"
        return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, default=BIN)
    parser.add_argument("--process", default="Standard")
    parser.add_argument("--filament", default="Generic")
    parser.add_argument("--vendor", default=None,
                        help="only printers whose name starts with this vendor")
    args = parser.parse_args()

    if not args.bin.is_file():
        raise SystemExit(f"validate: no binary at {args.bin}; build first")
    if not MODEL.is_file():
        raise SystemExit(f"validate: no model at {MODEL}")

    names = printer_names(args.bin)
    if args.vendor:
        names = [name for name in names if name.lower().startswith(args.vendor.lower())]
    print(f"validating {len(names)} printer(s) against {MODEL.name}")

    failures = []
    for index, name in enumerate(names, 1):
        reason = slice_one(args.bin, name, args.process, args.filament)
        if reason:
            failures.append((name, reason))
            print(f"  {index:>3}/{len(names)} FAIL {name}: {reason}", flush=True)

    print()
    print(f"{len(names) - len(failures)}/{len(names)} printers sliced")
    if failures:
        print("failed:")
        for name, reason in failures:
            print(f"  {name}: {reason}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
