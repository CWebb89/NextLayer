#!/usr/bin/env python3
"""Build next-layer printer bundles from an OrcaSlicer profile checkout.

Orca ships one directory per vendor (``resources/profiles/<Vendor>/``) holding
machine, process and filament JSON files plus an index (``<Vendor>.json``) that
lists them. next-layer discovers printers from vendor bundles: a repo directory
under a bundle root, one directory per vendor inside it, and that vendor's
``vendor.yaml`` describing its printer families plus one preset file per printer.

This tool writes such a bundle from Orca's profiles. Machine keys are translated
to the engine's vocabulary by ``tools/orca-convert``; anything that cannot be
translated is dropped and reported rather than invented.

Usage:
    generate.py --orca ~/dev/reference/OrcaSlicer/resources/profiles \\
                --out ../../upstream/resources/presets/orca-catalog \\
                --vendors Creality,Elegoo,Voron

    generate.py --list                    # vendors and machine counts
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "orca-convert"))
import convert as orca  # noqa: E402

BUNDLE_ID = "orca-catalog"
NOZZLE = "0.4"
KINDS = ("printer", "print", "filament")
SETTINGS_SECTION = {"printer": "printer_settings", "print": "print_settings",
                    "filament": "filament_settings"}

ENUM_DECLARATION = re.compile(r'add\("([a-z0-9_]+)"[,)]')
ENUM_VALUES = re.compile(r'make_pair\("([^"]+)"')


def engine_enums(source):
    """Enum vocabularies from the engine's own option definitions.

    A value outside its enum is not a soft error in this engine — it panics —
    so nothing may be emitted that the option does not accept.
    """
    text = source.read_text(errors="replace")
    declarations = list(ENUM_DECLARATION.finditer(text))
    vocabularies = {}
    for index, declaration in enumerate(declarations):
        boundary = (declarations[index + 1].start() if index + 1 < len(declarations)
                    else len(text))
        block = text[declaration.end():boundary]
        setter = block.find("set_enum<")
        if setter < 0:
            continue
        close = block.find("});", setter)
        if close < 0:
            continue
        vocabulary = set(ENUM_VALUES.findall(block[setter:close]))
        if vocabulary:
            vocabularies[declaration.group(1)] = vocabulary
    return vocabularies


def plain_values(mapping):
    """Scalars only: a preset value may not be a YAML mapping."""
    values = {}
    for key, value in mapping.items():
        if isinstance(value, dict):
            if "value" in value:
                values[key] = value["value"]
            continue
        values[key] = value
    return values


def engine_config(binary, model, action):
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "config.json"
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        subprocess.run([str(binary)] + action + [str(output)],
                       check=True, env=environment, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=300)
        return json.loads(output.read_text())["configuration"]


def borrowed_templates(directory):
    """The engine's own process, material and tool presets, as written by the app.

    Orca's process presets do not survive the trip: their extrusion widths and
    heights collide with engine options that were never equivalent, and the
    slice aborts in new_from_config_width. Until that conversion is done
    properly, a generated vendor borrows these, so a printer from the catalog
    slices out of the box. They are the exact files the app itself writes when it
    adds a custom printer, kept here so generation needs no user data.
    """
    return {kind: (directory / (kind + ".yaml")).read_text()
            for kind in ("print", "filament", "tool_print")}


def reference_keys(binary, model):
    """The settings the engine's own config carries, per container.

    A preset may only set keys its container knows. Asking the engine for a
    resolved config is the one source that cannot drift from the build.
    """
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "config.json"
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        subprocess.run([str(binary), str(model), "--dump-config", str(output)],
                       check=True, env=environment, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=300)
        configuration = json.loads(output.read_text())["configuration"]
    return {kind: set(configuration.get(SETTINGS_SECTION[kind], {})) for kind in KINDS}

def slugify(text):
    slug = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return slug or "printer"


def scalar(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(round(value, 6))
    text = str(value)
    if "\n" in text or "\r" in text:
        escaped = text.replace("\\", "\\\\").replace('"', '\\"')
        escaped = escaped.replace("\n", "\\n").replace("\r", "")
        return '"' + escaped + '"'
    return "'" + text.replace("'", "''") + "'"


def emit_mapping(mapping, indent):
    lines = []
    pad = " " * indent
    for key in sorted(mapping):
        value = mapping[key]
        if isinstance(value, list):
            if not value:
                lines.append(pad + key + ": []")
                continue
            lines.append(pad + key + ":")
            for item in value:
                if isinstance(item, list):
                    lines.append(pad + "- " + "[" + ", ".join(scalar(v) for v in item) + "]")
                else:
                    lines.append(pad + "- " + scalar(item))
        elif isinstance(value, dict):
            lines.append(pad + key + ":")
            lines.extend(emit_mapping(value, indent + 2))
        else:
            lines.append(pad + key + ": " + scalar(value))
    return lines


def index_of(orca_root, vendor):
    path = orca_root / (vendor + ".json")
    if not path.is_file():
        return {}
    return json.loads(path.read_text(errors="replace"))


def entries(index, key):
    return {entry["name"]: entry["sub_path"] for entry in index.get(key, [])}


def machine_presets(vendor_dir, vendor, index, nozzle, machines_filter=None):
    """Machine presets (not models) whose name carries the wanted nozzle."""
    wanted = []
    for name, sub in entries(index, "machine_list").items():
        if nozzle and nozzle not in name:
            continue
        if machines_filter and not any(token in name for token in machines_filter):
            continue
        if not (vendor_dir / sub).is_file():
            continue
        wanted.append((name, vendor_dir / sub))
    return wanted


def resolved(path):
    return orca.resolve_inherits(path)


def strip_nozzle(name, nozzle):
    for suffix in (" " + nozzle + " nozzle", " " + nozzle):
        if nozzle and name.endswith(suffix):
            return name[: -len(suffix)].strip()
    return name.strip()


def converted(data, kind, allowed, enums, drop=()):
    bucket = {"renamed": {}, "unsupported": []}
    values = {}
    for target, value in orca.convert_profile(data, bucket):
        if target is None or target in drop or value is None:
            continue
        if isinstance(value, dict) and "value" in value:
            value = value["value"]
        if allowed and target not in allowed:
            bucket["unsupported"].append(target)
            continue
        vocabulary = enums.get(target)
        if vocabulary is not None and str(value) not in vocabulary:
            bucket["unsupported"].append(target)
            continue
        values[target] = value
    return values, bucket


def bed_points(value):
    tokens = value if isinstance(value, list) else str(value).split(",")
    points = []
    for token in tokens:
        x, _, y = str(token).strip().partition("x")
        points.append(str(int(float(x))) + "x" + str(int(float(y))))
    return points


PRINTER_DROP = {
    "printer_variant",
    "nozzle_diameter",
    "default_print_profile",
    "default_filament_profile",
    "printable_area",
    "bed_shape",
    "thumbnails",
    "thumbnails_format",
}

POINT_LIST = {"extruder_offset"}

GCODE_KEYS = ("start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode",
              "toolchange_gcode", "between_objects_gcode")

PLACEHOLDER_RENAMES = {
    "bed_temperature_initial_layer_single": "first_layer_bed_temperature",
    "bed_temperature_initial_layer": "first_layer_bed_temperature",
    "nozzle_temperature_initial_layer": "first_layer_temperature",
    "nozzle_temperature": "temperature",
    "initial_layer_print_height": "first_layer_height",
    "retraction_length": "retract_length",
    "retraction_speed": "retract_speed",
    "z_hop": "retract_lift",
}

SPECIAL_PLACEHOLDERS = {
    "layer_num", "layer_z", "max_layer_z", "total_layer_count", "current_extruder",
    "next_extruder", "previous_extruder", "initial_extruder", "initial_tool",
    "num_extruders", "input_filename_base", "printer_preset", "print_preset",
    "filament_preset", "extruded_weight_total", "timestamp", "version",
    "first_layer_print_min", "first_layer_print_max",
}

TOKEN = re.compile(r"\[(?:([a-z0-9_]+)|([^\[\]]*))\]")


def translate_placeholders(text):
    def replace(match):
        name = match.group(1)
        if name is None:
            return match.group(0)
        return "[" + PLACEHOLDER_RENAMES.get(name, name) + "]"
    return TOKEN.sub(replace, text)


def unknown_placeholders(text, known):
    names = set()
    for expression in TOKEN.findall(text):
        candidate = expression[0] or expression[1]
        if not candidate or candidate.isdigit():
            continue
        if candidate in known or candidate in SPECIAL_PLACEHOLDERS:
            continue
        names.add(candidate)
    return names


def as_point_list(value):
    items = value if isinstance(value, list) else [value]
    return [str(item) for item in items]


def printer_values(data, machine_name, vendor, nozzle_float, allowed, enums,
                   known_placeholders, defaults):
    values, bucket = converted(data, "printer", allowed, enums, PRINTER_DROP)
    printable = data.get("printable_area")
    if printable:
        values["bed_shape"] = bed_points(printable)
    values["printer_model"] = machine_name
    values["printer_technology"] = "FFF"
    values["printer_vendor"] = vendor
    # Absolute E distances make this build export nothing at all, and relative E
    # is what Klipper and current Marlin expect anyway; the engine's own default.
    values["use_relative_e_distances"] = True
    for key in POINT_LIST & set(values):
        values[key] = as_point_list(values[key])
    dropped = set()
    for key in GCODE_KEYS:
        if not isinstance(values.get(key), str):
            continue
        translated = translate_placeholders(values[key])
        if not translated.strip():
            del values[key]
            continue
        unknown = unknown_placeholders(translated, known_placeholders)
        if unknown:
            dropped.update(unknown)
            del values[key]
            continue
        if "{" in translated or "}" in translated:
            dropped.add("brace expressions")
            del values[key]
            continue
        values[key] = translated
    # An empty layer-change template stops this build exporting at all, so where
    # the vendor has none, keep the engine's own.
    for key in ("before_layer_gcode", "layer_gcode"):
        if not str(values.get(key, "")).strip() and defaults.get(key):
            values[key] = defaults[key]
    return values, bucket, sorted(dropped)


def vendor_manifest(vendor_id, vendor_label, machines, nozzle):
    lines = [
        "kind: vendor",
        "id: " + scalar(vendor_id),
        "name: " + scalar(vendor_label),
        "version: 1.0.0",
        "features:",
        "  printer: {}",
        "  tool: {}",
        "  feeder: {}",
        "  sheet: {}",
        "printer_families:",
    ]
    for machine in machines:
        lines.append("- label: " + scalar(machine["name"]))
        lines.append("  base_model: " + machine["id"])
    lines.append("---")
    lines.append("kind: tool")
    lines.append("technology: FFF")
    lines.append("id: " + scalar(nozzle))
    lines.append("name: " + scalar(nozzle))
    lines.append("features:")
    lines.append("  nozzle_diameter:")
    lines.append("    default: " + nozzle)
    lines.append("---")
    lines.append("kind: sheet")
    lines.append("id: pei_smooth")
    lines.append("name: PEI Smooth")
    lines.append("type: pei_smooth")
    for machine in machines:
        lines.append("---")
        lines.append("kind: printer")
        lines.append("technology: FFF")
        lines.append("name: " + scalar(machine["name"]))
        lines.append("id: " + machine["id"])
        lines.append("model:")
        lines.append("  base_model: " + machine["id"])
        lines.append("  model: " + machine["id"] + "_MODEL")
        lines.append("tool_count: 1")
        lines.append("features: {}")
        lines.append("---")
        lines.append("kind: printer_config")
        lines.append("id: " + machine["config_id"])
        lines.append("name: " + scalar(machine["name"]))
        lines.append("printer: " + machine["id"])
        lines.append("legacy_printer_model:")
        lines.append("- " + scalar(machine["name"]))
        lines.append("tool_count: 1")
        lines.append("tools:")
        lines.append("- tool: " + scalar(nozzle))
        lines.append("sheet: pei_smooth")
    return "\n".join(lines) + "\n"


def preset_document(kind, name, values, condition=None):
    lines = ["kind: " + kind, "id: " + scalar("nl-" + slugify(name)), "name: " + scalar(name)]
    if condition:
        lines.append("condition: " + condition)
    lines.append("values:")
    lines.extend(emit_mapping(values, 2))
    return "\n".join(lines) + "\n"


def build_vendor(orca_root, vendor_dir, vendor, out_root, nozzle, nozzle_float, allowed,
                 enums, borrowed, placeholders, engine_defaults, report, machines_filter=None):
    index = index_of(orca_root, vendor)
    vendor_slug = slugify(vendor)
    cache = {}
    machines, unsupported = [], Counter()

    for preset_name, path in machine_presets(vendor_dir, vendor, index, nozzle, machines_filter):
        data = orca.resolve_inherits(path, cache)
        name = strip_nozzle(preset_name, nozzle)
        slug = slugify(name)
        if not slug.startswith(vendor_slug):
            slug = vendor_slug + "-" + slug
        values, bucket, dropped_gcode = printer_values(data, name, vendor, nozzle_float,
                                                      allowed["printer"], enums, placeholders,
                                                      engine_defaults)
        if dropped_gcode:
            report.append("%s: start/end G-code not imported (unknown placeholders: %s)"
                          % (name, ", ".join(dropped_gcode)))
        if "bed_shape" not in values:
            report.append("skip %s: no printable area" % name)
            continue
        unsupported.update(bucket["unsupported"])
        machines.append({"name": name, "slug": slug, "values": values,
                         "id": "NL_" + slug, "config_id": "nl-" + slug})

    if not machines:
        return None

    directory = out_root / vendor
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "vendor.yaml").write_text(
        vendor_manifest(vendor, vendor, machines, nozzle))
    (out_root / (vendor + ".idx")).write_text(
        "min_slic3r_version = 3.0.0-alpha0\n1.0.0 Generated from OrcaSlicer profiles\n")
    for machine in machines:
        (directory / ("preset-printer-" + machine["slug"] + ".yaml")).write_text(
            preset_document("printer", machine["name"], machine["values"],
                            'printer.model == "%s_MODEL"' % machine["id"]))

    (directory / "preset-print-standard.yaml").write_text(borrowed["print"])
    (directory / "preset-filament-generic.yaml").write_text(borrowed["filament"])
    (directory / "preset-tool-standard.yaml").write_text(borrowed["tool_print"])

    return {"vendor": vendor, "machines": len(machines), "unsupported": unsupported}


def vendor_directories(orca_root):
    found = []
    for directory in sorted(orca_root.iterdir()):
        if not directory.is_dir():
            continue
        if not (orca_root / (directory.name + ".json")).is_file():
            continue
        found.append(directory)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--orca", default=str(Path.home() / "dev/reference/OrcaSlicer/resources/profiles"))
    parser.add_argument("--out", default=str(Path(__file__).resolve().parents[2]
                                             / "upstream/resources/presets" / BUNDLE_ID))
    parser.add_argument("--vendors", help="comma-separated vendor directory names; default all")
    parser.add_argument("--machines", help="comma-separated substrings; only matching machine presets")
    parser.add_argument("--nozzle", default=NOZZLE)
    parser.add_argument("--bin", default=str(Path(__file__).resolve().parents[2]
                                            / "upstream/build/src/slic3r-app-qt/next-layer-qt"))
    parser.add_argument("--model", default=str(Path(__file__).resolve().parents[2]
                                              / "oracle/models/cube_20.stl"))
    parser.add_argument("--engine-source", default=str(Path(__file__).resolve().parents[2]
        / "upstream/src/slic3r-shared/src/Slic3r/Biz/Config/Legacy/PrintConfig.cpp"))
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    orca_root = Path(args.orca)
    directories = vendor_directories(orca_root)
    if args.list:
        for directory in directories:
            index = index_of(orca_root, directory.name)
            count = len(entries(index, "machine_list"))
            print("%-24s %d machines" % (directory.name, count))
        return 0

    wanted = None
    if args.vendors:
        wanted = {name.strip() for name in args.vendors.split(",") if name.strip()}
    out_root = Path(args.out)
    report, failed = [], []
    totals = {"machines": 0, "vendors": 0}
    allowed = reference_keys(args.bin, args.model)
    if not allowed["printer"]:
        print("the engine reported no printer settings; refusing to guess")
        return 2
    enums = engine_enums(Path(args.engine_source))
    borrowed = borrowed_templates(Path(__file__).resolve().parent / "templates")
    placeholders = set()
    for keys in allowed.values():
        placeholders |= keys
    engine_defaults = plain_values(
        engine_config(args.bin, args.model, ["--dump-config"]).get("printer_settings", {}))

    for directory in directories:
        vendor = directory.name
        if wanted is not None and vendor not in wanted:
            continue
        try:
            result = build_vendor(orca_root, directory, vendor, out_root, args.nozzle,
                                  float(args.nozzle.split()[0]), allowed, enums, borrowed,
                                  placeholders, engine_defaults, report,
                                  [token.strip() for token in (args.machines or "").split(",")
                                   if token.strip()])
        except Exception as error:  # a broken profile must not stop the catalog
            failed.append("%s: %s" % (vendor, error))
            continue
        if result is None:
            report.append("skip vendor %s: no convertible machines" % vendor)
            continue
        totals["machines"] += result["machines"]
        totals["vendors"] += 1
        print("%-22s %3d printers  %3d dropped keys"
              % (vendor, result["machines"], len(result["unsupported"])))

    print("\n%d vendors, %d printers into %s" % (totals["vendors"], totals["machines"], out_root))
    for line in report:
        print("  " + line)
    for line in failed:
        print("  FAILED " + line)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
