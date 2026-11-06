# Printer catalog

Generates a printer bundle — one preset per vendor machine — from the profiles
shipped in the reference checkout at `<reference checkout>/resources/profiles`.
The output is written into `upstream/resources/presets/vendor-catalog/`, which the
app already loads through `PresetStore::bundleRoots()`. Because it sits inside
`upstream/`, the bundle is a build artifact: git does not track it, and a fresh
clone regenerates it rather than inheriting a stale copy.

```sh
python3 tools/printer-catalog/generate.py                  # every vendor
python3 tools/printer-catalog/generate.py --vendors Voron  # one vendor
python3 tools/printer-catalog/generate.py --out /tmp/try   # somewhere else
```

Requires the reference checkout. `tools/check` fails loudly if the catalog is
missing from the build tree (it counts the printers `--dump-presets` reports and
slices one of them), so a fresh clone cannot pass while shipping 21 printers. The
gate resolves presets under a throwaway `XDG_DATA_HOME`: the app prefers
`~/.local/share/next-layer/presets`, so a copy left there hides the bundle in the
build tree from both the gate and `validate.py` without a word.

## What one printer preset is

A `PresetBundle` printer plus one preset process and one preset filament, because
the engine resolves a bundle as a package. Machine values come from the reference
machine profile; process and filament values are borrowed from the engine's
templates in `templates/`, since the reference's own process settings use vocabularies the
engine does not share (see the parity ledger's converter section).

The keymap and the value-to-enum guards live in `../preset-convert/convert.py` and
are imported, not duplicated: a reference key reaches the engine only if the engine
has that option, and a value reaches it only if the option's enum accepts it.

## Deliberate losses, and why

Every dropped key is counted in the run report. Three rules produce them:

- **No engine counterpart.** 3,792 keys across the catalog, overwhelmingly
  Bambu/AMS, belt-printer and chamber-specific settings.
- **G-code templates the engine cannot parse.** Reference G-code uses `[...]` names
  that do not exist here (`initial_no_support_extruder`, `plate_name`,
  `first_layer_print_min_0`), or placeholders that map cleanly and are renamed
  instead (`bed_temperature_initial_layer_single` → `first_layer_bed_temperature`).
  A template that still references an unknown name is dropped whole —
  166 of 450 machines — because half a start sequence is worse than none.
  Those presets print nothing sensible until the G-code is set in
  Printer → Custom G-code.
- **Brace expressions are kept, but their names are checked.** `{...}` is part of
  the engine's own language, and it evaluates: a retained template writes
  `M190 S60` from `{first_layer_bed_temperature[0]}`. An identifier the engine
  cannot resolve (`{machine_max_speed_z[0]*60}`) stops the export entirely — the
  engine exits 0 and writes no G-code at all — so the same name list is applied
  inside braces and the whole template is dropped if anything is unknown.
- **Forced values.** `use_relative_e_distances` is always true: absolute E makes
  this build export no G-code at all, and relative E is what Klipper and current
  Marlin expect.

An empty `before_layer_gcode` also stops export, so it falls back to the engine's
own template when a vendor supplies none.

## Slicing state

`validate.py` resolves and slices every machine the catalog ships, and checks the
output file, its size and that the vendor's start G-code reached the output — a
preset can resolve, list and look right while the engine writes nothing.

```sh
python3 tools/printer-catalog/validate.py --jobs 6      # whole catalog, ~6 min
python3 tools/printer-catalog/validate.py --vendor Voron
python3 tools/printer-catalog/validate.py --names-only  # duplicates only, seconds
```

It resolves presets under a throwaway `XDG_DATA_HOME`, because the app prefers
`~/.local/share/next-layer/presets` and a copy left there silently shadows the
bundle in the build tree; `--data-home` points it at a real one when needed. The
one generated copy that does live there, `local/vendor-catalog`, is a symlink into
the build tree, so it cannot go stale.

It sweeps the machines this bundle declares, read from each `vendor.yaml`'s
`printer_families` labels plus the vendor's tool name, not from `--dump-presets`.
The app's list also holds the engine's built-in vendor machines, whose
print profiles have their own names; sweeping those measures the built-in bundle
and reports failures the catalog cannot fix.

Measured on the current catalog: **432 machines, 423 slice with their vendor start
G-code, 9 write no G-code, 0 fail to resolve**, and no two of the 464 hardware
configs a fresh preset home lists share a name (432 catalog machines, 26 built-in
engine presets, 6 engine example machines; the live home lists 459 — the same set
plus the `Voron 2.4 350` fixture and minus the examples). The 9 are the same class:
Anycubic Kobra Max / Plus / Vyper, Flashforge Guider 2s / Adventurer 3 /
Adventurer 4 / Artemis, UltiMaker S5 ×2 — the vendor's start G-code still
references names the engine cannot evaluate, so the export stops silently.
`tools/check` slices one catalog printer, not all of them, and fails if two configs
share a name.

Before brace support, 230 catalog presets carried a vendor start G-code; now 284
do, with the same 9 machines failing and no accepted template dropped.

Two generation bugs made earlier measurements wrong, both now fixed:

- `slugify` drops punctuation, so `UltiMaker 2` and `UltiMaker 2+` both became
  `ultimaker-2`: the second machine overwrote the first one's preset file and
  vendor entry, and the survivor was published under the other machine's name.
  Every generated id is now unique within its vendor.
- `validate.py` split the column dump on double spaces. The dump pads names to 42
  columns and many printer names are longer, so it read the vendor as part of the
  name and reported 12 machines as unresolvable that resolve fine. It reads the
  app's `--dump-presets-json` now.

