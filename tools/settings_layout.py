#!/usr/bin/env python3
"""Build the settings layout the app loads.

The app shows every setting the engine defines, grouped by the engine's own
categories and option groups, which is a good inventory and a poor map: the
process tab has fourteen categories named after the engine's data model
("Layers & Surfaces", "Motion & Dynamics") rather than after the part being
printed. Next-layer's pages come from tools/settings-layout-map.json instead.

This script joins the two. It asks the built binary what it shows
(--dump-settings-json), applies the map, and writes
src/qt/assets/settings-layout.json. It fails if any setting the app shows is
left without a page, or if the map names a page or section that does not exist,
so a new engine setting cannot silently land nowhere.

    tools/settings_layout.py            # write the resource
    tools/settings_layout.py --report   # also list what each page holds

It needs the binary to be newer than its sources; run tools/check first.
"""

import argparse
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = ROOT / "upstream/build/src/next-layer-qt/next-layer-qt"
MODEL = ROOT / "oracle/models/cube_20.stl"
MAP = ROOT / "tools/settings-layout-map.json"
OUT = ROOT / "src/qt/assets/settings-layout.json"


def app_settings():
    result = subprocess.run(
        [str(BINARY), str(MODEL), "--dump-settings-json"],
        capture_output=True, text=True,
        env={**os.environ, "QT_QPA_PLATFORM": "offscreen"},
    )
    if result.returncode != 0:
        sys.exit(f"dump failed ({result.returncode}): {result.stderr.strip()[:400]}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        sys.exit(f"dump is not JSON: {error}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", action="store_true", help="print the pages and their sizes")
    parser.add_argument("--check", action="store_true",
                        help="fail when the committed layout is stale instead of writing it")
    args = parser.parse_args()

    spec = json.loads(MAP.read_text())
    entries = app_settings()

    pages = spec["pages"]
    order = {}
    for tab, tab_pages in pages.items():
        for page in tab_pages:
            for index, section in enumerate(page["sections"]):
                order[(tab, page["key"], section)] = index

    rules = {}
    for rule in spec["rules"]:
        key = (rule["tab"], rule["from"][0], rule["from"][1])
        if key in rules:
            sys.exit(f"two rules for {key}")
        rules[key] = tuple(rule["to"])

    key_rules = {name: tuple(target) for name, target in spec.get("keys", {}).items()}

    placed = {}
    unmapped = []
    for entry in entries:
        tab = entry["tab"]
        target = key_rules.get(entry["name"])
        if target is None:
            target = rules.get((tab, entry["engineSubtabTitle"], entry["engineGroupTitle"]))
        if target is None:
            unmapped.append(entry)
            continue
        page, section = target
        if (tab, page, section) not in order:
            sys.exit(f"{entry['name']} lands on undeclared {tab}/{page}/{section}")
        placed.setdefault((tab, page, section), []).append(entry["name"])

    if unmapped:
        for entry in unmapped:
            print(f"unmapped: {entry['tab']:8s} {entry['engineSubtabTitle']:26s} "
                  f"{entry['engineGroupTitle']:30s} {entry['name']}", file=sys.stderr)
        sys.exit(f"{len(unmapped)} setting(s) have no page; add a rule or a key to the map")

    document = {"version": 1, "tabs": {}}
    for tab, tab_pages in pages.items():
        out_pages = []
        for page in tab_pages:
            out_sections = []
            for section in page["sections"]:
                names = placed.get((tab, page["key"], section), [])
                if not names:
                    continue
                out_sections.append({"title": section, "settings": names})
            if not out_sections:
                continue
            out_pages.append({"key": page["key"], "title": page["title"], "sections": out_sections})
        document["tabs"][tab] = out_pages

    total = sum(len(section["settings"])
                for tab_pages in document["tabs"].values()
                for page in tab_pages
                for section in page["sections"])
    if total != len(entries):
        sys.exit(f"layout holds {total} settings, the app shows {len(entries)}")

    if args.check:
        committed = json.loads(OUT.read_text()) if OUT.exists() else None
        if committed != document:
            sys.exit(f"{OUT.relative_to(ROOT)} is stale: run tools/settings_layout.py")
        print(f"{OUT.relative_to(ROOT)} matches the engine and the map")
        return

    OUT.write_text(json.dumps(document, indent=2) + "\n")

    for tab, tab_pages in document["tabs"].items():
        count = sum(len(section["settings"]) for page in tab_pages for section in page["sections"])
        print(f"{tab}: {len(tab_pages)} pages, {count} settings")
        if args.report:
            for page in tab_pages:
                size = sum(len(section["settings"]) for section in page["sections"])
                print(f"  {page['title']} ({size})")
                for section in page["sections"]:
                    print(f"    {section['title']}: {len(section['settings'])}")
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
