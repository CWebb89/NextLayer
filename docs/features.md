# Feature ledger

What next-layer has, what it does not, and why. One line per feature so the
answer to "do we have it yet" is a lookup rather than a guess.
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
| The engine's own onboarding (account, online/offline, release notes) | n/a — that is the engine's own plater, and it is not hosted |

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
| Supports | 30 | done — grid/snug/**tree** style from `support_material_style`, four tree shapes (Tree, Tree slim, Tree strong, Tree hybrid) as parameter profiles over the engine's one organic generator, with the tree branch diameter, angle, double-wall and distance options exposed alongside them and editable after a shape is picked (confirmed in a real slice's `--dump-config`) |
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
| Keyboard shortcut map | not started — only Ctrl+O, Space, Return, R, Esc |

## Panel chrome

| Feature | Status |
| --- | --- |
| Process / Filament / Printer tabs | done |
| Vertical sub-tab rail | done |
| Panel docks left or right, animated, remembered across launches | done |
| Global preferences dialog | not started — the header toggle is the placeholder, as agreed |

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
| Arrange | done — the engine's own NFP packer (`Biz::Arrange`), applied in the document and undoable; `tools/check` packs two overlapping objects apart |
| Auto-orient | done — every distinct face normal is tried, the pose scoring best on printed height plus overhanging area wins (`tools/check`: 33.32 -> 27.43 mm) |
| Multi-select | done — Ctrl-click, Shift-click, Ctrl+A and a drag that moves the whole selection |
| Engine-level plates (one project, many beds, one slice) | done — plates belong to the document (`plateCount`, names, locks), the bar carries add / clone / lock / delete beside the selector, and the viewport draws a full bed per plate at its own offset. A lock makes the plate read-only: adding, moving, deleting, painting and clearing are refused with a message. Plates, names and locks travel in `.nextproj` |

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
| Fit plate (`F`, camera bar) | done — frames the whole `bed_shape`, which is the opening framing |
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
| Toolpath preview with per-layer slider | done — vertical two-handle slider down the right edge of the viewport, the expected placement and shape |
| Move slider (scrubs the moves inside the current layer) | done — horizontal bar along the bottom of the viewport, two handles, one tick per move; libvgcode's visible range is a move interval |
| Extruded ribbon geometry per path (not 1 px GL lines) | done — engine extrusion width per move, moves joined into one polyline per path with mitred corners, top face plus side walls spanning the layer height, fixed feature palette |
| Single-layer / whole-part preview modes | done — footer toggle |
| Line-type panel with time, share and filament per type, plus totals | done — time from the engine's per-move estimates; totals match the sliced file's footer (970.60/970.61 mm, 2.895/2.89 g, 0.081/0.08 cost, 810.9/812 s) |
| Panel placement and shape | done — top-right of the viewport, folding to its header row; columns are Line type / Time / % / Usage (length and mass), then the Options rows, then Total estimation (filament, cost, prepare time, model printing time, total time) |
| Colouring mode picker (feature type, height, width, speed, actual speed, fan, temperature, flow, layer time, tool) | done — the engine's own `ViewType`s; the numeric modes list their colour bands with the band's value |
| G-code window at the current move | done — numbered monospace lines with the producing line boxed, as many lines as fit; the current vertex's `gcode_id` indexes the file, so the boxed line is the one that asked for the move |
| Nothing overlaps in the viewport | done — the moves slider owns the bottom edge and publishes its height, the camera card and the right-hand column offset by it, and the layer slider publishes its width so the legend and g-code window stop left of it |
| Slider drags one layer at a time | done — verified headlessly by `--slider-test`, which drags both sliders with synthetic pointer events and reports the layer after every step |
| Orientation triad at the print origin | done — projected from printer (0,0,0), hidden when the origin leaves the viewport |
| Sliced geometry up to the slider (not just the last layer) | done |
| Per-line-type show/hide from the legend | done — dims the row, rebuilds the geometry |
| Solid model hidden in preview (no dim shell over the lines) | done |
| Cut-away to the current layer in the solid model | n/a — preview draws sliced lines |
| Travel/retraction filters as separate categories | staged — they are lines in the toolpath, not legend entries yet |
| G-code line colour schemes | staged — colour comes from the palette |
| Build plate artwork and grid | done — the printer's `bed_model` mesh is drawn in one near-black colour with the bed grid over it, as a Voron bed reads; no sheet texture and no vignette, so nothing looks like a blotch on the bed |
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
| Calibration suite | done — File ▸ Calibration…: ten tests, each with its own parameter page, then Build. See "Calibration" below for the per-test contract |

## Calibration

The reference menu is nine entries, each opening a dialog that shapes the plate before it
is built (`calib_dlg.cpp`, 1891 lines, one class per test). next-layer picks the
test in a list and edits its parameters in the same dialog -- one fewer modal, the
same parameters, and the same nine tests plus a squish ladder.

Every test is built from the engine's primitives plus the plate's own layer
G-code, so it slices, previews and sends like any plate. The band command is
chosen by the printer's own `gcode_flavor`, because `SET_PRESSURE_ADVANCE` on a
Marlin machine is an error and `M900` on Klipper stops the print.

| Test | Reference parameters | Ours |
| --- | --- | --- |
| Temperature tower | filament type (sets the range), start / end / step, model | done — same ranges (PLA 230-190, ABS 260-230, PETG 250-220, PCTG, TPU, PA-CF, PET-CF, Custom), auto-scale for the nozzle |
| Pressure advance | extruder type (DDE / Bowden), method (tower / line / pattern), start / end / step, print numbers | tower: done. **Line and pattern are not built** -- see below |
| Flow ratio | pass 1 (coarse) / pass 2 (fine) / YOLO, top-surface pattern | done — coarse 90-110 %, fine 98-102 %, very fine 99-101 %; patch size |
| Max volumetric speed | start / end / step in mm³/s | done — converted per band to a feed override from the line width, layer height and base speed |
| Retraction | start / end / step in mm | done — two towers 60 mm apart, one band per length |
| VFA | start / end / step in mm/s, auto-adjust, model | done — feed override per ring; auto-adjust is the base-speed parameter |
| Cornering | model (Ringing / Fast / SCV-V2), start / end | done — `SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY` on Klipper, `M205 J` on Marlin, `M566` on RepRap |
| Input shaping: frequency | model, shaper type, X / Y frequency start and end, damping | done — `SET_INPUT_SHAPER` per band, five Klipper shaper types |
| Input shaping: damping | model, shaper type, frequency X / Y, damping start and end | done |
| First layer squish | No reference equivalent; first-layer calibration elsewhere is a single line | done — six patches, each at its own `SET_GCODE_OFFSET Z`, so the ladder is a real Z test rather than six identical patches |
| Numbers printed on the plate | always on for PA and the towers | done — the value is extruded as geometry (the app's own text object) in a row in front of the tower, so a band is identifiable in the preview and on the bed |
| Save the measured value | The reference four-step wizard ends by writing the value into a preset and re-slicing | partly — "Apply to `<setting>`" writes the measurement into the live settings (temperature, pressure advance, flow ratio, max volumetric flow, retract length), where it shows as modified in the panel and rides the next preset save. The wizard's own coarse-then-fine re-run is not automated |

**What is still missing here.** The PA-line and PA-pattern tests are a
*g-code generator*, not a plate: `CalibPressureAdvance::generate_test` writes one
layer's worth of lines with a `SET_PRESSURE_ADVANCE` between each, bypassing the
slicer's layers entirely. This app's only channel into a running print is the
engine's `CustomGCode::Info`, which injects at layer changes. A per-object band
therefore needs one layer range per value, which is the tower. Line and pattern
stay unbuilt until the app can emit G-code of its own.

Calibration counts as coverage of the reference menu; what it does not cover is
the *Calibration Guide* entry (a wiki link) and the AMS-specific flows.

## Project and scene

| Feature | Status |
| --- | --- |
| Open STL by dialog or drag-and-drop | done |
| Open OBJ, 3MF, STEP and SVG by dialog or drag-and-drop | done — every format goes through the engine's own reader (`FileLoadingLogic::read_model_from_file`), and the dialog's filter is built from its extension list, so a format the reader accepts is never missing from the dialog. A file holding several objects imports all of them (measured: an 18-object 3MF project opens as 18 objects; a 568-object one as 568) |
| Project files with several plates | done — the plates are rebuilt from the project's own records: the scene's build list names the placed objects in read order and `Metadata/model_settings.config` names the plate each belongs to, so a 14-plate, 568-object project opens as 14 plates holding 8/15/1/16/16/100/100/100/100/100/4/1/4/3 objects, which is exactly what the file says. Each plate's group is then centred on its own bed, because the file's coordinates are one shared space with the plates laid out metres apart |
| Empty bed with no model | done |
| Multiple plates (app-level, one document each) | done |
| Object position survives slicing | done — an import is centred once, then slices run with dont_arrange; before this the engine's arrange re-centred every slice and moves were discarded |
| Icon set | Material Symbols Outlined, an outline style throughout |
| Several objects on one plate | done — the document holds N objects; a slice bakes them into one STL of separate shells and the engine prints every island |
| Per-object settings | done — 136 print settings are overridable per object; they reach the slicer through the plate path (`--slice-features`: a 0.30 mm per-object layer height gives 84 layers instead of 125) |
| Save/load project (`.3mf` with settings and objects) | needs-engine — `store_3mf(const std::string&, const Domain::Project&)` and `load_3mf` exist, but building a `Domain::Project` needs a live `ProjectInteractor`; doing that beside the worker-thread `CLI::run` slice would put two engine runtimes in one process |
| Undo / redo | done — `Ctrl+Z` / `Ctrl+Shift+Z` over add, delete, duplicate, transform and visibility |
| Sequential printing | staged — settings exist, no scene arrangement |

## Printer catalog

`tools/printer-catalog/generate.py` turns reference vendor profiles into a
next-layer bundle: **67 vendors, 450 printers**, written to
`upstream/resources/presets/vendor-catalog/` (a build artifact, regenerated, not
tracked). Pick one from the Printer dropdown — the list is searchable — and the
engine resolves it like any other preset.

| Aspect | State |
| --- | --- |
| Machine geometry (bed size, height, nozzle, extruder count, limits) | done — 432 machines, sliced end to end |
| Per-vendor printer listing and search | done — catalog counts 482 printers in `--dump-presets`, `tools/check` slices one and fails on duplicate names |
| Process and filament settings | staged — borrowed from the engine's own templates, not the vendor's own |
| Vendor start/end G-code | staged — imported for 284 of 432 machines, brace expressions included; the rest use names with no engine counterpart and are dropped whole (see below) |
| Per-vendor profile UI (vendor > machine > variant tree) | staged — the flat searchable list stands in for a grouped picker |

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

## Profile converter

`tools/preset-convert/convert.py` turns reference profiles (flat JSON, `inherits`
chains) into next-layer config JSON over the engine. It is a
keymap plus value transforms, not a settings reimplementation.

| Bucket | Count | Notes |
| --- | --- | --- |
| Engine FDM settings (next-layer) | 352 surfaced | 335 regular + 17 hidden, always visible |
| Distinct setting keys in those profiles | 912 | union of every shipped profile |
| Exact-name match | 138 | same key, same meaning |
| Renamed by keymap | 117 | e.g. `wall_loops` → `perimeters`, `bottom_shell_layers` → `bottom_solid_layers` |
| Unsupported (no engine counterpart) | ~657 | AMS, belt printer, chamber control, Bambu-specific, the reference `tree_support_*` names (the engine's own organic supports are `support_tree_*` and are exposed), jerk-per-axis |

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
`Platform::AbstractRenderModule`, `IDialogManager` and `AppServices`. The
engine's 3.0 line was exactly this extraction out of wx, so a second frontend can
host them.

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

## The gap, in one place

Everything above, sorted by what is actually missing rather than by tab. Counted
2026-11-06 against the reference implementation's `MainFrame`/`calib_dlg`/`Plater`
and the engine's plater.

| Missing | Why it is missing | Cost |
| --- | --- | --- |
| Preset visibility modes (Simple / Advanced / Expert) | the engine's `ConfigItemDef` has no mode field, so every setting is always shown; 17 legacy/identity settings already sit in per-tab "Advanced" sub-tabs | a curated list per mode; the settings model would filter, the panel needs no change |
| Compare two presets side by side | the app diffs the live config against the *current* preset (`Ctrl+Shift+D`), not one preset against another | a dialog over `PresetStore`'s two resolved configs |
| Multi-extruder per-slot editing | the settings model collapses filament values to slot 0, which matches a single-tool machine; multi-tool slicing itself works | a slot switcher in the Filament tab |
| Per-filament colour picker | `gui_type: color` is edited as text | one editor case |
| Bed shape editor (drag polygon, SVG/STL background) | `bed_shape` is edited as points; the plater's editor is not exposed | a canvas over the point list |
| Rotation and scale gizmo handles, snapping | the Move tool draws axis arrows; rotate/scale stay numeric | the gizmo half of the render-module host |
| Sequential printing | the settings exist, nothing arranges objects for it | arrange with a clearance pass |
| `.3mf` project | `store_3mf`/`load_3mf` exist but need a live `ProjectInteractor` beside the worker-thread slice | the plater host, again |
| PA line and PA pattern calibration | they are a G-code generator, not a plate; the app's only channel into a print is per-layer custom G-code | an in-layer G-code writer |
| Calibration wizard: save to preset, then re-run fine | "Apply to setting" writes the live value; the re-slice step is manual | a second pass over the same test |
| Travel / retraction as legend categories, G-code colour schemes | they are lines in the toolpath, not legend rows | preview legend model |
| SVG embossing | text is built through `Biz::Emboss::text2shapes`; SVG needs a contour source | an SVG importer into the same emboss path |
| Cut with connectors and dowels | `cutAtZ` caps both halves (`slice_mesh` + `Tesselate`); no plugs | cut dialog options |
| Height-range modifier volumes | per-object layer-height bands exist; per-Z modifier volumes do not | pick + placement in the object panel |
| Smooth variable-layer-height profile editor | bands (bottom/top/height) only, no adaptive curve | profile editor over `LayerHeightProfile` |
| Fuzzy-skin *painting* | the setting exists and is exposed (Process ▸ Walls & Perimeters) | a fifth brush kind in the paint tool |
| Vendor process and filament profiles in the catalog | borrowed from the engine's own templates, not the vendor's own | the converter's keymap, per vendor |
| Vendor G-code template salvage | a template naming an unknown option is dropped whole (9 of 432 machines write no G-code) | line-level salvage |
| Cloud, network accounts, AMS/MMU, SLA | out of scope by design, stated in the UI | — |

Deliberately not on that list: the tool rail's gizmos, the bed tabs, arrange,
paint and cut *as features* -- they are built. What is missing from them is the
render-module host described below, which would carry the gizmo half.

## What "done" means here

Every `done` row was run at least once through `--smoke` (rendered offscreen),
`--dump-*` (settings tree), `--transform` (engine path) or the oracle suite.
Interactive rows (clicking, dragging, the file dialog) are implemented but were
not exercised by hand; they are called out as such in the session report.
