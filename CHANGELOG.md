# Changelog

All notable changes to next-layer. Versions are `0.1.x` until the file formats and
the settings names stop moving; see `docs/known-issues.md` for what is still
unverified.

## Unreleased

- **Project export carries the whole document.** `File > Save project for another
  slicer` writes every plate as a `.3mf` PrusaSlicer and OrcaSlicer can open --
  geometry, the resolved configuration, per-object settings, layer height bands,
  painted facets, timeline markers and plate records -- and reopening it restores
  all of it. Verified by slicing the plate and its reopened copy: 189 layers,
  12652 moves and 2212.6 mm of filament, byte-identical.
- **A 3MF now opens where its author put it.** A one-object 3MF used to be treated
  as a bare mesh and dropped onto the bed, and every object's build transform was
  applied a second time on top of the instance the engine already had, which
  cancelled the author's rotation.
- **A project from another slicer never reports a setting at you.** A hundred and nine
  Bambu/Orca names are translated into this engine's — the wall, surface, sparse and
  internal-solid-infill width, speed and acceleration families, `wall_loops`,
  `top_shell_layers`, `initial_layer_print_height`, the whole `support_*` and
  `tree_support_*` families, the fan table and the overhang speed steps, `z_hop`,
  the machine and filament start/end G-code, and both `prime_tower_*` widths — with
  their values converted where the two write the same setting differently
  (`enable_support=1` to `support_material=everywhere`, `tree(auto)` to
  `organic`, `seam_position=back` to `rear`). A name with no counterpart here and
  a name nothing here knows are both dropped silently: the status bar stays empty,
  because there is nothing the person opening a downloaded project could do about
  it. Both ends of the table are gated — every target has to exist here, every
  source has to be a name that slicer defines — so a typo fails the build instead of
  quietly losing a setting. An object also keeps the label its author gave it, and a
  project file no longer reports its own object names as a setting it failed to read.
- **A per-tool setting written as one value applies to every tool.** A project from
  another slicer writes `inner_wall_speed = 150` where this engine's setting is a
  list; it now reads as the one-element list that means, and a
  semicolon-separated list still reads as written.
- **The plate's chrome sits off the bed.** Name, pencil and the action column are
  drawn on the plate's own plane but outside its edges -- the name to the left (a
  pencil-sized target now, it was 5 px), the six actions to the right -- and the
  camera's fit grew an apron on both sides so they are framed with the plate instead
  of drifting out of the window.
- **The window's left rail is gone.** It duplicated the mode switch in the header
  and the object list in the tool strip, and cost every plate 44 px of width. Its
  two remaining actions moved where they belong: printer setup to
  `File > Printer setup…` (`Ctrl+Alt+S`), reset view to the camera strip under the
  viewport (`R`). The settings panel is 44 px wider, and a width already dragged is
  widened once by exactly what the rail took.
- **The product is spelled NextLayer** in the window title, the header, the About
  dialog and the setup flow. The G-code producer line is unchanged.
- **The resize corner is a triangle**, quiet until the pointer reaches it, instead
  of three hairlines that read as part of the content.
- Fixed: a context menu opened near the bottom of the window ran off the screen;
  the preview legend's percentage column printed over the length beside it; the
  device view was transparent, so the bed grid showed through its address bar; the
  object list had no scroll indicator, and the status toast sat under it.

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
