---
id: preset-editor-dialog-real-editors-for-every-engi-20261103-134346
status: awaiting_approval
created: 2026-11-03T13:43:46.770Z
updated: 2026-11-03T13:43:46.770Z
---

# Preset editor dialog + real editors for every engine setting type

## Summary

Port Orca's preset-editing flow: the gear next to each preset dropdown opens a full tabbed preset dialog (Print / Filament / Printer) with the complete settings tree, a preset toolbar (combo, revert, save, save-as, delete, rename, search) and real per-type editors — colour picker, file picker, bed-shape polygon, point lists, string lists, mm/% unit switch, open enums — instead of today's small footer popup and generic text fields.

## Risks

- Preset YAML writing for the new value shapes (colour strings, point lists, string lists) may not round-trip through the engine's reader; valuesYaml currently covers numbers/enums/bools/strings and has to be extended and re-verified with a real resolve.
- The engine's ConfigItemDef carries no Simple/Advanced/Expert mode, so the dialog shows every setting (as the panel already does) rather than Orca's three visibility modes — a deliberate difference, recorded in the parity ledger.
- A modal in-window dialog differs from Orca's separate OS window; it keeps the app single-window and is cheaper to drive with the cockpit, but it is not pixel-identical to either reference.
- Out of scope because they need engine APIs we do not expose: physical-printer/print-host setup, preset export bundles, compare-presets diff dialog, incompatible-preset filtering, and multi-extruder per-slot editing (the model collapses filament values to slot 0, matching a single-tool machine).

## Steps

1. [ ] Extract the settings tree (group headers, rows, revert bullets, tooltips) out of SettingsPanel.qml into qml/views/SettingsTree.qml so the panel and the new dialog share one implementation
   - files: upstream/src/slic3r-app-qt/qml/views/SettingsPanel.qml, upstream/src/slic3r-app-qt/qml/views/SettingsTree.qml
2. [ ] Add qml/components/SettingEditor.qml that dispatches on type + guiType and covers all FFF cases: Boolean toggle, closed enum combo, open enum (i/f/s_enum_open) editable combo, int/float NumberField, FloatOrPercentage with an mm/% switch, Vec2d pairs, points lists, string lists, multiline strings, colour, file path, bed_shape button, substitutions and ramming params buttons
   - files: upstream/src/slic3r-app-qt/qml/components/SettingEditor.qml
3. [ ] Add the specialised dialogs those editors open: BedShapeDialog.qml (rect + polygon with add/remove/drag-ordered points), ColourDialog.qml (swatch grid + hex entry), SubstitutionsDialog.qml (gcode_substitutions rows: search, replace, params, notes), StringListDialog.qml (vector<string>/ramming params rows)
   - files: upstream/src/slic3r-app-qt/qml/views/BedShapeDialog.qml, upstream/src/slic3r-app-qt/qml/views/ColourDialog.qml, upstream/src/slic3r-app-qt/qml/views/SubstitutionsDialog.qml, upstream/src/slic3r-app-qt/qml/views/StringListDialog.qml
4. [ ] Extend SettingsModel with what the new editors need: a setValue overload carrying the FloatOrPercentage percent flag, setStringList/setPoints helpers, resetAll(tab), and preset-vs-modified reporting so a dialog can show a dirty count per tab
   - files: upstream/src/slic3r-app-qt/src/SettingsModel.hpp, upstream/src/slic3r-app-qt/src/SettingsModel.cpp
5. [ ] Extend PresetStore with the preset actions the dialog needs: savePreset(kind,name) that overwrites an existing user preset and refuses a system one with a reason, presetDescription(kind,name) read from the YAML, and kindForTab(tab)
   - files: upstream/src/slic3r-app-qt/src/PresetStore.hpp, upstream/src/slic3r-app-qt/src/PresetStore.cpp
6. [ ] Build qml/views/PresetEditorDialog.qml: modal in-window dialog with the three-tab strip, a toolbar (preset combo, revert-all, save, save-as, delete, rename, search field, modified count, add-printer on the printer tab), the shared SettingsTree as body, and an action message line; save on a system preset falls through to save-as as Orca does
   - files: upstream/src/slic3r-app-qt/qml/views/PresetEditorDialog.qml
7. [ ] Wire the three gear buttons to open the dialog on their tab, replace the current PresetSettingsPopup footer with an entry point on the printer tab for Add printer, and keep the SettingsPanel tab strip working as it does today
   - files: upstream/src/slic3r-app-qt/qml/views/SettingsPanel.qml, upstream/src/slic3r-app-qt/qml/views/PresetSettingsPopup.qml, upstream/src/slic3r-app-qt/qml/main.qml
8. [ ] Make bed_shape edits reach the viewport: re-read bed_shape and max_print_height from settingsModel into Host when they change, so a new bed in the dialog moves the plate instead of waiting for a resolve
   - files: upstream/src/slic3r-app-qt/src/main.cpp
9. [ ] Add a --preset-dialog TAB headless knob that opens the dialog on a tab before the smoke frame so its render is provable without a mouse
   - files: upstream/src/slic3r-app-qt/src/main.cpp
10. [ ] Verify: nl_build, oracle verify --runner qt (must stay 12/12), nl_smoke shots of the dialog on all three tabs, nl_cockpit to change a setting, save-as, rename and delete through real clicks, then update README, docs/orca-parity.md and MEMORY.md
   - files: upstream/src/slic3r-app-qt/README.md, docs/orca-parity.md
