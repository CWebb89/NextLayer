#!/usr/bin/env python3
"""Lists every place next-layer reaches into the C++ engine, and what replaces it.

The engine under `upstream/` is the last thing keeping this application tied to a
600k-line C++ tree. This tool reads `src/` and writes `docs/engine-boundary.md`: a
file-by-file inventory of the engine symbols each file uses, sorted into the
migration phases of `docs/rust-engine-plan.md`.

Run it with no arguments to rewrite the document, and with `--check` to fail when
the document is stale -- the same shape as the settings ledgers the engine project
keeps.
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
OUT = ROOT / "docs" / "engine-boundary.md"

SYMBOL = re.compile(r"\bSlic3r::[A-Za-z_][A-Za-z0-9_:]*")
INCLUDE = re.compile(r'#include\s*[<"](Slic3r/[^">]+|libslic3r/[^">]+)[">]')

PHASES = {
    0: "App shell",
    1: "Geometry and mesh",
    2: "Document and plate",
    3: "Settings",
    4: "Preview",
    5: "Plate operations",
    6: "Project files",
    7: "Optional features",
}

# Every engine name these files use, and the phase that retires it. A name that is
# not here is printed as unclassified rather than guessed at.
RETIRES = {
    # 0 -- the engine's application framework: services, jobs, presets, platform.
    "Slic3r::App::InitParams": 0,
    "Slic3r::App::AppServices": 0,
    "Slic3r::App::AppConfig": 0,
    "Slic3r::App::Theme": 0,
    "Slic3r::App::PresetUpdater": 0,
    "Slic3r::App::PresetUpdater::ControllerEnvironment": 0,
    "Slic3r::App::PresetBundle": 0,
    "Slic3r::App::PopNotification": 0,
    "Slic3r::App::PopNotification::PopNotificationCenter": 0,
    "Slic3r::App::Platform": 0,
    "Slic3r::App::Platform::IFileExplorerErrorListener": 0,
    "Slic3r::App::CLI": 0,
    "Slic3r::App::CLI::run": 0,
    "Slic3r::App::CLI::CLIRuntime": 0,
    "Slic3r::Biz::Platform": 0,
    "Slic3r::Biz::Platform::PlatformServices": 0,
    "Slic3r::Biz::Platform::PlatformServices::instance": 0,
    "Slic3r::Biz::Platform::JobManager": 0,
    "Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener": 0,
    "Slic3r::Biz::ProjectInteractor": 0,
    "Slic3r::Biz::Slicing": 0,
    "Slic3r::Biz::Slicing::Status": 0,
    "Slic3r::Biz::Slicing::StatusCode": 0,
    "Slic3r::Biz::store_stl": 0,
    # 1 -- geometry: points, meshes, transforms, bounds, colour.
    "Slic3r::Domain::Vec": 1,
    "Slic3r::Domain::TriangleMesh": 1,
    "Slic3r::Domain::Transform": 1,
    "Slic3r::Domain::Transformation": 1,
    "Slic3r::Domain::BoundingBox": 1,
    "Slic3r::Domain::Index": 1,
    "Slic3r::Domain::ZHeightPair": 1,
    "Slic3r::Domain::ZHeightPairs": 1,
    "Slic3r::Domain::indexed_triangle_set": 1,
    "Slic3r::Domain::indexed_triangle_set_with_color": 1,
    "Slic3r::Domain::Point": 1,
    "Slic3r::Domain::Polygon": 1,
    "Slic3r::Domain::Polyline": 1,
    "Slic3r::Domain::ExPolygon": 1,
    "Slic3r::Domain::PixelFormat": 1,
    "Slic3r::Domain::PixelFormat::RGBA": 1,
    # 2 -- the document: what is on the plate and what it carries.
    "Slic3r::Domain::Project": 2,
    "Slic3r::Domain::ModelObject": 2,
    "Slic3r::Domain::ModelVolume": 2,
    "Slic3r::Domain::BedInstance": 2,
    "Slic3r::Domain::SelectionId": 2,
    "Slic3r::Domain::SlicingId": 2,
    "Slic3r::Domain::Workbench": 2,
    # 3 -- settings.
    "Slic3r::Domain::ConfigContainer": 3,
    "Slic3r::Domain::ConfigPack": 3,
    "Slic3r::Domain::ConfigPackFDM": 3,
    "Slic3r::Domain::ConfigPackSLA": 3,
    "Slic3r::Domain::ConfigItemDef": 3,
    "Slic3r::Domain::get_defs_fdm": 3,
    # 4 -- preview.
    "Slic3r::App::libvgcode": 4,
    "Slic3r::App::libvgcode::FdmViewerInputData": 4,
    "Slic3r::Domain::GCodeExtrusionRole": 4,
    # 5 -- plate operations.
    "Slic3r::Biz::arrange": 5,
    "Slic3r::Biz::Algorithms::arrange": 5,
    # 7 -- features a Rust engine reaches last, or not at all.
    "Slic3r::Biz::Algorithms::TriangleSelector": 7,
    "Slic3r::Domain::TriangleSelector": 7,
    "Slic3r::Domain::TriangleSelector::TriangleSplittingData": 7,
    "Slic3r::Domain::TriangleSelector::TriangleStateType": 7,
    "Slic3r::Domain::FontList": 7,
    "Slic3r::Domain::FontFile": 7,
    "Slic3r::Domain::FontDescriptor": 7,
    "Slic3r::Domain::FontDescriptor::Type::file_path": 7,
    "Slic3r::Domain::Cut": 7,
    "Slic3r::Domain::Hollow": 7,
}


def phase_of(name: str) -> int | None:
    """The phase that retires an engine name, by exact match and then by family."""
    if name in RETIRES:
        return RETIRES[name]
    for prefix, phase in sorted(PREFIX_RETIRES, key=lambda row: -len(row[0])):
        if name.startswith(prefix):
            return phase
    return None


# Families: `Slic3r::Domain::Vec2d` and its siblings are one job, not four, and the
# engine will keep growing new spellings of the same thing.
PREFIX_RETIRES: list[tuple[str, int]] = [
    ("Slic3r::Domain::Vec", 1),
    ("Slic3r::Domain::Transform", 1),
    ("Slic3r::Domain::indexed_triangle_set", 1),
    ("Slic3r::Domain::TriangleMesh", 1),
    ("Slic3r::Domain::BoundingBox", 1),
    ("Slic3r::Domain::Polygon", 1),
    ("Slic3r::Domain::ExPolygon", 1),
    ("Slic3r::Domain::Polyline", 1),
    ("Slic3r::Domain::Point", 1),
    ("Slic3r::Domain::PixelFormat", 1),
    ("Slic3r::Domain::ZHeightPair", 1),
    ("Slic3r::Domain::TriangleSelector", 7),
    ("Slic3r::Biz::Algorithms::TriangleSelector", 7),
    ("Slic3r::Domain::Font", 7),
    ("Slic3r::Domain::Config", 3),
    ("Slic3r::Domain::get_defs", 3),
    ("Slic3r::Domain::Model", 2),
    ("Slic3r::Domain::Project", 2),
    ("Slic3r::App::libvgcode", 4),
    ("Slic3r::App::Preset", 0),
    ("Slic3r::App::Platform", 0),
    ("Slic3r::App::PopNotification", 0),
    ("Slic3r::App::CLI", 0),
    ("Slic3r::Biz::Platform", 0),
    ("Slic3r::Biz::Slicing", 0),
    ("Slic3r::Biz::ProjectInteractor", 0),
    ("Slic3r::Biz::arrange", 5),
    ("Slic3r::Biz::Algorithms::arrange", 5),
]


def files() -> list[Path]:
    return sorted(
        path
        for path in SRC.rglob("*")
        if path.suffix in {".cpp", ".hpp"}
        and path.is_file()
        and "target" not in path.relative_to(SRC).parts
    )


def scan(path: Path) -> tuple[Counter, set[str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    symbols = Counter(SYMBOL.findall(text))
    includes = {match for match in INCLUDE.findall(text)}
    return symbols, includes


def main() -> int:
    per_phase: dict[int, set[str]] = defaultdict(set)
    per_file: list[tuple[Path, Counter, set[str]]] = []
    unclassified: Counter = Counter()
    included: Counter = Counter()

    for path in files():
        symbols, includes = scan(path)
        if not symbols and not includes:
            continue
        per_file.append((path, symbols, includes))
        included.update(sorted(includes))
        for name in symbols:
            phase = phase_of(name)
            if phase is None and len(name.split("::")) > 2:
                unclassified[name] += symbols[name]
            else:
                per_phase[phase].add(name)

    lines: list[str] = []
    add = lines.append
    add("# The engine boundary")
    add("")
    add("Generated by `tools/engine-boundary.py`; edit the tool, not this file.")
    add("")
    add(
        f"{len(per_file)} of {len(files())} files under `src/` reach into the C++ engine under "
        f"`upstream/`. Every one of them is a line in the migration described by "
        f"`docs/rust-engine-plan.md`; when this document has no rows left, the "
        f"application builds against Rust and nothing else."
    )
    add("")
    add("## What each phase retires")
    add("")
    add("| Phase | Area | Engine names | Files |")
    add("| ---: | --- | ---: | ---: |")
    for phase in sorted(PHASES):
        names = per_phase.get(phase, set())
        touched = sum(1 for _, symbols, _ in per_file if any(phase_of(n) == phase for n in symbols))
        add(f"| {phase} | {PHASES[phase]} | {len(names)} | {touched} |")
    add("")
    if unclassified:
        add("## Names with no phase yet")
        add("")
        add("Used by the application and not yet placed in a phase. Each is decided")
        add("when its phase is planned, not guessed at here. Namespace prefixes are")
        add("left out of this list.")
        add("")
        add("```")
        for name, count in unclassified.most_common():
            add(f"{count:>3}  {name}")
        add("```")
        add("")

    add("## Engine headers included directly")
    add("")
    if included:
        add("| Header | Files |")
        add("| --- | ---: |")
        for name, count in included.most_common():
            add(f"| `{name}` | {count} |")
    else:
        add("None.")
    add("")

    add("## Every file, and what it uses")
    add("")
    add("| File | Phase | Engine names it uses |")
    add("| --- | ---: | --- |")
    for path, symbols, _ in per_file:
        phases = sorted({phase if phase is not None else -1 for phase in map(phase_of, symbols)}) or [-2]
        shown = ", ".join(str(phase) if phase >= 0 else "?" for phase in phases)
        names = sorted(symbols)
        if len(names) > 6:
            summary = ", ".join(f"`{n.split('::')[-1]}`" for n in names[:6]) + f", +{len(names) - 6}"
        else:
            summary = ", ".join(f"`{n.split('::')[-1]}`" for n in names)
        add(f"| `{path.relative_to(ROOT)}` | {shown} | {summary} |")
    add("")

    document = "\n".join(lines)
    if "--check" in sys.argv[1:]:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != document:
            print(f"{OUT} is stale; run tools/engine-boundary.py", file=sys.stderr)
            return 1
        print(f"{OUT} is current")
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(document, encoding="utf-8")
    print(f"wrote {OUT}")
    print(f"files reaching into the engine: {len(per_file)}")
    print(f"names without a phase: {len(unclassified)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
