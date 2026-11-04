---
id: port-printer-filament-process-presets-from-prusa-20261103-091940
status: awaiting_approval
created: 2026-11-03T09:19:40.094Z
updated: 2026-11-03T10:25:16.029Z
---

# Port printer/filament/process presets from PrusaSlicer and OrcaSlicer

## Summary

Build a preset layer (resolve → flat engine configs → picker UI) and feed it from two sources: the engine's own YAML bundle (11 printers × nozzles, 288 filaments, 13 print presets — already in-tree and native) and a measured Orca converter. Orca's settings that the Prusa 3.0 engine does not implement cannot be ported; the plan exposes the convertible subset and reports the rest.

## Risks

- 952 of Orca's 947 distinct config keys have no engine counterpart in the pinned Prusa 3.0 engine (measured: 155 map by name, ~200 more need semantic renames, 792 are Orca-only such as wall_loops/sparse_infill_density/bambu-specific). Those settings cannot be shown or honoured without implementing features in upstream, which the project rule forbids. Orca printers will therefore convert with a fraction of their settings; the coverage report will say exactly which.
- Orca G-code macros and firmware flavours assume Orca/Bambu placeholders and variables. They convert as strings, but a Bambu machine profile will not produce valid G-code for a Voron, and vice versa; the shipped set should be judged per vendor/hardware, not blindly.
- Licensing: OrcaSlicer profiles and assets are AGPL-3.0. Importing them into this PrusaSlicer fork (AGPL-3.0) is licence-compatible, but it forces the whole project to stay AGPL if it is ever distributed. Confirm before importing Orca assets (bed meshes, thumbnails) rather than only JSON settings.
- Orca's profile tree is 104 MB / 14.7k JSON files. It must not be vendored into the repo; the converter should read from ~/dev/reference/OrcaSlicer (or a configurable path) and emit only the shipped subset.
- The YAML bundle resolver has to emulate engine semantics (variant conditions, inherits, tool_nozzle arrays, per-slot filament values). Any divergence shows up as a wrong default config rather than a crash, so every generated preset is slice-verified instead of eyeballed.

## Steps

1. [x] Write tools/presets/resolve_prusa.py: parse the multi-document YAML bundle (509 docs, 126 unique variant conditions), resolve inherits (*common*, *PLA*, *default_print_*), evaluate printer/nozzle/feeder conditions, merge printer+print+filament into one flat engine config per (printer model, nozzle). Emit assets/presets/prusa/*.json plus manifest.json with family, nozzle, bed_model/bed_texture, default print/filament.
   - files: tools/presets/resolve_prusa.py
   - note: Pivoted: instead of hand-rolling a Python YAML/variant-condition resolver, the engine's own CLI resolves the whole bundle natively (--query-printer-models, --query-print-tool-filament-profiles, --printer/--print/--material + --save). PresetStore drives it in-process. Strictly better than reimplementing the bundle format.
2. [x] Verify resolver output with the engine, not by inspection: load each generated config through the CLI and slice oracle/models/cube_20.stl; fail if any config is rejected or produces empty G-code. Record the printer/print/filament counts.
   - files: tools/presets/resolve_prusa.py, oracle/oracle.py
   - note: Resolved MK4 0.20mm SPEED + Prusament PLA through the engine, seeded SettingsModel, sliced cube_20.stl to valid 9180-line G-code. 32 printer profiles, 5 print profiles and 254 filament profiles enumerated for MK4.
3. [x] Add a PresetStore QObject (printer/process/filament lists + selection + manifest loading) and wire it into SettingsModel (seed values from the selected preset instead of the single base-config.json) and SliceController (config file from preset). Add --dump-presets, --printer, --process, --filament headless knobs so the whole path is testable without a window.
   - files: upstream/src/slic3r-app-qt/src/PresetStore.hpp, upstream/src/slic3r-app-qt/src/PresetStore.cpp, upstream/src/slic3r-app-qt/src/main.cpp, upstream/src/slic3r-app-qt/src/SettingsModel.cpp, upstream/src/slic3r-app-qt/src/SliceController.cpp, upstream/src/slic3r-app-qt/CMakeLists.txt
   - note: PresetStore (PresetStore.hpp/.cpp) with refreshPrinters/selectPrinter/resolve, headless knobs --dump-presets/--printer/--process/--filament, SettingsModel::seedFromConfigJson, configJson() now overlays the resolved template.
4. [ ] Make the printer preset drive the scene: bed_shape, max_print_height, printer_model/variant, bed_model/bed_texture (copy the 8 bundled bed assets), so switching printers reconfigures the plate, camera fit and plate mesh the way Orca does. Keep printer coordinates.
   - files: upstream/src/slic3r-app-qt/src/main.cpp, upstream/src/slic3r-app-qt/src/Host.cpp, upstream/src/slic3r-app-qt/qml/views/Viewport.qml
5. [ ] Build the picker UI: printer / process / filament selectors in the top bar with vendor grouping, searchable, showing the modified-field counts; keep the existing 335-setting editor bound to the selection. Verify with cockpit screenshots and --dump-presets.
   - files: upstream/src/slic3r-app-qt/qml/main.qml, upstream/src/slic3r-app-qt/qml/panels, upstream/src/slic3r-app-qt/src/PresetStore.cpp
6. [ ] Write tools/import-orca/ with a data-driven keymap.yaml (Orca key -> engine key + value/enum maps), an Orca inheritance resolver (inherits chains, fdm_machine_common/fdm_klipper_common), and a coverage report. Pilot on Voron (0.1, 2.4 250/300/350, Trident, Switchwire × 8 nozzles) and prove each converted machine config slices a cube.
   - files: tools/import-orca/keymap.yaml, tools/import-orca/import_orca.py
7. [ ] Run the converter across all 72 Orca vendors (1384 machine, 3371 process, 5669 filament profiles). Ship every profile whose convertible key coverage clears the threshold, tag each preset with its unmapped keys, and write docs/orca-import-coverage.md with per-vendor counts and the 792 engine-less keys grouped by the feature they belong to.
   - files: tools/import-orca/import_orca.py, docs/orca-import-coverage.md
8. [ ] Extend the oracle with a presets check: slice a fixed cube with every shipped printer/process preset and assert exit 0, non-empty G-code and no unknown-key warnings; keep the existing 11/11 byte-identical regression as the gate.
   - files: oracle/oracle.py, oracle/presets.py
9. [ ] Update docs/orca-parity.md (preset dropdowns, printer list, filament/process coverage, per-printer assets) and the project memory with the measured numbers and the mapping decisions.
   - files: docs/orca-parity.md
