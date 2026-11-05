---
id: shapes-and-modifier-volumes-rail-object-toggle-w-20261105-193254
status: awaiting_approval
created: 2026-11-05T19:32:54.408Z
updated: 2026-11-05T20:00:55.312Z
---

# Shapes and modifier volumes, rail object toggle, welcome screen, remaining polish

## Summary

Give the plate the CAD-suite half of Orca/Prusa: primitive shapes added as objects, and modifier / negative / support-enforcer / support-blocker volumes attached to an object, both carried through the project file and the slice path (the engine's own CSG in the FDM slice already handles all four volume types). Then move the object-list toggle into the rail as a third icon, replace the first-run popup with a proper welcome + printer setup screen, and close the remaining gaps: unsaved-changes guard, object rename, real statistics, units.

## Risks

- The engine applies negative/modifier volumes in the FDM slice path (PrintObjectSlice slices them, slices_to_regions booleans them), but a modifier volume with no settings simply becomes a region with the object's settings; per-volume settings are not part of this step
- Adding primitive objects needs a mesh in the app's local frame; the engine's primitives are XY-centred with Z from 0, which matches, but slender/rounded shapes will be a plain box for now
- Unsaved-changes guard touches window close handling (Host.cpp), which the headless smoke path also exercises

## Steps

1. [x] Add a Volume model to PlateDocument: volumes per object (mesh, type MODEL_PART/NEGATIVE_VOLUME/PARAMETER_MODIFIER/SUPPORT_BLOCKER/SUPPORT_ENFORCER, visible, local transform), addShape(kind,w,d,h) creating a primitive object, addVolume(objectId, kind, type), removeVolume, and volumes in objectInfo()
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.hpp, upstream/src/slic3r-app-qt/src/PlateDocument.cpp
   - note: Volume model in PlateDocument: per-object volumes with type, local transform, visibility; addShape/addVolume/removeVolume/renameObject; volumes in objectInfo
2. [x] Generate the primitives with the engine's own its_make_cube / its_make_cylinder / its_make_sphere and keep the app's XY-centred, Z-at-zero convention
   - files: upstream/src/slic3r-app-qt/src/PlateDocument.cpp
   - note: Primitives from the engine's own make_cube/make_cylinder/make_sphere, normalised to XY-centred with min Z at zero
3. [x] Carry volumes to the slicer: PlateSliceObject gains a volume list with types and transforms, slicePlate builds each with construct_ptr(object, mesh, type) and keeps paint/overrides on the model part
   - files: upstream/src/slic3r-app-qt/src/SliceRuntime.hpp, upstream/src/slic3r-app-qt/src/SliceRuntime.cpp
   - note: PlateSliceVolume to the slicer; construct_ptr with the volume type so the FDM slice booleans negatives and reads modifiers
4. [x] Persist volumes in the .nextproj project file and restore them on load
   - files: upstream/src/slic3r-app-qt/src/ProjectFile.cpp
   - note: Volumes serialised as name/type/visible/matrix/mesh and restored
5. [x] Draw modifier volumes in the viewport with one colour per type (negative red, modifier amber, blocker purple, enforcer green) and keep picking on the model part only
   - files: upstream/src/slic3r-app-qt/src/PlateScene.cpp
   - note: PlateScene volume mesh with one colour per role, translucent node in the viewport
6. [x] Add the shape and modifier UI: an Add menu in the viewport toolbar (box/cylinder/sphere/slender, and modifier/negative/blocker/enforcer on the selection), and nested volume rows with type badges and remove buttons in the object list
   - files: upstream/src/slic3r-app-qt/qml/views/ViewportToolbar.qml, upstream/src/slic3r-app-qt/qml/views/ObjectListPanel.qml, upstream/src/slic3r-app-qt/qml/main.qml
   - note: Add menu in the toolbar (box/cylinder/sphere + modifier/negative/blocker/enforcer), volume rows in the object list, ghosted selected object so interior volumes are visible
7. [x] Gate it: a --shape-test self-test that slices a box, then the same box with a negative cylinder through it, and asserts the printed volume drops; plus a support-enforcer case that raises support moves
   - files: upstream/src/slic3r-app-qt/src/SceneSelftest.cpp, upstream/src/slic3r-app-qt/src/main.cpp, tools/check
   - note: --shape-test gate: box → negative cylinder cuts filament 3899→2823 mm → remove restores → second shape; gate in tools/check
8. [x] Move the object-list toggle out of the drawer strip and into the rail as a third icon under Preview; keep the slide-out panel, opened by that icon
   - files: upstream/src/slic3r-app-qt/qml/views/Rail.qml, upstream/src/slic3r-app-qt/qml/main.qml
   - note: Object-list toggle moved into the rail under Preview as a third entry with the object count; drawer strip removed
9. [ ] Replace the first-run popup with a welcome screen: brand hero, what next-layer is, and a printer setup wizard (vendor list, machine, material, quality, summary) that can be entered from the welcome or skipped for later
   - files: upstream/src/slic3r-app-qt/qml/views/SetupFlow.qml, upstream/src/slic3r-app-qt/qml/main.qml
10. [ ] Close the remaining gaps: unsaved-changes guard on quit and window close, inline object rename, statistics (time, filament, layer count) in the footer from the last slice, and the units preference wired to displayed lengths
   - files: upstream/src/slic3r-app-qt/src/Host.cpp, upstream/src/slic3r-app-qt/src/main.cpp, upstream/src/slic3r-app-qt/qml/views/FooterBar.qml, upstream/src/slic3r-app-qt/qml/views/ObjectListPanel.qml
11. [ ] Verify: tools/check all gates green including the new shape gate, oracle 12/12, and cockpit captures of the welcome screen, a modifier volume sliced, and the rail toggle
   - files: tools/check
