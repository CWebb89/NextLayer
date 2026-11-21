# The port graph

Every C++ module under `src/qt/src`, what it needs from the others, and the order
the port can retire them in. `docs/rust-engine-plan.md` says *what* each phase
replaces; this says *in what order the files can move*, and which ones can never
move because phase 7 deletes them instead.

Measured 2026-11-20 by 27 read-only surveys (one per module pair, five at a time)
plus the twelve already surveyed. Nothing here was edited to produce it.

## How a row was established

- **Includes** — the module's own `#include "…"` lines, read from the file, not
  inferred. A module that only forecloses a type by declaring a pointer to it is
  *not* listed; a dependency here means a header the module actually includes.
  Two surveys named their dependents as dependencies (`MeshGeometry` reported
  `Session.hpp` and `PlateScene.hpp`; both include `MeshGeometry`, not the
  reverse) and are corrected here.
- **Engine refs** — counted out of `docs/engine-boundary.md` §"Every file, and
  what it uses", summing the module's rows and expanding the `+N` truncation.
  The surveys' own counts were used where they agreed and recomputed where they
  did not (`GcodeView` 4 → 33, `Toolpath` 8 → 2, `PreviewData` and `HfwEngine`
  likewise). A module with no row in that section has **0**.
- **QML surface** — `Q_PROPERTY` / `Q_INVOKABLE` / `signals:` counts in the
  module's header, and how `main.cpp` registers it.
- **Lines** — `wc -l` of the module's own files.

## The modules

`⧉` = already surveyed, carried over. `✝` = phase 7 deletes it, so it has no
level. `↻` = Rust-backed adapter header: the logic is already in
`src/app/src/<Name>.rs`, so it is level 0 by construction — the work is deleting
the header and its two lines, not writing a replacement.

| Module | Lines | Includes (src/qt/src) | Engine refs | QML surface | Verdict | Deps before port |
| --- | ---: | --- | ---: | --- | --- | --- |
| PlateDocument ⧉ | 4479 | Arranger, Paint✝, SliceRuntime✝, ThreeMfProject, ConfigBoxJson, ForeignSettings, HfwEngine, ProjectFile, Orient, Cut✝, Hollow✝, Text✝, GcodeBrand↻, Scratch↻ | 30 | 27 props / 102 invokables / 13 signals, singleton | port, phase 2 | Arranger, Orient, ConfigBoxJson, HfwEngine, ProjectFile, ThreeMfProject, ForeignSettings |
| SceneSelftest ⧉ | 3028 | PlateDocument | 26 | 0 | **delete, phase 7** | — |
| main.cpp ⧉ | 2573 | every singleton, 28 includes | 20 | 0 own; registers 13 singletons + 21 context props | **stays C++** (DoD 5) | — |
| SettingsModel ⧉ | 1648 | PresetYaml | 3 | 21 invokables / 1 signal, singleton, 14 QML files | **landed 2026-11-21** — logic in `src/app/src/SettingsModel.rs`; the header is the QObject shell, the `QVariant` conversions and the `get_defs_fdm()` transcription | PresetYaml |
| PresetStore ⧉ | 1654 | Resources↻, SliceRuntime✝, PresetFiles, PresetImport, PresetYaml, SettingsModel | 0 | 11 props / 22 invokables / 10 signals, singleton | port | PresetFiles, PresetImport, SettingsModel |
| PresetImport ⧉ | 1332 | PresetFiles, SettingsModel | 0 | 0 | port | PresetFiles, SettingsModel |
| Calibration ⧉ | 1086 | PlateDocument, SettingsModel | 0 | 3 members, singleton | port | PlateDocument, SettingsModel |
| HfwEngine ⧉ | 1115 | SliceRuntime✝, Paint✝ | 20 | 0 | **landed 2026-11-21** — logic in `src/app/src/HfwEngine.rs`; the header is the Qt-typed adapter that turns `TriangleMesh`, `Transformation` and paint into the bridge's arrays | PlateDocument |
| SliceRuntime ✝⧉ | 1183 | — | 67 | 0 | **delete, phase 7** | four plate structs must move first |
| PlateScene ⧉ | 979 | PlateDocument, Paint✝, MeshGeometry | 10 | 20 props / 7 invokables / 2 signals, singleton | port | PlateDocument, MeshGeometry |
| GcodeView ⧉ | 926 | FeatureColors, PreviewData | 33 | 6 props / 4 signals, `qmlRegisterType` | port, phase 4 | FeatureColors, PreviewData |
| PreviewData ⧉ | 733 | FeatureColors | 23 | 0 | port, phase 4 | FeatureColors |
| SliceController | 752 | GcodeBrand↻, HfwEngine, PlateDocument, PlateThumbnail, SettingsModel, SliceRuntime✝, Scratch↻ | 0 | 10 props / 8 invokables / 10 signals, singleton | port, phase 2 | HfwEngine, PlateDocument, SettingsModel, PlateThumbnail |
| PrinterLink | 638 | PresetStore | 22 | 12 props / 8 invokables / 3 signals, singleton | port, phase 0 | PresetStore |
| ThreeMfProject | 637 | miniz only | 0 | 0 | **landed 2026-11-21** — logic in `src/app/src/ThreeMfProject.rs` (its own archive reader), the header is the adapter | — |
| Toolpath | 613 | PreviewData, GcodeView | 2 | 21 props / 14 invokables / 1 signal, singleton | port, phase 4 | PreviewData, GcodeView |
| ProjectFile | 497 | Paint✝, Scratch↻ | 14 | 0 | **landed 2026-11-21** — logic in `src/app/src/ProjectFile.rs`, the header is the adapter | — |
| Paint ✝ | 481 | — | 19 | 0 | **delete, phase 7** | shared types move first |
| ForeignSettings | 416 | — | 0 | 0 | port, phase 3 | — |
| QtPlatform | 391 | — | 19 | 0 | port, phase 0 | SliceRuntime✝ (its install site) |
| ConfigBoxJson | 307 | — | 14 | 0 | port, phase 3 | PlateDocument |
| MeshGeometry | 298 | — | 4 | 2 props / 1 invokable / 1 signal, singleton, AUTOMOC | **shell split landed 2026-11-21** — the buffers and the raycast are `src/app/src/MeshGeometry.rs`; the header keeps the `QQuick3DGeometry` class | — |
| PresetFiles | 247 | PresetYaml | 0 | 0 | **landed 2026-11-21** — logic in `src/app/src/PresetFiles.rs`; header-only, so the `.hpp` stays an adapter for `PresetStore.cpp` and `PresetImport.cpp` | PresetYaml |
| Text ✝ | 230 | — | 23 | 0 | **delete, phase 7** | — |
| TransformController | 223 | Scratch↻ | 7 | 1 prop / 7 invokables / 3 signals, singleton | port, phase 5 | — |
| Host | 215 | Palette↻, SliceRuntime✝ | 1 | 14 props / 1 invokable / 4 signals, singleton | **landed 2026-11-21** — logic in `src/app/src/Host.rs`; the header is the QObject shell over Qt's own path resolution and the palette copy | — |
| Orient | 194 | — | 5 | 0 | port, phase 5 | PlateDocument |
| Session | 186 | Host, MeshGeometry, PlateDocument, SliceController | 1 | 3 props / 5 invokables / 1 signal, singleton | **landed 2026-11-21** — the view, the naming and the camera arithmetic are `src/app/src/Session.rs`; the header is the Qt shell holding the four pointers it pushes into | PlateDocument, SliceController |
| Arranger | 158 | — | 27 | 0 | port, phase 5 | PlateDocument |
| Cut ✝ | 151 | — | 13 | 0 | **delete, phase 7** | PlateDocument::cutAtZ moves first |
| PlateThumbnail | 144 | — | 0 | 0 | port, phase 1 | — |
| FeatureColors | 60 | — | 0 | 0 | port, phase 4 | — |
| PresetYaml | 56 | — | 0 | 0 | **landed 2026-11-21** — logic in `src/app/src/PresetYaml.rs`; header-only, so the `.hpp` stays an adapter until `PresetStore.cpp` ports | — |
| Palette ↻ | 48 | — | 0 | 2 props / 0 invokables / 0 signals, not registered | port, phase 0 | — |
| Hollow ✝ | 45 | — | 4 | 0 | **delete, phase 7** | PlateDocument::hollowObject moves first |
| Scratch ↻ | 38 | — | 0 | 0 | port, phase 2 | — |
| GcodeBrand ↻ | 34 | — | 0 | 0 | port, phase 0 | — |
| Resources ↻ | 24 | — | 0 | 0 | port, phase 0 | — |
| GlewWrap | 14 | — | 0 | 0 | port, phase 4 | — |

39 modules, 27,833 C++ lines — that is every line of the backend as `tools/rust-port` counts it. Six modules are deletions, one is `main.cpp`.

## The level assignment

`Level 0 = nothing still in C++ among its deps. Level N = 1 + the highest level
among its deps.` Three rules make that computable rather than circular:

1. **Deletions do not set a level.** `Paint`, `Text`, `Cut`, `Hollow`,
   `SliceRuntime` and `SceneSelftest` are removed, not ported — their dependents
   gain nothing from waiting for them, they gain from the deletion landing first.
   Their ordering is the deletion table below.
2. **`main.cpp` has no level.** DoD 5 keeps it C++, so it is an edit site, not a
   port target: its registration lines change inside everyone else's commit.
   Treating it as a dependency would make `Host`, `MeshGeometry` and `Session`
   unportable forever.
3. **The four `↻` adapters are level 0 as *work*, not as deletions.** Their headers are
   Qt-typed shims onto `src/app/src/<Name>.rs` (`GcodeBrand.hpp`: four inline
   functions converting `std::string` to `QString`), so a module that includes one
   is not waiting on a port. But the header is what keeps the spelling for C++
   that has not moved, and it is included by unported files — `GcodeBrand` by
   `PlateDocument`, `SliceController`, `SceneSelftest`, `main`; `Scratch` by nine
   files including `SliceController.hpp`; `Palette` by `Host`, `main`;
   `Resources` by `PresetStore`, `main`. **None of the four can be deleted before
   those consumers are ported**; deleting one now leaves the C++ with no
   Qt-typed entry point. They die with their last C++ includer, like every other
   header.

### What porting a `.cpp` looks like, in this repository's own pattern

`GcodeBrand.hpp` is the reference: the logic lives in Rust, the header keeps four
inline Qt-typed adapters, the `.cpp` is gone. So porting `X.cpp` is three moves,
not one:

1. the logic goes to `src/app/src/<Name>.rs`, exposed over `bridge.rs` in
   plain types (see `hfw.rs`);
2. `X.hpp` becomes adapters onto the bridge, keeping every spelling its C++
   callers already use;
3. `X.cpp` and its line in `tools/cpp-baseline` leave in the same commit.

Step 2 is why a leaf's includer count is the real cost of porting it, not its
line count: `TransformController` has 223 lines and one includer (`main.cpp`),
while `Scratch.hpp` has 38 lines and nine. A header-only module (`FeatureColors`,
`PresetYaml`) has no `.cpp` to delete, so it is not a `rust-port` win at all and
belongs in the commit that ports its includers.

| Level | Count | Modules |
| ---: | ---: | --- |
| 0 | 15 | FeatureColors, ForeignSettings, GcodeBrand↻, GlewWrap, Host, MeshGeometry, Palette↻, PlateThumbnail, PresetYaml, ProjectFile, QtPlatform, Resources↻, Scratch↻, ThreeMfProject, TransformController |
| 1 | 3 | PresetFiles (PresetYaml), PreviewData (FeatureColors), SettingsModel (PresetYaml) |
| 2 | 2 | PresetImport (PresetFiles, SettingsModel), GcodeView (FeatureColors, PreviewData) |
| 3 | 2 | PresetStore (PresetImport, SettingsModel), Toolpath (PreviewData, GcodeView) |
| 4 | 1 | PrinterLink (PresetStore) |
| — | 5 | **PlateDocument, Arranger, Orient, ConfigBoxJson, HfwEngine — one strongly connected component** |
| A+1 | 3 | PlateScene, Calibration, SliceController (each needs PlateDocument or HfwEngine) |
| A+2 | 1 | Session (needs SliceController) |
| — | 1 | main.cpp — never ported |
| deleted | 6 | Paint, Text, Cut, Hollow, SliceRuntime, SceneSelftest |

### The cycle at the centre

`PlateDocument` includes `Arranger`, `ConfigBoxJson`, `HfwEngine`, `Orient`,
`ProjectFile` and `ThreeMfProject`; four of those take its types back:

| Edge | Evidence |
| --- | --- |
| PlateDocument → Arranger | `PlateDocument.hpp:6`, `PlateDocument.cpp` includes `Arranger.hpp` |
| Arranger → PlateDocument | `PlaceableObject`/`Placement` are declared in `Arranger.hpp` but `applyPlacements` takes the document's objects (`PlateDocument.hpp:582`, body `PlateDocument.cpp:2572-2613`) |
| PlateDocument → ConfigBoxJson | `PlateDocument.cpp` includes `ConfigBoxJson.hpp`; `overriddenSettings` takes `const Slic3r::Domain::ConfigBox&`, passed as `ModelVolume::volume_settings` at `PlateDocument.cpp:1301` and `:1508` |
| PlateDocument ↔ HfwEngine | `PlateDocument.cpp` includes `HfwEngine.hpp`; `HfwEngine::setPaint` walks the document's `Object` paint channels |
| PlateDocument ↔ Orient | `autoOrient` is declared on the document, implemented in `Orient.cpp`, and takes the document's mesh |
| PlateDocument → ProjectFile | `PlateDocument.cpp` builds `ProjectObject`/`ProjectMetadata` (`:3537-3571`) — one-way, so **`ProjectFile` is level 0, not part of the cycle** |

Five files that cannot be separated by ordering. Two ways out, and the plan has
already chosen the second: port them as one commit, or extract the vocabulary
they share (`Object`, `Volume`, `PaintKind`, `PlaceableObject`, `Transform3d`)
into a Rust module first and let the five become five separate ports. The
shared-type track is `PlateDocument` + `ProjectFile`, which is why `ProjectFile`
(level 0, 497 lines, a closed pair of functions) goes first and proves the
vocabulary the other four are waiting on.

### The vocabulary ProjectFile settled (2026-11-21)

`cxx` will not put `TriangleMesh`, `Transformation`, `QVariantMap` or
`TriangleSplittingData` in a shared struct, so porting `ProjectFile` meant choosing
what the five files in the cycle will speak when they arrive. The choice, now in
`src/app/src/bridge.rs` and used by `ProjectFile.hpp`:

| concept | the form that crosses | why this one |
| --- | --- | --- |
| geometry | binary STL bytes, `Vec<u8>` (`ProjectObjectRecord.mesh`) | the project file already stores exactly these bytes, so the port added no format; `store_stl`/`load_stl` stay in the C++ adapter because the engine's mesh is still there, and they disappear into `mesh::Mesh` when `PlateDocument` lands |
| transform | sixteen `f64`, row-major 4×4 (`matrix`) | the file's own array, so nothing is reinterpreted; flattened because cxx carries `Vec<f64>` inside a shared struct, not `[f64; 16]` |
| paint channel | flat `(triangle index, bitstream start)` `i64` pairs, the bitstream as `0`/`1` text, and the state numbers the channel sets (`ProjectPaint`) | it is the file's encoding, engine-free; `used_states` is sized from `TRIANGLE_STATE_TYPE_COUNT` in the adapter, which is the only side that knows the count |
| layer height profile | flat `(z, height)` `f64` pairs | same reason as the matrix |
| settings | a discriminated record: `kind` 0 text / 1 bool / 2 int / 3 number / 4 `{value, is_percent}`, plus `truth`, `number`, `text` | a `QVariant` cannot cross, and these five are the only shapes the app stores |
| names | `ProjectObjectRecord`, `ProjectVolumeRecord`, `ProjectMetadataRecord`, `ProjectMarkerRecord` | `next_layer::ProjectObject` is taken by the C++ struct its callers still build, so the shared structs carry a `Record` suffix rather than a second name for the same thing |

One consequence worth stating: after this, `PlateDocument.cpp` still owns the STL
round trip, because its meshes are engine meshes. That conversion is the last
engine-shaped thing in the project file path and it leaves with `PlateDocument`.

## The deletions

| Module | Lines | What has to move first |
| --- | ---: | --- |
| SliceRuntime | 1183 | The four plate structs (`PlateSliceObject`, `PlateLayerGcode`, `PlateInLayerPolyline`, and the by-value `SliceRuntime` member) live in `SliceRuntime.hpp` and three files include them: `SliceController.hpp` (member), `HfwEngine.hpp`, `PlateDocument.hpp`. They move to the Rust plate module; `SliceController`, `HfwEngine`, `PlateDocument`, `PresetStore` and `QtPlatform` drop the include in the same commit (`Host` already has: its `.cpp` left in the phase-0 port, and `HfwEngine`'s left with its own). `tools/check` drives `--scene-selftest`, `--calibration-test` and `--paint-perf` into `SceneSelftest.cpp → SliceRuntime::slice`, so those gate expressions go too. |
| SceneSelftest | 3028 | Every `tools/check` feature gate calls it: `--mesh-tools-test` (cut/hollow/text), `--scene-selftest` (orient/arrange), `--paint-perf`, `--calibration-test`. The gate edits land in the same commit as the deletion, and each one has to name the capability it loses; `--engine-capabilities` is where the panel then reads "unavailable". |
| Paint | 481 | Not a leaf: `Paint.hpp` carries `PaintKind` and `PaintSession`, `PlateDocument.hpp` keeps a `QHash<qint64, shared_ptr<PaintSession>>` in its own API, `Object::supportPaint/seamPaint/toolPaint` are `TriangleSelector::TriangleSplittingData`, and `ProjectFile.cpp` uses `paintToText`/`paintFromText` from `ProjectFile.hpp`. Support and seam paint **left** the deleted list in phase 7 (HFW carries both and saves both), so the document model must reach the ABI — `HfwEngine::setPaint` already reduces facet states to `HFW_PAINT_SUPPORT`/`HFW_PAINT_SEAM` — before the file can go. Only tool paint is genuinely unreachable: the ABI has no channel for it. |
| Text | 230 | `TextFont` is declared here and consumed by `PlateDocument.cpp` (`fonts()`:539, `addText`:553), so `PlateDocument`'s `Q_INVOKABLE addText`/`fonts` leave in the same commit, and `main.qml:2568-2570` (`TextObjectDialog`) with them. `tools/check:1275` greps the `text:` line out of `--mesh-tools-test`. |
| Cut | 151 | `CutHalves` in `Cut.hpp` is consumed by `PlateDocument.cpp:590, 640, 644`, and `PlateDocument::cutAtZ` is both a QML binding (`main.qml:1603`) and a gate (`--mesh-tools-test`). `cutMesh` also serves calibration: `trimToBand` calls it twice (`:641, :645`), so deleting `Cut` removes band trimming as well as the cut tool. |
| Hollow | 45 | Only caller is `PlateDocument::hollowObject` (`:679`); the `Q_INVOKABLE` (`PlateDocument.hpp:500`), `main.qml`'s `HollowDialog` (`:2580`) and the `objHollow` menu row go with it, and `SceneSelftest.cpp:1694` drives it as a feature gate. |

## Traps, by module

The ones that change the order or the design. Three each at most, as surveyed.

**Level 0, clean leaves** — `FeatureColors`: the whole file is one free function
whose `QVariantMap` palette argument is `Q_UNUSED`, and matching is by the
engine's own `;TYPE:` labels, most of them with two spellings ("Outer
perimeter"/"Outer wall") — a port keyed to one dialect silently falls through to
the default colour. `ForeignSettings`: `oracle/foreign_settings_check.py` reads a
three-column TSV out of `--dump-foreign-settings` as the gate, and
`foreignNameFor`'s "weakest match wins" scan is `QHash`-iteration-order
dependent, so a `HashMap` rewrite changes which target wins. `PlateThumbnail`: the
header comment contradicts the code — the blocks are written at offset 0, not
before the config block, and the gate requires offset 0. `ThreeMfProject`:
`__first_triangle` is a synthetic key injected into a volume's settings to map
triangle ranges and must not reach the settings ledger; a port needs its own
archive reader in place of `miniz`. `TransformController`: the seven
`Q_INVOKABLE`s have no QML caller (only `busy` and `applied` are bound), its
`cut()` wraps `transform.cut_z` — a phase-7 capability — and the free
`runTransform(modelPath, outputPath, std::function<void(InitParams&)>)` behind
`--transform` cannot keep its engine-typed signature. `QtPlatform`: it mixes a
phase-0 pair (`AppConfigProvider`, `FileExplorerHandler`) with `FontManager`,
which is font loading — phase 7 — and the two providers are installed from
`SliceRuntime.cpp:173-176`, the file phase 7 deletes, so this port *is* that
wiring's new home. `ProjectFile`: `ProjectObject` carries `TriangleMesh` and
`Transformation` by value and is consumed by `PlateDocument.cpp:3537-3571`; the
gate is `--project-version-test`. `Host`: `Session` holds a `Host*`, so the C++
header outlives the port; its only engine call is
`get_import_extensions`, already `hfw_mesh_load_extensions()`. `MeshGeometry`:
`setColoredMesh` clears the geometry buffer but never `m_positions`, so
`raycast()` answers against the previous mesh — the port must decide whether to
reproduce that. `GlewWrap`: it exists only because of `-Wl,--wrap=glewInit`
(`src/qt/CMakeLists.txt:182`); a Rust port has to carry the wrap into the cargo
link line.

**Level 1–4** — `PresetYaml`: header-only, no `.cpp`, so no `tools/rust-port`
count moves when it goes; `quoted()`/`number()`'s exact formatting is the
format contract for preset start/end G-code. `PresetFiles`: header-only too, and
`struct CustomPrinter` plus the `printers.json`/`vendor.yaml` layout is the wire
format between `PresetStore`, `PresetImport` and the engine's preset resolver;
`root()` goes through `XDG_DATA_HOME`, which the user-preset gate redirects.
`SettingsModel`: it reads the engine's `ConfigDefs` directly. `PreviewData` and
`GcodeView`: both bind libpgcode/libvgcode types; `GcodeView` is the only
`qmlRegisterType` in the application. `Toolpath`: it owns the vertex-space
mapping the preview slider works in (`m_layerVertexRanges`, `m_vertexGcodeIds`),
`attachView(GcodeView*)` is an invokable taking a raw pointer, and `main.cpp` reads
`Toolpath.currentLayer` through a `QQmlExpression`. `PrinterLink`: all 22 engine
names are the print-host family with no HFW equivalent, it locates its data
through `PresetStore::dataDir()`, and `--print-host-test` drives it against
`tools/fake-moonraker.py`.

**The cycle** — `Arranger`: the engine arithmetic is 1e-6 fixed point and the
input is a bed-plane silhouette, not a footprint (`transform.translation()` is
zeroed and `project_mesh` slices it). `ConfigBoxJson`: percentage/float values
leave as `{value,is_percent}` maps and overridability is decided from the engine's
per-setting `FDMConfigLocation`, not from names. `Orient`: scoring is
`height + 0.35*overhang - 0.05*contact` with candidates deduped at
`dot > 0.9995` and capped to 64 by area — and `--scene-selftest` fails if the
oriented height exceeds the tilted one. `HfwEngine`: it needs about thirty
`hfw_*` externs the ABI does not have yet. `Session`: it dereferences
`object->mesh` (a `Slic3r::Domain::TriangleMesh*`), so `PlateDocument::Object` is
the shared type, and its camera framing negates Y for Qt's Y-up world.
`SliceController`: the plate-transport structs it uses by value are in
`SliceRuntime.hpp`, and its layer/move/filament figures are parsed back out of
the written G-code rather than recomputed — two footer spellings, both
supported.

## Batch table

| Batch | Modules | Level |
| --- | ---: | --- |
| 0 | **15** | 0 |
| 1 | 3 | 1 |
| 2 | 2 | 2 |
| 3 | 2 | 3 |
| 4 | 1 | 4 |
| cycle, as one commit or after a vocabulary extraction | 5 | — |
| 5 | 3 | A+1 |
| 6 | 1 | A+2 |
| never | 1 | main.cpp |
| deleted | 6 | phase 7 |

Fifteen modules at level 0, and the largest coherent group among them is the
**eight leaves with real logic**: `FeatureColors`, `ForeignSettings`, `GlewWrap`,
`MeshGeometry`, `PlateThumbnail`, `PresetYaml`, `ThreeMfProject`,
`TransformController` — 1,848 lines, no shared types, six of them gated
individually. The four `↻` adapters (`GcodeBrand`, `Palette`, `Resources`,
`Scratch`) are level 0 for the *work*, but 144 lines of header is the whole
saving and each one is blocked on its own unported consumers — see rule 3 above.
The order that pays is the one sorted by includers, cheapest first:
`TransformController` (1) → `PlateThumbnail` (2) → `QtPlatform` (2, with the
`SliceRuntime` deletion) → `Host` (2) → `MeshGeometry` (3) → `ForeignSettings` (3)
→ `ThreeMfProject` (2, but needs its own archive reader) → `ProjectFile` (2, and
the vocabulary the centre is waiting on).

## Found while surveying, don't fix

- `src/qt/qml/…/PresetEditorDialog.qml:797` calls
  `PresetStore.importedPresetName()`, which is not `Q_INVOKABLE` — a `TypeError`
  today.
- `tools/cpp-baseline` lists `main.cpp` while DoD 5 says the host `main` stays
  C++. The baseline is a "files that exist today" list, so the two are not
  literally in conflict, but the port-direction gate can never report `main.cpp`
  as progress, and nothing else records that one line is permanent.
- `tools/check` drives `--scene-selftest` and `--calibration-test` from files
  phase 7 deletes (`SceneSelftest.cpp`, and `SliceRuntime::slice` behind it), so
  the deletion commits change the gate rather than only the source.
- `SliceRuntime.cpp:173-176` is the only place the platform providers are
  installed, and `QtPlatform` — which implements them — is a separate module that
  looks like a leaf. Deleting `SliceRuntime` without moving that install would
  leave the application with no `IAppConfigProvider`.
