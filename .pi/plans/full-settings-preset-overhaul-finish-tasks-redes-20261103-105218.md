---
id: full-settings-preset-overhaul-finish-tasks-redes-20261103-105218
status: awaiting_approval
created: 2026-11-03T10:52:18.072Z
updated: 2026-11-03T11:42:36.914Z
---

# Full settings/preset overhaul: finish tasks, redesign panel, user presets, wizard

## Summary

Finish the open parity tasks, then rebuild the settings surface (resizable panel, Orca-style pickers + per-picker settings popups, all settings always visible), then add user-saveable presets and a custom-printer wizard on the engine's native profile tree.

## Risks

- Steps 9-10 (user presets + wizard) depend on the 3.0 alpha's YAML vendor-bundle schema; hand-authoring it may need reverse-engineering, and the wizard-generated bundle must pass the engine's own loader
- The resizable panel touches the viewport width binding in main.qml, which is also animated on side-toggle
- Orca's extra ~792 settings are engine features with no 3.0 counterpart; the converter will map/rename what exists and document the rest rather than fake them

## Steps

1. [x] Task 4: copy the 8 bundled bed STL assets into the app data dir and wire Host::setPrinter to pick the per-printer bed model + confirm the viewport re-fits on bed change
   - files: upstream/src/slic3r-app-qt/src/Host.cpp, upstream/src/slic3r-app-qt/qml/views/Viewport.qml
2. [x] Task 8 (revised): surface the 17 hidden legacy-alias settings so every engine setting is always visible, no expert toggle
   - files: upstream/src/slic3r-app-qt/src/SettingsModel.cpp
3. [x] Task 9: add an oracle case that slices through a resolved preset (MK4 + speed + PLA) so preset resolution is regression-checked
   - files: oracle/oracle.py
4. [x] Tasks 6-7: write the Orca config converter + keymap (convertible/renamed/unsupported), pilot it on Voron, and record coverage in the parity ledger
   - files: tools/orca-convert/convert.py, docs/orca-parity.md
5. [x] Task 10: update docs/orca-parity.md and the memory store to reflect the new preset machinery
   - files: docs/orca-parity.md, ~/.pi/agent/projects-memory/next-layer/MEMORY.md
6. [x] Make the settings panel resizable: drag handle on its inner edge, persisted width, viewport re-anchors live
   - files: upstream/src/slic3r-app-qt/qml/main.qml, upstream/src/slic3r-app-qt/qml/views/SettingsPanel.qml
7. [x] Reorganize the picker bar + tabs into an Orca-style layout that fits at any panel width (labels, dropdowns, per-picker 'settings' buttons, save/reset controls)
   - files: upstream/src/slic3r-app-qt/qml/views/SettingsPanel.qml
8. [x] Add the Orca-style printer-settings popup and filament-settings popup showing the resolved machine/material config, grouped and read-only
   - files: upstream/src/slic3r-app-qt/qml/views/PrinterSettingsDialog.qml, upstream/src/slic3r-app-qt/qml/views/FilamentSettingsDialog.qml
9. [ ] User presets: point datadir at ~/.local/share/next-layer, wire save/rename/delete of the current resolved config as a user preset through the engine, and refresh the pickers
   - files: upstream/src/slic3r-app-qt/src/PresetStore.hpp, upstream/src/slic3r-app-qt/src/PresetStore.cpp, upstream/src/slic3r-app-qt/src/main.cpp
10. [ ] Setup wizard: QML flow to add a custom FFF printer (bed size, nozzle, filament), generating a user vendor bundle + printer profile the engine enumerates
   - files: upstream/src/slic3r-app-qt/qml/views/Wizard.qml, upstream/src/slic3r-app-qt/src/PresetStore.cpp
