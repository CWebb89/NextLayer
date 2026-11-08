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
  slicer's own names; what this engine has a definition for is restored, the rest
  is named in the status bar and left out. The names it cannot map are the ones
  that mean the same thing under a different name -- `sparse_infill_density` for
  `fill_density`, `wall_loops` for `perimeters`, `enable_support` for
  `support_material` -- and that table is not written, because guessing it would
  change prints silently. `docs/features.md` has the measured state of both.
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
