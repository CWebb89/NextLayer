# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Curtis Webb
"""Checks both ends of every setting renamed from another slicer's dialect.

`ForeignSettings.cpp` carries the table; this proves it. The binary prints each row
with whether the name it translates *to* exists in this engine's own definitions,
and this script checks the other end: every name it translates *from* has to be an
option the other slicer actually defines, from the snapshot in
`oracle/fixtures/orca-options.txt`.

A name that is not really that slicer's would silently never match a file; a name
this engine does not have would silently never apply. Both are caught here.

    python3 oracle/foreign_settings_check.py
    python3 oracle/foreign_settings_check.py --refresh /path/to/PrintConfig.cpp
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SNAPSHOT = os.path.join(HERE, "fixtures", "orca-options.txt")
BINARY = os.path.join(ROOT, "upstream", "build", "src", "next-layer-qt", "next-layer-qt")


def refresh(path):
    source = open(path, errors="replace").read()
    names = sorted(set(re.findall(r'\badd(?:_nullable)?\(\s*"([a-zA-Z0-9_]+)"', source)))
    with open(SNAPSHOT, "w") as handle:
        handle.write("# Every option name OrcaSlicer defines, taken from its own "
                     "PrintConfig.cpp\n")
        handle.write("# (add(\"name\", coType) entries, one per line).\n#\n")
        handle.write("# Vendored so the gate can prove both ends of a rename: every name\n")
        handle.write("# ForeignSettings.cpp translates has to be in this list, and every "
                     "name it\n")
        handle.write("# translates to has to exist in this engine. Regenerate with\n")
        handle.write("#   python3 oracle/foreign_settings_check.py --refresh \\\n")
        handle.write("#       /path/to/OrcaSlicer/src/libslic3r/PrintConfig.cpp\n")
        handle.write("\n".join(names) + "\n")
    print(f"foreign settings: refreshed {len(names)} option name(s) in the snapshot")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--refresh", metavar="PRINTCONFIG", help="rewrite the snapshot")
    parser.add_argument("--bin", default=BINARY)
    arguments = parser.parse_args()
    if arguments.refresh:
        return refresh(arguments.refresh)

    if not os.path.exists(arguments.bin):
        print(f"foreign settings: FAIL — no binary at {arguments.bin}", file=sys.stderr)
        return 1

    run = subprocess.run([arguments.bin, "--dump-foreign-settings"],
                         capture_output=True, text=True)
    rows = []
    missing_here = []
    for line in run.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) != 3:
            continue
        source, target, verdict = parts
        rows.append((source, target))
        if verdict != "ok":
            missing_here.append(f"{source} -> {target}")

    if not rows:
        print("foreign settings: FAIL — the binary printed no table", file=sys.stderr)
        return 1

    known = {line.strip() for line in open(SNAPSHOT)
             if line.strip() and not line.startswith("#")}
    not_theirs = [f"{source} -> {target}" for source, target in rows if source not in known]

    if missing_here or not_theirs:
        for entry in missing_here:
            print(f"foreign settings: {entry} — this engine has no such setting",
                  file=sys.stderr)
        for entry in not_theirs:
            print(f"foreign settings: {entry} — not a name the other slicer defines",
                  file=sys.stderr)
        print(f"foreign settings: FAIL — {len(missing_here)} target(s) and "
              f"{len(not_theirs)} source(s) wrong", file=sys.stderr)
        return 1

    print(f"foreign settings: {len(rows)} rename(s), every source known to that slicer "
          f"and every target known here")
    return 0


if __name__ == "__main__":
    sys.exit(main())
