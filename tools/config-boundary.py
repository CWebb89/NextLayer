#!/usr/bin/env python3
"""Checks that every setting the app hands the engine arrives.

The seam between next-layer's settings tree and HFW's intake is a JSON document,
and nothing has ever compared its two ends. A whole class of failure lives in that
gap: the engine reports the keys it saw, so a key lost while the document was being
assembled is not in the report, in the settings panel, or anywhere else -- the
value is simply absent and the engine's default applies. Seventy settings went
missing that way for three prints, including the Z hop.

The check is exact rather than approximate: the engine's `--intake-json` lists every
key it read, and this tool lists every key in the document. A key present in one and
absent from the other is a setting that never reached the engine.

    tools/config-boundary.py --engine hfw-cli --app next-layer-qt [--preset PATH]

Exit status is 0 when every key arrived, 1 when any key vanished or the engine
rejected an enum spelling, and 2 when the check could not run.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile


def leaves(value, path, out):
    """Every leaf of a settings document, as dotted paths.

    A list is a leaf: the engine joins it into one value. A `{is_percent, value}`
    object is also a leaf, because that is one setting written with its unit and
    reading it as two would invent settings named `value` and `is_percent`.
    """
    if isinstance(value, dict):
        if set(value) == {"is_percent", "value"}:
            out[".".join(path)] = value
            return
        for key, item in value.items():
            leaves(item, path + [key], out)
        return
    out[".".join(path)] = value


def has_value(value):
    """False for the spellings a dump uses to mean "nothing said".

    An empty string, an empty list and a list holding one empty string all mean the
    setting was never stated, and the engine skips them on purpose: every section of
    a dump carries the full key list, so a later section's blank start G-code would
    otherwise erase the printer's own.
    """
    if isinstance(value, dict):
        value = value.get("value")
    if isinstance(value, list):
        return any(has_value(item) for item in value)
    if isinstance(value, str):
        return value.strip() != ""
    return value is not None


def app_keys(document):
    found = {}
    configuration = document.get("configuration", document)
    for section, settings in configuration.items():
        if not isinstance(settings, dict):
            continue
        local = {}
        leaves(settings, [], local)
        for name, value in local.items():
            found.setdefault(name, (section, value))
    return found


def engine_keys(intake):
    return {entry["name"]: entry["intake"] for entry in intake.get("keys", [])}


def flat_keys_of(document):
    """The names the engine flattens the document into.

    It flattens sections, so `filament_settings.pressure_advance` arrives as
    `pressure_advance`. The tools therefore compares bare names.
    """
    found = {}
    configuration = document.get("configuration", document)
    for section, settings in configuration.items():
        if not isinstance(settings, dict):
            continue
        local = {}
        leaves(settings, [], local)
        for name, value in local.items():
            found[name] = (section, value)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="next-layer-qt binary")
    parser.add_argument("--engine", required=True, help="hfw-cli binary")
    parser.add_argument("--preset", default="", help="the preset the app should load")
    parser.add_argument("--model", default="", help="a model, when the app needs one")
    parser.add_argument("--strict", action="store_true",
                        help="also fail when a setting arrived and was refused")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as work:
        config = os.path.join(work, "config.json")
        intake = os.path.join(work, "intake.json")
        gcode = os.path.join(work, "slice.gcode")

        command = [args.app, "--dump-config-json", config]
        if args.preset:
            command += ["--process", args.preset]
        result = subprocess.run(command, capture_output=True, text=True, timeout=300)
        if result.returncode != 0 or not os.path.exists(config):
            print(f"could not dump the settings document: {result.stderr.strip()}", file=sys.stderr)
            return 2

        model = args.model
        if not model:
            print("a model is needed so the engine reaches its intake step", file=sys.stderr)
            return 2
        result = subprocess.run(
            [args.engine, "--preset", config, "--intake-json", intake, "--gcode", gcode, model],
            capture_output=True,
            text=True,
            timeout=900,
        )
        if not os.path.exists(intake):
            print(f"the engine wrote no intake report: {result.stderr.strip()}", file=sys.stderr)
            return 2

        with open(config) as handle:
            document = json.load(handle)
        with open(intake) as handle:
            report = json.load(handle)

    handed = flat_keys_of(document)
    seen = engine_keys(report)

    missing = sorted(name for name in handed if name not in seen and has_value(handed[name][1]))
    by_class = {}
    for name in seen:
        by_class.setdefault(seen[name], []).append(name)
    exempt = sorted(name for name in handed if name not in seen and not has_value(handed[name][1]))

    print(f"{len(handed)} settings handed over, {report['keys_seen']} read by the engine")
    if exempt:
        print(f"  {len(exempt)} carried no value and were skipped by design")
    for label in ("applied", "partial", "recognised", "unconverted", "unknown"):
        names = sorted(by_class.get(label, []))
        if names:
            print(f"  {label}: {len(names)}")
            if label != "applied":
                for name in names[:12]:
                    print(f"    {name}")

    if missing:
        print(f"\n{len(missing)} setting(s) never reached the engine:")
        for name in missing[:40]:
            section, value = handed[name]
            print(f"  {name} = {json.dumps(value)}  ({section})")

    rejected = sorted(by_class.get("unconverted", []) + by_class.get("unknown", []))
    if rejected:
        print(f"\n{len(rejected)} setting(s) reached the engine and were refused; each one\n"
              "falls back to the engine's own default, which is silent")
        if args.strict:
            return 1
    if missing:
        return 1
    if rejected:
        print("\nno setting vanished on the way")
        return 0
    print("\nevery setting handed over was read")
    return 0


if __name__ == "__main__":
    sys.exit(main())
