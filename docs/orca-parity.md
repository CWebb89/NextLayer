# Orca / Prusa parity ledger

What next-layer has, what it does not, and why. One line per feature so the
answer to "do we have everything Orca has" is a lookup rather than a guess.
Status values:

- **done** — works in this build and was exercised.
- **staged** — the engine supports it and the app has a hook, but the UI is
  disabled or the work is not finished.
- **needs-engine** — the engine has the feature but nothing below its own plater
  exposes it (no CLI flag, no domain API we can call).
- **n/a** — deliberately out of scope (vendor cloud, GUI-only conveniences we
  replace with something else).

## Settings — tabs and sub-tabs

The tab tree is the engine's own taxonomy (`ConfigItemDef::Category` and
`OptionGroup`), not a hand-written list. Verify with
`next-layer-qt --dump-tabs` and inspect one sub-tab with
`next-layer-qt --dump-subtab process/104`.

### Process (`FDMConfigLocation::Print`, 222 settings → 14 sub-tabs)

| Sub-tab | Settings | Status |
| --- | --- | --- |
| Layers & Surfaces | 17 | done |
| Walls & Perimeters | 22 | done |
| Infill | 15 | done |
| Bed adhesion | 13 | done |
| Supports | 30 | done |
| Speed | 25 | done |
| Motion & Dynamics | 22 | done |
| Extrusion & Retraction | 19 | done |
| Multi-Material | 28 | done |
| Precision & Slicing | 19 | done |
| Custom G-code | 1 | done |
| Output options | 1 | done |
| Notes | 1 | done |
| Advanced (legacy aliases + profile identity) | 9 | done |

### Filament (`FDMConfigLocation::Filament`, 65 settings → 8 sub-tabs)

Single material slot today. The saved config keeps the engine's per-slot array
shape; a second material needs the multi-slot UI, which is not built.

| Sub-tab | Settings | Status |
| --- | --- | --- |
| Material & Temperatures | 13 | done |
| Extrusion & Calibration | 6 | done |
| Cooling | 17 | done |
| Multi-Material | 20 | done |
| Overrides | 3 | done |
| Custom G-code | 3 | done |
| Notes | 1 | done |
| Advanced (filament identity) | 2 | done |
| Per-filament colour picker (`gui_type: color`) | — | staged — edited as text |

### Printer (`FDMConfigLocation::Printer`, 65 settings → 8 sub-tabs)

| Sub-tab | Settings | Status |
| --- | --- | --- |
| General | 17 | done |
| Bed | 3 | done — `bed_shape` shown as points, no visual bed editor |
| Custom G-code | 11 | done |
| Machine limits | 19 | done |
| Multiple extruders | 1 | done |
| Single extruder MM setup | 7 | done |
| Printer notes | 1 | done |
| Advanced (printer identity: model/variant/vendor) | 6 | done |
| Bed shape editor (drag polygon, SVG/STL background) | — | needs-engine — the plater's bed editor is not exposed |
| Physical printer / host offsets | — | n/a — vendor connection dialogs |

### Setting features, app-wide

| Feature | Status |
| --- | --- |
| Search across every tab, results labelled Tab › Sub-tab › Group | done |
| Per-field revert (⟲ appears on modified fields) | done |
| Per-sub-tab reset | done |
| Modified counters per tab and per sub-tab | done |
| Preset dropdowns (system / user profiles) | done — Printer / Process / Filament, cascading + auto-resolve via the engine's own bundle |
| Preset editor dialog behind the gear | done — tabs, preset combo, revert, Save / Save as / Rename / Delete, search across every tab, Add printer |
| One editor per setting type | done — boolean, closed and open enums, numbers with the definition's range and units, mm/% switch, X/Y pairs, point lists, bed shape, colour, file picker, G-code substitutions, string lists, multi-line G-code |
| Per-setting revert | done — the ⟲ on a modified row |
| Revert a whole tab from the dialog | done |
| Rename / delete a user preset | done — both refuse a vendor preset and say why |
| Add a custom printer (bed size, height) | done — writes a self-contained vendor bundle (vendor + printer + tool + sheet + printer/process/tool/filament presets) from the current settings, then selects it; hand-authoring the 3.0 YAML was the risk and it is gone |
| Save a *printer* preset from the Printer tab | staged — a second printer preset on an existing hardware config appears as a duplicate profile name, so a new machine is the supported route |
| Preset visibility modes (Simple / Advanced / Expert) | not started — the engine's `ConfigItemDef` carries no mode field, so every setting is always shown and the 17 hidden legacy/identity settings sit in per-tab "Advanced" sub-tabs |
| Physical printer / print host setup | needs-engine — no printer-host API in the app layer yet |
| Preset export bundle, compare-presets diff | not started |
| Multi-extruder per-slot editing | staged — the settings model collapses filament values to slot 0, which matches a single-tool machine |
| "Compare presets" / diff view | not started |
| Per-object override editor | needs-engine — object config containers exist in Domain, no plater API |
| `FloatOrPercentage` unit switch (mm ↔ %) | done — a two-state button beside the value, which rewrites the stored value in the other unit |
| Keyboard shortcut map (Orca's full list) | not started — only Ctrl+O, Space, Return, R, Esc |

## Panel chrome

| Feature | Status |
| --- | --- |
| Process / Filament / Printer tabs | done |
| Vertical sub-tab rail | done |
| Panel docks left or right, animated, remembered across launches | done |
| Global preferences dialog (Orca's Ctrl+P) | not started — the header toggle is the placeholder, as agreed |

## Viewport toolbar (one strip over the viewport)

Plates on the left, tools next, object actions on the right — one panel.

| Action | Status |
| --- | --- |
| Add plate / switch plate / remove plate (middle-click a tab) | done — app-level plates: each holds its own model and G-code |
| Open / import model | done — dialog or drag-and-drop appends to the plate; multi-file drop adds every file |
| Duplicate | done — the document copies the selected object beside it |
| Centre on bed | done — the document moves the selected object's centre onto the bed centre |
| Drop to bed | done — the document lifts/lowers the selected object until its minimum Z sits on the bed |
| Cut at Z | staged — still runs the engine on the single selected file, so it replaces the plate; the document needs a mesh split |
| Export G-code | done — writes the sliced file to a chosen path |
| Object list (name, visibility, X/Y, centre/drop/duplicate/delete) | done — `Ctrl+L` or the list button; acts on the document, so a plate edit is undoable |
| Undo / redo (`Ctrl+Z`, `Ctrl+Shift+Z`) | done — snapshot stack over the document's objects, transforms and visibility |
| Arrange | needs-engine — `arrange_and_wait` is internal to the CLI app, not a flag; imports are shelf-packed instead |
| Auto-orient | needs-engine — no CLI flag |
| Multi-select | staged — one selection at a time; the document holds N objects but the tools act on the selected one |
| Engine-level plates (one project, many beds, one slice) | needs-engine — `Domain::Bed`/`BedContainer` exist, no plater command |

## Tool rail

Now part of the same strip.

| Tool | Status |
| --- | --- |
| Move (centre at X/Y, or drag the model on the bed) | done |
| Rotate (X/Y/Z degrees) | done |
| Scale (uniform) | done |
| Cut at Z | done |
| Measure (two points, distance, deltas, angle from vertical) | done |
| Fuzzy skin | done — jumps to Process › Walls & Perimeters |
| Lay on face | needs-engine — gizmo only |
| Support painting | needs-engine — paint-on gizmo only |
| Seam painting | needs-engine — paint-on gizmo only |
| MMU / multi-material painting | needs-engine — paint-on gizmo only |
| Text object | needs-engine — text-to-mesh lives in the plater |
| Variable layer height | needs-engine — layer-height profile API is plater-only |
| Assemble (split parts) | needs-engine — needs multi-object scenes |
| Calibration suite (flow, pressure advance, retraction…) | not started |
| Gizmo handles, drag-rotate/scale, snapping | not started — transforms are numeric panels apart from drag-move |

## Camera and selection

| Feature | Status |
| --- | --- |
| Left-drag orbit | done |
| Right-drag / middle-drag pan | done |
| Wheel zoom | done |
| `R` / `Home` resets yaw, pitch, zoom and pan | done |
| View presets — iso, top, bottom, front, rear, left, right (keys 1–7, corner camera bar) | done |
| Orthographic / perspective toggle (`O`, camera bar) | done — the ortho camera carries the perspective framing so the switch does not resize the plate |
| Fit plate (`F`, camera bar) | done — frames the whole `bed_shape`, which is Orca's opening framing |
| Layer step from the keyboard (Up / Down) | done |
| Click to select an object (accent tint) | done — picking walks every visible object's triangles, nearest hit wins, the document's selection follows |
| Drag the selected object on the bed with the Move tool | done — commits `setPosition` on the document, so it is undoable and does not touch the file |
| Object list / per-object context menu | done — object list docked bottom-right; a context menu is still Orca-only |
| Multi-select | staged — one selection at a time |
| Ray picking for selection and measure | done — Möller–Trumbore over the mesh in printer coordinates; View3D.pick() returns nothing in this app |
| Snap to bed centre, rotation snapping | not started |
| Gizmo handles (red/green/blue arrows) for axis drags | not started — drag moves freely on the grabbed plane |

## Preview

| Feature | Status |
| --- | --- |
| Toolpath preview with per-layer slider | done — vertical two-handle slider down the right edge of the viewport, Orca's placement and shape |
| Move slider (scrubs the moves inside the current layer) | done — horizontal bar along the bottom of the viewport, two handles, one tick per move; libvgcode's visible range is a move interval |
| Extruded ribbon geometry per path (not 1 px GL lines) | done — engine extrusion width per move, moves joined into one polyline per path with mitred corners, top face plus side walls spanning the layer height, fixed feature palette |
| Single-layer / whole-part preview modes | done — footer toggle, as Orca's |
| Line-type panel with time, share and filament per type, plus totals | done — time from the engine's per-move estimates; totals match the sliced file's footer (970.60/970.61 mm, 2.895/2.89 g, 0.081/0.08 cost, 810.9/812 s) |
| Panel placement and shape | done — top-right of the viewport as in Orca, folding to its header row; columns are Line type / Time / % / Usage (length and mass), then the Options rows, then Total estimation (filament, cost, prepare time, model printing time, total time) |
| Colouring mode picker (feature type, height, width, speed, actual speed, fan, temperature, flow, layer time, tool) | done — the engine's own `ViewType`s; the numeric modes list their colour bands with the band's value |
| G-code window at the current move | done — numbered monospace lines with the producing line boxed, as many lines as fit; the current vertex's `gcode_id` indexes the file, so the boxed line is the one that asked for the move |
| Nothing overlaps in the viewport | done — the moves slider owns the bottom edge and publishes its height, the camera card and the right-hand column offset by it, and the layer slider publishes its width so the legend and g-code window stop left of it |
| Slider drags one layer at a time | done — verified headlessly by `--slider-test`, which drags both sliders with synthetic pointer events and reports the layer after every step |
| Orientation triad at the print origin | done — projected from printer (0,0,0), hidden when the origin leaves the viewport |
| Sliced geometry up to the slider (not just the last layer) | done |
| Per-line-type show/hide from the legend | done — dims the row, rebuilds the geometry |
| Solid model hidden in preview (no dim shell over the lines) | done |
| Cut-away to the current layer in the solid model | n/a — preview draws sliced lines, as Orca does |
| Travel/retraction filters as separate categories | staged — they are lines in the toolpath, not legend entries yet |
| G-code line colour schemes | staged — colour comes from the palette |
| Build plate artwork and grid | done — the printer's `bed_model` mesh is drawn in one near-black colour with Orca's grid over it, as its Voron bed reads; no sheet texture and no vignette, so nothing looks like a blotch on the bed |
| Bed grid | done — 10 mm lines with every fifth heavier, anchored on the print origin, one pixel wide at any zoom (a shader distance field, not geometry), fading out when a cell would cover fewer than a few pixels |
| One layer slider | done — the vertical slider down the right edge is the only one; the footer reports the layer and the view mode without duplicating the track |
| Origin axes | done — red X, green Y, blue Z arrows standing at the print origin, so the bed's front-left corner says where zero is; drawn in the scene in printer coordinates |
| Layer view modes | done — Range / All / 1 layer, on top of the vertical layer slider |
| Tool marker | off — the engine defaults it on, which draws a translucent cylinder on the bed beside the print |

## Project and scene

| Feature | Status |
| --- | --- |
| Open STL by dialog or drag-and-drop | done |
| Empty bed with no model | done |
| Multiple plates (app-level, one document each) | done |
| Object position survives slicing | done — an import is centred once, then slices run with dont_arrange; before this the engine's arrange re-centred every slice and moves were discarded |
| Icon set | Material Symbols Outlined, matching Orca's outline style |
| Several objects on one plate | done — the document holds N objects; a slice bakes them into one STL of separate shells and the engine prints every island |
| Per-object settings | needs-engine — the merge is geometry only, so object-level configs are not applied yet |
| Save/load project (`.3mf` with settings and objects) | needs-engine — `store_3mf(const std::string&, const Domain::Project&)` and `load_3mf` exist, but building a `Domain::Project` needs a live `ProjectInteractor`; doing that beside the worker-thread `CLI::run` slice would put two engine runtimes in one process |
| Undo / redo | done — `Ctrl+Z` / `Ctrl+Shift+Z` over add, delete, duplicate, transform and visibility |
| Sequential printing | staged — settings exist, no scene arrangement |

## Printer catalog

`tools/orca-catalog/generate.py` turns the OrcaSlicer vendor profiles into a
next-layer bundle: **67 vendors, 450 printers**, written to
`upstream/resources/presets/orca-catalog/` (a build artifact, regenerated, not
tracked). Pick one from the Printer dropdown — the list is searchable — and the
engine resolves it like any other preset.

| Aspect | State |
| --- | --- |
| Machine geometry (bed size, height, nozzle, extruder count, limits) | done — 450 machines, sliced end to end |
| Per-vendor printer listing and search | done — catalog counts 471 printers in `--dump-presets`, `tools/check` slices one |
| Process and filament settings | staged — borrowed from the PrusaResearch templates, not the vendor's own |
| Vendor start/end G-code | staged — imported for 96 of 450 machines; the rest use Orca-only placeholders and are dropped whole (see below) |
| Per-vendor profile UI (vendor > machine > variant tree) | staged — the flat searchable list stands in for Orca's grouped picker |

Known loss: a G-code template referencing a placeholder the engine does not have
(`initial_no_support_extruder`, `plate_name`) is dropped entirely rather than
half-applied, so those presets have no start/end sequence until it is written in
Printer → Custom G-code. The same rule rejects `{brace expressions}`, which the
engine does support in its own defaults — conservative, and to be revisited.
Line-level salvage and brace support are the obvious next steps.

Slicing state, measured across the whole catalog: 480 printers listed, 426
sliced, 54 did not — mostly PrusaResearch presets without a `Standard` process;
the catalog machines among them are Anycubic Kobra Max / Kobra Plus / Vyper and
Flashforge Artemis / Guider 2s.

## Orca config converter

`tools/orca-convert/convert.py` turns OrcaSlicer profiles (flat JSON, `inherits`
chains) into next-layer config JSON over the PrusaSlicer 3.0 engine. It is a
keymap plus value transforms, not a settings reimplementation.

| Bucket | Count | Notes |
| --- | --- | --- |
| Engine FDM settings (next-layer) | 352 surfaced | 335 regular + 17 hidden, always visible |
| Orca distinct setting keys | 912 | union of every shipped profile |
| Exact-name match | 138 | same key, same meaning |
| Renamed by keymap | 117 | e.g. `wall_loops` → `perimeters`, `bottom_shell_layers` → `bottom_solid_layers` |
| Unsupported (no engine counterpart) | ~657 | AMS, belt printer, chamber control, Bambu-specific, `tree_support_*`, jerk-per-axis |

Run `convert.py --report` for the current keymap and a list of renamed targets
missing from the engine. Run `convert.py <machine.json> <process.json> … --out
config.json` to convert; the output is a `configuration` fragment to merge onto a
base config.

**Known limits.** Names and units map cleanly; value *formats* do not always:
some enum vocabularies (`gcode_flavor`, `machine_limits_usage`), and per-extruder
vector shapes differ between the two slicers. G-code templates are translated
token by token (`PLACEHOLDER_RENAMES`) and validated against the engine's own
option names; a template that still carries an unknown name, or a brace
expression, is dropped and reported instead of being written out broken. The
keymap is the reference; the conversion is a starting point, not a lossless port.

## What "done" means here

Every `done` row was run at least once through `--smoke` (rendered offscreen),
`--dump-*` (settings tree), `--transform` (engine path) or the oracle suite.
Interactive rows (clicking, dragging, the file dialog) are implemented but were
not exercised by hand; they are called out as such in the session report.
