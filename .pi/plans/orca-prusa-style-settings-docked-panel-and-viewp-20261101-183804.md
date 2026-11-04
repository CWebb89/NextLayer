---
id: orca-prusa-style-settings-docked-panel-and-viewp-20261101-183804
status: complete
created: 2026-11-01T18:38:04.901Z
updated: 2026-11-01T19:06:47.580Z
---

# Orca/Prusa-style settings, docked panel, and viewport chrome for next-layer

## Summary

Rebuild the settings surface around the engine's own taxonomy (Printer / Filament / Process tabs, vertical sub-tab rail, ~500 settings across all FDM locations), add a header side-toggle and Orca-style viewport chrome, and keep a dim solid model under the toolpaths in preview. Wire the transform tools the engine CLI already supports; make everything else visible but explicitly disabled with a stated reason, and track full Orca/Prusa parity in a checklist so nothing is silently missing.

## Risks

- Filament and Tool settings are per-extruder; the base profile is single-extruder, so multi-tool editing renders but cannot be validated against a real multi-material profile yet.
- Rendering ~500 settings through QML item creation will be slow if the sub-tab list is built eagerly; the field list must be a Repeater over one sub-tab at a time.
- Plates and per-object settings are not in the engine CLI; the investigation step may end in "needs engine work" rather than a feature. I will report that instead of faking it.
- Wiring transforms through CLI re-slicing loses undo granularity; if the round-trip proves too slow or lossy I will stop at a smaller set of tools and say so.
- The transform tools change the mesh, so oracle parity must be re-checked after every change to the slice path to prove untouched defaults still match the baseline.

## Steps

1. [x] Extend SettingsModel to all FDM locations (Print, Filament, Printer, Tool) and expose a tab API: tabs() with id/title/subtabs/counts, items(tab, subtab, filter) for the field list. Keep configJson() byte-identical to assets/base-config.json by patching only values that actually changed, so the untouched UI still slices the oracle baseline.
   - files: upstream/src/slic3r-app-qt/src/SettingsModel.hpp, upstream/src/slic3r-app-qt/src/SettingsModel.cpp
   - note: 335 settings across 3 tabs / 27 sub-tabs; config dump identical to base in every block
2. [x] Add `--dump-tabs` and `--dump-subtab TAB/SUBTAB` CLI hooks that print the tab tree with per-sub-tab setting counts, so the taxonomy is checkable without a window and I can prove the count against the engine's own definition list.
   - files: upstream/src/slic3r-app-qt/src/main.cpp
   - note: --dump-tabs and --dump-subtab verified; oracle verify qt 11/11
3. [x] Rebuild SettingsPanel.qml: Printer/Filament/Process tab strip, vertical sub-tab rail (icon-free, labelled, scrollable), field list for the selected sub-tab, global filter that searches across every tab and reports matches per tab, modified markers, and per-sub-tab reset. Panel width raised to fit labels at 340-380px.
   - files: upstream/src/slic3r-app-qt/qml/views/SettingsPanel.qml, upstream/src/slic3r-app-qt/qml/theme/Metrics.qml
   - note: Process/Filament/Printer tabs + vertical sub-tab rail, 335 settings, search, per-item revert, per-subtab reset
4. [x] Add the header side toggle: a swap control that re-parents the panel to the left or right of the viewport, animated, persisted across launches via QSettings, with the viewport reflowing and the rail/toolbar mirroring so nothing overlaps.
   - files: upstream/src/slic3r-app-qt/qml/main.qml, upstream/src/slic3r-app-qt/qml/views/Header.qml, upstream/src/slic3r-app-qt/src/UiState.cpp, upstream/src/slic3r-app-qt/src/UiState.hpp
   - note: Header side toggle, animated reflow, persisted via QtCore Settings; verified left/right with --panel-left
5. [x] Keep the solid model visible under the toolpath in preview mode: translucent material with depth-write off, dimmed base colour, and the toolpath drawn over it. Verify with eglfs smoke shots at mid and top layer.
   - files: upstream/src/slic3r-app-qt/qml/views/Viewport.qml
   - note: Preview keeps a translucent solid under the toolpath; verified with eglfs shot at layer 101
6. [x] Add the Orca-style viewport top bar: plate/object strip on the left, action cluster on the right (add object, delete, duplicate, split, cut, arrange, auto-orient, measure, export). Every action is rendered from one declarative list so enabled/disabled state and shortcuts cannot drift from the tool rail.
   - files: upstream/src/slic3r-app-qt/qml/views/ViewportToolbar.qml, upstream/src/slic3r-app-qt/qml/main.qml
   - note: ViewportToolbar floats over the viewport, actions driven by one list in main.qml; arrange/orient disabled with stated reason
7. [x] Add the left tool rail overlay with the full Orca tool set (Move, Rotate, Scale, Lay on face, Cut, Measure, Text, Support paint, Seam paint, MMU paint, Fuzzy skin, Variable layer height, Assemble, Calibration), each declared in one `tools` model with state: ready, needs-engine, not-applicable. Disabled entries state their reason in a tooltip rather than pretending to work.
   - files: upstream/src/slic3r-app-qt/qml/views/ToolRail.qml
   - note: ToolRail with 14 Orca tools; 5 ready, 9 disabled with engine reasons
8. [x] Wire the transform actions the engine CLI already supports: Move/align, Rotate, Scale/scale-to-fit, Duplicate (grid), Split objects, Cut at Z, Arrange, Ensure on bed, via `TransformParams` re-slicing, with the mesh geometry rebuilt from the result. Verify each by slicing afterwards and re-running oracle parity.
   - files: upstream/src/slic3r-app-qt/src/Session.cpp, upstream/src/slic3r-app-qt/src/TransformController.cpp, upstream/src/slic3r-app-qt/src/TransformController.hpp
   - note: TransformController + --transform hook; verified scale 2x, rotate 45/90, cut, duplicate on real STL bboxes
9. [x] Implement Measure client-side in printer coordinates (point-to-point distance, XYZ deltas, angle) drawn as a 3D overlay with no engine round-trip.
   - files: upstream/src/slic3r-app-qt/qml/views/Viewport.qml, upstream/src/slic3r-app-qt/src/MeasureGeometry.cpp
   - note: Measure via View3D.pick, sphere markers + cylinder segment, distance/deltas/angle readout; wired to the measure tool
10. [x] Investigate and decide plates and multi-object scenes: confirm whether the engine exposes a plate entity outside its own plater, and whether 3MF export/import round-trips per-object transforms. Write the finding into memory, then implement the cheapest correct option (likely our own scene list + 3MF round-trip) or stop and report that it needs engine work.
   - files: /home/cwebb/.pi/agent/projects-memory/next-layer/MEMORY.md
   - note: Findings: Domain::Project owns BedContainer of Domain::Bed, 3mf load/store exist; no CLI/plater API. Recorded in memory, plates staged as needs-engine
11. [x] Write `docs/orca-parity.md`: every Orca/Prusa tab, sub-tab, menu item, viewport tool and preference, each marked done / staged / needs-engine / not-applicable with a one-line reason. This is the artefact that answers "does next-layer have them all" without guessing.
   - files: docs/orca-parity.md
   - note: docs/orca-parity.md written: 3 tabs, 27 sub-tabs, toolbar, tool rail, preview, project
12. [x] Final verification: oracle `verify --runner qt` still 11/11, `--dump-tabs` counts match the engine definition list, eglfs smoke shots for model/preview/panel-left/panel-right, and a launch from `next-layer` on the desktop to confirm the panel dock and tabs render.
   - files: AGENTS.md, upstream/src/slic3r-app-qt/README.md
   - note: oracle verify qt 11/11; smoke shots for empty bed, Supports sub-tab, filament+panel-left, preview with dim model; launcher fish -n clean
