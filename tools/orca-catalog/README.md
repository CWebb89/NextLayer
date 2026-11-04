# Orca printer catalog

Generates a printer bundle — one preset per vendor machine — from the profiles
shipped in the OrcaSlicer checkout at `~/dev/reference/OrcaSlicer/resources/profiles`.
The output is written into `upstream/resources/presets/orca-catalog/`, which the
app already loads through `PresetStore::bundleRoots()`. Because it sits inside
`upstream/`, the bundle is a build artifact: git does not track it, and a fresh
clone regenerates it rather than inheriting a stale copy.

```sh
python3 tools/orca-catalog/generate.py                  # every vendor
python3 tools/orca-catalog/generate.py --vendors Voron  # one vendor
python3 tools/orca-catalog/generate.py --out /tmp/try   # somewhere else
```

Requires the OrcaSlicer checkout. `tools/check` fails loudly if the catalog is
missing from the build tree (it counts the printers `--dump-presets` reports and
slices one of them), so a fresh clone cannot pass while shipping 21 printers.

## What one printer preset is

A `PresetBundle` printer plus one preset process and one preset filament, because
the engine resolves a bundle as a package. Machine values come from the Orca
machine profile; process and filament values are borrowed from the PrusaResearch
templates in `templates/`, since Orca's own process settings use vocabularies the
engine does not share (see the parity ledger's converter section).

The keymap and the value-to-enum guards live in `../orca-convert/convert.py` and
are imported, not duplicated: an Orca key reaches the engine only if the engine
has that option, and a value reaches it only if the option's enum accepts it.

## Deliberate losses, and why

Every dropped key is counted in the run report. Three rules produce them:

- **No engine counterpart.** 3,792 keys across the catalog, overwhelmingly
  Bambu/AMS, belt-printer and chamber-specific settings.
- **G-code templates the engine cannot parse.** Orca G-code uses `[...]` names
  that do not exist here (`initial_no_support_extruder`, `plate_name`,
  `first_layer_print_min_0`), brace expressions (`{max_layer_z+0.5}`), or
  placeholders that map cleanly and are renamed instead
  (`bed_temperature_initial_layer_single` → `first_layer_bed_temperature`).
  A template that still references an unknown name is dropped whole —
  354 of 450 machines — because half a start sequence is worse than none.
  Those presets print nothing sensible until the G-code is set in
  Printer → Custom G-code.
- **Forced values.** `use_relative_e_distances` is always true: absolute E makes
  this build export no G-code at all, and relative E is what Klipper and current
  Marlin expect.

An empty `before_layer_gcode` also stops export, so it falls back to the engine's
own template when a vendor supplies none.
