#!/usr/bin/env python3
"""G-code oracle for next-layer.

Slices a fixed set of models with fixed settings through a slicer binary and
records a fingerprint of the resulting G-code. Once captured, any change to the
slicer (upstream rebase, local patch, new build flags) can be proven not to have
altered the toolpaths.

    python oracle.py capture [--bin PATH] [--label NAME]
    python oracle.py verify  [--bin PATH]

`capture` writes baseline/manifest.json. `verify` re-slices and compares.
Exit code is 0 when everything matches, 1 otherwise.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
DEFAULT_BIN = PROJECT / "upstream" / "build" / "src" / "slic3r-app-launcher" / "slic3r-app-launcher"
CONFIG = ROOT / "configs" / "base.json"
MODELS = ROOT / "models"
BASELINE = ROOT / "baseline"

UUID = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", re.IGNORECASE)
ISO_STAMP = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z")
GENERATED_STAMP = re.compile(r",? on \d{4}-\d{2}-\d{2} at \d{2}:\d{2}:\d{2} UTC")
MEMORY_STAMP = re.compile(r"\bmemory\b")


def sha256(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def normalize(gcode):
    """Strip the tokens that legitimately change between runs (wall clock,
    freshly minted UUIDs, memory statistics) and nothing else. Every emitted
    toolpath byte must survive unchanged."""
    kept = []
    for line in gcode.splitlines():
        if line.startswith(";"):
            line = UUID.sub("<uuid>", line)
            line = ISO_STAMP.sub("<time>", line)
            line = GENERATED_STAMP.sub("", line)
            if MEMORY_STAMP.search(line):
                continue
        kept.append(line.rstrip())
    return "\n".join(kept).rstrip() + "\n"


def stats(gcode):
    extrusions = 0
    filament = None
    layers = 0
    for line in gcode.splitlines():
        if line.startswith("G1") and ("E" in line):
            extrusions += 1
        elif line.startswith(";LAYER_CHANGE"):
            layers += 1
        elif line.startswith("; filament used [mm] ="):
            filament = line.split("=", 1)[1].strip()
    return {"lines": len(gcode.splitlines()), "extrusions": extrusions, "layers": layers, "filament_mm": filament}


CASES = [
    {"name": "cube_default", "model": "cube_20.stl", "settings": {}},
    {"name": "cube_fine_0p10", "model": "cube_20.stl", "settings": {"layer_height": 0.1}},
    {"name": "cube_coarse_0p30", "model": "cube_20.stl", "settings": {"layer_height": 0.3}},
    {"name": "cube_infill_40", "model": "cube_20.stl", "settings": {"fill_density": {"value": 40.0, "is_percent": True}}},
    {"name": "cube_single_perimeter", "model": "cube_20.stl", "settings": {"perimeters": 1, "fill_density": {"value": 0.0, "is_percent": True}}},
    {"name": "cylinder_default", "model": "cylinder_r10_h20.stl", "settings": {}},
    {"name": "sphere_supports", "model": "sphere_r12.stl", "settings": {"support_material": "everywhere"}},
    {"name": "sphere_default", "model": "sphere_r12.stl", "settings": {}},
    {"name": "sphere_seam_nearest", "model": "sphere_r12.stl", "settings": {"seam_position": "nearest"}},
    {"name": "bridge_default", "model": "bridge_30.stl", "settings": {}},
    {"name": "bridge_supports", "model": "bridge_30.stl", "settings": {"support_material": "everywhere"}},
]

# Preset cases resolve a printer/process/filament triple through the engine's
# vendor bundle and slice with the resolved config, so a change in preset
# resolution (the bundle, inheritance, variant conditions) shows up here before
# it reaches a window. Resolution goes through the app's own --dump-config path,
# which is the --runner qt surface; other runners do not exercise it.
PRESET_CASES = [
    {
        "name": "mk4_speed_pla",
        "model": "cube_20.stl",
        "preset": {
            "printer": "Prusa MK4 0.4",
            "process": "0.20mm SPEED @MK4 0.4",
            "filament": "Prusament PLA @MK4 0.4",
        },
    },
]

ALL_CASES = CASES + PRESET_CASES


def build_config(case, base):
    config = json.loads(json.dumps(base))
    print_settings = config["configuration"]["print_settings"]
    for key, value in case["settings"].items():
        if key not in print_settings:
            raise SystemExit(f"case {case['name']}: '{key}' is not a print setting in the base config")
        print_settings[key] = value
    return config


def slice_case(binary, case, base, runner):
    model = MODELS / case["model"]
    if not model.is_file():
        raise SystemExit(f"missing model {model}; run gen_models.py first")
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        gcode_path = tmpdir / "out.gcode"
        preset = case.get("preset")

        if preset is not None:
            resolved = tmpdir / "resolved.json"
            resolve = [
                str(binary),
                "--printer", preset["printer"],
                "--process", preset["process"],
                "--filament", preset["filament"],
                "--dump-config", str(resolved),
            ]
            result = subprocess.run(
                resolve,
                cwd=PROJECT / "upstream",
                capture_output=True,
                text=True,
                timeout=600,
            )
            if result.returncode != 0 or not resolved.is_file():
                tail = "\n".join(result.stderr.splitlines()[-12:])
                raise SystemExit(f"case {case['name']}: preset resolve failed\n{tail}")
            config_path = resolved
        else:
            config_path = tmpdir / "config.json"
            config_path.write_text(json.dumps(build_config(case, base)))

        if runner == "launcher":
            command = [str(binary), str(model), "--load", str(config_path), "-g", "--output", str(gcode_path)]
        elif runner == "qt":
            command = [str(binary), str(model), "--slice", f"{config_path},{gcode_path}"]
        else:
            command = [str(binary), str(model), str(config_path), str(gcode_path)]
        result = subprocess.run(
            command,
            cwd=PROJECT / "upstream",
            capture_output=True,
            text=True,
            timeout=600,
        )
        if result.returncode != 0 or not gcode_path.is_file():
            tail = "\n".join(result.stderr.splitlines()[-12:])
            raise SystemExit(f"case {case['name']}: slicing failed\n{tail}")
        return gcode_path.read_text(errors="replace")


def capture(binary, label, runner):
    base = json.loads(CONFIG.read_text())
    BASELINE.mkdir(parents=True, exist_ok=True)
    gcode_dir = BASELINE / "gcode"
    gcode_dir.mkdir(exist_ok=True)
    try:
        shown = binary.relative_to(PROJECT)
    except ValueError:
        shown = binary
    manifest = {"label": label, "binary": str(shown), "cases": {}}
    for case in ALL_CASES:
        if case.get("preset") is not None and runner != "qt":
            print(f"  SKIPPED  {case['name']:<22} (preset cases need --runner qt)")
            continue
        first = normalize(slice_case(binary, case, base, runner))
        second = normalize(slice_case(binary, case, base, runner))
        if first != second:
            raise SystemExit(f"case {case['name']}: slicing is not deterministic, oracle cannot run")
        digest = sha256(first)
        (gcode_dir / f"{case['name']}.gcode").write_text(first)
        entry = {
            "model": case["model"],
            "settings": case.get("settings", {}),
            "sha256": digest,
            "stats": stats(first),
        }
        if case.get("preset") is not None:
            entry["preset"] = case["preset"]
        manifest["cases"][case["name"]] = entry
        info = manifest["cases"][case["name"]]["stats"]
        print(f"  captured {case['name']:<22} {digest[:16]}  {info['layers']}L {info['extrusions']}E")
    (BASELINE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"baseline written: {BASELINE / 'manifest.json'}")


def verify(binary, runner):
    manifest_path = BASELINE / "manifest.json"
    if not manifest_path.is_file():
        raise SystemExit("no baseline; run capture first")
    manifest = json.loads(manifest_path.read_text())
    base = json.loads(CONFIG.read_text())
    failures = 0
    skipped = 0
    for case in ALL_CASES:
        if case.get("preset") is not None and runner != "qt":
            print(f"  SKIPPED  {case['name']:<22} (preset cases need --runner qt)")
            skipped += 1
            continue
        expected = manifest["cases"].get(case["name"])
        if expected is None:
            print(f"  MISSING  {case['name']} (not in baseline)")
            failures += 1
            continue
        try:
            actual = normalize(slice_case(binary, case, base, runner))
        except SystemExit as error:
            print(f"  FAIL     {case['name']}: {error}")
            failures += 1
            continue
        digest = sha256(actual)
        if digest == expected["sha256"]:
            print(f"  ok       {case['name']:<22} {digest[:16]}")
        else:
            failures += 1
            produced = BASELINE / "gcode" / f"{case['name']}.actual.gcode"
            produced.write_text(actual)
            print(f"  CHANGED  {case['name']:<22} {expected['sha256'][:16]} -> {digest[:16]}")
            print(f"           diff -u {BASELINE / 'gcode' / (case['name'] + '.gcode')} {produced}")
    total = len(ALL_CASES) - skipped
    print(f"\n{total - failures}/{total} match baseline '{manifest['label']}'")
    return 0 if failures == 0 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=["capture", "verify"])
    parser.add_argument("--bin", default=str(DEFAULT_BIN), help="slicer binary to exercise")
    parser.add_argument("--label", default="engine-3.0.0-alpha12", help="name for this baseline")
    parser.add_argument("--runner", choices=["launcher", "app", "qt"], default="launcher", help="argument style the binary expects")
    args = parser.parse_args()
    binary = Path(args.bin).resolve()
    if not binary.is_file():
        raise SystemExit(f"no such binary: {binary}")
    if args.action == "capture":
        capture(binary, args.label, args.runner)
        return 0
    return verify(binary, args.runner)


if __name__ == "__main__":
    sys.exit(main())
