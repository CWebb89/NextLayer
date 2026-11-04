---
id: preview-feature-parity-view-types-option-layers--20261102-173731
status: complete
created: 2026-11-02T17:37:31.199Z
updated: 2026-11-02T18:06:29.214Z
---

# Preview feature parity: view types, option layers, headless verification

## Summary

Wire up the libvgcode capabilities we currently ignore — all 11 view types with gradient palettes, and the 9 option layers (travels, wipes, retractions, seams, tool changes) with per-option colour and visibility — exposed through the existing QML legend, with headless CLI knobs so every state is verifiable without a mouse.

## Risks

- The legend QML is a flat list of extrusion roles; a view-type selector changes its content shape, so this touches more QML than the C++ side.
- Speed/flow/height ranges need sane min/max per view type or the gradient legend will read as noise; libvgcode exposes ranges via ColorRange but the thresholds may need clamping to the actual gcode distribution.
- Travels and retractions add geometry; at 2x supersampling the frame cost may exceed the measured 5 ms, needs re-measuring rather than assuming.

## Steps

1. [x] Add view-type support to GcodeView: setViewType(int) mapping to libvgcode::ViewType, palette selection via set_color_range_palette, and a viewType property; legend switches between extrusion roles and range thresholds accordingly.
   - files: upstream/src/slic3r-app-qt/src/GcodeView.hpp, upstream/src/slic3r-app-qt/src/GcodeView.cpp
   - note: GcodeView: 13 ViewTypes + range legends from ColorRange values/palette
2. [x] Add option-layer support to GcodeView: visibility, colour, travels/wipes radius for Travels, Wipes, Retractions, Unretractions, Seams, ToolChanges, ColorChanges, PausePrints, CustomGCodes.
   - files: upstream/src/slic3r-app-qt/src/GcodeView.hpp, upstream/src/slic3r-app-qt/src/GcodeView.cpp
   - note: GcodeView: 9 option layers — visibility, colour, estimated time
3. [x] Expose both in the QML preview UI: view-type selector in the legend header and a toggle list for options beneath the line-types list.
   - files: upstream/src/slic3r-app-qt/qml/views/Viewport.qml, upstream/src/slic3r-app-qt/qml
   - note: PreviewLegend.qml: view-type ComboBox, range mode rows, options list
4. [x] Add headless knobs to main.cpp — --view-type NAME and --hide-option NAME / --show-option NAME — so each state can be captured with --smoke --shot and asserted in CI.
   - files: upstream/src/slic3r-app-qt/src/main.cpp
   - note: main.cpp: --view-type, --hide-option + startup context props
5. [x] Verify by capturing every view type and option toggle headlessly at a fixed camera, confirming the oracle still matches 11/11 and the existing smoke checks stay green.
   - files: oracle/oracle.py
   - note: Verified: oracle 11/11, wayland/xcb/model/empty/slice/vgcode-shot all pass, clangd clean
