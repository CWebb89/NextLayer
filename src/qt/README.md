# src/qt

next-layer's application shell: Qt Quick + Qt Quick 3D over the slicing engine,
linked as a library. Nothing here modifies engine code; the engine builds this
directory from outside its own tree, through `patches/` and `tools/bootstrap`.
`src/cli` is the headless slicing harness beside it.

Printer coordinates are Z-up and authoritative. Exactly one node in
`qml/views/Viewport.qml` (`printSpace`) rotates into Qt Quick 3D's Y-up world.
Mesh data, camera maths, bed, and toolpaths all stay in printer
coordinates.

## Build and run

```sh
cd upstream && cmake --build build --target next-layer-qt -j4
build/src/next-layer-qt/next-layer-qt          # empty bed, ready for a drop
build/src/next-layer-qt/next-layer-qt ../../../oracle/models/cube_20.stl
```

`~/.local/bin/next-layer` wraps the build-tree binary so the app starts from any
directory with any arguments. The QML entry point is resolved in this order:
`--qml PATH`, then `<exe dir>/qml/main.qml`, then the source directory baked in
at configure time (`NEXT_LAYER_QML_DIR`), which is what makes hot QML editing
work without a rebuild.

Running with no model opens the empty bed; dropping an STL on the window or
pressing Ctrl+O loads one, and `SliceController` is re-pointed at it, so the
preview clears and the Slice button enables.

Headless render check (the only platform that gives real GL headlessly):

```sh
QT_QPA_PLATFORM=eglfs build/src/next-layer-qt/next-layer-qt \
    ../../../oracle/models/bridge_30.stl --smoke --shot /tmp/frame.png
```

Slicing parity, no window (`oracle.py verify --runner qt` drives this):

```sh
build/src/next-layer-qt/next-layer-qt MODEL --slice CONFIG.json,OUT.gcode
```

The remaining options exist to make the app's own pipeline checkable without a
mouse:

| option | what it proves |
| --- | --- |
| `--dump-config FILE` | the settings panel's `configJson()` round-trips the engine config |
| `--dump-tabs` | the tab tree and per-sub-tab setting counts (335 settings, 3 tabs, 27 sub-tabs) |
| `--dump-subtab TAB/SUBTAB` | every setting in one sub-tab with its type and value, e.g. `process/104` |
| `--dump-plates` | the plate count and how many objects each plate holds after loading — what proves a project file's plates were rebuilt |
| `--dump-objects` | every loaded object's name, facet count, plate and world position, in plate order |
| `--dump-applied` | every loaded object's applied overrides and their values — what a project file's records, and another slicer's settings, became here |
| `--dump-foreign-settings` | every setting renamed from another slicer's dialect, with whether this engine has the name it translates to — the table `oracle/foreign_settings_check.py` gates |
| `--settings-tab TAB/SUBTAB` | opens the panel on that sub-tab, so a `--smoke` shot proves it renders |
| `--panel-left` | docks the settings panel on the left for the same shot |
| `--transform SPEC,OUT` | runs the engine transform path and writes a transformed STL: `rotate=x,y,z;scale=f;cut=z;duplicate=n;move=x,y;center;drop` |
| `--selftest-slice` | the worker-thread slice path runs and its G-code parses back; it slices through the app's own engine runtime (`SliceRuntime`), not a second engine |
| `--keep-gcode PATH` | with `--selftest-slice`, copies the G-code to PATH so two slice paths can be compared byte for byte |
| `--export-project CONFIG,OUT` | writes the document as a project 3MF another slicer reads, reopens it through the app's own 3MF path and slices it: fails unless both toolpaths hash the same. What it reports — overrides, layer height bands, painted facets, plates — is what the gate reads |
| `--preview FILE` | a G-code file loads into the toolpath preview before QML starts |
| `--preview-stats GCODE,OUT` | the engine's G-code processor reads a file and writes per-line-type statistics as JSON |
| `--layer N` | opens the preview at layer N, for a shot of a specific layer |
| `--layer-from N` | opens the preview with layer N as the first visible layer |
| `--moves FROM,TO` | limits the current layer to moves FROM,TO (1-based) |
| `--camera-view NAME` | opens with a camera preset: `iso`, `top`, `bottom`, `front`, `rear`, `left`, `right` |
| `--ortho` | opens with the orthographic camera |
| `--slider-test` | drags both preview layer sliders with synthetic pointer events and reports the layer after each step |
| `--single-layer` | previews only that layer instead of the part up to that layer |
| `--view YAW,PITCH,ZOOM` | camera pose for a shot, so a close-up of the geometry is reproducible |
| `--hide-feature NAMES` | hides line types by legend name, proving the preview filter rebuilds the geometry |
| `--view-type NAME` | preview colouring mode by name (`Feature type`, `Speed`, `Flow`, `Height`, `Layer time`, `Tool`, `Filament`, ...), so a shot proves the range legend and colours |
| `--hide-option NAMES` | hides preview options by name (`Travel`, `Wipe`, `Retract`, `Seam`, ...), proving the option layer toggles |
| `--plates N` | starts with N build plates, exercising the plate add/switch path |
| `--input-test` | feeds synthetic mouse events through the window and prints the orbit/pan/zoom/drag-move results, so camera and picking changes are testable headlessly |
| `--printer NAME` / `--process NAME` / `--filament NAME` | resolves a full config from the engine's presets and seeds the panel with it |
| `--dump-presets` | lists every printer profile, and with `--printer` that printer's process and filament profiles |
| `--dump-presets-json` | with `--dump-presets`, prints the same list as JSON on stdout, so a tool gets exact names instead of 42-column output it has to guess its way through |
| `--set NAME=VALUE` | sets a setting before the preset actions below run (repeatable), so a change can be made without a mouse |
| `--add-printer NAME,WIDTH,DEPTH,HEIGHT` | writes a custom printer vendor bundle from the current settings, then resolves and selects it |
| `--save-preset KIND,NAME` | saves the tab's changed values as a user preset (`kind` is `print` or `filament`) and selects it |
| `--rename-preset KIND,OLD,NEW` / `--delete-preset KIND,NAME` | renames or removes a user preset |
| `--preset-dialog process\|filament\|printer` | opens the preset editor on that tab before the frame is captured, so its render is provable headlessly |

## Presets

Two preset kinds exist on top of the engine's own bundle, and both are plain
files the engine enumerates on its next evaluation — no in-memory state has to
survive.

**User presets** live in `<data>/presets/user/<repo>/<vendor>/<kind>-<name>.yaml`
and inherit the currently selected preset of their kind. Inheritance in this
engine is by **id, not name**: the vendor's own preset ids are opaque strings
(`*common COREONE*`, a UUID), so `PresetStore` reads the `root_id` of the
selected preset from the resolved config's root-level `preset` block and writes
that. Only the values changed in the panel are written (`SettingsModel::valuesYaml`).
Rename rewrites the `name:` and `id:` lines, delete removes the file.

**Custom printers** are a whole vendor bundle in
`<data>/presets/local/next-layer-custom/Custom/`: a `vendor.yaml` holding the
vendor document, one `kind: printer` document and one `kind: printer_config`
document per machine (plus a `kind: tool` and a `kind: sheet`), a
`preset-printer-<slug>.yaml` carrying the complete printer settings, and
`preset-print-standard.yaml`, `preset-tool-standard.yaml`,
`preset-filament-generic.yaml` written from the settings. `printers.json`
is that bundle's own manifest (the loader reads only `*.yaml`, so a JSON file is
invisible to it) and every `vendor.yaml` is regenerated from the manifest.
Adding a machine that already exists refreshes its bundle instead of failing, so
the shared process/filament/tool presets can be rewritten from new settings.
A rewritten preset keeps its `id:` — another preset may inherit it, and the
engine asserts on an id it cannot resolve.

The printer preset must be conditioned on `printer.model == "NL_<slug>_MODEL"`:
a printer preset with no condition applies to *every* hardware config of its
vendor, which produces one picker entry per machine with the wrong bed. The
tool print preset needs no values at all — the engine only requires one to exist
— and giving it any at all puts a partial `toolprint_settings` block in the
resolved config, which makes `--slice` export nothing.

`printer_model` in the resolved config is not a preset value: it is
`hw_config.legacy_printer_model` or the printer id, so the bundle sets
`legacy_printer_model: ['<printer name>']` and the app's plate lookup finds the
right asset by name.

Saving a preset merges into whatever the file already holds, so a second save
adds its changes instead of replacing the first batch. A save keeps the parent
the file already declares: the resolved config reports the selected preset's own
id once a user preset is selected, and writing that as the parent makes the
engine inherit a preset from itself.

## Preset editor

The gear beside each preset dropdown opens `views/PresetEditorDialog.qml`, the
counterpart of the engine's parameters dialog: the whole resolved configuration of
that preset kind in one window, with its own toolbar. Print / Filament / Printer tabs,
a preset combo, revert-everything, Save, Save as, Rename, Delete (two clicks:
the first turns the button into a confirmation), an Add printer form on the
printer tab, and a search box that filters across every tab. Edits apply to the
live settings at once; Save writes the changed values into the preset file and
reloads, which clears the modified markers.

Save on an engine preset is refused with a reason instead of silently saving a
copy. The printer tab cannot save a preset: a second printer
preset on one hardware config shows up as a duplicate profile name, so a new
machine goes through Add printer.

`views/SettingsTree.qml` renders the settings for both the docked panel and the
dialog, so a setting looks and behaves the same in both. Each row's editor is
`components/SettingEditor.qml`, which dispatches on the definition's own
`type` and `gui_type` the way `OptionsGroup::build_field` does:

| engine definition | editor |
| --- | --- |
| Boolean, closed Enum | toggle, dropdown |
| open enums (`i_/f_/s_enum_open`) | free text plus the choice list |
| Int, Float, Percentage | number field with the definition's range and units |
| FloatOrPercentage (`unit_or_percentage`) | number field plus an mm/% switch |
| `Vec2d` | X and Y fields |
| `points` (extruder offsets) | point-list dialog |
| `bed_shape` | bed shape dialog: rectangle helper or an explicit polygon |
| `color` | swatch plus hex entry, opening a colour dialog |
| `file_picker` | path field with a file dialog |
| `substitutions` | G-code substitution rows (search, replace, params, notes) |
| `ramming_params` | free-text dialog, the value is the engine's own string |
| string lists (`textfields`, `Strings`) | comma field plus a one-value-per-line dialog |
| multi-line strings | text area, edited in place |

A nested `QVariantList` (a point list) reaches QML as an array-like object whose
elements are not plain arrays, so point settings are read through
`SettingsModel::points()`, which returns a flat `[x0, y0, x1, y1, …]` list.
Editing `bed_shape` or `max_print_height` moves the plate immediately: `main.cpp`
re-reads both from the settings model on every change.

## Interaction model

Camera: left-drag orbits, right- or middle-drag pans, the wheel zooms, `R` resets.
Objects: click selects, left-drag on the model moves it on a plane at the grabbed
height and commits through `transform.align_xy` on release. Hits come from
`MeshGeometry::raycast`, not `View3D.pick()`, which returns nothing here.

Positioning is the app's job: slicing runs with `dont_arrange` set, otherwise the
engine's arrange step would re-centre the object on every slice and discard the
user's move. A fresh import is centred and dropped to Z 0 once by
`Session::openTransformed`, which writes that mesh to a scratch STL and slices
that file.

## QML ↔ C++ contract

`main.cpp` registers these QML singletons and context properties. Names are
load-bearing; views bind to them directly.

### Singletons (import NextLayer 1.0)

| name | type | members |
| --- | --- | --- |
| `Host` | QObject | `palette: var` (36 MD3 roles), `paletteSource: string`, `modelPath: string` (writable, `modelPathChanged`), `bedShape: var` (`[[x,y],…]`), `maxPrintHeight: real`, `baseConfigPath: string`, `focusWorld: var` (`[x,y,z]` camera focus in Qt Quick 3D world space, `framingChanged`), `modelRadius: real` |

### Context properties

| name | type | purpose |
| --- | --- | --- |
| `meshGeometry` | QQuick3DGeometry | loaded STL in millimetres, printer coordinates, minimum Z on the bed plane and XY centre moved onto the bed centre; `boundsMin`/`boundsMax` in the same space |
| `bedPlateGeometry` | QQuick3DGeometry | the printer's `bed_model` plate mesh, centred on the bed with its top face on z = 0; `bedPlateLoaded` says whether the printer has one |
| `bedGridGeometry` | QQuick3DGeometry | generated bed grid plus outline in printer coordinates, drawn above the plate top and below its underside |
| `bedVignetteUrl` | string | baked radial vignette for the plate top |
| `toolpath` | Toolpath | G-code model: `layerCount`, `currentLayer`, `firstLayer`, `layerMoveCount`, `firstMove`, `lastMove`, `moveCount`, `layerZ`, `loaded`, `featureLegend`, `geometry` (QQuick3DGeometry) |
| `session` | Session | the open document: `hasModel`, `path`, `error`, `displayName()`, `open(path)` |
| `settingsModel` | SettingsModel | settings definitions bound to the engine's config defs |
| `sliceController` | SliceController | runs the engine, exposes progress and results |

### SettingsModel

The tabs are the engine's taxonomy: `FDMConfigLocation` picks the tab
(Print → Process, Filament → Filament, Printer → Printer), `Category` is the
sub-tab and `OptionGroup` the section inside it.

- `Q_INVOKABLE var tabList()` → `[{key, title, count, modified, subtabs: [{key, title, count, modified}]}]`
- `Q_INVOKABLE var groups(string tab, string subtab, string filter)` → `[{key, title, tab, tabTitle, subtab, subtabTitle, items: [item]}]`;
  empty `tab` searches every tab, empty `subtab` every sub-tab of that tab
- item: `{name, label, fullLabel, tooltip, type, guiType, value, min, max, units, choices: [{value, label}], modified, percent, multiline, fullWidth}`
  - `type` is one of `Float`, `Int`, `Boolean`, `Enum`, `Percentage`, `String`, `Strings`, `FloatOrPercentage`
  - `value` is a list for vector-valued settings (`Vec2d`, `vector<double>`, …)
  - `guiType` carries the engine's hint for specialised editors (`color`, `file`, `bedShape`, `points`, `substitutions`, `ramming_params`, `unitOrPercentage`), which is what `SettingEditor` dispatches on
- `Q_INVOKABLE var value(string name)`
- `Q_INVOKABLE var points(string name)` — a point-list setting flattened to
  `[x0, y0, x1, y1, …]`, because a nested list arrives in QML as an array-like
  object rather than a JS array
- `Q_INVOKABLE void setValue(string name, var value)`
- `Q_INVOKABLE void reset(string name)`
- `Q_INVOKABLE void resetSubtab(string tab, string subtab)`
- `Q_INVOKABLE int modifiedCount(string tab)` (empty string = every tab)
- `Q_INVOKABLE string tabDump()` — the `--dump-tabs` text
- `Q_INVOKABLE string configJson()` → full `--load` config (template `assets/base-config.json`
  with print/filament/printer blocks replaced by current values; filament values
  are written back as the engine's per-slot arrays)
- `Q_INVOKABLE string valuesYaml(string tab, bool changedOnly)` — the `values:`
  body of an engine preset, typed the way preset values are stored (percentages
  carry `%`, points are `x xy`, vectors are lists, multi-line strings use the
  double-quoted style)
- `Q_INVOKABLE void clearModified()` — the saved values become the new baseline
- signal `changed()`

### PresetStore

Drives the engine's own bundle through `Slic3r::App::CLI::run` — every query and
every resolution is a real engine run, never a reimplementation of the vendor
YAML. One worker thread at a time; the engine is never driven from two.

- `printers`, `printProfiles`, `filamentProfiles`, `currentPrinter`,
  `currentPrint`, `currentFilament`, `busy`, `error`
- `Q_INVOKABLE void refreshPrinters()`, `selectPrinter(name)`, `resolve(printer, print, filament)`
- `Q_INVOKABLE void reload(printer, print, filament)` — one run: list printers, list the
  printer's profiles, resolve the triple, then emit; `print`/`filament` fall back to
  the first profile and a printer name matches by prefix (the engine appends the
  nozzle to the hardware config name)
- `Q_INVOKABLE string saveUserPreset(kind, name, tab)`,
  `renameUserPreset(kind, oldName, newName)`, `deleteUserPreset(kind, name)`,
  `bool presetIsUser(kind, name)`
- `Q_INVOKABLE string createPrinter(name, bedWidth, bedHeight, bedDepth)`
- every mutating call returns `""` on success or the reason it refused

### TransformController

- `Q_INVOKABLE void rotate(real x, real y, real z)`, `scale(real factor)`,
  `moveTo(real x, real y)`, `center()`, `dropToBed()`, `cut(real z)`,
  `duplicate(int count)` — each runs the engine on a worker thread, exports STL,
  and hands the path back through `applied(path, message)`; `failed(message)`
  carries the engine's refusal
- `void setModelPath(string path)` — re-pointed by `Session::modelChanged`
- `readonly bool busy`

### SliceController

- `Q_INVOKABLE void slice()` — slices the current model with the current settings;
  `setModelPath()` re-points it when a different model is opened, and slicing
  with no model reports `failed("no model is open")` instead of running
- `readonly bool busy`, `readonly string status`, `readonly string gcodePath`,
  `readonly real elapsedMs`, `readonly int layerCount`, `readonly int moveCount`
- signals `finished()`, `failed(string message)`

### Session

- `Q_INVOKABLE bool open(string path)` — loads an STL, replaces the viewport
  geometry, re-frames the camera and re-points `sliceController`; returns false
  and sets `error` when the mesh will not load
- `Q_INVOKABLE bool openTransformed(string path, string label)` — same, with the
  document shown as `label` (the post-transform STL lives in a temp directory)
- `Q_INVOKABLE void clear()` — back to the empty bed
- `Q_INVOKABLE string displayName()`
- `readonly bool hasModel`, `readonly string path`, `readonly string error`
- signal `modelChanged()`

### Toolpath

- `Q_INVOKABLE bool load(string gcodePath)`
- `Q_INVOKABLE void setLayer(int index)` — renders every layer up to `index`
- `Q_INVOKABLE void setFeatureVisible(string name, bool visible)` — rebuilds the
  geometry without the hidden line type
- `Q_INVOKABLE void clear()`
- `readonly int layerCount`, `readonly int currentLayer`, `readonly int moveCount`,
  `readonly real layerZ`, `readonly bool loaded`
- `readonly var featureLegend` → `[{name, color, moves}]`, colour as `"#rrggbb"`
- `readonly ToolpathGeometry* geometry` — the `Model`'s geometry source
- signal `changed()`

## QML layout

- `views/SettingsPanel.qml` — Process / Filament / Printer tabs, a horizontal
  strip of the engine's option groups, then two-column rows (label left, editor
  in a fixed right column). Cross-tab search, per-group and per-tab reset. The
  tab tree comes from `settingsModel.tabList()`; nothing here hardcodes a setting.
- `views/ViewportToolbar.qml` — the single strip over the viewport: plate tabs,
  the object actions, the manipulation tools, then the
  "Slice plate" and "Export G-code" pills. Icons are Material Symbols through
  `components/Icon.qml`. `main.qml` owns the plate list (`plates`,
  `activePlate`), the tool list (`tools`) and the action list
  (`objectActions`); the strip only renders and reports clicks.
- `views/TransformPanel.qml` — numeric panel for whichever tool needs a value
  (rotate, scale, move, cut, duplicate). Engine-only tools are never opened: they
  are disabled with the reason in their tooltip.
- `views/Viewport.qml` — camera (left-drag orbit, right/middle-drag pan, wheel
  zoom, `R` reset), selection, drag-to-move on the bed plane, measure overlay.
- `views/PreviewLegend.qml` — the slice is drawn as extruded ribbons (quads,
  0.44 mm wide, flat-shaded) for every layer up to the slider; clicking a row
  hides that line type through `toolpath.setFeatureVisible()`. Colours are the
  fixed palette in `src/FeatureColors.hpp`, not theme roles.
- `views/Header.qml` — mode switch and the settings side toggle; the choice is
  persisted through QtCore `Settings` in `main.qml`, so it survives restarts.
- Plates are an application concept: each plate holds a model path, a display
  label and its own G-code, and switching plates swaps the session and the
  toolpath. They are not engine `Domain::Bed` plates — that needs a plater API
  the CLI does not expose (see `docs/features.md`).
- `docs/features.md` (repo root) is the feature ledger this UI is measured
  against.

## Assets
- `assets/base-config.json` — a complete `--load` config captured from the stock
  engine with `--save` (copy of `oracle/configs/base.json`). Our app only
  replaces `configuration.print_settings`; printer and filament blocks stay as
  captured. Regenerate with `--save` if the engine's config shape changes.
- `src/FeatureColors.hpp` — the single mapping from `;TYPE:` feature name to
  palette role. Both `Toolpath` and the QML legend use it, so colours cannot
  drift.
- Bed artwork is not ours, and neither is the plate mesh. `Host.bedTextureUrl`
  is the printer's `bed_texture` (its logo, and for some printers its own grid)
  and `bedPlateGeometry` is its `bed_model` STL, both rasterised/loaded once.
  Voron machines are not in the engine's vendor manifest, so their assets live in
  `assets/bed/` and are copied from the profiles the machine settings also come
  from. `main.cpp` maps printer model to asset, so adding a machine means one
  entry in `bedAssetsForModel`.
- The plate mesh is centred on the bed with its top face on z = 0 and drawn
  0.03 mm down (the engine's `GROUND_Z`) so a model resting on z = 0 cannot z-fight it.
  `cullMode: NoCulling` keeps the underside visible, which is what makes the bed
  read as an object when you orbit below it.
- The grid is generated (`src/BedGrid.cpp`), not textured: 10 mm lines with every
  fifth heavier, anchored on the print origin and clipped to the `bed_shape`
  polygon, drawn above the plate top and again under its underside. Qt Quick 3D
  has no line width, so the lines are thin quads sized to land on roughly one
  pixel at the zoom where the whole plate is in view.
- The radial vignette behind the artwork is baked once to
  `<data>/cache/vignette.png` (0.365 centre to 0.235 corners, the engine's
  `printbed` shader constants; Qt Quick 3D has no radial-gradient material) and
  multiplied by `viewportPlateSide`. Delete the cache file to regenerate.
- The axis triad sits at the print origin, projected from printer (0,0,0) through
  `View3D.mapFrom3DScene` every frame, and hides when the origin leaves the
  viewport.
- Viewport tones (`viewportBackground`, `viewportPlate`, `viewportPlateSide` in
  `theme/Colors.qml`) reproduce a three-tone separation — background 0.329,
  plate 0.2666, plate side 0.133 luminance — on the active matugen hue. The
  lights are two, ported from the engine's `gouraud` shader:
  key at eye-space `(-0.4575, 0.4575, 0.7625)` at 0.48, fill at
  `(0.6985, 0.1397, 0.6985)` at 0.18, ambient 0.3. No shadow map and no SSAO:
  the engine ships both off by default, and the shadow pass was our largest
  per-frame cost.
- The vertical layer slider is `components/LayerSlider.qml` (right edge of the
  viewport) and the move slider is `components/MovesSlider.qml` (bottom bar).
  Both are single-mouse-area controls whose pressed handle is the nearer of the
  two, and both track the pointer by *delta* from the press point, so a layout
  change mid-drag cannot make a step skip a layer.
- The move slider addresses libvgcode's visible range, which counts the
  renderer's vertices — one per G-code move. `Toolpath` builds that per-layer
  vertex table while loading, from the engine's own `MoveVertices`, because only
  there do the parsed file and the render input meet.
- Camera presets and the orthographic toggle live in a card at the viewport's
  bottom right (`Viewport.setView`, `Viewport.orthographic`, `Viewport.fitPlate`),
  with keys 1–7, `O`, `F`, and `R`/`Home` to reset.

## Palette

`Palette` resolves the 36-key matugen MD3 contract in this order:
`$NEXT_LAYER_COLORS`, `~/.cache/matugen/next-layer-colors.json`,
`~/.cache/matugen/klipshell-colors.json`, then the compiled-in defaults in
`qml/theme/Colors.qml`. Registering a proper matugen target means editing
`~/.config/matugen`, so for now the app borrows the shell's rendered palette.
