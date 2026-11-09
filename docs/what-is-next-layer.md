# What next-layer is, in plain words

This page exists because "I don't understand any part of next-layer" is a fair thing
to say about a repository that grew fast. Nothing here is jargon. It is the answer to
six questions: what it is, what you do with it, where the hard parts came from, what
works today, what doesn't, and how you can tell for yourself.

## 1. What it is

A 3D printer slicer — the program that turns a model into G-code a printer can run.

It is built on top of a large open-source slicing engine, the same family of code that
PrusaSlicer and OrcaSlicer grew out of. That engine is in `upstream/` and does the
geometry: cutting the model into layers, generating perimeters, infill, supports,
seams, and writing the G-code. **We did not write that and we are not going to.**

Everything else is ours: the window, the 3D viewport, the settings panels, the G-code
preview, project files, printer connections. That is why this repository is mostly Qt
and QML and only links the engine as a library.

The one-line version: **PrusaSlicer's engine, our interface.**

## 2. What you do with it

The workflow is the same as any slicer, and every step exists today:

1. Open a model (STL, 3MF, STEP, OBJ) or a project from another slicer.
2. Pick printer, process and filament presets.
3. Move, rotate, scale, lay flat, arrange on the plate.
4. Slice, look at the result layer by layer, read the time and material estimate.
5. Send it to a printer or export G-code.

What makes this one different from a skin over an existing app: it is built for tool
changers — several extruders, a different filament in each, tool changes handled.
Manual multi-material, not colour mixing.

## 3. Where the hard parts came from

Two things people are surprised by:

- **The G-code is not ours.** If a print comes out badly because of geometry — a
  wall gap, a bad seam, weak infill — the fix is in the engine, which is a patch away
  but is real C++ work. This is the "engine surgery" you keep hearing about.
- **The settings are the engine's, with PrusaSlicer's names.** The interface shows
  the settings the engine actually understands. When a downloaded file uses a name
  from another slicer, the app translates it where the meaning matches, and keeps it
  untouched where it doesn't. It never silently drops a setting.

## 4. What works today

Verified by measurement, not by memory:

| Thing | How it is known |
| --- | --- |
| Slicing correctness | 12 of 12 oracle cases produce **byte-identical G-code** to the reference slicer's own output |
| The app's own path | 41 gates in `tools/check`, each reading a number out of its tool, not just an exit code |
| A real print | The 3D Benchy, printed on the Voron and reported as good as Orca's |
| Foreign projects | Your downloaded Bambu/Orca projects open with zero complaints; 109 settings translate |
| Viewport, preview | Rendered and measured headlessly, and driven with real mouse and keyboard |

## 5. What does not work yet

Honest list, in order of how much it matters:

| Gap | What it means in practice |
| --- | --- |
| A project saved from an *opened* 3MF shifts its objects by half their footprint | Reopening a saved project moves the models. A bug, not a preference. |
| Exports mix two file dialects | Orca reads the Bambu-style half and ignores the PrusaSlicer half, so a per-object setting can be lost on the way back out. |
| A colour-change marker crashes PrusaSlicer on re-export | Measured: PrusaSlicer 2.9.6 segfaults on the marker this app writes. Their bug, our trigger. |
| Per-perimeter fan speeds, a height-based speed limit | Bambu files use these; this engine has no concept of them. |
| Filament mapping tables for multi-material projects | Same. |
| A separate "internal solid infill" pattern | Not a gap: this engine names a pattern per surface instead, and has all 22 patterns. |

Nothing in that table stops you slicing and printing. Two of the three bugs are in
saving and reopening a project.

## 6. How to tell for yourself whether something works

- `tools/check` — every gate, including the byte-exact oracle. It does not build, and
  a binary older than its sources fails the first gate, so a green run means
  "measured against this code".
- `tools/check --ui` — adds a real window, captured and read.
- The app's own headless switches: `--selftest-slice`, `--smoke --shot`, `--dump-config`,
  `--dump-tabs`. See `src/qt/README.md`.
- The two reference slicers are installed on this machine. A file this app writes can
  be opened in OrcaSlicer and PrusaSlicer, re-exported, and inspected — which is how
  the two file-format bugs above were found.

If a claim in this repository is not backed by one of those, treat it as unverified.

## 7. The order the work should happen in

1. **File format correctness** — the three bugs in section 5. This is your actual
   daily workflow: open a downloaded project, slice, save, continue in another
   slicer.
2. **A real print of something larger than a Benchy**, to catch problems numbers
   cannot see.
3. **Engine work** — fan tables, height speed limits, filament maps — one at a time,
   each followed by the oracle.

Features that Orca has and this app does not are not urgent while the three bugs in
section 5 are unfixed.
