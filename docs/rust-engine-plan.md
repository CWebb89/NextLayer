# Retiring the C++ engine

The plan for running next-layer on Rust and nothing else.

Written 2026-11-17, at the state where the application slices a plate through HFW
but still links a 605,785-line C++ engine for everything else. `docs/engine-boundary.md`
is the measured worklist that goes with this document; it is generated from `src/`
and is the honest measure of progress.

## Where the code is

| Repository | What it is | Size | State |
| --- | --- | --- | --- |
| `HFW/HFW-final` | The slicing engine, Rust, clean-room | 32,071 lines, 12 crates | Slices plates, writes G-code, verifies its own output. Engine gate green. |
| `next-layer` | The application, C++/Qt/QML | 26,505 lines, 76 files | Slices through HFW when asked; 43 files still reach into the C++ engine. |
| `next-layer/upstream` | A restructured PrusaSlicer, the engine | 605,785 lines, 28 modules | Linked today. Not part of the application's git history. |

The application is 43 files away from owning nothing but its own interface. Those
43 files are the whole of this plan.

## The rule this plan follows

**No C++ engine. No translated C++. No vendored engine.** Every capability the
application needs is either:

1. written in Rust, in HFW, behind a stable C ABI, or
2. written in C++ in the application, as its own code.

There is no third category. "Take it from upstream" is not an option that survives
this plan, and a partial port is worse than either kind of full one, because a
second dependency graph is what makes this project hard to hold in your head.

## What is being replaced, in the order it must be done

### Phase 0 — One engine, one switch (done when the legacy path is unreachable by default)

HFW is currently reached through `NEXT_LAYER_ENGINE=hfw`, a boolean that chooses
between two complete paths. That switch is the seam, and it is where future
divergence grows. It becomes a `SliceEngine` interface with exactly one
implementation. The legacy path is deleted, not deprecated — `SliceRuntime.cpp`
(987 lines) mostly exists to drive the C++ engine's CLI, and it goes with it.

**Delivers:** one slice path, and the 987-line runtime that drove the other one removed.

### Phase 1 — A library boundary (the decision that shapes the rest)

HFW is a **subprocess** today: the application writes STLs and a config, runs
`hfw-cli`, reads G-code back. That is right for slicing and wrong for everything
interactive, because the viewport needs mesh data sixty times a second and a
process boundary cannot carry it.

HFW grows a **C ABI crate** — `crates/capi` — with a flat, versioned surface:

```
hfw_mesh_load_file   hfw_mesh_from_bytes   hfw_mesh_transform   hfw_mesh_bounds
hfw_mesh_free        hfw_plate_slice       hfw_gcode_free       hfw_last_error
```

Handles, not Rust types, cross the boundary. `hfw-cli` keeps working and is how
the engine is tested; the application uses the ABI. The application keeps the
subprocess call only for `--slice-all`-style batch work, if at all.

**Delivers:** the application can hold a mesh without a process, and phases 2–6
become possible.

### Phase 2 — The document model (the real reason the engine is still linked)

`Project`, `ModelObject`, `ModelVolume`, `BedInstance`, `SelectionId`. This is how
the application says "what is on the plate". The C++ engine owns it; HFW has no
model at all (it takes a list of meshes).

The plate model is written **in Rust, in HFW** — `crates/plate` — because the
engine needs the same object the application shows: mesh, transform, per-object
settings, name, the plate it belongs to. `PlateDocument.cpp` (3,544 lines) becomes
a view over it rather than its owner.

**Delivers:** opening, saving, arranging and slicing all speak the same object.

### Phase 3 — Settings without `ConfigContainer`

HFW already parses 383 settings from Prusa and Orca presets, applies 299 of them,
and can dump the lot as JSON. The application's `SettingsModel` (1,552 lines) and
`ConfigBoxJson` move onto that JSON. One settings ledger, one naming table, one
place a new setting is taught.

**Delivers:** `unconverted`/`unknown` keys are reported identically in the panel and
in the engine's output.

### Phase 4 — Preview

`GcodeView` already parses G-code itself; what it borrows from the engine is types
(`libvgcode::FdmViewerInputData`, `GCodeExtrusionRole`). HFW can emit every path
with its role (`--json-paths`), and the G-code already carries `;TYPE:` markers.
The preview is a small, self-contained replacement — days, not weeks.

**Delivers:** the 5,000-line `libvgcode`+`libpgcode` pair is no longer linked.

### Phase 5 — Plate operations

Load, scale, rotate, translate, mirror, delete, duplicate: HFW's mesh API plus its
own geometry. Arrange: HFW has shelf layout (`--spacing`, `--no-arrange`); a nesting
arranger is a project of its own and is **not** in this plan's definition of done.
One plate of parts is shelf layout; that is what gets used.

**Delivers:** `Arranger.cpp`, `Orient.cpp`, `TransformController.cpp` deleted.

### Phase 6 — Project files

The application writes a `.3mf` carrying the plate and each object's settings. HFW
already *reads* that format. The writer belongs to HFW (`crates/threemf` grows a
write side) so read and write cannot drift.

**Delivers:** `ProjectFile.cpp`, `ThreeMfProject.cpp` deleted; projects no longer
need a C++ engine to open.

### Phase 7 — The features that ship without the engine, or not at all

This is where the plan stops pretending. Each of these exists today only because
the C++ engine carries it, and each is a project in its own right:

| Feature | Plan |
| --- | --- |
| Paint / adhesion painting, seam painting | Not in scope. Marked unavailable in the UI. |
| Text embossing, font loading | Not in scope. |
| Cut, hollow | Not in scope. |
| STEP import | Not in scope; needs an OCCT-class kernel. |
| SLA | Not in scope. |
| MMU / multi-nozzle colour mixing | Not in scope. Tool changers are. |
| Nesting arranger | Not in scope (Phase 5 ships shelf layout). |

Saying this out loud is the point: with these out, the remaining work is
bookkeeping around a working engine. With them in, this is a decade.

## Definition of done

1. `src/qt/CMakeLists.txt` links Qt and HFW. Nothing else.
2. `docs/engine-boundary.md` has no rows.
3. `git rm -r upstream/` changes nothing about what the application does.
4. `next-layer-qt` slices, previews, saves, reopens and prints with no C++ engine
   on disk.

## Order of work, and why

```
Phase 0  ─┬─ Phase 1 ─┬─ Phase 2 ─┬─ Phase 3 ── Phase 6
          │           │           ├─ Phase 4
          │           │           └─ Phase 5
```

Phase 1 blocks everything: without a library boundary the application cannot hold
the plate model HFW would own. Phase 2 is the largest single piece and the one
that retires the most C++. 3, 4, 5 and 6 are independent of each other once 2 lands.

## What each phase closes, measured

From `docs/engine-boundary.md` at the time of writing:

| Phase | Files that stop reaching into the engine |
| --- | ---: |
| 0 — application shell | 11 |
| 1 — geometry and mesh | 28 |
| 2 — document and plate | 5 |
| 3 — settings | 6 |
| 4 — preview | 6 |
| 7 — optional features | 13 |

Files overlap across phases; the union is 43 of 76. The count that matters is the
union going to zero.

## Risks

- **Phase 1 is irreversible in shape.** A C ABI is a compatibility promise. Get the
  mesh/slice handles right before phases 2–6 build on them.
- **Phase 2 can quietly grow.** A document model wants undo, selections, per-volume
  settings, instance transforms. Ship mesh + transform + settings first; the rest is
  a second pass.
- **The optional features are where the users of other slicers will object.** Paint
  and text are the two people notice. If they turn out to matter, they are separate
  projects against HFW's own geometry, never a return to upstream.
- **Two paths existing at once is the failure mode this plan exists to end.** Every
  phase deletes the C++ it replaces in the same commit that lands its replacement.

## Progress ledger

Update this table as phases land; it is the one thing in this document that should
change every time something is finished.

| Phase | State |
| --- | --- |
| 0 — one engine, one switch | Not started |
| 1 — C ABI | Not started |
| 2 — document model | Not started |
| 3 — settings | Not started |
| 4 — preview | Not started |
| 5 — plate operations | Not started |
| 6 — project files | Not started |
| 7 — optional features | Out of scope |
