# Known issues

What is wrong, missing, or unverified in `0.1.2`, in the order it is likely to be
hit. This is the user-facing half of `docs/features.md`; that ledger is the
complete inventory, this is the part you need before you start a print.

## The engine boundary (HFW is the only slicer as of 2026-11-18)

- **HFW's config reader now accepts the dialect's spellings, including the
  support switch.** Until 2026-11-18 it rejected 15 values as "value spelling this
  engine rejects" — `support_material = everywhere`, `arc_fitting = emit_center`,
  `ironing_type = top`, every `15%` written where a plain number belongs — and the
  consequence was a print: a plate asking for supports everywhere sliced with none.
  All of them now convert, measured at 0 unconverted of 360 keys; the two gates that
  covered it (supports and arcs) pass.
- **The plate goes to HFW as a plate file, and paint is the one thing still left
  behind.** `SliceController` writes HFW's own project document and slices it with
  `hfw-cli --plate`, so per-object settings, volumes with their kinds and modifier
  settings, per-object layer heights, the timeline markers and the in-layer
  calibration strokes all reach the file as of 2026-11-20. **Painted facets do not**:
  the app's paint lives in the engine's split-triangle form and has no conversion
  into HFW's per-facet states yet, so support and seam paint still print as if the
  part were unpainted. A plate that changes tool is refused with a message rather
  than sliced on one extruder.
- **A model named on the command line is placed differently from a plate.** A plate
  file prints where the document put it, so `--plate` keeps positions. The single
  model path (`--slice`, `--selftest-slice`) has no document, so it asks HFW to
  centre the model on the bed (`--place`); that is the same placement the C++
  engine's arranger would have picked. Arrange, auto-orient and place-on-face still
  come from the C++ engine's geometry and are unaffected.
- **`print_sequence = by object` is now refused unless the profile states the
  toolhead.** HFW requires `gantry_height` (as before) and
  `extruder_clearance_radius` (new, 2026-11-20): every object must fit under the
  gantry, and two objects printed one after the other must be at least the head's
  own radius apart. A profile that does not state the radius is refused with a
  message naming the setting rather than sliced with a guess; the Orca and Prusa
  profiles both carry it. The check now also runs through the C ABI, which used to
  ignore the setting and slice the plate layer by layer.
- **`oracle/engine_capabilities_check.py --from-ledger` cannot be trusted.** It
  stops parsing at the first `###` subsection heading, so it reports 70 unapplied
  settings where the ledger holds 364 rows and the shipped list holds 175. Running
  it would silently un-hide 105 settings the engine cannot apply. Fix the parse
  before regenerating that file; nothing else reads it.

## Unverified on hardware

- **The in-app updater does not replace an AppImage.** An AppImage runs from a
  read-only mount, so the update button names the new version and points at the
  releases page instead of offering an install. Download the new AppImage, make it
  executable and put it where the old one was. The tarball and AUR installs do
  update in place.

- **Multi-tool printing has never been printed.** Per-tool filaments, per-tool
  colours and the wipe tower are implemented and in the ledger, and `tools/check`
  slices a single-tool machine only. If you have a tool changer, you are the
  first person testing that path: watch the first tool change closely.
- **One printer has printed anything.** A Voron 2.4 R2 350 on Klipper +
  Moonraker. Marlin, RepRapFirmware, PrusaLink and OctoPrint are implemented
  against their documented APIs and untested against a real machine.
- **The retraction the engine prints is the profile chain's, and a printer preset
  does not carry it.** `retract_length` lives in the print settings: putting it in the
  printer preset is ignored by the resolver, which is how the first benchy through the
  whole chain printed 2 mm at 25 mm/s 481 times — PrusaSlicer's bowden-era default,
  roughly three times what a direct drive wants. A machine created by this
  application now writes 0.5 mm / 40 mm/s / 0.2 mm lift into its process preset;
  a machine added before that needs the values in its process preset by hand.
- **Arranged objects were packed touching until `96349ea`.** `arrangeOnBed` left
  `Settings::scaled_offset` at zero, so a plate of two objects fused into one part:
  two 20 mm cubes came out as a 40 mm solid sharing a wall face. It now sets the
  3 mm the arrange gizmo and cut gizmo already use, which puts 6 mm between models.
- **Retraction speed was capped by the machine limit, not lost.** Two objects sliced
  with `retract_length: 0.8` and `retract_speed: 35` emitted `G1 E-0.80000 F1500`, and
  the first reading was that the speed never crossed the boundary. It does: the config
  the app hands over carries `retract_speed: 35`, and the engine's `e_move` clamps every
  extruder move to `machine_max_feedrate_e`, which the imported Voron printer preset
  held at 25 mm/s -- a Prusa figure. Set to 120 mm/s, the same plate emits
  `G1 E-0.80000 F2100`. Worth remembering as a class: a correct setting can still be
  inoperative because a machine limit masks it.
- **`retract_lift` was zero, so no Z hop was ever emitted.** Every travel on the
  two-cube plate ran at exactly the layer height and the only lift in the file was the
  end-of-print one. The Voron print preset now sets `retract_lift: 0.4`, and each
  layer's Z max is layer height + 0.4.
- **A brim was drawn on plates that turned it off, until HFW `b575414`.** The engine
  read `brim_width` and never `brim_type`, so a profile stating `no_brim` with the
  remembered width of 5 mm got 13 loops: layer 0 of a two-cube plate spanned
  146.7..203.3 instead of 152.2..197.8. Both the slicer and the writer's skirt margin
  now ask `PrinterConfig::adhesion_brim_loops()`, which answers `no_brim` first.
- **Every per-feature acceleration switch printed two unknown commands, until HFW
  `b575414`.** `SAVE_VELOCITY_LIMIT`/`RESTORE_VELOCITY_LIMIT` are Prusa firmware's and
  Klipper has neither, so a benchy log carried 697 of them and the acceleration was
  never actually restored. Klipper now restores with `SET_VELOCITY_LIMIT ACCEL=`,
  Marlin with `M204 S`.
- **A settings dump lost 70 settings before the engine saw them, until HFW `b06d9ce`.**
  OrcaSlicer writes `filament_purge_multiplier` as a list of `{is_percent, value}`
  objects, and the engine's flattening of a JSON dump read such an object as a bare
  token: the array swallowed the brace that closed its section, and every setting
  after it in that section disappeared with no report line. On the Voron preset that
  was `retract_lift`, `pressure_advance` and `filament_max_volumetric_speed`. A real
  dump now yields 366 settings where it yielded 296. **This, not a missing preset
  value, is why `retract_lift: 0.4` produced no Z hop** for three prints.
- **`;TYPE:` and object cancellation reach the file, since HFW `0c1d24d` and
  `e112d8e`.** `;TYPE:` used to depend on the verbatim-comment switch, which
  next-layer sends off, so the preview had nothing to colour by; it is now always
  written. Every object is defined at its own centre and bracketed by
  `EXCLUDE_OBJECT_START`/`END`, and a Klipper printer sets `exclude_object` in
  `SettingsModel::configJson()` -- the settings tree has no such entry because the
  reference slicers treat it as part of their Klipper output rather than a switch.
- **A silent setting now says so.** `PrinterConfig::audit()` reports a value a machine
  limit overrides and a value of zero that switches a feature off; each finding also
  lands in the G-code header as `; AUDIT:`. The live Voron preset reports nothing
  today, which is the state worth keeping it in.
- **A wall bead no longer carries a rectangle's worth of plastic.** Both reference
  slicers subtract the two corners a bead leaves unfilled, `h²(1 − π/4)`, which is
  10.7% of a 0.4 × 0.2 rectangle; HFW charged the full rectangle, so every perimeter
  over-extruded by that much against a profile calibrated on libslic3r. Wall beads
  now follow the references and skin and infill keep the rectangle, because those
  beads merge into their neighbours and fill the region solid (HFW `2d3f700`).
  Measured effect on the two-cube plate: filament 1041.9 mm → 988.1 mm against
  libslic3r's 911.2 mm, so the gap is 7.8% instead of 12.5% and all of it is in
  infill, not walls. Run `tools/parity.py` before and after any flow change; the
  oracle in HFW (`scripts/oracle.py`) says whether the references agree.
- **HFW deposits about a quarter more sparse infill than libslic3r for the same
  `infill_density`.** On the same plate: 10% requested, 1488 infill moves over
  9474.9 mm against libslic3r's 1019 moves over 8073.1 mm. Walls match after the fix
  above and the skin classes agree in total, so the remaining filament difference is
  this one. Not yet diagnosed.

- **The calibration suite's band commands were only ever printed by Klipper.**
  Input shaping is Klipper-only by design and refuses on other flavours rather
  than emit commands that do nothing; the Marlin-side equivalents (`M593`) are
  not implemented.
- **The PA line and pattern tests have been sliced, not printed.** Their geometry
  is now real toolpaths extruded inside the first layer (or the first four, for
  the pattern) through the engine's in-layer channel, and `tools/check` reads the
  extruding moves back out of the G-code to prove they are there — but no plate
  from the calibration suite has been through a nozzle yet, so nothing is known
  about how the lines look in practice, whether 3.5 mm of spacing is the right
  spacing, or whether the stroke-drawn value labels are legible on the bed.
- **A calibration plate saved as a project loses its in-layer geometry.** The
  polylines the PA line and pattern tests print live on the plate in memory, not
  in the project file: reopening a saved project gives the same objects and bands
  but not the generated toolpaths. Rebuilding the test from the calibration dialog
  is the way back.

## Slicing and files

- **An imported preset is the slicer's numbers, not this engine's behaviour.** The
  import translates what it can and names what it cannot, but a process preset
  written by another slicer describes another engine's options: expect the
  perimeters, infill and speeds to match and expect exotic features (scarf seams,
  interlocking, per-height speed tables) not to. Slice a test print before
  trusting an imported profile with a real part.
- **A file that is already sliced cannot be sliced again.** A `.gcode` or a
  `.gcode.3mf` opens in the toolpath preview; nothing here can recover a mesh
  from toolpaths, so there is no way to edit and re-slice one.
- **The plate thumbnail is the on-screen view.** It is captured from the viewport
  at the moment you press slice, so it shows your camera angle, not a canonical
  isometric render.

- **A project saved as `.nextproj` is ours alone.** PrusaSlicer and OrcaSlicer
  cannot read it. `File > Save project for another slicer` writes a `.3mf`
  project instead -- every plate, with the configuration, the per-object settings,
  the variable layer height, the painted facets and the timeline markers -- and
  this app reads all of that back. A *Bambu/Orca* project keeps an object's
  settings as plain metadata in `Metadata/model_settings.config` under that
  slicer's own names, and `ForeignSettings` translates 109 of them -- and their
  values -- into the settings this engine has. Opening a downloaded project never
  reports anything, and nothing is lost either: a setting this engine cannot apply
  -- because the behaviour does not exist here (skeleton and skin infill density,
  per-perimeter fan speeds, the per-height speed table, filament maps), because it
  belongs to the print rather than the object (`initial_layer_print_height`), or
  because nothing here knows the name -- is kept, listed read-only in the object
  panel, and written back into the project's `Metadata/model_settings.config` when
  the document is saved. `docs/features.md` has the measured state of both.
- **Organic supports and a variable layer height profile cannot be combined.** The
  engine refuses the pair outright (`VariableLayerHeightAndOrganicSupports`), and so
  does the slicer this dialect comes from, so a Bambu/Orca project that carries both
  will not slice until one of them is dropped. This is engine behaviour, not a
  reading of the file. **Measured 2026-11-11, and there is a second half to it:** a
  project written from a file that carried neither now comes back with a layer
  height profile the source never had (`Metadata/Slic3r_PE_layer_heights_profile.txt`,
  1-6 bands at 0.1/0.2 mm), so re-saving a Bambu project with organic supports
  produces a file this app itself refuses to slice (`slicing did not finish
  (status 7); error 13 support_material_style`). Reproduce with
  `--export-project` on `oracle/fixtures/bambu_project.py`'s fixture and reopen the
  written file. Not fixed; the writer is where the profile is invented.
- **A project this app exports makes PrusaSlicer's command line crash on
  re-export.** `prusa-slicer FILE.3mf --export-3mf` segfaults (exit 139) when the
  file carries a timeline marker that is not a tool change: a pause, a colour
  change or custom G-code. Bisected to one element -- the marker, and this is not
  this app's fault: in PrusaSlicer 2.9.6's own `src/libslic3r/Format/3mf.cpp`,
  `_add_custom_gcode_per_print_z_file_to_archive` calls `config->opt_string(...)`
  on a `DynamicPrintConfig*` the CLI export path never sets, and only the
  tool-change branch reaches its data without touching `config`. The same file
  opens fine in their GUI, where the config is passed. Nothing this app writes can
  avoid it while keeping the marker. Measured with `prusa-slicer 2.9.6+arch5`.
  This entry used to say the crash was on our side by way of a colour field holding
  text; that was fixed separately and did not change the crash.
- **CORRECTION 2026-11-09: the "half footprint shift" was a measurement error, not a
  bug.** Saving an opened Bambu project is now measured directly -- the export
  selftest opens the file with `addObject`, saves it through the same
  `SliceController::exportProject3mf` the Save dialog calls, reopens the result and
  compares each object's placed centre -- and reports **0.000 mm** for the fixture,
  so the gate `tools/check` runs now asserts it. The earlier "10 mm on a 20 mm cube"
  came from comparing a foreign project's authored positions against `--export-project`'s
  output while that flag was building its *own* two-object plate; the two files were
  never the same plate. No export wrote a wrong transform.
- **Saving a project that came from a 3MF still changes its toolpath**, and this is
  real and reproducible: `next-layer-qt FOREIGN.3mf --export-project CONFIG,OUT.3mf`
  prints `the project slices differently from the plate it came from` with identical
  layer, move and filament totals but a different digest, so the G-code differs
  without changing how much material it uses. It also reports one *more* per-object
  override on the reopened plate than the file carried, which is the first thing to
  look at. It does not reproduce when the plate was authored from an STL, which is
  why no gate saw it; feeding a `.3mf` to the export selftest now exercises it.
- **Opening a single-object 3MF now honours the file.** A 3MF is treated as a
  scene with its own placement rather than as a mesh to drop on the bed, which is
  what the importer did before; an object exported from CAD at the origin opens at
  the origin.
- **Sequential printing** is exposed as settings with no scene arrangement: the
  slicer will not stop you from arranging objects so the gantry hits them.
- **A file written by a newer build is refused**, by design, with a message
  naming both versions. There is no downgrade path.
- **A crash leaves nothing behind.** No crash reporter, no log file: stderr and
  that is all. Run from a terminal if you want to report a crash.

## Interface

- **FIXED 2026-11-17: a saved preset was invisible until the app restarted.**
  `PresetStore::saveUserPreset` wrote the file and never re-read the engine's preset
  bundle, so the copy was not in the picker, `reload` resolved the parent it inherited
  instead, and the window said "saved" while the settings jumped back to their preset
  values. `renameUserPreset` and `deleteUserPreset` had the same gap. All three now call
  `SliceRuntime::reloadPresets()`, which the import path already did. Verified through
  the window: supports off on "2.4", header "Save preset", `Save as my preset` with the
  offered name — the picker switched to "2.4 copy", the value stayed off, and a fresh
  `--dump-config-json` for that preset reports `support_material: none`.
- **FIXED 2026-11-17: the editor refused a save it could have offered.** For a preset
  that ships with the engine, `Save` answered "this preset ships with the engine … give
  it your own name" and stopped, while the route that works (`Save as`) was the
  unlabelled second button. `Save` is now hidden for a non-user preset, the copy button
  is the primary one and reads "Save as my preset", and the name field opens filled with
  a free name ("2.4 copy"). The window also opens on the category the caller was
  looking at instead of always the first, so a change made on Support is on screen
  rather than one tab away. A copy may no longer be saved under an existing preset's
  name: that writes a second preset with the same name, and the engine resolves such a
  pair to whichever it read first.
- **A machine saved with "Save as" is written but cannot be chosen.** The copy
  lands in `presets/user/…/printer-<name>.yaml` with the whole value set, and the
  window reports it saved, but the machine list the window offers is built from the
  vendor bundle's `printers.json`, which a user preset never enters — so the copy
  is not in the Machine dropdown and cannot be selected. Measured 2026-11-10:
  `--dump-presets` lists no such machine and the dropdown does not show it. The
  gate `tools/check --ui` reports it as a skip until this is fixed.
- **Scaling is about the model's own corner, not its centre.** Scaling a 20 mm cube to
  1.5 makes it 30 mm, but it grows toward +X/+Y instead of staying where it was: the
  transform scales the object in place around its own bounding-box minimum. Move, rotate
  and scale do now reach the viewport (they did not before 2026-11-10 — the model snapped
  back after a drag, which is fixed), so what is left is the pivot.
- **Typing in the transform panel re-slices nothing but rebuilds the view on every
  keystroke**, so a heavy plate will feel sticky while a number is typed. Nothing is lost;
  it is a pause.
- Painting is functional and measured (15.6 ms per dab on an 82k-facet part) but has not
  had a usability pass since: the brush radius on concave geometry, the erase path and
  painting across a hard edge are the known weak spots.
- Per-filament colour is edited as text rather than a colour picker.
- Travel and retraction are not separate filter categories in the preview's
  legend; they are lines in the toolpath.
- The G-code preview's line colours come from the palette, not from selectable
  colour schemes.
- Printer selection is one flat searchable list, not a vendor → machine →
  variant tree.
- The home screen's artwork is not drawn yet: the shipped asset is a raster on
  opaque black, and the page only shows a picture whose source is an SVG, so
  dropping one in `src/qt/assets/brand` and pointing `heroSource` at it fills the
  right half of the page.
- There is no recent-files list in the File menu, and the selected-object card in
  the viewport carries size and position but not volume or triangle count
  (right-click ▸ Statistics… has those).

## Packaging

- Linux only, x86-64 only. Windows and macOS are not built, and the engine's
  dependency tree does not cross-compile.
- The AppImage bundles its dependencies; the AUR package does not compile
  anything (`next-layer-bin`), because an AUR build of this engine is hours of
  compilation.
- The built-in updater replaces the install root. It works for an AppImage and
  would not work for a Flatpak, which is part of why there is no Flatpak yet
  (`docs/packaging.md`).

## What is deliberately not here

Telemetry, crash uploads, accounts, cloud anything. `docs/network.md` lists the
two network calls the app can make and the one that is dormant.
