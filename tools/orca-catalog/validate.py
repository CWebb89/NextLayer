#!/usr/bin/env python3
"""Slice every printer the app lists, and report the ones that produce nothing.

    python3 tools/orca-catalog/validate.py                 # whole catalog, ~7 min at --jobs 4
    python3 tools/orca-catalog/validate.py --vendor Voron

A printer preset can resolve, appear in the dropdown and carry sensible settings
while the engine writes no G-code at all and still exits 0 -- three separate
causes did exactly that while the catalog was built. Exit code alone proves
nothing, so this checks the output file, its size, and that the vendor's start
G-code reached the output.

The run resolves presets with a throwaway XDG_DATA_HOME so it measures the
bundle in the build tree. The app prefers `~/.local/share/next-layer/presets`,
so a copy left there shadows every regenerated bundle without a word.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "upstream/build/src/slic3r-app-qt/next-layer-qt"
BUNDLE = ROOT / "upstream/resources/presets/orca-catalog"
MODEL = ROOT / "oracle/models/cube_20.stl"
MINIMUM_BYTES = 100_000
START_WINDOW = 8000
MINIMUM_MARKER = 8


def app_environment(data_home: Path) -> dict:
    return dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_DATA_HOME=str(data_home))


def printer_names(binary: Path, data_home: Path) -> list[str]:
    """Hardware config names, from the app's machine-readable dump.

    The column dump pads names to 42 columns, and a printer name is routinely
    longer than that, so anything parsing it by whitespace turns a good machine
    into a failure its own regex caused.
    """
    result = subprocess.run([str(binary), "--dump-presets", "--dump-presets-json"],
                            env=app_environment(data_home), capture_output=True, timeout=900)
    payload = result.stdout.decode(errors="replace").strip().splitlines()
    for line in reversed(payload):
        if line.startswith("["):
            return [entry["name"] for entry in json.loads(line)]
    raise SystemExit("validate: --dump-presets-json printed no JSON")


def catalog_names(bundle: Path, nozzle: str) -> list[str]:
    """Hardware config names this bundle publishes, read from the files it ships.

    Every generated vendor declares its machines as printer_families labels and
    exactly one tool, and the engine composes a config name as the family label
    plus the tool name. Filtering the app's dump instead would mix in the
    engine's built-in Prusa machines, which carry their own print-profile names.
    """
    names = []
    for path in sorted(bundle.glob("*/vendor.yaml")):
        in_families = False
        for line in path.read_text().splitlines():
            if line.startswith("printer_families:"):
                in_families = True
            elif in_families and line.startswith("- label: "):
                label = line[len("- label: "):].strip().strip("'")
                names.append(f"{label} {nozzle}")
            elif in_families and not line.startswith((" ", "-")):
                in_families = False
    return names


def duplicate_names(names: list[str]) -> list[str]:
    """Names published by more than one hardware config.

    Resolution matches a config by name and takes the first hit, while config
    ids are fresh UUIDs per load, so a duplicated name resolves to a different
    machine on different runs. That is a data defect, not a slicing failure, so
    it is checked before anything is sliced.
    """
    counts = Counter(names)
    return sorted(name for name, count in counts.items() if count > 1)


def outside_placeholders(line: str) -> list[str]:
    runs, depth, current = [], 0, []
    for char in line:
        if char in "[{":
            depth += 1
            if depth == 1:
                runs.append("".join(current))
                current = []
        elif char in "]}":
            depth = max(0, depth - 1)
        elif depth == 0:
            current.append(char)
    runs.append("".join(current))
    return runs


def start_marker(config: Path) -> str:
    settings = json.loads(config.read_text())["configuration"]["printer_settings"]
    for line in str(settings.get("start_gcode", "")).splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        runs = [run.strip() for run in outside_placeholders(line)]
        runs = [run for run in runs if len(run) >= MINIMUM_MARKER]
        if runs:
            return max(runs, key=len)
    return ""


def slice_one(binary: Path, data_home: Path, name: str, process: str, filament: str) -> tuple[str, bool]:
    with tempfile.TemporaryDirectory() as tmp:
        config = Path(tmp) / "config.json"
        gcode = Path(tmp) / "out.gcode"
        resolve = subprocess.run([str(binary), "--printer", name, "--process", process,
                                  "--filament", filament, "--dump-config", str(config)],
                                 env=app_environment(data_home), capture_output=True, timeout=600)
        if not config.is_file():
            return f"no config (exit {resolve.returncode})", False
        marker = start_marker(config)
        subprocess.run([str(binary), str(MODEL), "--slice", f"{config},{gcode}"],
                       env=app_environment(data_home), capture_output=True, timeout=600)
        if not gcode.is_file():
            return "no G-code written", bool(marker)
        size = gcode.stat().st_size
        if size < MINIMUM_BYTES:
            return f"{size} bytes of G-code", bool(marker)
        if marker and marker not in gcode.read_text(errors="replace")[:START_WINDOW]:
            return f"start G-code missing ({marker})", True
        return "", bool(marker)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, default=BIN)
    parser.add_argument("--nozzle", default="0.4",
                        help="tool name the catalog appends to a printer name")
    parser.add_argument("--process", default="Standard")
    parser.add_argument("--filament", default="Generic")
    parser.add_argument("--vendor", default=None,
                        help="only printers whose name starts with this vendor")
    parser.add_argument("--jobs", type=int, default=4, help="slices in flight at once")
    parser.add_argument("--names-only", action="store_true",
                        help="check that no two hardware configs share a name, slice nothing")
    parser.add_argument("--data-home", type=Path, default=None,
                        help="preset home; default is a throwaway directory")
    args = parser.parse_args()

    if not args.bin.is_file():
        raise SystemExit(f"validate: no binary at {args.bin}; build first")
    if not MODEL.is_file():
        raise SystemExit(f"validate: no model at {MODEL}")

    data_home = args.data_home or Path(tempfile.mkdtemp(prefix="orca-catalog-home-"))
    data_home.mkdir(parents=True, exist_ok=True)
    print(f"preset home {data_home}")

    names = printer_names(args.bin, data_home)
    if args.names_only:
        duplicates = duplicate_names(names)
        print(f"{len(names)} hardware configs, {len(duplicates)} duplicate name(s)")
        for name in duplicates:
            print(f"  DUPLICATE {name}")
        return 1 if duplicates else 0
    bundle_names = catalog_names(BUNDLE, args.nozzle)
    names = bundle_names or names
    if args.vendor:
        names = [name for name in names if name.lower().startswith(args.vendor.lower())]
    print(f"validating {len(names)} printer(s) against {MODEL.name}", flush=True)

    def run(indexed: tuple[int, str]) -> tuple[int, str, str, bool]:
        index, name = indexed
        reason, checked = slice_one(args.bin, data_home, name, args.process, args.filament)
        return index, name, reason, checked

    outcomes = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for outcome in pool.map(run, enumerate(names, 1)):
            outcomes.append(outcome)
            index, name, reason, _ = outcome
            if reason:
                print(f"  {index:>3}/{len(names)} FAIL {name}: {reason}", flush=True)

    failures = [(name, reason) for _, name, reason, _ in outcomes if reason]
    unchecked = sum(0 if checked else 1 for _, _, _, checked in outcomes)
    silent = [entry for entry in failures if entry[1].startswith("start G-code")]
    print()
    print(f"{len(names) - len(failures)}/{len(names)} printers sliced with their vendor start G-code")
    if silent:
        print(f"{len(silent)} sliced but dropped the vendor start G-code")
    if unchecked:
        print(f"{unchecked} carried no start G-code line to check")
    if failures:
        print("failed:")
        for name, reason in failures:
            print(f"  {name}: {reason}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
