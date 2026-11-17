#!/usr/bin/env python3
"""Slices one model through both engines and compares what the files claim.

The open question this answers is the one that decides when libslic3r can be
deleted: does the clean-room engine put the same plastic in the same place in the
same time? Two files from two engines never match byte for byte -- path order,
seam placement and infill anchoring all differ by design -- so the comparison is on
the numbers a print is judged by, each with a tolerance, plus the geometry the two
files actually cover.

    tools/parity.py --app next-layer-qt [--model PATH] [--printer NAME]
                    [--process NAME] [--filament NAME] [--keep DIR]

Exit status is 0 when every number is inside its tolerance, 1 when one is not, and
2 when a slice did not happen at all.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

# How far apart two independent slicers may land. Filament and volume are set by
# the geometry and are the tightest; time depends on the acceleration model each
# engine uses, so it gets the widest window.
TOLERANCE = {
    "layers": 0.0,
    "filament_mm": 0.05,
    "volume_mm3": 0.05,
    "time_s": 0.15,
    "top_z": 0.01,
}

HEAD_FILAMENT = re.compile(r";\s*filament used(?: \[mm\])?\s*=\s*([0-9.]+)")
HEAD_TIME = re.compile(r";\s*estimated printing time(?: \([^)]*\))?\s*=\s*(.+)$")
MOVE = re.compile(r"^G[01]\s(.*)$")


def to_seconds(text):
    """`616 s`, `10m 24s` and `1h 2m 3s` all become seconds.

    The two engines write the same estimate in the two dialects their headers come
    from, and comparing one spelling only would compare 616 with nothing.
    """
    total = 0.0
    found = False
    for amount, unit in re.findall(r"([0-9.]+)\s*([hms])", text):
        found = True
        total += float(amount) * {"h": 3600.0, "m": 60.0, "s": 1.0}[unit]
    return total if found else float("nan")


def slice_with(app, model, presets, gcode, engine_env):
    environment = dict(os.environ)
    environment.update(engine_env)
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [app, model, "--selftest-slice", "--keep-gcode", gcode]
    for flag, name in presets:
        command += [flag, name]
    result = subprocess.run(command, capture_output=True, text=True, timeout=900, env=environment)
    if not os.path.exists(gcode) or os.path.getsize(gcode) == 0:
        print(f"no G-code from {app} {engine_env}: {result.stdout[-400:]}{result.stderr[-400:]}",
              file=sys.stderr)
        return None
    return gcode


def summarise(path):
    """The numbers a print is judged by, read from the file itself."""
    layers = 0
    filament = None
    seconds = None
    top_z = 0.0
    height = 0.0
    low = [float("inf")] * 2
    high = [float("-inf")] * 2
    extruding_moves = 0
    with open(path) as handle:
        for line in handle:
            if line.startswith(";"):
                if ";LAYER_CHANGE" in line:
                    layers += 1
                match = HEAD_FILAMENT.search(line)
                if match:
                    filament = float(match.group(1))
                match = HEAD_TIME.search(line)
                if match:
                    seconds_value = to_seconds(match.group(1))
                    if seconds_value == seconds_value:
                        seconds = seconds_value
                continue
            match = MOVE.match(line)
            if not match:
                continue
            words = {}
            for token in match.group(1).split():
                if len(token) > 1 and token[0] in "XYZE":
                    try:
                        words[token[0]] = float(token[1:])
                    except ValueError:
                        pass
            if "X" in words and "Y" in words:
                x, y = float(words["X"]), float(words["Y"])
                low[0], low[1] = min(low[0], x), min(low[1], y)
                high[0], high[1] = max(high[0], x), max(high[1], y)
            if "E" in words:
                extruding_moves += 1
                # The top of the part is the highest Z a bead was laid at: the end
                # G-code lifts the head clear afterwards, and that lift is not the
                # model's height.
                top_z = max(top_z, height)
            if "Z" in words:
                height = float(words["Z"])
    return {
        "layers": layers,
        "filament_mm": filament if filament is not None else float("nan"),
        "time_s": seconds if seconds is not None else float("nan"),
        "top_z": top_z,
        "width": high[0] - low[0],
        "depth": high[1] - low[1],
        "extruding_moves": extruding_moves,
    }


def spread(a, b):
    if a == b:
        return 0.0
    scale = max(abs(a), abs(b))
    return abs(a - b) / scale if scale else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="next-layer-qt binary")
    parser.add_argument("--model", default="oracle/models/cube_20.stl")
    parser.add_argument("--printer", default="")
    parser.add_argument("--process", default="")
    parser.add_argument("--filament", default="")
    parser.add_argument("--keep", default="", help="directory to keep both G-code files in")
    args = parser.parse_args()

    presets = []
    for flag, name in (("--printer", args.printer), ("--process", args.process),
                       ("--filament", args.filament)):
        if name:
            presets.append((flag, name))

    work = args.keep or tempfile.mkdtemp(prefix="parity-")
    os.makedirs(work, exist_ok=True)
    paths = {
        "libslic3r": os.path.join(work, "libslic3r.gcode"),
        "hfw": os.path.join(work, "hfw.gcode"),
    }
    engines = {"libslic3r": {}, "hfw": {"NEXT_LAYER_ENGINE": "hfw"}}

    results = {}
    for name, environment in engines.items():
        path = slice_with(args.app, args.model, presets, paths[name], environment)
        if path is None:
            return 2
        results[name] = summarise(path)

    reference, candidate = results["libslic3r"], results["hfw"]
    print(f"{'':16}{'libslic3r':>14}{'hfw':>14}{'spread':>10}  verdict")
    failed = False
    for key in ("layers", "filament_mm", "time_s", "top_z", "width", "depth"):
        left, right = reference[key], candidate[key]
        difference = spread(left, right)
        limit = TOLERANCE.get(key)
        verdict = ""
        if limit is not None:
            verdict = "ok" if difference <= limit else f"FAIL > {limit:.0%}"
            failed = failed or difference > limit
        print(f"{key:16}{left:>14.3f}{right:>14.3f}{difference:>9.1%}  {verdict}")
    print(f"{'extruding moves':16}{reference['extruding_moves']:>14d}"
          f"{candidate['extruding_moves']:>14d}")
    if args.keep:
        print(f"\nkept: {paths['libslic3r']}\n      {paths['hfw']}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
