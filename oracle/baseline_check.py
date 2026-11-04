#!/usr/bin/env python3
"""Check that the baseline G-code files still match the manifest fingerprints.

`oracle.py verify` compares a freshly sliced fingerprint against the sha256 in
`baseline/manifest.json`; the `.gcode` files beside it are the human-readable
copy that a CHANGED report tells you to diff against. Nothing reads them, so
they can drift from the manifest without any verdict changing, which turns that
diff into a lie. This closes that hole.

    python baseline_check.py        exit 0 when every file matches
"""

import sys
from pathlib import Path

from oracle import ALL_CASES, BASELINE, normalize, sha256


def main():
    manifest_path = BASELINE / "manifest.json"
    if not manifest_path.is_file():
        print("no baseline; run oracle.py capture first")
        return 1
    import json

    manifest = json.loads(manifest_path.read_text())
    bad = []
    checked = 0
    for case in ALL_CASES:
        entry = manifest["cases"].get(case["name"])
        if entry is None:
            bad.append(f"{case['name']}: not in the manifest")
            continue
        path = BASELINE / "gcode" / f"{case['name']}.gcode"
        if not path.is_file():
            bad.append(f"{case['name']}: {path.name} is missing")
            continue
        checked += 1
        actual = sha256(normalize(path.read_text(errors="replace")))
        if actual != entry["sha256"]:
            bad.append(f"{case['name']}: {path.name} is {actual[:16]}, manifest says {entry['sha256'][:16]}")
    for line in bad:
        print(f"  {line}")
    print(f"{checked - len(bad)}/{checked} baseline files match the manifest")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
