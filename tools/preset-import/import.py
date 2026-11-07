#!/usr/bin/env python3
"""Import a user's own reference-slicer presets into a next-layer vendor bundle.

That project keeps a user's edits as thin overrides that inherit a vendor profile
(``"inherits": "Voron 2.4 350 0.4 nozzle"``). next-layer resolves presets by
name out of a vendor bundle, and the engine's own vendor bundle has no
Voron in it at all -- which is how a print went out with a 220 mm bed profile
on a 350 mm machine.

So importing is three steps: resolve each user preset against the reference
installation the user actually runs (system bundles plus their own overrides),
translate it with ``tools/preset-convert``, and write a bundle whose presets carry
the user's own names. A preset that cannot be translated cleanly is reported,
and one that cannot slice is removed rather than shipped.

Usage:
    import.py                                       # Voron, from ~/.config/<the reference slicer>
    import.py --vendor Voron --out DIR --dry-run
    import.py --list                                # user presets and their bases
"""

import argparse
import re
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "preset-convert"))
sys.path.insert(0, str(REPO / "tools" / "printer-catalog"))

import convert as preset_convert  # noqa: E402
import generate as catalog  # noqa: E402

USER_KINDS = (("machine", "printer"), ("process", "print"), ("filament", "filament"))
META_KEYS = preset_convert.META


def preset_index(source_root, vendor):
    """Every preset the installation knows, by name; the user's own files win."""
    index = {}
    for pattern in ("system/**/*.json", "user/default/*/*.json"):
        for path in sorted(glob.glob(str(source_root / pattern), recursive=True)):
            try:
                data = json.loads(Path(path).read_text(encoding="utf-8-sig", errors="replace"))
            except (OSError, ValueError):
                continue
            for entry in data if isinstance(data, list) else [data]:
                if isinstance(entry, dict) and entry.get("name"):
                    index[entry["name"]] = entry
    return index


def resolve(name, index, seen=None):
    """Merge a preset with its inherits ancestors, the child winning."""
    seen = seen or set()
    entry = index.get(name)
    if entry is None or name in seen:
        return {}
    seen.add(name)
    merged = {}
    parent = entry.get("inherits")
    if parent:
        merged.update(resolve(parent, index, seen))
    merged.update({key: value for key, value in entry.items() if key != "inherits"})
    return merged


def user_presets(source_root):
    found = []
    for kind, target in USER_KINDS:
        for path in sorted(glob.glob(str(source_root / "user/default" / kind / "*.json"))):
            data = json.loads(Path(path).read_text(encoding="utf-8-sig", errors="replace"))
            if isinstance(data, dict) and data.get("name"):
                found.append((kind, target, data["name"]))
    return found


def family_label(name, nozzle):
    """The picker shows "<family> <nozzle>", so the family is the name without it."""
    label = re.sub(r"\s*\b" + re.escape(nozzle) + r"\b\s*(nozzle)?", "", name)
    return label.strip(" -") or name


def model_documents(label, model_id, tool):
    """A printer model and its config, in the two shapes vendor.yaml wants."""
    family = "- label: %s\n  base_model: %s\n" % (catalog.scalar(label), model_id)
    printer = (
        "\n---\nkind: printer\ntechnology: FFF\nname: %s\nid: %s\nmodel:\n"
        "  base_model: %s\n  model: %s_MODEL\ntool_count: 1\nfeatures: {}\n"
        % (catalog.scalar(label), model_id, model_id, model_id))
    config = (
        "\n---\nkind: printer_config\nid: %s\nname: %s\nprinter: %s\n"
        "legacy_printer_model:\n- %s\ntool_count: 1\ntools:\n- tool: %s\nsheet: pei_smooth\n"
        % (catalog.scalar(catalog.slugify(label)), catalog.scalar(label), model_id,
           catalog.scalar(label), catalog.scalar(tool)))
    return family, printer + config


def add_vendor_documents(vendor_directory, families, documents, tools):
    path = vendor_directory / "vendor.yaml"
    text = path.read_text()
    if families:
        boundary = text.find("\n---\n")
        if boundary >= 0:
            text = text[:boundary + 1] + "".join(families) + text[boundary + 1:]
        else:
            text = text + "".join(families)
    text += "".join(tools) + "".join(documents)
    path.write_text(text)


def existing_tools(text):
    return set(re.findall(r"^kind: tool\n(?:.*\n)*?id: '([^']+)'", text, re.M))


def tool_document(tool):
    return (
        "\n---\nkind: tool\ntechnology: FFF\nid: %s\nname: %s\nfeatures:\n"
        "  nozzle_diameter:\n    default: %s\n"
        % (catalog.scalar(tool), catalog.scalar(tool), tool))


def slice_verdict(binary, model, printer, process, filament):
    """A preset that cannot resolve and slice is not shipped."""
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    with tempfile.TemporaryDirectory() as directory:
        config = Path(directory) / "config.json"
        gcode = Path(directory) / "out.gcode"
        resolve_run = subprocess.run(
            [str(binary), str(model), "--printer", printer, "--process", process,
             "--filament", filament, "--dump-config", str(config)],
            env=environment, capture_output=True, text=True, timeout=600)
        if resolve_run.returncode != 0 or not config.is_file():
            return False, (resolve_run.stderr or resolve_run.stdout).strip().splitlines()[-1:] or ["no config"]
        slice_run = subprocess.run(
            [str(binary), str(model), "--slice", "%s,%s" % (config, gcode)],
            env=environment, capture_output=True, text=True, timeout=600)
        if slice_run.returncode != 0 or not gcode.is_file():
            return False, (slice_run.stderr or slice_run.stdout).strip().splitlines()[-1:] or ["no gcode"]
        layers = sum(1 for line in gcode.read_text(errors="replace").splitlines()
                     if line.startswith(";LAYER_CHANGE"))
        if layers == 0:
            return False, ["sliced no layers"]
    return True, ["%d layers" % layers]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from", dest="source", required=True, help="directory of reference presets to read")
    parser.add_argument("--vendor", default="Voron")
    parser.add_argument("--bundle-id", default="next-layer-catalog")
    parser.add_argument("--out", default=None)
    parser.add_argument("--bin", default=str(REPO / "upstream/build/src/next-layer-qt/next-layer-qt"))
    parser.add_argument("--model", default=str(REPO / "oracle/models/cube_20.stl"))
    parser.add_argument("--engine-source", default=str(REPO / (
        "upstream/src/slic3r-shared/src/Slic3r/Biz/Config/Legacy/PrintConfig.cpp")))
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--nozzle", default="0.4", help="only import machines with this nozzle")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--keep-failed", action="store_true",
                        help="leave presets in place that do not resolve and slice")
    args = parser.parse_args()

    source_root = Path(args.source)
    out = Path(args.out) if args.out else (
        Path.home() / ".local/share/next-layer/presets/local" / args.bundle_id)
    index = preset_index(source_root, args.vendor)
    presets = user_presets(source_root)

    if args.list:
        for kind, target, name in presets:
            print("%-9s %-34s inherits %s"
                  % (kind, name, index.get(name, {}).get("inherits", "-")))
        return 0

    if not (source_root / "system" / args.vendor).is_dir():
        print("no %s vendor bundle under %s/system" % (args.vendor, source_root))
        return 2

    binary = Path(args.bin)
    model = Path(args.model)
    allowed = catalog.reference_keys(binary, model)
    if not allowed["printer"]:
        print("the engine reported no printer settings; refusing to guess")
        return 2
    enums = catalog.engine_enums(Path(args.engine_source))
    placeholders = set()
    for keys in allowed.values():
        placeholders |= keys
    defaults = catalog.plain_values(
        catalog.engine_config(binary, model, ["--dump-config"]).get("printer_settings", {}))

    vendor_models = {entry.get("printer_model")
                     for name, entry in index.items()
                     if entry.get("type") == "machine"}

    if args.dry_run:
        print("would write %s" % out)

    stage = out
    if not args.dry_run:
        shutil.rmtree(stage / args.vendor, ignore_errors=True)
    vendor_directory = stage / args.vendor

    machines = []
    refused = []
    for kind, target, name in presets:
        if kind != "machine":
            continue
        data = resolve(name, index)
        if data.get("printer_model") not in vendor_models:
            continue
        nozzle = str(float(data.get("nozzle_diameter", ["0.4"])[0]))
        if nozzle != args.nozzle:
            continue
        if not data.get("printable_area"):
            refused.append("%s: no printable area" % name)
            continue
        machines.append((name, data))

    if not args.dry_run and machines:
        staged_root = Path(tempfile.mkdtemp(prefix="preset-import-"))
        try:
            shutil.copytree(source_root / "system" / args.vendor, staged_root / args.vendor)
            index_path = staged_root / (args.vendor + ".json")
            shutil.copy(source_root / "system" / (args.vendor + ".json"), index_path)
            vendor_index = json.loads(index_path.read_text(encoding="utf-8-sig"))
            for name, data in machines:
                relative = "machine/%s.json" % name
                target = staged_root / args.vendor / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(data, indent=1) + "\n")
                vendor_index.setdefault("machine_list", []).append(
                    {"name": name, "sub_path": relative})
            index_path.write_text(json.dumps(vendor_index, indent=1) + "\n")
            stage.mkdir(parents=True, exist_ok=True)
            catalog_result = subprocess.run(
                [sys.executable, str(REPO / "tools/printer-catalog/generate.py"),
                 "--from", str(staged_root), "--vendors", args.vendor,
                 "--out", str(stage), "--nozzle", args.nozzle,
                 "--bin", str(binary), "--model", str(model),
                 "--engine-source", str(args.engine_source)],
                capture_output=True, text=True, timeout=1800)
        finally:
            shutil.rmtree(staged_root, ignore_errors=True)
        if catalog_result.returncode != 0:
            print(catalog_result.stdout)
            print(catalog_result.stderr)
            return 2
        for line in catalog_result.stdout.splitlines():
            if line.strip():
                print("catalog: " + line.strip())

    written = {"printer": 0, "print": 0, "filament": 0}
    dropped = {}
    print_names, filament_names = [], []

    for kind, target, name in presets:
        data = resolve(name, index)
        if kind == "machine":
            continue
        values, bucket = catalog.converted(data, target, allowed[target], enums)
        if not values:
            refused.append("%s: nothing translated" % name)
            continue
        written[target] += 1
        if args.dry_run:
            continue
        document = ["kind: " + target, "id: " + str(uuid.uuid4()),
                    "name: " + catalog.scalar(name), "values:"]
        document.extend(catalog.emit_mapping(values, 2))
        slug = catalog.slugify(name)
        (vendor_directory / ("preset-" + target + "-" + slug + ".yaml")).write_text(
            "\n".join(document) + "\n")
        (print_names if target == "print" else filament_names).append(name)
        if bucket["unsupported"]:
            dropped[name] = sorted(set(bucket["unsupported"]))

    if args.dry_run:
        print("dry run: %d printer, %d print, %d filament presets from %d user files"
              % (len(machines), written["print"], written["filament"], len(presets)))
        return 0

    machine_names = []
    for name, _ in machines:
        label = catalog.NAME_OVERRIDES.get(catalog.strip_nozzle(name, args.nozzle),
                                           catalog.strip_nozzle(name, args.nozzle))
        machine_names.append(label + " " + args.nozzle)

    print("\nwrote %s" % stage)
    print("  printers:  %s" % (", ".join(machine_names) or "-"))
    print("  prints:    %s" % (", ".join(print_names) or "-"))
    print("  filaments: %s" % (", ".join(filament_names) or "-"))
    for name in sorted(dropped):
        print("  %s: %d key(s) the engine has no setting for, dropped" % (name, len(dropped[name])))
    for line in refused:
        print("  refused " + line)

    if print_names and filament_names and machine_names:
        failures = []
        for printer in machine_names:
            for process in print_names:
                for filament in filament_names:
                    ok, detail = slice_verdict(binary, model, printer, process, filament)
                    print("  %-4s %s / %s / %s: %s"
                          % ("ok" if ok else "FAIL", printer, process, filament,
                             ", ".join(detail)))
                    if not ok:
                        failures.append((process, filament))
        for name in (set() if args.keep_failed
                     else {item for pair in failures for item in pair}):
            for path in vendor_directory.glob("preset-*-*.yaml"):
                if path.name.startswith(("preset-print-" + catalog.slugify(name),
                                         "preset-filament-" + catalog.slugify(name))):
                    path.rename(path.with_suffix(".yaml.disabled"))
                    print("  disabled %s: it does not slice" % path.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
