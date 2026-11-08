#!/usr/bin/env python3
"""Build next-layer printer bundles from a reference profile checkout.

That project ships one directory per vendor (``resources/profiles/<Vendor>/``) holding
machine, process and filament JSON files plus an index (``<Vendor>.json``) that
lists them. next-layer discovers printers from vendor bundles: a repo directory
under a bundle root, one directory per vendor inside it, and that vendor's
``vendor.yaml`` describing its printer families plus one preset file per printer.

This tool writes such a bundle from the reference's profiles. Machine keys are translated
to the engine's vocabulary by ``tools/preset-convert``; anything that cannot be
translated is dropped and reported rather than invented.

Usage:
    generate.py --from <reference checkout>/resources/profiles \\
                --out ../../upstream/resources/presets/vendor-catalog \\
                --vendors Creality,Elegoo,Voron

    generate.py --list                    # vendors and machine counts
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "preset-convert"))
import convert as preset_convert  # noqa: E402

BUNDLE_ID = "vendor-catalog"
NOZZLE = "0.4"

# The bundle id is also the directory name a preset store scans, and the scan
# order is alphabetical, so this name must sort after the bundle next-layer ships
# for its own printer. Measured: with an id sorting before `next-layer-custom`,
# "Voron 2.4 350 0.4" resolved into this catalog, whose Voron entry carries one
# generic filament instead of the nine the app's own bundle lists, and the
# filament gate failed. Renaming the display name alone does not prevent it.
# The engine ships its own vendor bundle, and preset resolution matches a
# hardware config by display name and then takes the first hit. Hardware config
# ids are fresh UUIDs on every load, so the map that orders them is re-shuffled
# each run: two bundles publishing the same name make resolution pick a
# different machine run to run (measured 5 failures in 12 identical
# invocations for the same printer name). Those machines come from that built-in
# bundle already, so the catalog does not publish them a second time.
SKIPPED_VENDORS = {"Prusa"}

# A vendor's filament list is mostly machine-specific colour and material
# combinations. The generic entries are the ones a printer needs to slice out of
# the box, and they are the same family the app's own bundle lists.
GENERIC_FILAMENT = re.compile(r"generic", re.IGNORECASE)
FILAMENT_SUFFIX = re.compile(r"\s*@.*$")
# A printer's first-run filament should be the one most of them use. The
# reference lists them alphabetically, so the fallback alone would hand a PLA
# machine a spool of ABS at 260 °C.
FILAMENT_PREFERENCE = (re.compile(r"generic pla\b", re.IGNORECASE),
                       re.compile(r"generic petg\b", re.IGNORECASE),
                       re.compile(r"generic\b", re.IGNORECASE))
NOZZLE_TOKEN = re.compile(r"0\.[0-9]+")

# Display names that would shadow a machine next-layer's own fixtures ship. The
# custom Voron 2.4 350 bundle is the default machine, so the reference's profiles for the
# same printer are kept but named apart.
NAME_OVERRIDES = {"Voron 2.4 350": "Voron 2.4 350 (imported)"}
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

    the reference's process presets do not survive the trip: their extrusion widths and
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


def index_of(source_root, vendor):
    path = source_root / (vendor + ".json")
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8-sig", errors="replace"))


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
    return preset_convert.resolve_inherits(path)


def strip_nozzle(name, nozzle):
    for suffix in (" " + nozzle + " nozzle", " " + nozzle):
        if nozzle and name.endswith(suffix):
            return name[: -len(suffix)].strip()
    return name.strip()


def converted(data, kind, allowed, enums, drop=()):
    bucket = {"renamed": {}, "unsupported": []}
    values = {}
    for target, value in preset_convert.convert_profile(data, bucket):
        if target is None or target in drop or value is None:
            continue
        if isinstance(value, dict) and "value" in value:
            value = value["value"]
        if allowed and target not in allowed:
            bucket["unsupported"].append(target)
            continue
        if target in preset_convert.DROP_KEYS:
            bucket["unsupported"].append(target)
            continue
        vocabulary = enums.get(target)
        if vocabulary is not None and str(value) not in vocabulary:
            bucket["unsupported"].append(target)
            continue
        if kind == "print" and _zero_means_default(target, value):
            bucket["unsupported"].append(target)
            continue
        values[target] = value
    return values, bucket


ZERO_DEFAULT_KEYS = re.compile(r"(^|_)(acceleration|speed|jerk)$|_extrusion_width$")


def _zero_means_default(key, value):
    """the reference writes 0 where it means "use the inherited value".

    Copying that zero through is not neutral in this engine: a zero acceleration
    reaches the time estimator and the slice dies in std::length_error.
    """
    if not isinstance(value, (int, float)) or value != 0:
        return False
    return bool(ZERO_DEFAULT_KEYS.search(key))


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
              "toolchange_gcode", "between_objects_gcode",
              "start_filament_gcode", "end_filament_gcode")

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
    "total_layer_count", "current_extruder", "initial_extruder", "initial_tool",
    "num_extruders", "input_filename_base", "extruded_weight_total", "timestamp",
    "version", "first_layer_print_min", "first_layer_print_max",
}

# Placeholders that exist later in the print than the start sequence does. In
# ``start_gcode`` the engine evaluates the template before any of this is known and
# reports one of these as a slicing error that writes no G-code at all, while the
# same name resolves in ``layer_gcode``, ``end_gcode`` or ``toolchange_gcode``.
# Measured one name at a time against the engine, not guessed.
START_UNAVAILABLE = {
    "layer_num", "layer_z", "max_layer_z", "next_extruder", "previous_extruder",
    "printer_preset", "print_preset", "filament_preset",
}

TOKEN = re.compile(r"\[(?:([a-z0-9_]+)|([^\[\]]*))\]")
BRACE = re.compile(r"\{([^{}]*)\}")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
QUOTED = re.compile(r'"[^"]*"|\'[^\']*\'')
LEGACY_DECLARATION = re.compile(r'this->add\("([a-z0-9_]+)",\s*(co[A-Za-z]+)\)')
VECTOR_TYPES = {"coFloats", "coInts", "coStrings", "coBools", "coEnums", "coPercents",
                "coPoints"}
BLOCK_OPEN = re.compile(r"\{[ \t]*if\b")
BLOCK_CLOSE = re.compile(r"\{[ \t]*endif\b")
ENGINE_OPTIONS = (Path(__file__).resolve().parents[2] / "upstream/src/slic3r-shared/src/Slic3r"
                  / "Biz/Config/Legacy/PrintConfig.cpp")
_VECTOR_SETTINGS = None
EXPRESSION_WORDS = {
    "if", "elsif", "else", "endif", "then", "and", "or", "not", "true", "false",
    "min", "max", "random", "digits", "zdigits", "int", "round", "ceil",
    "is_nil", "one_of", "empty", "size", "interpolate_table", "repeat",
}


def vector_settings():
    """Settings that hold one value per tool, plus the two print-extent placeholders.

    An expression may only read one of these with an index: the engine's parser
    refuses a bare reference to a vector-valued setting and writes no G-code at
    all, which is how a whole machine ends up unsliceable over one macro. The
    reference writes the bare form where its own parser tolerates it.

    Which settings are per-tool is not in the domain declarations — those name a
    per-tool option by its scalar element type — so it is read from the legacy
    option table, where the plural ``coInts``/``coFloats``/... spelling is the
    vector one.
    """
    global _VECTOR_SETTINGS
    if _VECTOR_SETTINGS is None:
        types = {}
        if ENGINE_OPTIONS.is_file():
            for match in LEGACY_DECLARATION.finditer(ENGINE_OPTIONS.read_text(errors="replace")):
                types[match.group(1)] = match.group(2)
        _VECTOR_SETTINGS = {name for name, kind in types.items() if kind in VECTOR_TYPES}
        _VECTOR_SETTINGS |= {"first_layer_print_min", "first_layer_print_max"}
    return _VECTOR_SETTINGS


def translate_placeholders(text):
    """Rename the reference's setting names to the ones this engine declares.

    A setting is named in two ways: ``[name]`` in G-code text and bare ``name``
    inside a ``{expression}``. Both have to be renamed, or a conditional over a
    renamed setting is dropped as if it referenced something unknown. A bare
    reference to a vector-valued setting is given index 0, because an unindexed
    one is a parse error and the first tool is the only one these profiles mean.
    """
    def rename(name):
        renamed = PLACEHOLDER_RENAMES.get(name)
        return name if renamed is None else renamed

    def replace_token(match):
        name = match.group(1)
        if name is None:
            return match.group(0)
        return "[" + rename(name) + "]"

    def replace_expression(match):
        body = match.group(1)
        pieces = []
        last = 0
        for found in IDENTIFIER.finditer(body):
            pieces.append(body[last:found.start()])
            name = rename(found.group(0))
            following = body[found.end():found.end() + 1]
            pieces.append(name + "[0]" if name in vectors and following != "[" else name)
            last = found.end()
        pieces.append(body[last:])
        return "{" + "".join(pieces) + "}"

    vectors = vector_settings()
    return BRACE.sub(replace_expression, TOKEN.sub(replace_token, text))


def known_for(key, known_placeholders):
    """Every name a template in this key may reference in this engine."""
    known = known_placeholders | SPECIAL_PLACEHOLDERS | START_UNAVAILABLE
    if key == "start_gcode":
        known = known - START_UNAVAILABLE
    return known


def unknown_placeholders(text, known):
    names = set()
    for expression in TOKEN.findall(text):
        candidate = expression[0] or expression[1]
        if not candidate or candidate.isdigit():
            continue
        if candidate in known:
            continue
        names.add(candidate)
    return names


def unknown_expression_names(text, known):
    names = set()
    for body in BRACE.findall(text):
        code = QUOTED.sub('""', body)
        for identifier in IDENTIFIER.findall(code):
            if identifier in known or identifier in EXPRESSION_WORDS:
                continue
            names.add(identifier)
    return names


def as_point_list(value):
    items = value if isinstance(value, list) else [value]
    return [str(item) for item in items]


EXTRUDER_RESET_LINE = re.compile(r"^[ \t]*[gG]92[ \t]*[eE](0(\.0*)?|\.0+)[ \t]*(;.*)?$")


def is_extruder_reset(line):
    return EXTRUDER_RESET_LINE.match(line) is not None


def align_extruder_reset(values):
    """Keep G92 E0 where the addressing mode allows it.

    The engine refuses to export at all when ``G92 E0`` sits in
    ``before_layer_gcode`` with absolute extruder addressing, or when relative
    addressing finds no reset anywhere. Both are one line in a vendor template,
    so the template is repaired rather than the machine dropped. A reset counts
    as its own line, which is how the engine reads it, so ``G92 E0.0`` and a
    trailing comment are resets too.
    """
    def without_reset(text):
        return "\n".join(line for line in text.splitlines() if not is_extruder_reset(line))

    if "use_relative_e_distances" in values and not values["use_relative_e_distances"]:
        for key in ("before_layer_gcode", "layer_gcode"):
            values[key] = without_reset(str(values.get(key, "")))
        return
    layer = str(values.get("layer_gcode", ""))
    before = str(values.get("before_layer_gcode", ""))
    if not any(is_extruder_reset(line) for line in (layer + "\n" + before).splitlines()):
        values["layer_gcode"] = (layer.rstrip() + "\nG92 E0\n").lstrip("\n")


def repair_gcode(values, known_placeholders):
    """Keep as much of a vendor G-code template as this engine can evaluate.

    One macro from a machine's own firmware is enough to make the engine write no
    G-code at all, so a template naming a setting this build does not have used to
    be dropped whole: the machine then printed with the engine's generic start
    sequence instead of its own priming and levelling. Only the lines carrying an
    unresolvable name are dropped now, and a conditional goes with them as one
    piece, because half of an ``{if}``/``{endif}`` pair is a parse error rather than
    a smaller version of the same thing.

    Returns which template went missing entirely and which one survived with lines
    removed, so the run report says what a machine lost instead of that it lost
    something.
    """
    dropped, salvaged = {}, {}
    for key in GCODE_KEYS:
        if not isinstance(values.get(key), str):
            continue
        translated = translate_placeholders(values[key])
        if not translated.strip():
            del values[key]
            continue
        kept, lost = salvage_lines(translated, known_for(key, known_placeholders))
        if not lost:
            values[key] = translated
            continue
        if kept.strip():
            values[key] = kept
            salvaged[key] = sorted(lost)
            continue
        dropped[key] = sorted(lost)
        del values[key]
    return dropped, salvaged


def note_gcode(result, scope, report):
    """Say what a preset lost, where it kept only part of a G-code template."""
    dropped, salvaged = result
    for key, unknown in dropped.items():
        report.append("%s: %s not imported (unknown placeholders: %s)"
                      % (scope, key, ", ".join(unknown)))
    for key, unknown in salvaged.items():
        report.append("%s: %s kept without the lines naming %s"
                      % (scope, key, ", ".join(unknown)))


def salvage_lines(text, known):
    """The template with the lines this engine cannot parse left out."""
    kept, lost = [], set()
    chunk, depth = [], 0

    def unresolvable(body):
        return unknown_placeholders(body, known) | unknown_expression_names(body, known)

    def settle(lines):
        body = "\n".join(lines)
        names = unresolvable(body)
        if names:
            lost.update(names)
        else:
            kept.extend(lines)

    for line in text.splitlines():
        chunk.append(line)
        depth += len(BLOCK_OPEN.findall(line)) - len(BLOCK_CLOSE.findall(line))
        if depth > 0:
            continue
        depth = 0
        settle(chunk)
        chunk = []
    if chunk:
        settle(chunk)
    return "\n".join(kept), lost


def printer_values(data, machine_name, vendor, nozzle_float, allowed, enums,
                   known_placeholders, defaults):
    values, bucket = converted(data, "printer", allowed, enums, PRINTER_DROP)
    printable = data.get("printable_area")
    if printable:
        values["bed_shape"] = bed_points(printable)
    values["printer_model"] = machine_name
    values["printer_technology"] = "FFF"
    values["printer_vendor"] = vendor
    # Relative E is what most of these machines want, and this build writes no
    # G-code at all with absolute E on a Klipper flavour. The Marlin flavours
    # are the exception: relative E with them writes no G-code either, so the
    # flavour decides and a machine that states its own preference keeps it.
    flavor = str(values.get("gcode_flavor", ""))
    values.setdefault("use_relative_e_distances", flavor not in ("marlin", "marlin2"))    # The reference sets this to mean "this machine can carry a multi-material
    # unit", a capability. In this engine it turns MMU printing on, and an MMU
    # slice with one tool writes no G-code at all. This app drives tool
    # changers, never an MMU.
    values["single_extruder_multi_material"] = False
    # An extruder offset is written in the engine's own `0x0` spelling even though its
    # reader reports a type mismatch for it on every load: the value is required for a
    # multi-extruder machine to slice at all (dropping it fails with "error 5
    # extruder_offset"), and the bundle loader rejects the nested-list spelling that
    # would avoid the warning. A warning beats a tool changer that cannot slice.
    for key in POINT_LIST & set(values):
        values[key] = as_point_list(values[key])
    dropped, salvaged = repair_gcode(values, known_placeholders)
    # An empty layer-change template stops this build exporting at all, so where
    # the vendor has none, keep the engine's own.
    for key in ("before_layer_gcode", "layer_gcode"):
        if not str(values.get(key, "")).strip() and defaults.get(key):
            values[key] = defaults[key]
    align_extruder_reset(values)
    return values, bucket, dropped, salvaged


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


def unique_machine_name(machines, name, values, report):
    """Disambiguate a machine name against the ones already emitted.

    Identical name and identical values is the same machine published twice by
    the vendor's index, which is dropped. A name clash with different settings
    is renamed, because the engine resolves presets by name.
    """
    for machine in machines:
        if machine["name"] != name:
            continue
        if machine["values"] == values:
            report.append("skip %s: published twice by the vendor" % name)
            return None
        suffix = 2
        taken = {existing["name"] for existing in machines}
        while "%s (%d)" % (name, suffix) in taken:
            suffix += 1
        renamed = "%s (%d)" % (name, suffix)
        report.append("renamed %s -> %s: the name already exists with other settings"
                      % (name, renamed))
        return renamed
    return name


def unique_slug(machines, slug):
    """Keep generated ids unique.

    slugify drops punctuation, so "UltiMaker 2" and "UltiMaker 2+" both slug to
    "ultimaker-2" and the second machine overwrote the first one's preset file
    and vendor entry: one machine silently disappeared and the survivor was
    published under the other one's name.
    """
    taken = {machine["slug"] for machine in machines}
    return free_slug(taken, slug)


def free_slug(taken, slug):
    """A file stem no earlier preset of this kind already claimed."""
    if slug not in taken:
        taken.add(slug)
        return slug
    index = 2
    while "%s-%d" % (slug, index) in taken:
        index += 1
    candidate = "%s-%d" % (slug, index)
    taken.add(candidate)
    return candidate


def retitle(document, name, condition):
    """One of the borrowed template files, published under its own name."""
    lines = [line for line in document.splitlines()
             if not line.startswith("name: ") and not line.startswith("condition: ")]
    lines[1:1] = ["name: " + scalar(name)]
    if condition:
        lines.insert(2, "condition: " + condition)
    return "\n".join(lines) + "\n"


def process_name_for(machine_name, data, procs):
    """The vendor's own process for a machine, by the machine's own default.

    The reference names the default process on the machine, and that name is
    usually a key of the vendor's own process list: exact, then with the nozzle
    suffix the machine name carries and the process name does not, then the
    first process in the family. Nothing is guessed past that; the caller falls
    back to this app's own standard profile and says so.
    """
    seed = str(data.get("default_print_profile") or "")
    candidates = [part.strip() for part in seed.split(";") if part.strip()]
    for candidate in candidates:
        if candidate in procs:
            return candidate
    lowered = {key.lower(): key for key in procs}
    for candidate in candidates:
        trimmed = re.sub(r"\s*\(?0\.[0-9]+ nozzle\)?", "", candidate).strip().lower()
        if trimmed in lowered:
            return lowered[trimmed]
    family = re.sub(r"\s*0\.[0-9]+ nozzle$", "", machine_name).strip().lower()
    for name in procs:
        if "@" in name and name.rsplit("@", 1)[1].strip().lower() == family:
            return name
    return None


def inherits_lookup(source_root):
    """Resolve an ``inherits`` name anywhere under the profile checkout.

    The reference keeps a vendor's generic filament inheriting the system preset
    another vendor's directory defines, so a sibling-only lookup resolves such a
    preset to its own handful of overrides and nothing else.
    """
    by_name = {}
    for path in sorted(source_root.glob("*/**/*.json")):
        by_name.setdefault(path.stem, path)
    def lookup(name):
        return by_name.get(name)
    return lookup


def build_vendor(source_root, vendor_dir, vendor, out_root, nozzle, nozzle_float, allowed,
                 enums, borrowed, placeholders, engine_defaults, report, machines_filter=None,
                 lookup=None):
    index = index_of(source_root, vendor)
    vendor_slug = slugify(vendor)
    cache = {}
    machines, unsupported = [], Counter()

    for preset_name, path in machine_presets(vendor_dir, vendor, index, nozzle, machines_filter):
        data = preset_convert.resolve_inherits(path, cache, lookup)
        name = NAME_OVERRIDES.get(strip_nozzle(preset_name, nozzle),
                                  strip_nozzle(preset_name, nozzle))
        slug = slugify(name)
        if not slug.startswith(vendor_slug):
            slug = vendor_slug + "-" + slug
        slug = unique_slug(machines, slug)
        values, bucket, dropped_gcode, salvaged_gcode = printer_values(
            data, name, vendor, nozzle_float, allowed["printer"], enums, placeholders,
            engine_defaults)
        note_gcode((dropped_gcode, salvaged_gcode), name, report)
        if "bed_shape" not in values:
            report.append("skip %s: no printable area" % name)
            continue
        name = unique_machine_name(machines, name, values, report)
        if name is None:
            continue
        unsupported.update(bucket["unsupported"])
        machines.append({"name": name, "slug": slug, "values": values,
                         "id": "NL_" + slug, "config_id": "nl-" + slug,
                         "data": data})

    if not machines:
        return None

    directory = out_root / vendor
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "vendor.yaml").write_text(
        vendor_manifest(vendor, vendor, machines, nozzle))
    (out_root / (vendor + ".idx")).write_text(
        "min_slic3r_version = 3.0.0-alpha0\n1.0.0 Generated from reference slicer profiles\n")

    (directory / "preset-tool-standard.yaml").write_text(borrowed["tool_print"])

    procs = entries(index, "process_list")
    published_processes = {}
    process_slugs = set()
    for machine in machines:
        machine["process"] = process_name_for(machine["name"], machine.pop("data"), procs)

    for name in sorted({machine["process"] for machine in machines if machine["process"]}):
        path = vendor_dir / procs[name]
        if not path.is_file():
            continue
        values, bucket = converted(preset_convert.resolve_inherits(path, cache, lookup),
                                   "print", allowed["print"], enums)
        if not values:
            continue
        note_gcode(repair_gcode(values, placeholders),
                   "%s process %s" % (vendor, name), report)
        unsupported.update(bucket["unsupported"])
        published_processes[name] = values

    # A print preset's condition is evaluated without a printer, so a condition
    # referencing the machine drops the preset instead of scoping it. Every
    # vendor process is therefore published once, under its own name, shared by
    # the machines whose own default points at it.
    for name, values in published_processes.items():
        (directory / ("preset-print-" + free_slug(process_slugs, slugify(name)) + ".yaml")).write_text(
            preset_document("print", name, values))

    fallback = 0
    for machine in machines:
        if machine["process"] in published_processes:
            continue
        fallback += 1
        report.append("%s: no convertible vendor process, publishing this app's "
                      "standard profile for it" % machine["name"])
        (directory / ("preset-print-" + free_slug(process_slugs, slugify(machine["name"] +
                                                                         " Standard")) + ".yaml")).write_text(
            retitle(borrowed["print"], machine["name"] + " Standard", ""))

    filaments = 0
    published_filaments = set()
    filament_names = []
    filament_slugs = set()
    for name, sub in entries(index, "filament_list").items():
        if not GENERIC_FILAMENT.search(name):
            continue
        # The reference repeats one material once per nozzle and per machine
        # family. This bundle ships 0.4 machines, so a variant belonging to
        # another nozzle is another machine's filament, and the first variant
        # of a material is the one published under its plain name.
        nozzles = NOZZLE_TOKEN.findall(name)
        if nozzles and nozzle not in nozzles:
            continue
        base = FILAMENT_SUFFIX.sub("", name).strip()
        if base in published_filaments:
            continue
        path = vendor_dir / sub
        if not path.is_file():
            continue
        values, bucket = converted(preset_convert.resolve_inherits(path, cache, lookup),
                                   "filament", allowed["filament"], enums)
        if not values:
            continue
        note_gcode(repair_gcode(values, placeholders),
                   "%s filament %s" % (vendor, name), report)
        published_filaments.add(base)
        unsupported.update(bucket["unsupported"])
        unique = base if vendor.lower() in base.lower() else "%s (%s)" % (base, vendor)
        (directory / ("preset-filament-" + free_slug(filament_slugs, slugify(unique)) + ".yaml")).write_text(
            preset_document("filament", unique, values))
        filament_names.append(unique)
        filaments += 1
    if not filaments:
        (directory / "preset-filament-generic.yaml").write_text(borrowed["filament"])

    # Which process and filament each machine resolves with, for validate.py: the
    # bundle loader reads YAML, so this record cannot be mistaken for a preset,
    # and a machine is never checked against another vendor's process.
    default_filament = filament_names[0] if filament_names else "Generic"
    for pattern in FILAMENT_PREFERENCE:
        preferred = [name for name in filament_names if pattern.search(name)]
        if preferred:
            default_filament = sorted(preferred)[0]
            break
    defaults = {}
    for machine in machines:
        name = machine["process"] if machine["process"] in published_processes \
            else machine["name"] + " Standard"
        defaults[machine["name"] + " " + nozzle] = {"process": name,
                                                     "filament": default_filament}
    (directory / "defaults.json").write_text(json.dumps(defaults, indent=1, sort_keys=True) + "\n")

    # The engine's PrinterSettings has no slot for a machine's default process or
    # filament, so they are published in defaults.json for the tools instead;
    # writing them onto the printer preset logs "Invalid key" once per load.
    for machine in machines:
        (directory / ("preset-printer-" + machine["slug"] + ".yaml")).write_text(
            preset_document("printer", machine["name"], machine["values"],
                            'printer.model == "%s_MODEL"' % machine["id"]))

    return {"vendor": vendor, "machines": len(machines),
            "own_processes": len(published_processes), "fallback": fallback,
            "filaments": filaments, "unsupported": unsupported}


REFERENCE_ONLY_VENDORS = {"Custom"}


def vendor_directories(source_root):
    """Vendor directories worth publishing as the app's own machines.

    The reference's ``Custom`` vendor holds printer templates for a user to copy
    (``MyKlipper``, ``MyMarlin``) rather than hardware, and next-layer already
    owns that name: it publishes its own custom bundle there and blocks the
    engine's samples, so a machine published under it can never resolve.
    """
    found = []
    for directory in sorted(source_root.iterdir()):
        if not directory.is_dir():
            continue
        if directory.name in REFERENCE_ONLY_VENDORS:
            continue
        if not (source_root / (directory.name + ".json")).is_file():
            continue
        found.append(directory)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from", dest="source", required=True, help="directory of reference profiles to read")
    parser.add_argument("--out", default=str(Path(__file__).resolve().parents[2]
                                             / "upstream/resources/presets" / BUNDLE_ID))
    parser.add_argument("--vendors", help="comma-separated vendor directory names; default all")
    parser.add_argument("--machines", help="comma-separated substrings; only matching machine presets")
    parser.add_argument("--nozzle", default=NOZZLE)
    parser.add_argument("--bin", default=str(Path(__file__).resolve().parents[2]
                                            / "upstream/build/src/next-layer-qt/next-layer-qt"))
    parser.add_argument("--model", default=str(Path(__file__).resolve().parents[2]
                                              / "oracle/models/cube_20.stl"))
    parser.add_argument("--engine-source", default=str(Path(__file__).resolve().parents[2]
        / "upstream/src/slic3r-shared/src/Slic3r/Biz/Config/Legacy/PrintConfig.cpp"))
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    source_root = Path(args.source)
    directories = vendor_directories(source_root)
    if args.list:
        for directory in directories:
            index = index_of(source_root, directory.name)
            count = len(entries(index, "machine_list"))
            print("%-24s %d machines" % (directory.name, count))
        return 0

    wanted = None
    if args.vendors:
        wanted = {name.strip() for name in args.vendors.split(",") if name.strip()}
    out_root = Path(args.out)
    # A full run owns its output directory: renamed or skipped machines leave
    # files behind, and a stale printer preset is published exactly like a fresh
    # one, so a leftover name reintroduces the duplicate-name defect below.
    if wanted is None and not args.machines and out_root.is_dir():
        shutil.rmtree(out_root)
        print("cleared %s" % out_root)
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
    lookup = inherits_lookup(source_root)

    for directory in directories:
        vendor = directory.name
        if wanted is not None and vendor not in wanted:
            continue
        if vendor in SKIPPED_VENDORS:
            report.append("skip vendor %s: the engine ships its own bundle for it, and a "
                          "second copy of a machine name makes preset resolution "
                          "non-deterministic" % vendor)
            continue
        try:
            result = build_vendor(source_root, directory, vendor, out_root, args.nozzle,
                                  float(args.nozzle.split()[0]), allowed, enums, borrowed,
                                  placeholders, engine_defaults, report,
                                  [token.strip() for token in (args.machines or "").split(",")
                                   if token.strip()], lookup)
        except Exception as error:  # a broken profile must not stop the catalog
            failed.append("%s: %s" % (vendor, error))
            continue
        if result is None:
            report.append("skip vendor %s: no convertible machines" % vendor)
            continue
        totals["machines"] += result["machines"]
        totals["vendors"] += 1
        print("%-22s %3d printers  %3d own processes  %3d filaments  %3d dropped keys"
              % (vendor, result["machines"], result["own_processes"], result["filaments"],
                 len(result["unsupported"])))

    print("\n%d vendors, %d printers into %s" % (totals["vendors"], totals["machines"], out_root))
    for line in report:
        print("  " + line)
    for line in failed:
        print("  FAILED " + line)

    kinds = {"printer": "*/preset-printer-*.yaml", "print": "*/preset-print-*.yaml",
             "filament": "*/preset-filament-*.yaml"}
    duplicates = []
    counts = {}
    for kind, pattern in kinds.items():
        published = {}
        for path in sorted(out_root.glob(pattern)):
            text = path.read_text()
            name = next((line[len("name: "):].strip("'")
                         for line in text.splitlines() if line.startswith("name: ")), path.stem)
            published.setdefault(name, set()).add(text)
        counts[kind] = sum(len(paths) for paths in published.values())
        # The same name over byte-identical settings is one preset published
        # twice, which resolution handles; the same name over different
        # settings is the defect that made a machine resolve differently from
        # run to run.
        duplicates.extend("%s preset %s (%d different settings)" % (kind, name, len(texts))
                          for name, texts in sorted(published.items()) if len(texts) > 1)
    print("\n%d printer, %d process and %d filament presets"
          % (counts["printer"], counts["print"], counts["filament"]))
    for line in duplicates:
        print("  DUPLICATE " + line)
    return 1 if failed or duplicates else 0


if __name__ == "__main__":
    sys.exit(main())
