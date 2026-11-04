---
id: next-layer-qt-becomes-a-real-app-styled-shell-li-20261101-160036
status: awaiting_approval
created: 2026-11-01T16:00:36.123Z
updated: 2026-11-01T16:00:36.123Z
---

# next-layer-qt becomes a real app: styled shell, live slicing, toolpath preview

## Summary

Turn next-layer-qt from a mesh viewer into a launchable klipshell-styled slicer: matugen MD3 chrome, orbitable fit-to-view viewport, a real Slice button running the engine in-process, and a G-code layer preview driven by feature-coloured line geometry. Settings panel is generated at runtime from Domain::get_defs_fdm(). Upstream diff stays at our two directories plus their two add_subdirectory lines.

## Risks

- Engine global init (AppServices singleton, DBus single-instance handler) running inside a Qt process: verified repeatable, but never verified concurrently with a live QML engine. Step 1 de-risks this before any UI depends on it.
- QQuick3DGeometry line rendering with vertexColorsEnabled is API-confirmed but not yet proven to shade as intended on this GL stack; fallback is one Model per feature type with its own flat material.
- Settings are read from FDMConfigLocation::Print + Printer defs only; object/volume overrides and filament settings are out of scope for this pass.
- Large G-code (a real Voron part can be 500k moves) may make per-layer geometry rebuilds slow; the parser keeps a per-layer segment index so only the active layer is uploaded.
- Registering a matugen target for next-layer would need a ~/.config/matugen edit, which is confirm-first and not done in this pass - the app reads klipshell's rendered palette as a fallback.
- The sphere_r12 black wedge is left unfixed in this pass; it needs a geometry-normal dump and iterative diagnosis, not more lighting changes.

## Steps

1. [ ] Establish the engine-in-Qt path: link slic3r-app-cli into next-layer-qt, add App::init_common/init_paths, and a headless --slice MODEL CONFIG OUT.gcode option. Verify the produced G-code matches the oracle baseline for cube_20 (proves the engine runs correctly alongside Qt, before any UI depends on it).
   - files: upstream/src/slic3r-app-qt/CMakeLists.txt, upstream/src/slic3r-app-qt/src/main.cpp
2. [ ] Port the design system to Qt Quick: qml/theme/Colors.qml (36-key matugen MD3 contract, klipshell's text ladder + selection tokens), Metrics.qml, Type.qml. Palette resolved in C++ with search order $NEXT_LAYER_COLORS -> ~/.cache/matugen/next-layer-colors.json -> ~/.cache/matugen/klipshell-colors.json -> embedded defaults, since registering a matugen target means editing ~/.config/matugen.
   - files: upstream/src/slic3r-app-qt/qml/theme/Colors.qml, upstream/src/slic3r-app-qt/qml/theme/Metrics.qml, upstream/src/slic3r-app-qt/qml/theme/Type.qml, upstream/src/slic3r-app-qt/src/Palette.cpp
3. [ ] Build the shell layout: header (title, model name, Model/Preview toggle), 44px icon rail with view switcher, viewport, 340px settings panel, footer with layer slider and Slice button. Window 1600x950, resizable. Cards use 2px outlineVariant borders at Metrics radius, JetBrainsMono Nerd Font throughout.
   - files: upstream/src/slic3r-app-qt/qml/main.qml, upstream/src/slic3r-app-qt/qml/components/
4. [ ] Viewport interaction: camera fit-to-view derived from mesh bounds, left-drag orbit, wheel zoom, and a 250x210 bed grid emitted as printer-coordinate line geometry inside printSpace (one transform into Y-up only).
   - files: upstream/src/slic3r-app-qt/src/BedGrid.cpp, upstream/src/slic3r-app-qt/qml/views/Viewport.qml
5. [ ] SettingsModel: enumerate Domain::get_defs_fdm() filtered to FDMConfigLocation::Print and Printer, expose name/label/full_label/tooltip/category/option_group/type/value/min/max/choices to QML as a QVariantList, with setValue(name, value) and a configJson() writer. QML renders grouped category cards with a search filter; number, checkbox and combobox editors by gui_type.
   - files: upstream/src/slic3r-app-qt/src/SettingsModel.cpp, upstream/src/slic3r-app-qt/src/SettingsModel.hpp, upstream/src/slic3r-app-qt/qml/views/SettingsPanel.qml
6. [ ] SliceController: invokable slice() running App::CLI::run on a worker thread with the current settings written to a temp config, emitting started/finished(gcodePath, elapsedMs, layers, moves)/failed. UI shows a progress state and the result stats in the footer.
   - files: upstream/src/slic3r-app-qt/src/SliceController.cpp, upstream/src/slic3r-app-qt/src/SliceController.hpp
7. [ ] Toolpath preview: C++ G-code parser (port of klipshell's GcodeParse.js semantics: G90/G91, M82/M83, G92, G2/G3, ;TYPE: features, LAYER_CHANGE/Z: layers) producing per-layer, per-feature segments; ToolpathGeometry turns a layer into a Lines primitive with per-vertex colour. Layer slider + feature legend wired to it.
   - files: upstream/src/slic3r-app-qt/src/GcodeParser.cpp, upstream/src/slic3r-app-qt/src/ToolpathGeometry.cpp, upstream/src/slic3r-app-qt/qml/views/PreviewBar.qml
8. [ ] Integrate in main.cpp, build inside the memory-capped cgroup, and verify headlessly: --smoke --shot on the model view and on a sliced preview, coverage non-zero, exit 0. Then capture the real window with shot.
   - files: upstream/src/slic3r-app-qt/src/main.cpp, upstream/src/slic3r-app-qt/CMakeLists.txt
9. [ ] Record the session's durable findings in the memory store: get_defs_fdm as the settings metadata source, CLI::run repeatability plus the DBus single-instance warning, the Qt Quick 3D line/vertex-colour API, palette resolution order, and the corrected GUI_Descriptions dead end.
   - files: /home/cwebb/.pi/agent/projects-memory/next-layer/MEMORY.md
