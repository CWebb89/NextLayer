# Roadmap

The order of work, so that the next action is never a question. This file is
the per-project copy; the umbrella copy across all four projects is
`~/dev/README.md` → *The print ecosystem*.

## Where this project sits

| # | Project | What it is | Finish line |
|---|---|---|---|
| 1 | **next-layer** (this project) | the slicer app | downloadable alpha: two real prints recorded in `docs/real-prints.md`, one release artifact |
| 2 | layer | printer control panel | released |
| 3 | HFW | our own slicing engine | settings ledger 100% `applied` or `n/a`, differential slice green, behind a flag |
| 4 | LayerMind | print health | frozen: records and explains, no new features |

next-layer goes first because it is the only one that becomes a product soon,
and because it is the **grader** every later engine claim is measured against.

## The one shared vocabulary

There is one config dialect — the PrusaSlicer/Orca setting names — and three
consumers of it: this app's settings UI, Orca/Prusa preset files, and HFW. The
app does not invent settings; it enumerates the engine's definition table and
shows what is in it.

So two rules hold here permanently:

1. **No control may be shown for a setting the active engine ignores.** Hide it
   or disable it with the reason; a control that does nothing is a lie.
2. **Every setting in the table is implemented, or has a written reason.** The
   checklist is HFW's `docs/SETTINGS-LEDGER.md`, generated from this
   repository's own `upstream/` definition table.

## Now

1. Two real prints through this app, recorded with the G-code hash and the
   Moonraker job record — the finish line for the alpha.
2. One release artifact (`docs/release-readiness.md`, `docs/packaging.md`).
3. The engine swap waits for HFW's ledger, behind a flag, with the rule above
   enforced by a gate rather than by care.

## How the no-dead-control rule is enforced

`src/qt/assets/engine-capabilities.json` lists `{"engine": name, "unsupported":
[setting, ...]}` — every setting HFW's own ledger records as one it cannot apply —
and is read at start-up, removing those settings from every surface: the sidebar,
the per-object editor and the value dump. It is generated from HFW's ledger, so
the two numbers are the same measurement from opposite ends:

```sh
python3 oracle/engine_capabilities_check.py --from-ledger
python3 oracle/engine_capabilities_check.py --check src/qt/assets/engine-capabilities.json
```

`--engine-capabilities <file>` overrides the shipped list and
`--engine-capabilities none` offers every setting, which is how the gate compares
three states: list off, list passed on the command line, and the shipped default.
It requires that every listed name really is a setting the sidebar shows (a typo
would otherwise silently do nothing), that none of them survive the filter, that
the counts differ by exactly the number listed, and that the shipped default hides
exactly what the file it was generated from hides.

Measured 2026-11-18: **352 settings offered with the list off, 177 with it — 175
hidden**, the 175 HFW's ledger records as not applied. The figure moves whenever
HFW's ledger does: regenerate the list, then run the gate.
