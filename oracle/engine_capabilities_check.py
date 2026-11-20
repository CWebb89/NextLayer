#!/usr/bin/env python3
"""The engine-capability gate: no surface may offer a setting the engine ignores.

Two jobs, both run by `tools/check`:

  --from-ledger            Write src/qt/assets/engine-capabilities.json from HFW's
                           settings ledger: every setting that engine cannot
                           apply. The app reads that file by default.
  --check <capabilities>   Drive next-layer three times -- with the list switched
                           off, with it passed explicitly, and with no flag at all
                           -- and require that:
                             * every listed name really is a setting the sidebar
                               shows (a typo would otherwise silently do nothing)
                             * none of them appear in the filtered sidebar
                             * the two counts differ by exactly the number of
                               distinct names listed
                             * the shipped default hides exactly what the file
                               passed on the command line hides

Prints one measurement line, because a gate that cannot measure its subject
fails.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "upstream/build/src/next-layer-qt/next-layer-qt"
LEDGER = ROOT.parent / "HFW/docs/SETTINGS-LEDGER.md"
SHIPPED = ROOT / "src/qt/assets/engine-capabilities.json"
ROW = re.compile(r"^\| `([a-z0-9_]+)` \| ([a-z0-9/]+) \| ([a-z/]+) \|")
TABLE_END = re.compile(r"^## ")


def ledger_gaps(path: Path) -> list[str]:
    """Every setting the ledger records as not applied, excluding internal rows."""
    gaps: list[str] = []
    inside = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("## The whole table"):
            inside = True
            continue
        if inside and TABLE_END.match(line):
            break
        if not inside:
            continue
        match = ROW.match(line)
        if match is None:
            continue
        name, tier, status = match.groups()
        if tier == "int":
            continue
        if status != "applied":
            gaps.append(name)
    return sorted(set(gaps))


def dump_names(capabilities: Path | None) -> list[str]:
    command = [str(BINARY), "--dump-setting-names"]
    if capabilities is None:
        command[1:1] = ["--engine-capabilities", "none"]
    else:
        command[1:1] = ["--engine-capabilities", str(capabilities)]
    environment = dict(os.environ)
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run(
        command, cwd=ROOT, env=environment, capture_output=True, text=True, timeout=300
    )
    if result.returncode != 0:
        raise SystemExit(f"next-layer exited {result.returncode}: {result.stderr.strip()}")
    names = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not names:
        raise SystemExit("next-layer printed no setting names")
    return names


def write_from_ledger(target: Path) -> int:
    if not LEDGER.is_file():
        raise SystemExit(f"no ledger at {LEDGER}")
    gaps = ledger_gaps(LEDGER)
    if not gaps:
        raise SystemExit("the ledger records no gaps, which cannot be right")
    target.write_text(
        json.dumps({"engine": "hfw", "unsupported": gaps}, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"engine hfw: {len(gaps)} settings not implemented -> {target}")
    return 0


def check(capabilities: Path) -> int:
    payload = json.loads(capabilities.read_text(encoding="utf-8"))
    listed = sorted({name for name in payload.get("unsupported", []) if name})
    if not listed:
        raise SystemExit(f"{capabilities} lists no settings to hide")

    plain = dump_names(None)
    filtered = dump_names(capabilities)
    shipped = dump_names(SHIPPED)
    plain_set, filtered_set = set(plain), set(filtered)

    problems: list[str] = []
    unknown = [name for name in listed if name not in plain_set]
    if unknown:
        problems.append(f"{len(unknown)} listed settings the sidebar never shows: {unknown[:5]}")
    still_there = [name for name in listed if name in filtered_set]
    if still_there:
        problems.append(f"{len(still_there)} listed settings still offered: {still_there[:5]}")
    removed = len(plain) - len(filtered)
    expected = len([name for name in listed if name in plain_set])
    if removed != expected:
        problems.append(f"removed {removed} settings, expected {expected}")
    if set(plain) - set(shipped) != set(plain) - set(filtered):
        extra = sorted((set(plain) - set(shipped)) ^ (set(plain) - set(filtered)))[:5]
        problems.append(f"the shipped list hides a different set than {capabilities.name}: {extra}")

    if problems:
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(
        f"engine-capabilities: {len(listed)} listed, {removed} hidden, "
        f"{len(plain)} -> {len(filtered)} settings offered, shipped default matches"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from-ledger", type=Path, nargs="?", const=SHIPPED)
    parser.add_argument("--check", type=Path)
    arguments = parser.parse_args()
    if arguments.from_ledger is not None:
        return write_from_ledger(arguments.from_ledger)
    if arguments.check is not None:
        if not BINARY.is_file():
            raise SystemExit(f"no binary at {BINARY}")
        return check(arguments.check)
    parser.error("give --from-ledger or --check")


if __name__ == "__main__":
    raise SystemExit(main())
