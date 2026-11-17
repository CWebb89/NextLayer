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

`--engine-capabilities <file>` takes `{"engine": name, "unsupported": [setting,
...]}` and removes those settings from every surface: the sidebar, the per-object
editor and the value dump. The engine this application ships with lists none, so
the plain sidebar stays at 352 settings; a different engine hides what it cannot
apply.

`oracle/engine_capabilities_check.py` drives the app twice — once plain, once
with a capability list — and requires that every listed name really is a setting
the sidebar shows (a typo would otherwise silently do nothing), that none of
them survive the filter, and that the counts differ by exactly the number listed.
`tools/check` runs it against `oracle/engine-capabilities-hfw.json`, generated
from HFW's ledger:

```sh
python3 oracle/engine_capabilities_check.py --from-ledger
python3 oracle/engine_capabilities_check.py --check oracle/engine-capabilities-hfw.json
```

Measured 2026-11-16: **352 settings offered with this engine, 175 with HFW's
capability list (177 hidden)** — the same 175 HFW's own ledger records as applied.
The two numbers are the same measurement from opposite ends, which is why a swap
cannot quietly overstate what the engine does. The figure moves whenever HFW's
ledger does: regenerate the list, then run the gate.
