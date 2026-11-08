#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Curtis Webb
"""Build the filament library this app ships, from a reference profile checkout.

The reference slicer keeps the filaments that fit every machine in one directory --
``OrcaFilamentLibrary/filament`` -- with no compatible-printers restriction, and shows
them for every printer. That is why its material list runs from PLA through PETG,
ABS, ASA, PC, PA, PVA, HIPS and TPU to the chopped-fibre variants, while a bundle
built the way ours is carries only what a vendor's own index mentions.

The engine scopes a filament to the vendor bundle it lives in, so the library is
shipped as data (``src/qt/assets/filaments``) and copied into a bundle when a machine
is created or first seen. This tool writes that data.

Usage:
    tools/filament-library/generate.py \
        --from ~/dev/reference/OrcaSlicer/resources/profiles \
        --out  src/qt/assets/filaments
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/printer-catalog"))

import generate as catalog  # noqa: E402  (path set up above)

SUFFIX = re.compile(r"\s*@\s*[^@]+$")


def library_files(source_root: pathlib.Path):
    return sorted((source_root / "OrcaFilamentLibrary" / "filament").glob("*.json"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from", dest="source", required=True,
                        help="reference checkout's resources/profiles directory")
    parser.add_argument("--out", default=str(ROOT / "src/qt/assets/filaments"),
                        help="directory the converted presets are written to")
    parser.add_argument("--bin", default=str(ROOT / "upstream/build/src/next-layer-qt/next-layer-qt"),
                        help="a next-layer binary, read for the engine's setting names")
    parser.add_argument("--engine-source",
                        default=str(ROOT / "upstream/src/slic3r-shared/src/Slic3r/Biz/Config/Legacy/PrintConfig.cpp"),
                        help="the engine's own option definitions, read for its enum vocabularies")
    parser.add_argument("--model", default=str(ROOT / "oracle/models/cube_20.stl"),
                        help="model any binary invocation opens")
    arguments = parser.parse_args()

    source_root = pathlib.Path(arguments.source).expanduser().resolve()
    if not (source_root / "OrcaFilamentLibrary").is_dir():
        print("no OrcaFilamentLibrary in %s" % source_root, file=sys.stderr)
        return 1

    files = library_files(source_root)
    if not files:
        print("the filament library is empty", file=sys.stderr)
        return 1

    allowed = catalog.reference_keys(arguments.bin, arguments.model)
    enums = catalog.engine_enums(pathlib.Path(arguments.engine_source))
    placeholders = set()
    for keys in allowed.values():
        placeholders |= keys
    cache = {}
    lookup = catalog.inherits_lookup(source_root.parent.parent)
    out = pathlib.Path(arguments.out)
    out.mkdir(parents=True, exist_ok=True)
    for stale in out.glob("*.yaml"):
        stale.unlink()

    written = []
    failed = {}
    unsupported = {}
    slugs = set()
    published = set()
    for path in files:
        name = SUFFIX.sub("", path.stem).strip()
        # The library repeats one material per nozzle variant (``@System`` and
        # ``@base``); a bundle ships 0.4 machines, so the first variant is the one
        # published under the plain name.
        if name in published:
            continue
        try:
            data = catalog.preset_convert.resolve_inherits(path, cache, lookup)
        except Exception as error:
            failed[path.name] = str(error)
            continue
        values, bucket = catalog.converted(data, "filament", allowed["filament"], enums)
        if not values:
            failed[path.name] = "nothing survived conversion"
            continue
        # A template naming a setting this engine does not have used to make the
        # whole preset unusable at slice time (error 38, no G-code at all), so the
        # same salvage the catalog runs applies here.
        catalog.repair_gcode(values, placeholders)
        for key in bucket["unsupported"]:
            unsupported[key] = unsupported.get(key, 0) + 1
        published.add(name)
        slug = catalog.free_slug(slugs, catalog.slugify(name))
        (out / ("%s.yaml" % slug)).write_text(catalog.preset_document("filament", name, values))
        written.append(name)

    print("%d library filaments written to %s" % (len(written), out))
    if failed:
        print("\n%d file(s) not converted:" % len(failed))
        for name, reason in sorted(failed.items()):
            print("  %s: %s" % (name, reason))
    if unsupported:
        print("\nsettings the engine does not have, dropped (files affected):")
        for key, count in sorted(unsupported.items(), key=lambda entry: -entry[1])[:12]:
            print("  %-28s %d" % (key, count))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
