# Retiring the C++ engine

The plan for running next-layer on Rust and nothing else.

Written 2026-11-17, at the state where the application slices a plate through HFW
but still links a 605,785-line C++ engine for everything else. `docs/engine-boundary.md`
is the measured worklist that goes with this document; it is generated from `src/`
and is the honest measure of progress.

The plan has **two halves that move at different speeds**, and the ledger at the
bottom of this file keeps them apart since 2026-11-20. The engine half is the
crates HFW must gain; it landed. The application half is the code in this
repository that stops calling the C++ engine; it has not started.

## Where the code is

| Repository | What it is | Size | State |
| --- | --- | --- | --- |
| `HFW` | The slicing engine, Rust, clean-room | 45,788 lines, 15 crates | Slices plates, writes G-code, verifies its own output. Gate green (`fish scripts/test-hfw.fish`, 82 sections + two-reference oracle). Every crate this plan named has landed. |
| `next-layer` | The application, Qt/QML with a cxx-qt backend | 26,550 C++ lines in 72 files, 917 Rust lines, 20,156 QML lines | Slices through HFW's C library; **3 of 72** backend files have a Rust replacement (`Resources`, `GcodeBrand`, `Scratch`), with the C++→Rust bridge (`src/app/src/bridge.rs`) proved end to end. |
| `next-layer/upstream` | A restructured PrusaSlicer, the engine | 605,785 lines, 28 modules | Linked today: `src/qt/CMakeLists.txt` still names `slic3r-app-cli`, `slic3r-biz-arrange`, `libpgcode`, `miniz`, `OCCTWrapper`. Not part of the application's git history. |

The application is 43 files away from owning nothing but its own interface. Those
43 files are the whole of this plan, and `tools/rust-port` is the bar: it counts
the C++ files in `src/qt/src` against the `.rs` files in `src/app/src` that
shadow them by name.

## The rule this plan follows

**No C++ engine. No translated C++. No vendored engine.** Every capability the
application needs is written in Rust — in HFW behind the ABI when the engine is the
right owner, in this repository's own `src/app` crate when the application is.

**Amended 2026-11-20.** The rule as first written allowed a second category, C++ in
the application as its own code, and left ~30 files there. That was the expensive
kind of compromise: two languages, one QML surface declared twice, and a port that
can stop half-done without anyone noticing. The decision is now that the whole
backend becomes Rust, and the only C++ left in this repository is what `upstream/`
compiles until the linker line stops naming it.

There is no third category. "Take it from upstream" is not an option that survives
this plan, and a partial port is worse than either kind of full one, because a
second dependency graph is what makes this project hard to hold in your head.

### How a file is ported

The C++ is deleted in the same step its Rust replacement lands — the rule the
risks section states, made mechanical:

1. the replacement goes into `src/app/src/<Name>.rs`;
2. the `.cpp` leaves the source list in `src/qt/CMakeLists.txt`, and the header
   stays only while a not-yet-ported C++ file still includes it;
3. QML keeps its own spelling: every object it reaches is re-declared as a cxx-qt
   QML element or singleton, so no `.qml` file changes for the port;
4. `tools/rust-port` moves by one and `tools/check` stays green in the same step.

Where Rust has to call code that is still C++, the bridge is `cxx`; where it calls
the engine, it is HFW's C ABI (`crates/capi/include/hfw.h`), so the engine boundary
stays the one promise both repositories agree on.

### The Qt boundary stays C++, and that is not a leftover (decided 2026-11-20)

"All Rust" is achievable everywhere except one seam. Qt's meta-object system is a
C++ ABI: a `QObject` is a vtable plus static `QMetaObject` data, and signal/slot
dispatch is C++ calling convention. Any language that hosts `QQmlApplicationEngine`
has to produce those objects, which is what cxx-qt does — it generates C++ and runs
`moc` over it. So the choice is not "Rust or C++ here", it is *whose* C++:

| option | verdict |
| --- | --- |
| Write our own Qt↔Rust bridge | Own code generator plus moc integration, months of work, and it still emits C++ compiled against Qt headers. No behaviour gained over cxx-qt. Rejected. |
| A pure-Rust Qt 6 binding | None exists. `qmetaobject-rs` also generates C++ and compiles it with `cc`. Rejected: nothing to adopt. |
| Replace Qt/QML with a Rust UI toolkit (egui, Slint) | The only genuinely all-Rust answer, and it deletes the entire QML surface, the Qt Quick 3D viewport and every widget behaviour already matched against a reference slicer. A different project, not a port. |

So the boundary is: **our code is Rust, and Qt's own C++ is the only C++ left** —
the generated glue plus a host `main` that starts the engine up. "No C++ engine" is
the promise; "no C++ compiler ever" is not reachable while QML is the front end.

### The last CMake, and why it outlives the engine until the end (decided 2026-11-20)

CMake is not here because of C++. It is here because that is how Qt is discovered
and how the binary is assembled, and the CMake being run today is `upstream/`'s own:
`patches/0001` is two `add_subdirectory` lines splicing `src/qt` and `src/cli` into
PrusaSlicer's build system. Delete `upstream/` and that build system is gone.

After it, cargo emits the executable, and the pieces already exist in cxx-qt 0.10:
`cxx-qt-build` compiles the generated glue with `cc::Build` (no CMake),
`QmlModule`/`.qrc()` embeds QML and resources into the binary, and `qt-build-utils`
supplies Qt's include and link flags for `cargo:rustc-link-lib`.

**It cannot land before then, and that is the whole reason to write it down.** Until
`upstream/` is gone the application links `slic3r-shared`, `slic3r-domain`,
`libvgcode`, `OCCTWrapper` and the rest — sixty-odd CMake targets that only
`upstream/`'s build system knows how to produce, with a dependency prefix and
configure flags to match. Cargo cannot build them. So the cargo-emitted binary
arrives in the same commit as `git rm -r upstream/`, and what it costs is real:
resource embedding moves from `qt_add_resources` to a `.qrc`; `qmllint` becomes a
gate that calls the tool directly instead of a Qt CMake helper; `dist/` deployment
is scripted rather than `cmake --install`. QML keeps loading from disk during
development through `NEXT_LAYER_QML_DIR` either way.

### What is deleted rather than ported

Phase 7's thirteen files — `Paint`, `Text`, `Cut`, `Hollow` and the feature gates
in `SliceRuntime.cpp` and `SceneSelftest.cpp` that drive them — are **deleted**, not
rewritten: the settings surface already carries their seven `capabilities` rows, so
the panel can render "unavailable" from one document. `SceneSelftest.cpp` (2,527
lines) and `SliceRuntime.cpp` (987 lines, 109 engine references between them) are the
largest single deletion in this plan and need no replacement at all.

### What the two reference harnesses lose with `upstream/`

- `oracle/oracle.py capture` re-slices with the stock engine to rewrite the
  baselines. After `upstream/` goes it can never run again: the twelve files in
  `oracle/baseline/` become frozen goldens and `verify` becomes a regression gate
  against them, not a comparison with another engine.
- `tools/parity.py` drives the C++ engine's own launcher as its reference arm. Its
  own message — "there is no second engine to compare against" — is the retirement
  notice; the arm goes with `upstream/`.

Both decisions belong in the commit that removes `upstream/`, so nothing keeps
looking for a reference arm that no longer exists.

## What is being replaced, in the order it must be done

### Phase 0 — One engine, one switch (done when the legacy path is unreachable by default)

**Landed 2026-11-18.** The switch is gone: `hfwRequested()` was deleted, both
libslic3r branches in `SliceController` were deleted with it, and `--slice`
(run by the import, file, filament and catalog gates) now goes through
`sliceWithHfw` rather than the C++ CLI. `SliceRuntime::slice`/`slicePlate` remain
only as the entry points `SceneSelftest`'s feature gates drive, and they go with
the features phase 7 puts out of scope.

The two settings surfaces that had to change with it: the settings HFW's ledger
records as not applied are now **hidden by default** (shipped as
`src/qt/assets/engine-capabilities.json`, `--engine-capabilities none` to switch
the filter off), and the print file records the settings it was sliced with,
because HFW writes header lines and no config block.

Two defects were found and fixed on the way, both older than the swap:
`--selftest-slice` and `--slice-all` emitted their verdict before the event loop
started, so a refusal hung the process with a session bus present; and neither
resolved the remembered printer/process/filament, so a headless slice of a set-up
machine refused itself.

Still open in this phase: nothing. HFW's config intake converts every spelling a
preset in this application's bundle hands it — 0 of 360 keys unconverted on the
Voron configuration, measured 2026-11-18 — so supports, arcs and ironing now reach
the engine instead of being silently dropped.

### Phase 1 — A library boundary (the decision that shapes the rest)

HFW was a **subprocess** when this phase was written: the application wrote STLs
and a config, ran `hfw-cli`, read G-code back. That is right for slicing and wrong
for everything interactive, because the viewport needs mesh data sixty times a
second and a process boundary cannot carry it.

HFW grows a **C ABI crate** — `crates/capi` — with a flat, versioned surface:

```
hfw_mesh_load_file   hfw_mesh_from_bytes   hfw_mesh_transform   hfw_mesh_bounds
hfw_mesh_free        hfw_plate_slice       hfw_plate_slice_placed  hfw_gcode_free
hfw_last_error
```

Handles, not Rust types, cross the boundary. `hfw-cli` keeps working and is how
the engine is tested; the application uses the ABI and no longer spawns anything.
`hfw_plate_slice_placed` is the command line's `--place` in the ABI: `hfw_plate_slice`
keeps the document's coordinates, so a caller with bare geometry and no document has
no other way to ask for the centring.

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

**Landed 2026-11-20.** `hfw_plate_preview_json` returns every planned bead per
layer with position, per-vertex width, flow and the writer's own `;TYPE:` label, so the
viewer can be written against the engine instead of `libvgcode`. A long job runs on its
own thread (`hfw_job_start`/`_poll`/`_cancel`/`_wait`/`_take_gcode`), so the UI keeps
drawing and a plate edit can cancel work in flight; progress is polled rather than
pushed, and cancellation is observed between objects and before the file is written.

`GcodeView` already parses G-code itself; what it borrows from the engine is types
(`libvgcode::FdmViewerInputData`, `GCodeExtrusionRole`). HFW can emit every path
with its role (`--json-paths`), and the G-code already carries `;TYPE:` markers.
The preview is a small, self-contained replacement — days, not weeks.

**Delivers:** the 5,000-line `libvgcode`+`libpgcode` pair is no longer linked.

### Phase 5 — Plate operations

**Landed 2026-11-20.** The plate document and the ABI carry duplicate, turn about the
object's centre, mirror in place, delete, and a bed arrange; scale, translate and any
composed placement are `hfw_plate_object_placement`. The arranging packer moved from
`cli` into `crates/plate` so the panel and the command line share it. Shelf layout
only — the nesting arranger stays out of scope, as this section says.

Load, scale, rotate, translate, mirror, delete, duplicate: HFW's mesh API plus its
own geometry. Arrange: HFW has shelf layout (`--spacing`, `--no-arrange`); a nesting
arranger is a project of its own and is **not** in this plan's definition of done.
One plate of parts is shelf layout; that is what gets used.

**Delivers:** `Arranger.cpp`, `Orient.cpp`, `TransformController.cpp` deleted.

### Phase 6 — Project files

**Landed 2026-11-20.** A plate saves as a `.3mf` (`hfw_plate_project_bytes`) and
reopens as the same plate (`hfw_plate_open_project`), slicing to byte-identical
G-code. The layout is a standard model part; the enabled flags and each object's
settings are in `Metadata/hfw_project.xml`, written by inverting the settings parser
(`Settings::key_value`) so a value we save always loads. Still open: plate-wide
settings in the project part, instances, and `ProjectFile.cpp`'s extra UI state,
which is the application's to keep.

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
| Adhesion painting | Adhesion paint is `brim_type`. Support and seam paint **left this table**: HFW carries both per triangle (`hfw_plate_object_paint`, channels `HFW_PAINT_SUPPORT` and `HFW_PAINT_SEAM`), consumes support paint inside support generation and seam paint in the writer's seam chooser, and saves both in the project — so the app's paint tool has an engine behind it as soon as it calls the ABI. |
| Text embossing, font loading | Not in scope. |
| Cut, hollow | Not in scope. |
| STEP import | Not in scope; needs an OCCT-class kernel. |
| SLA | Not in scope. |
| MMU / multi-nozzle colour mixing | Not in scope. Tool changers are. |
| Nesting arranger | Not in scope (Phase 5 ships shelf layout). |

Saying this out loud is the point: with these out, the remaining work is
bookkeeping around a working engine. With them in, this is a decade.

## Definition of done

1. `cargo build` emits `next-layer-qt` on its own: no CMakeLists anywhere in this
   repository, no dependency prefix, no `upstream/`.
2. `docs/engine-boundary.md` has no rows — nothing includes a `Slic3r/` header.
3. `git rm -r upstream/` changes nothing about what the application does.
4. `next-layer-qt` slices, previews, saves, reopens and prints with no C++ engine
   on disk.
5. The only C++ compiled is cxx-qt's generated glue and the host `main`, both from
   this repository.

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

Files overlap across phases; the union is 44 of 71 source files under `src/`. The
count that matters is the
union going to zero.

## Phase 2 was blocked on five capabilities, and all five have landed (measured 2026-11-20)

`PlateDocument::sliceObjectsFor` hands the slicer five things the C ABI has no call
for. They are not details — each is a shipped feature:

| the app passes | ABI | what stops working |
| --- | --- | --- |
| three `TriangleSelector` paint channels (support, seam, tool) | **support and seam done 2026-11-20**: `hfw_mesh_paint_set`, `hfw_plate_object_paint`, consumed by `supports::seed_masks` and by the writer's seam chooser, both carried in the project; only the tool channel has no call | support and seam painting work through the ABI; per-tool paint does not, and cannot until multi-material is real |
| `volumes`: part, negative, modifier, support blocker/enforcer | **part, negative, support blocker and support enforcer done 2026-11-20** (`hfw_plate_object_add_volume`): the first two through the kernel's ring booleans on every layer's outline, the last two through the same mask a painted facet drives | modifier volumes still have no call, because a layer under one carries two settings at once and the planner does not split a layer into regions |
| plate-file transport (`hfw-cli --plate`) | **done 2026-11-20** (HFW `4de43eb` + `52c802e`): a plate file is sliced through `hfw_plate::Plate`; the run reports what the document carried and warns about a stroke whose Z matches no layer. `crates/cli/tests/plate.rs` drives the binary end to end | — |
| the app holds that plate | **done 2026-11-20**: `SliceController` builds an `HfwPlate` through the C ABI (`hfw_mesh_new`, `hfw_plate_object_set`, volumes, layer heights, markers, strokes) and slices it in process with `hfw_plate_slice` — no file, no subprocess (since 2026-11-20 late); `hfw_plate_project_bytes` writes the same plate when the caller wants to save it. The app links HFW's static library (`src/qt/CMakeLists.txt` builds `hfw-capi` with cargo); `tools/check`'s plate-transport gate slices a document with a modifier, a marker and a stroke and reads all three back out of the G-code | — but paint is reduced on the way: the app's split triangles become one state per facet (area majority), so a painted edge lands on a facet boundary |
| modifier volumes (settings scoped to a region) | **done 2026-11-20** (`5c42493`): `VolumeKind::Modifier`; the plan is split into settings groups, each planned over the object's own slicing planes and folded together; settings arrive as key/value rows through `hfw_plate_volume_set`, stored in project format version 7 and resolved over the object's own at slice time | nothing: this closes the list of things the C++ plate hands the slicer |
| in-layer polylines (the calibration channel) | **done 2026-11-20**: `hfw_plate_set_in_layer` / `_count` / `_get`, carried in project format version 6, printed as real beads under `;TYPE:Custom` at the end of the layer they name, with their own width, height, speed and a G-code prefix; `hfw-cli --in-layer` reads the same list from a file | nothing: this is the last calibration band |
| plate timeline markers (custom G-code, pause, colour change at Z) | **done 2026-11-20**: `hfw_plate_set_markers` / `_count` / `_get`, carried in project format version 5 and written at the layer that reaches the height, merged with the profile's own pause and colour-change heights; `hfw-cli --markers` reads the same list from a file. A **tool-change** marker is refused, not emitted: HFW has one extruder | nothing for the three kinds that exist; the tool-change row waits on multi-material |
| per-object `ZHeightPairs` layer heights | **done 2026-11-20**: `hfw_plate_object_layer_heights`, carried in project format version 4, with `by layer` merging the plate on Z instead of on layer number | nothing: the app's own `ZHeightPairs` maps onto the ABI pair for pair |
| `layerGcodes`: custom G-code, pause, tool change at Z | nothing on the plate | pause markers and calibration's own G-code |
| `inLayer` polylines: calibration labels and bands | nothing | every calibration test's band text and pattern |

Two ways forward, and the choice is a product one, not an implementation one:

1. **Grow the ABI to carry all five — chosen 2026-11-20.** Paint is the expensive
one: HFW had no triangle-state model and its `capabilities` list declined paint, text,
cut and hollow outright, so this is engine work with a real cost, not a mapping. The
support channel of it has landed in HFW (`4aa19c2`, `docs/PAINT.md`); seam paint,
volumes, per-object layer heights, layer-G-code markers and in-layer polylines are the
rest of the list, and each one is a feature ported forward, not a mapping.
2. **Split the document** — rejected 2026-11-20 as a hack: it would leave the paint
model, volumes and text in C++ permanently, and painting is the one thing the app has
to do better than the references rather than later.

**Option 1 was chosen and it is finished.** Every row above is an ABI call in both
directions: paint (support and seam; the tool channel waits on multi-material),
volumes of all four kinds including modifiers, per-object layer heights, timeline
markers and in-layer strokes. `tools/check`'s `--plate-readback-test` and
`--plate-document-test` are what keep that true — a document goes in, comes back
rebuilt from nothing but what was read, and both slice to the same bytes.

So `PlateDocument.cpp` is no longer blocked on the engine's capabilities. What is
still in the way is the vocabulary: `tools/engine-boundary.py` counts **20 engine
symbols in the file and 10 in its header** — `TriangleMesh`, `Transformation`,
`Transform3d`, `Index3`, `Model`, `ModelObject`, `ModelInstance`, `ModelVolume`,
`ModelVolumeType`, `ZHeightPair(s)`, `Vec3d`, `Vec3f`,
`TriangleSelector::TriangleSplittingData`, `indexed_triangle_set_with_color`,
`read_model_from_file`, `store_stl`, `get_import_extensions`, `construct_ptr`,
`Algorithms::TriangleMesh`. Those are phase 1's geometry and mesh boundary, not
phase 2's document model, which is why the two phases now move in the other order:
the types go Rust first, and the plate becomes a view over them.

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

Two tables, because the two halves are not the same work and were being read as
one. "Engine" is what HFW had to grow; "application" is what this repository had
to stop calling the C++ engine for.

| Phase | Engine half (HFW) | Application half (this repo) |
| --- | --- | --- |
| 0 — one engine, one switch | n/a | **Landed 2026-11-18**; the C++ slice branches are gone, HFW's ledger hides 175 settings, `tools/check` was green |
| 1 — C ABI | **Landed 2026-11-20**: `crates/capi` v1, `include/hfw.h`, exercised from C | **Landed 2026-11-20 (transport)**: `HfwEngine.cpp` slices through `hfw_plate_slice` / `hfw_plate_slice_placed` and writes the text itself; the `hfw-cli` subprocess, the temporary plate file, `hfwBinary()` and its start-up check are gone, and `tools/check` is green with the oracle at 12/12 byte-identical. The file itself is still C++, and porting it is blocked on the mesh types `SliceRuntime.hpp` carries (`Slic3r::Domain::TriangleMesh`, `Transformation`, `ModelVolumeType`, `TriangleSelector`) |
| 2 — document model | **Engine half done 2026-11-20** (HFW `d5cea38`): `crates/plate` plus a *read-back* surface — object ids, name, placement, enabled flag, settings JSON, geometry with its paint, and each volume's kind/geometry/settings — so a document round-trips through the engine byte for byte. `tools/check`'s `--plate-readback-test` builds a plate from a document, reads every field back through the ABI, rebuilds a second plate from what it read and requires the two project files to be identical (13,650 bytes) | **Unblocked, not started**: `PlateDocument.cpp` (3,543 lines) is still the plate. Everything it hands the slicer has an ABI call in both directions now. What it still owns is the *view's* state — undo, plates and plate names, selection, tool colours, per-volume visibility, and each object's source path. **The view direction is proved too**: `--plate-document-test` slices a document, reads the engine's plate back into a second document that never saw the first, and requires both to print the same 647,349 bytes |
| 3 — settings | **Landed 2026-11-20**: settings surface JSON + `hfw_plate_object_set` | **Not started**: `SettingsModel.cpp` (1,568) still reads the C++ `ConfigContainer` |
| 4 — preview | **Landed 2026-11-20**: `hfw_plate_preview_json` + threaded job | **Not started**: `GcodeView`/`PreviewData` still borrow libvgcode/libpgcode types |
| 5 — plate operations | **Landed 2026-11-20**: duplicate/rotate/mirror/arrange in `crates/plate` | **Not started**: `Arranger.cpp`, `Orient.cpp`, `TransformController.cpp` still slice with the C++ engine's geometry |
| 6 — project files | **Landed 2026-11-20** (HFW `7f25a00`): `hfw_plate_project_bytes`/`hfw_plate_open_project`, object placement carried as a pose rather than baked into the coordinates, and `hfw_plate_set_metadata`/`_get` for state the engine has no concept of (stored base64, project format v8) | **Application half landed 2026-11-20**: `saveProject`/`loadProject` go through the engine's document plus that metadata, so a saved project is one file the engine can slice. `ProjectFile.cpp` keeps only its reader, for projects written before the move; `ThreeMfProject.cpp` still serves the foreign-project import and the `.3mf` another slicer opens |
| 7 — optional features | **Settled 2026-11-20**: seven `capabilities` rows, `docs/SCOPE.md` | **Not started**: the thirteen files are deleted, not ported |

Outside the phases, because they needed no engine at all, five shell files are
already Rust and deleted from `src/qt/src`: `Resources`, `GcodeBrand`, `Scratch`,
`Palette` and `Updater` (the last as a cxx-qt QML singleton over `ureq`). They
proved the shape every later port reuses — one `#[cxx::bridge]` in
`src/app/src/bridge.rs`, the C++ header kept as inline Qt adapters, the `.cpp`
deleted — and `tools/rust-port` now reads **4 of 69** backend files replaced, 8 Rust
files, 1,639 lines.
