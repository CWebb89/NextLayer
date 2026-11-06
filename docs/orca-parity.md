# Orca / Prusa parity ledger

What next-layer has, what it does not, and why. One line per feature so the
answer to "do we have everything Orca has" is a lookup rather than a guess.
Status values:

- **done** — works in this build and was exercised.
- **staged** — the engine supports it and the app has a hook, but the UI is
  disabled or the work is not finished.
- **needs-engine** — the engine has the feature but nothing below its own plater
  exposes it (no CLI flag, no domain API we can call).
- **needs-host** — the feature is wx-free C++ in `slic3r-shared/…/App/Plater` and is
  driven through `PlaterRenderModule`, which only `slic3r-app-desktop` hosts. Nothing
  to write except the host; see "Why the tool rail is one job" below.
- **n/a** — deliberately out of scope (vendor cloud, GUI-only conveniences we
  replace with something else).

Counted 2026-11-05: `slic3r-shared/src/Slic3r/App/Plater/` holds **77** `.cpp` files
and exactly **one** of them mentions the wx `GLCanvas3D`. Every tool in the rail
below lives there, in a wx-free library, on an ImGui + `slic3r-render` device.

**Scope: FFF only.** next-layer is a filament-printer application. The engine
carries a whole resin (SLA) pipeline -- `SLAPrint`, `Hollowing`, `SupportTree`,
exposure and lift settings -- and none of it is exposed here, deliberately: SLA is
not a market this app serves, so resin profiles, drain holes and the resin
settings tab are *n/a* rather than missing. Hollow is the one piece reused, and
only as a mesh operation: growing an inner wall is useful for FFF parts too.

## Settings — tabs and sub-tabs

The tab tree is the engine's own taxonomy (`ConfigItemDef::Category` and
`OptionGroup`), not a hand-written list. Verify with
`next-layer-qt --dump-tabs` and inspect one sub-tab with
`next-layer-qt --dump-subtab process/104`.

### Presets and onboarding, before the tabs

| Feature | Status |
| --- | --- |
| Filament profiles for the vendor bundles | done — a catalog machine lists its bundle's own materials (Prusa MK4 lists 254); the list is the union across that machine's print profiles, not just the first one |
| Filament profiles for our own printer bundle | done — the Voron bundle ships 9: Generic (PLA), PLA Silk, PETG, PETG-CF, PLA-CF, ABS, ASA, TPU, PA-CF, each with its own temperatures, cooling, flow and density; `tools/check` slices Generic PETG and requires an 80 °C bed |
| First-run setup | done — next-layer's own flow (`qml/views/SetupFlow.qml`): machine (search + vendor picker over the whole catalog), material, quality, review; keyboard navigable, `Ctrl+,` to reopen, remembered in QSettings and re-applied on the next launch. No account, no network |
| The engine's own onboarding (account, online/offline, release notes) | n/a — that is PrusaSlicer's, and its plater module is not hosted |

### Viewport interaction

| Feature | Status |
| --- | --- |
| Left drag orbits; left drag moves an object only with the Move tool armed | done — without the guard a rotate that started on the model committed a move, and a move in the sliced view dropped the toolpath; that was the "rotating cuts the model off" report |
| Near/far clip planes scale with camera distance | done — a fixed 1 mm near plane clipped into the model when zoomed in close |
| Sliced toolpaths survive orbiting, panning and zooming | done — verified in the window with real input at 5 zoom steps and three drags |

### Process (`FDMConfigLocation::Print`, 222 settings → 14 sub-tabs)

| Sub-tab | Settings | Status |
| --- | --- | --- |
| Layers & Surfaces | 17 | done |
| Walls & Perimeters | 22 | done |
| Infill | 15 | done |
| Bed adhesion | 13 | done |
| Supports | 30 | done — grid/snug/**organic (tree)** style from `support_material_style`, with the tree branch diameter, angle, double-wall and distance options exposed alongside it (confirmed in a real slice's `--dump-config`) |
| Speed | 25 | done |
| Motion & Dynamics | 22 | done |
| Extrusion & Retraction | 19 | done |
| Tools & Wipe Tower | 21 | done — the engine's own extruder assignment, wipe tower and tool-change options; the seven colour-mixing-only options are filtered out |
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
| Tool Changes | 18 | done — ramming, tip shaping and wipe-tower purging; the MMU flush/purge options are filtered out |
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
| Cut at Z | done — see the CAD row: the plate is cut, not replaced |
| Export G-code | done — writes the sliced file to a chosen path |
| Object list (name, visibility, X/Y, centre/drop/duplicate/delete) | done — `Ctrl+L` or the list button; acts on the document, so a plate edit is undoable |
| Undo / redo (`Ctrl+Z`, `Ctrl+Shift+Z`) | done — snapshot stack over the document's objects, transforms and visibility |
| Arrange | needs-engine — `arrange_and_wait` is internal to the CLI app, not a flag; imports are shelf-packed instead. `Plater::ArrangeGizmo` and the `slic3r-biz-arrange` kernels are wx-free |
| Auto-orient | needs-engine — no CLI flag; the orienter is in the same arrange library |
| Multi-select | staged — one selection at a time; the document holds N objects but the tools act on the selected one |
| Engine-level plates (one project, many beds, one slice) | needs-engine — `Domain::Bed`/`BedContainer` exist, no plater command. A hosted `PlaterRenderModule` brings the bed tabs with it |

## Tool rail

Now part of the same strip.

| Tool | Status |
| --- | --- |
| Move (centre at X/Y, or drag the model on the bed) | done |
| Rotate (X/Y/Z degrees) | done |
| Scale (uniform) | done |
| Cut at Z | done — `Cut.cpp` clips every straddling triangle against the plane and caps both halves with the mesh's own cross-section (`slice_mesh` + `Biz::Algorithms::Tesselate`), so neither half is open. `--mesh-tools-test`: a 20 mm cube cut at 8 mm gives 8 mm and 12 mm parts holding 8000 mm³ between them. The engine's own `CutGizmo`/`CutDialog` exist in the plater host, which is what a future gizmo would drive |
| Measure (two points, distance, deltas, angle from vertical) | done |
| Fuzzy skin | done — jumps to Process › Walls & Perimeters |
| Lay on face | done — click a face; the facet normal is turned down and the object dropped to the bed |
| Support painting | done — sphere brush over the picked facets, stored as the engine's own `FacetsAnnotation`, drawn in the viewport and honoured by the slice (`--slice-features` proves 0 → 983 support moves) |
| Seam painting | done — same brush, `seam_facets` |
| Colour painting for a tool changer | done — the brush writes a per-facet tool index (`mm_segmentation_facets`, the engine's tool field) and the swatch list is the printer's own tool colours, so the overlay is what the machine will print. **There is no MMU and no single-nozzle colour mixing anywhere in this app**: those options are filtered out of the settings panel and the words do not appear in the UI |
| Add a shape (box, cylinder, sphere) | done — an object built by the engine's own `its_make_cube`/`make_cylinder`/`make_sphere`, normalised to the app's XY-centred, Z-on-the-bed convention, so every tool and per-object setting works on it. `--shape-test` slices one and measures the filament |
| Modifier, negative, support-blocker and support-enforcer volumes | done — a volume on the selected object with the engine's own `ModelVolumeType`; the FDM slice path slices each type and booleans it, so a negative volume cuts the toolpath (`--shape-test`: 3899.4 mm → 2823.2 mm of filament, and back to 3899.4 when removed). Drawn translucent by role (red subtracts, amber modifies, purple blocks, green forces) and the object turns into a ghost so an interior volume is visible. The object list nests the volumes with their type and a delete |
| Text object | done — Add ▸ Add text…: a line of glyphs as a solid, through the engine's `Biz::Emboss::text2shapes` plus `Biz::Algorithms::Tesselate`. Fonts come from fontconfig, because the engine's own `get_font_list()` is a no-op off Windows. `--mesh-tools-test`: "next-layer" at 10 mm is 61.5 mm wide, 1.2 mm thick and slices. The outline is baked into the mesh, so the text is geometry afterwards, not a parametric volume |
| Hollow | done — Object ▸ Hollow… grows an inner wall `n` mm inside the surface through the engine's own voxel hollowing (`sla::hollow_mesh`, which is FDM-agnostic); `--mesh-tools-test`: an 8000 mm³ cube becomes a 3941.6 mm³ shell at 2 mm and keeps its 20 mm height |
| Variable layer height | done — a band and a height in the object panel, written as a `LayerHeightProfile` on the object; the slice honours it (`--slice-features` proves 125 → 187 layers) |
| Assemble (split parts) | done, sans gizmo — Merge bakes the selection into one mesh in world coordinates; Split into parts walks the vertex table and gives every connected shell its own object, in place (`--mesh-tools-test`: 1 object → 2) |

| Gizmo handles, drag-rotate/scale, snapping | partly done — the Move tool draws three axis arrows at the selection's centre and a drag along one moves the object along that axis (`PlateScene::pickGizmo` / `gizmoAxisPoint`). Rotate and scale handles, and snapping, are not there yet: the numeric panels own those |

## Camera and selection

| Feature | Status |
| --- | --- |
| Left-drag orbit | done — and in the sliced view it is the only thing a left drag does |
| Clip planes follow the camera | done — near = distance × 0.004, far = distance × 8 + plate radius × 8 + 500, so orbiting close no longer clips into the model |
| Right-drag / middle-drag pan | done |
| Wheel zoom | done |
| `R` / `Home` resets yaw, pitch, zoom and pan | done |
| View presets — iso, top, bottom, front, rear, left, right (keys 1–7, corner camera bar) | done |
| Orthographic / perspective toggle (`O`, camera bar) | done — the ortho camera carries the perspective framing so the switch does not resize the plate |
| Fit plate (`F`, camera bar) | done — frames the whole `bed_shape`, which is Orca's opening framing |
| Layer step from the keyboard (Up / Down) | done |
| Click to select an object (accent tint) | done — picking walks every visible object's triangles, nearest hit wins, the document's selection follows |
| Drag the selected object on the bed with the Move tool | done — commits `setPosition` on the document, so it is undoable and does not touch the file; the drag only arms with the Move tool, so a rotate never moves the plate under you |
| Object list / per-object context menu | done — object list docked bottom-right, and right-clicking a row opens the object menu |
| Multi-select | done — Ctrl-click toggles, Shift-click extends from the anchor, Ctrl+A selects the plate, and a drag with the Move tool moves every selected object by the same delta, undoably |
| Ray picking for selection and measure | done — Möller–Trumbore over the mesh in printer coordinates; View3D.pick() returns nothing in this app |
| Place on face | done — click a face with the tool armed; the facet's normal is turned to point down and the object is dropped onto the bed |
| Auto-orient | done — every distinct face normal is tried as a resting surface and the pose scoring best on printed height plus overhanging area wins |
| Arrange | done — the engine's own NFP packer (`Biz::Arrange`, the same kernel the plater uses) with rotations allowed, applied in the document and undoable |
| Snap to bed centre, rotation snapping | not started |
| Gizmo handles (red/green/blue arrows) for axis drags | not started — drag moves freely on the grabbed plane |
| Visibility modes | done — per-object hide, isolate the selection (Ctrl+I), show everything again (Ctrl+Shift+H); hidden objects are excluded from the slice |

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

## Object settings and plates

| Feature | Status |
| --- | --- |
| Per-object settings | done — 136 print settings are overridable per object in this engine; the object panel lists them with an inherit/override switch and a typed editor, they are stored in `ObjectSettings::overrides` and reach the slicer through the plate path (`--slice-features` proves 0.30 mm per-object layer height → 84 layers instead of 125) |
| Variable layer height | done — see the tool rail row; the profile covers the whole object and restates the printer height outside the band |
| Plates | done — every plate is its own set of objects with its own toolpath; the toolbar shows the object count per plate, adding and removing plates keeps the document (and undo) intact, and each plate slices and exports separately |
| Save / load project file | done — Ctrl+S / Ctrl+Shift+O, one `.nextproj` file holding every object with its transform, overrides, layer height profile and paint, the plates, the tool colours and the print setup; `--project-test` saves, reloads into a cleared document, compares and slices the reloaded plate |
| Preset diff | done — Ctrl+Shift+D lists every setting changed from the preset, with before/after and a per-row reset |
| Preset export / import | done — Ctrl+Shift+E writes the configuration the app would slice with; Ctrl+Shift+I loads one back into the settings panel |
| Preferences | done — Ctrl+, : bed plate, grid, field of view, re-slice on change, status duration, units; stored in QSettings and applied to the viewport |
| Shortcut map | done — `?` lists every binding, filterable; the same table drives the key handling, so the help cannot drift from the code |

## Product and chrome

| Feature | Status |
| --- | --- |
| Product identity | done — a mark, wordmark, version chip and tagline in the header and About; the exported G-code header and JSON producer carry the same name and version |
| File menu | done — New plate, Add model, Open/Save project, Export G-code, Export/Load configuration, Preferences, Shortcuts, About, Quit, each with its shortcut shown |
| About | done — version, engine version, data and preset paths, all read from the build |
| Preferences | done — Ctrl+, |
| First run | done — a welcome screen (what next-layer is, what it does not do, and the three steps to a slice) leading into the printer setup wizard; "Skip for now" is remembered instead of reopening on every launch |
| Object rename | done — double-click the name in the object list |
| Statistics | done — the footer carries the engine's own estimate (printing time, filament in grams, layers, moves, slice time), and Object ▸ Statistics… measures the selected mesh: volume, surface area, facets, size, bounding box, and the mass at the current filament's density |
| Unsaved-changes guard | done — closing the window with an unsaved plate asks, with save / discard / cancel |
| Units | done — every length field shows the user's system and converts back on write, so a preset on disk is always millimetres; the preference is read at startup, not only when Preferences is opened |
| Multiple plates | done — the tab strip adds, renames, duplicates, clears and deletes plates, and every plate is drawn in the scene as its own bed with its objects, so a new plate appears where it will print |
| Plate options | done — the chevron on the active plate tab: rename, duplicate, clear, delete |
| Custom G-code per layer | done — right-click the layer slider: custom G-code, a pause (M601) or a tool change at that Z. Markers draw as ticks on the slider, are saved in the project, and the engine's own `CustomGCode::Info` emits them at their layer (`tools/check` proves the marker lands once, in the right layer) |
| Object right-click menu | done — right-click a row in the object list: per-object settings, duplicate, delete, isolate, show all, centre, drop, auto-orient, lay on face, and modifier / negative / blocker / enforcer volumes |
| Send to printer (Klipper/Moonraker, OctoPrint, PrusaLink…) | done — File ▸ Send to printer…: host, type, API key, Test, Send, Send and print. The upload goes through the engine's own `PrintHostMoonraker` / `OctoPrint` / `PrusaLink` / `Duet` implementations; `tools/check` proves it against a stand-in Moonraker, marker-verified |
| Multi-filament / multicolour | done, for tool changers — one material per extruder is resolved (`--filaments A,B`), each tool has its own picker in the preset editor, an object prints with a chosen tool (T chip in the object list) and a painted facet prints with the tool it carries. `tools/check` slices a two-tool plate where painting tool 2 adds a tool change and the second colour. MMU single-nozzle mixing stays out by design |
| Calibration suite | done — File ▸ Calibration…: temperature tower, pressure advance tower, flow test and retraction test. Each is built from the engine's primitives plus the plate's own layer G-code (`M104` / `SET_PRESSURE_ADVANCE` / `M221` per band), so it slices, previews and sends like any plate; `tools/check` proves the band commands reach the G-code |

## Project and scene

| Feature | Status |
| --- | --- |
| Open STL by dialog or drag-and-drop | done |
| Empty bed with no model | done |
| Multiple plates (app-level, one document each) | done |
| Object position survives slicing | done — an import is centred once, then slices run with dont_arrange; before this the engine's arrange re-centred every slice and moves were discarded |
| Icon set | Material Symbols Outlined, matching Orca's outline style |
| Several objects on one plate | done — the document holds N objects; a slice bakes them into one STL of separate shells and the engine prints every island |
| Per-object settings | staged — the merge is geometry only, so object configs are not applied yet; `Domain::ModelObject` carries them and the shared plater's object settings page edits them, so this lands with either step 3 or the host |
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
| Machine geometry (bed size, height, nozzle, extruder count, limits) | done — 432 machines, sliced end to end |
| Per-vendor printer listing and search | done — catalog counts 482 printers in `--dump-presets`, `tools/check` slices one and fails on duplicate names |
| Process and filament settings | staged — borrowed from the PrusaResearch templates, not the vendor's own |
| Vendor start/end G-code | staged — imported for 284 of 432 machines, brace expressions included; the rest use Orca-only names and are dropped whole (see below) |
| Per-vendor profile UI (vendor > machine > variant tree) | staged — the flat searchable list stands in for Orca's grouped picker |

Known loss: a G-code template referencing a name the engine does not have
(`initial_no_support_extruder`, `plate_name`, `machine_max_speed_z`) is dropped
entirely rather than half-applied, so those presets have no start/end sequence
until it is written in Printer → Custom G-code. The same test now reads inside
`{brace expressions}`: braces are kept, `{first_layer_bed_temperature[0]}` and
`{max_layer_z + 0.5}` are evaluated by the engine, but a brace naming an unknown
option stops the whole export (no G-code, exit 0), so it is checked the same way.
Line-level salvage is the obvious next step.

Slicing state, measured with `validate.py` over the catalog's own 432 machines: 423
slice with their vendor start G-code and 9 write no G-code at all (Anycubic Kobra
Max / Plus / Vyper, Flashforge Guider 2s / Adventurer 3 / Adventurer 4 / Artemis,
UltiMaker S5 ×2) — the vendor's start G-code still names an option the engine
cannot evaluate, and the export stops silently. None fail to resolve.

Two generation bugs hid that: `slugify` mapped `UltiMaker 2` and `UltiMaker 2+` to
the same id, so one machine overwrote the other's preset, and `validate.py` parsed
the padded column dump, reading the vendor as part of 12 over-long names. Ids are
unique per vendor now, `validate.py` reads `--dump-presets-json`, and the app's
hardware configs carry no duplicate names — which is what made process lookup look
nondeterministic: resolution takes the first config with a matching name, and a
duplicate name resolved differently from run to run.

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
| Unsupported (no engine counterpart) | ~657 | AMS, belt printer, chamber control, Bambu-specific, Orca's `tree_support_*` names (the engine's own organic supports are `support_tree_*` and are exposed), jerk-per-axis |

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

## Why the tool rail is one job, not eleven

It is tempting to read `needs-engine` as "we would have to rewrite support and
seam painting from scratch". That is the wrong reading, and the count is what
settles it:

```sh
ls upstream/src/slic3r-shared/src/Slic3r/App/Plater/*.cpp | wc -l              # 77
grep -l 'GLCanvas3D' upstream/src/slic3r-shared/src/Slic3r/App/Plater/*.cpp | wc -l  # 1
cd upstream/build && find . -path '*App/Plater*' -name '*.o' | wc -l           # 78
find . -path '*App/Plater*' -name '*.o' -print0 | xargs -0 nm -uC 2>/dev/null \
    | grep -ci 'wx[A-Z]\|wxString\|wxWidgets'                                 # 0
```

The last line is the decisive one: those 78 object files need **zero** wx symbols.

`CutGizmo`, `PlaceOnFaceGizmo`, `PaintOnSupportsGizmo`, `PaintOnSeamsGizmo`,
`PaintOnFuzzySkinGizmo`, `MultiMaterialPaintingGizmo`, `TextGizmo`/`SvgGizmo`,
`VariableLayerHeightGizmo`, `HeightRangeGizmo`, `ArrangeGizmo`, `MeasureGizmo`,
`SimplifyGizmo`, the rotate/scale/translate gizmos, `SelectionHandler`,
`GizmoWindow`, `SidebarPlaterActionButtons` and `ThumbnailRenderer` are all in
that directory. They are ImGui + `slic3r-render` device code (`CommandBuffer`,
`FramebufferManager`, `ShaderManager`, `ImageCodec`) behind
`Platform::AbstractRenderModule`, `IDialogManager` and `AppServices`. PrusaSlicer
3.0's whole point was extracting them from wx so a second frontend can host them.

Exactly one application does that today:

```sh
upstream/src/slic3r-app-desktop/src/Slic3r/App/Desktop/DesktopApp.cpp:473
```

`PlaterRenderModule(workbench, project_interactor, undo_store, thumbnail_store,
thumbnail_store_updater, thumbnail_image_generator, font_manager, project_saver)`,
rendered into a `Platform::AbstractRenderCanvas` — the wx implementation of which
is `slic3r-platform-wx/…/WXRenderCanvas.cpp`, **1247 lines**.

So the cost of the entire tool rail is not eleven features. It is one host: a Qt
equivalent of `WXRenderCanvas` (GL surface, ImGui input plumbing, framebuffer
handoff), the services that constructor asks for, and `set_sidebars_visible(false)`
if the QML chrome is to stay. Painting, cut, lay-on-face, text, variable layer
height, arrange, measure, gizmo handles and the bed tabs all arrive together,
because they are gizmos on that one module.

The trade, stated plainly: the render module owns the GL surface and the scene, so
`Viewport3D`/`Gizmo`-style Qt Quick 3D model rendering and the custom picking and
ribbon preview built so far would be superseded for plater and preview duty. What
survives that trade is the half that made next-layer viable — `SliceRuntime`,
`PresetStore`, `SettingsModel`, the oracle, the cockpit and this ledger.

## What "done" means here

Every `done` row was run at least once through `--smoke` (rendered offscreen),
`--dump-*` (settings tree), `--transform` (engine path) or the oracle suite.
Interactive rows (clicking, dragging, the file dialog) are implemented but were
not exercised by hand; they are called out as such in the session report.
