# Known issues

What is wrong, missing, or unverified in `0.1.0`, in the order it is likely to be
hit. This is the user-facing half of `docs/features.md`; that ledger is the
complete inventory, this is the part you need before you start a print.

## Unverified on hardware

- **Multi-tool printing has never been printed.** Per-tool filaments, per-tool
  colours and the wipe tower are implemented and in the ledger, and `tools/check`
  slices a single-tool machine only. If you have a tool changer, you are the
  first person testing that path: watch the first tool change closely.
- **One printer has printed anything.** A Voron 2.4 R2 350 on Klipper +
  Moonraker. Marlin, RepRapFirmware, PrusaLink and OctoPrint are implemented
  against their documented APIs and untested against a real machine.
- **The calibration suite's band commands were only ever printed by Klipper.**
  Input shaping is Klipper-only by design and refuses on other flavours rather
  than emit commands that do nothing; the Marlin-side equivalents (`M593`) are
  not implemented.

## Slicing and files

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
  reading of the file.
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

- Per-filament colour is edited as text rather than a colour picker.
- Travel and retraction are not separate filter categories in the preview's
  legend; they are lines in the toolpath.
- The G-code preview's line colours come from the palette, not from selectable
  colour schemes.
- Printer selection is one flat searchable list, not a vendor → machine →
  variant tree.

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
