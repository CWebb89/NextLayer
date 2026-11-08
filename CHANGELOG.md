# Changelog

All notable changes to next-layer. Versions are `0.1.x` until the file formats and
the settings names stop moving; see `docs/known-issues.md` for what is still
unverified.

## Unreleased

- **Project export for another slicer.** `File > Save project for another
  slicer` writes the active plate as a `.3mf` that PrusaSlicer and OrcaSlicer can
  open: geometry, per-object settings, layer height profile, paint, the resolved
  configuration and plate records, verified by reopening it in this app and
  slicing the same toolpath (125 layers, 6362 moves, 1100.3 mm of filament).
- **A 3MF now opens where its author put it.** A one-object 3MF used to be treated
  as a bare mesh and dropped onto the bed, and every object's build transform was
  applied a second time on top of the instance the engine already had, which
  cancelled the author's rotation.

## 0.1.0 — 2026-11-08

First public alpha: it slices, it prints, and one finished print is recorded in
`docs/real-prints.md`.

Slicing
- Plates: several plates per document, each with its own objects, and slicing and
  export per plate.
- Objects: move, rotate, scale, mirror, lay flat, place on face, cut, split, and
  hollow, plus modifier and negative volumes with their own settings.
- Painting: support, seam and tool painting with a radius brush.
- Per-object overrides, variable layer height, layer markers.

Settings
- The engine's full settings model, tabbed and searchable: 3 tabs, 300+ settings.
- Preset import from another slicer's configuration directory, translating its
  setting names and counting what has no equivalent instead of guessing.
- A custom printer bundle written from the app, and a filament library of 29
  materials installed into each machine.

Preview and device
- Its own G-code preview with a layer slider, feature legend, travel and
  retraction toggles, and per-layer estimates.
- Send to Moonraker, OctoPrint, PrusaLink or Duet/RepRapFirmware; host
  offsets applied to the G-code on the way.

Calibration
- Ten tests generated as geometry: temperature, flow, retraction, pressure
  advance, first layer, input shaping, overhang, tolerance and bridging.

Application
- Qt Quick 3D viewport in printer coordinates, with its own camera, bed rendering
  and model picking.
- Bundled icon font, so the interface looks the same on a machine that has never
  installed Material Symbols.
- A first-launch alpha notice, dismissed once and remembered.

Packaging
- A self-contained AppImage, a relocatable tarball, and an Arch `-bin` package.
- An in-app update check that only runs when you press the button.

Verification
- 36 gates in `tools/check`, 12/12 slicing oracle cases on both binaries.
- One recorded print on hardware, with the G-code hash and the printer's own job
  record.
