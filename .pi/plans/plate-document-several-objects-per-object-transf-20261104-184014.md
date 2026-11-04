---
id: plate-document-several-objects-per-object-transf-20261104-184014
status: complete
created: 2026-11-04T18:40:14.975Z
updated: 2026-11-04T20:42:50.356Z
---

# Plate document: several objects, per-object transforms, save/load, undo

## Summary

Replace the one-model-per-window session with a real document hosted on the engine's own project/scene API (ProjectInteractor + SceneInteractor), so several objects live on a bed with their own transforms, the scene slices as one job, the project saves as `.3mf`, and edits are undoable. Rendering, settings and preview stay as they are; the slice path must keep oracle parity at 12/12.

## Risks

- The engine's project/scene interactors are built for its own app; running them inside Qt's event loop may need a dispatch bridge. That is exactly what the first step tests, before any UI depends on it.
- Multi-object slicing may only be exposed through the engine's file-based export path. Fallback: write a temporary `.3mf` and slice that — slower per slice, but reuses the same code the oracle already proves.
- Per-object rendering with Qt Quick 3D is unproven past a handful of objects; the plate may need instancing if a real part library (20+ objects) stutters.
- The 12 oracle cases are all single-object, so parity alone would not catch a scene regression — the new scene gate has to cover that gap.
- This is the largest change since the app was built: it rewires the document, the slice path and the viewport together. Expect the settings panel and preview to keep working untouched, but every headless knob (`--dump-*`, `--smoke`, `--selftest-slice`) needs re-verifying as it lands.

## Steps

1. [x] Spike the seam before building anything on it: add a `--scene-selftest` flag that stands up an in-process scene session, loads two STLs as separate objects on the bed, offsets one by 40 mm, slices, and prints object count, each object's position, and the G-code move count. This answers the one risky question — whether the engine's headless project/scene API runs inside our Qt app — and proves placement survives slicing.
   - files: upstream/src/slic3r-app-qt/src/main.cpp, upstream/src/slic3r-app-qt/src/PlateDocument.hpp, upstream/src/slic3r-app-qt/src/PlateDocument.cpp
   - note: --scene-selftest: 2 objects on one bed, moved apart, sliced as one job (100 layers, 4791 moves left of X=145 / 4798 right). Oracle 12/12, tools/check 7/7. Two traps recorded: an undo provider must be installed or a finished arrange job asserts; load_models_to_project arranges asynchronously and clobbers placement.
2. [x] Build the document model: PlateDocument owns the engine project (one bed, N objects), and exposes to QML an object list (id, name, path, position, rotation, scale, selected) with signals for add, remove, transform and selection. Session becomes a thin view over the document instead of its own single-path state.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.hpp, upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/src/Session.hpp, upstream/src/slic3r-app-qt/src/Session.cpp, upstream/src/slic3r-app-qt/src/main.cpp
   - note: PlateDocument added: normalised geometry, per-object Transformation (offset/rotation/scale), shelf placement, duplicate/remove/select, aggregate bounds, baked world meshes and staged STLs. Session is now a view over it (hasModel/path/displayName/error + refresh of geometry, camera pivot and slicer input). --scene-selftest drives the document end to end: 2 objects on the bed, +40 mm move, 45-degree rotation (footprint 28.28 mm), duplicate/remove/select, then slices both objects through the engine (100 layers, 6612 moves, X 113.23..176.82). tools/check 7/7, oracle 12/12, app window verified with cockpit.
3. [x] Render every object in the viewport: one mesh component per object driven by the document, click-picking returns which object was hit, and the selected object carries the accent tint. Keeps printer Z-up end to end; existing camera and bed drawing untouched.
   - files: upstream/src/slic3r-app-qt/qml/views/Viewport.qml, upstream/src/slic3r-app-qt/src/MeshGeometry.hpp, upstream/src/slic3r-app-qt/src/main.cpp
   - note: Every visible object is drawn from the document: one mesh for the unselected objects and one for the selected object (accent tint), rebuilt on document change. Ray picking walks each object's triangles with the transform inverted, nearest hit wins, selection follows. Verified in cockpit: two models side by side, click to select, tint moves.
4. [x] Object list panel matching Orca's placement: rows with name, visibility and per-object position; select, delete and duplicate. Verify with a cockpit capture beside Orca rather than from the code.
   - files: upstream/src/slic3r-app-qt/qml/views/Viewport.qml, upstream/src/slic3r-app-qt/qml/main.qml
   - note: ObjectListPanel.qml: docked bottom-right (Ctrl+L or the list button), one row per object with visibility, name, size, editable X/Y, and a footer acting on the selection (centre, drop, duplicate, delete). Verified in cockpit.
5. [x] Import many: File > Add model and multi-file drag-and-drop append to the plate instead of replacing it; the first object is centred on the bed, later ones placed beside it by the engine. Open/New clears the plate.
   - files: upstream/src/slic3r-app-qt/qml/main.qml, upstream/src/slic3r-app-qt/src/PlateDocument.cpp
   - note: Add model / Ctrl+O opens a multi-file dialog; drag-and-drop adds every dropped file; a plate is cleared only by Open/New. Startup accepts several positional models.
6. [x] Slice the whole scene in one job and keep parity: the slice path stops slicing a single file and slices the document, whichever route the spike proves — the in-process slicing API, or writing a temporary `.3mf` and slicing that. `oracle verify --runner qt` must stay 12/12, and `tools/check` 7/7.
   - files: upstream/src/slic3r-app-qt/src/SliceController.cpp, upstream/src/slic3r-app-qt/src/PlateDocument.cpp
   - note: The plate slices as one job: the document bakes every visible object into one STL of separate shells and the worker-thread CLI slices that. tools/check gained a plate gate (2 objects, 125 layers, 9818 moves, X span 54.6 mm); UI slice through cockpit matched: 850 ms, 125 layers, 9820 moves, preview opened on both objects. Oracle still 12/12.
7. [x] Undo and redo over document edits with Ctrl+Z / Ctrl+Shift+Z, covering add, delete, transform and duplicate — the engine already carries an undo stack, so this is wiring, not invention.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/qml/main.qml
   - note: Snapshot stack in the document covering add, delete, duplicate, transform, visibility and clear, coalescing repeated edits to one object into one step; Ctrl+Z / Ctrl+Shift+Z wired. Verified in cockpit: delete then undo restored the object and the render.
8. [!] Save and open projects as `.3mf` (objects plus settings in one file), File > Save / Save As / Open project, window title showing the project name and a dirty marker. Reopening a saved project must reproduce the same G-code as slicing it before saving.
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.cpp, upstream/src/slic3r-app-qt/qml/main.qml
   - note: Not built. store_3mf(filepath, Domain::Project) needs a live engine project, and the app already runs a second runtime per slice on a worker thread (CLI::run); adding a GUI-thread ProjectInteractor beside it means two engine runtimes in one process, which is exactly the coupling step 1 found crashes in arrange. The fix is to move slicing onto one long-lived runtime first (in-process SlicingInteractor, as --scene-selftest proves works), then save/load from that same project.
9. [x] Close the loop: add a scene gate to `tools/check` (N objects on the plate, slice reports all of them), update the parity ledger rows this closes, and record the engine-API findings in the project memory.
   - files: tools/check, docs/orca-parity.md
   - note: tools/check has a plate gate (2 objects sliced as one job, X span >= 40 mm) and now runs 8/8. docs/orca-parity.md updated: object list, undo, multi-object plate, picking and dragging marked done; per-object settings and .3mf save/load marked needs-engine with the reason.
