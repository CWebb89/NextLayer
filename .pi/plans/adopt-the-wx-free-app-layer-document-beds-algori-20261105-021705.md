---
id: adopt-the-wx-free-app-layer-document-beds-algori-20261105-021705
status: awaiting_approval
created: 2026-11-05T02:17:05.741Z
updated: 2026-11-05T09:17:01.305Z
---

# Adopt the wx-free app layer: document, beds, algorithms, then the render module

## Summary

Adopt PrusaSlicer 3.0's own wx-free app layer (`slic3r-domain` + `slic3r-shared` Biz/App) as next-layer's document and algorithm core, keeping the Qt Quick 3D viewport until Phase 3, when it is replaced by a Qt `AbstractRenderModule` so upstream's painting/lay-on-face/gizmo code runs unchanged. Every step is gated by `tools/check` and the G-code oracle.

## Risks

- Two engine runtimes in one process is the hazard that made `arrange` assert in the earlier scene spike: steps 2-3 must remove the `CLI::run` slice path, not sit beside it. `SlicingInteractor` must prove byte-identical G-code against the oracle before the old path is deleted.
- Preset-bundle bootstrap replaces `PresetStore`'s `CLI::run` JSON hack — it can regress the whole preset/Add-printer flow, so step 2 keeps `--dump-presets` and `tools/fixtures diff regression` as gates.
- The flaky preset lookup (step 1) may be engine-internal and not fixable without touching upstream files; the fallback is to make our own resolution deterministic (sort candidates by vendor/preset id) and record the engine bug rather than patching upstream.
- Widgets/QML versus raw GL: `slic3r-render` is GL-only, so Phase 3 forces the Quick scene's RHI backend to OpenGL and constrains where a Qt Quick overlay can sit above the viewport. If QQuickFramebufferObject proves unworkable with Quick 3D, the fallback is a native `QOpenGLWindow` viewport with QML chrome in a separate window region.
- `ObjectSettingsInteractor` per-object configs apply only if the merge in step 3 writes real `ModelObject::config` containers; if the engine's slice path ignores them for our import route, the step needs the same import path the engine's own loader uses.
- Phase 1-2 estimated at multi-week across several sessions; oracle parity (12/12) and `--smoke` coverage are the stop-the-line gates at each step, and a flaky 12th case (step 1) undermines all of them.

## Steps

1. [x] Make preset resolution deterministic. Reproduce `--process "0.20mm SPEED @MK4 0.4"` resolving ~50-65% of runs, find the unordered lookup in the CLI preset path (slic3r-app-cli preset selection) and fix ordering there or in PresetStore; then run `nl_oracle verify --runner qt` three times in a row and require 12/12 each time, since a gate that flips is not a gate.
   - files: upstream/src/slic3r-app-cli/, upstream/src/slic3r-app-qt/src/PresetStore.cpp
   - note: Root cause was duplicate hardware-config names from a slugify collision, not an engine race. Fixed in generate.py (unique_slug), added --dump-presets-json and a duplicate-name gate in tools/check. Oracle 12/12 on three consecutive runs; catalog sweep identical across two runs.
2. [x] Bootstrap the app-layer runtime: construct `Domain::Workbench`, one `Project` with a `BedContainer`, load presets through `Domain::Preset::Bundle` instead of the `CLI::run` file hack, and wire `ProjectInteractor` (with `StdMainThreadDispatcher` and an `IThumbnailImageGenerator`) plus `SlicingInteractor`/`ResultExportInteractor` for G-code. Add a `--selftest-document` knob that slices cube_20 through the interactor and compares against the current CLI path, then confirm `oracle verify --runner qt` is still 12/12.
   - files: upstream/src/slic3r-app-qt/src/AppRuntime.hpp, upstream/src/slic3r-app-qt/src/AppRuntime.cpp, upstream/src/slic3r-app-qt/CMakeLists.txt
   - note: Shipped as `SliceRuntime` (one `CLIRuntime` per process, created on first use) with `slice`, `printerModels`, `profilesFor`, `resolvedConfig`. `SliceController` slices through it and `PresetStore` asks it for every preset query — no `CLI::run` anywhere in the app. Equivalent of the `--selftest-document` knob is `--selftest-slice` (plus `--keep-gcode` for byte comparison): toolpath sha256 identical to the old forked path (7,634 non-comment lines, `4e20ca44…`). Oracle 12/12; `tools/check` 9/9; cockpit slice 199 layers / 10,982 moves / 334 ms.
3. [ ] Move the plate onto the engine model. `PlateDocument` keeps its QML surface (objects, transforms, visibility, selection, undo) but holds `Domain::Model` instances inside `Project`'s bed rather than baked STL buffers, and `SliceController` slices from the model via `SlicingInteractor` instead of re-merging meshes and calling `CLI::run`. Gate: default plate (one cube) byte-identical to its current G-code, oracle 12/12, `--smoke` coverage unchanged.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.hpp, upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/src/SliceController.cpp, upstream/src/slic3r-app-qt/src/PlateScene.cpp
4. [ ] Engine-level beds replace app-level plates: one `Project`, N `BedInstance`s with their own config containers, the plate tab strip becomes bed selection, and a slice covers the selected bed. Retire the per-plate document copies.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/qml/views/ViewportToolbar.qml, upstream/src/slic3r-app-qt/qml/main.qml
5. [ ] Ship the document-level tools that need no render module, each as a QML action plus a document command: arrange (`ArrangeInteractor::arrange` / `arrange_new_instances` — the `arrange_and_wait` equivalent), auto-orient, cut at Z (`CutUtils` on volumes, replacing the whole-plate re-slice), variable layer height (`Domain::LayerHeightProfile`), text objects (`EmbossShape`/`TextConfiguration` + `App/Scene/EmbossCreate`), and assemble/split into volumes. Each verified with `--smoke` coverage and an oracle run.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/src/TransformController.cpp, upstream/src/slic3r-app-qt/qml/views/TransformPanel.qml, upstream/src/slic3r-app-qt/qml/views/ObjectListPanel.qml
6. [ ] Per-object settings and project files: `ObjectSettingsInteractor` backed by a QML per-object override panel (with the engine's own override rows), and `.3mf` save/load through `Biz/Format/3mf` so objects, per-object configs, transforms and beds round-trip. Gate: save, reload, re-slice, compare G-code byte-for-byte.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/qml/views/ObjectListPanel.qml, upstream/src/slic3r-app-qt/src/Session.cpp
7. [ ] Phase 3, separate plan once the above is green: implement `QtRenderModule : AbstractRenderModule` and `QtRenderCanvas : AbstractRenderCanvas` on a `QQuickFramebufferObject` with an OpenGL RHI backend, hosting upstream `App/Scene` (Camera, Clipper, TriangleMeshManager, GizmoManager) and ImGui, then switch on the gizmo commands: support/seam/MMU/fuzzy painting, lay-on-face, drag-rotate/scale handles and snapping. This replaces the Qt Quick 3D viewport, so it gets its own plan and gate set.
   - files: upstream/src/slic3r-app-qt/src/QtRenderModule.hpp, upstream/src/slic3r-app-qt/src/QtRenderModule.cpp, upstream/src/slic3r-app-qt/qml/views/Viewport.qml
