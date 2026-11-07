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

A `PresetBundle` printer plus the vendor's own process and filament presets,
because the engine resolves a bundle as a package. All three come from the
reference profiles: machine values from the machine profile, process values from
the process the machine names as its default, filaments from the vendor's generic
material presets. An earlier version of this tool borrowed this app's own standard
process for every machine, which sliced 400 printers with one generic profile
written for none of them.

The keymap and the value-to-enum guards live in `../preset-convert/convert.py` and
are imported, not duplicated: a reference key reaches the engine only if the engine
has that option, and a value reaches it only if the option's enum accepts it.

Each vendor also gets a `defaults.json` recording which process and filament each
machine resolves with. `validate.py` slices with exactly that pair, so a machine is
never checked against a neighbouring model's process.

## Deliberate losses, and why

Every drop is named in the run report, with the template and the names that caused
it. The rules produce them:

- **No engine counterpart.** 3,792 keys across the catalog, overwhelmingly
  Bambu/AMS, belt-printer and chamber-specific settings.
- **G-code templates the engine cannot parse.** Reference G-code uses setting names
  that do not exist here (`initial_no_support_extruder`, `plate_name`), or
  placeholders that map cleanly and are renamed instead
  (`bed_temperature_initial_layer_single` → `first_layer_bed_temperature`). A
  template that still references an unknown name is dropped whole, because one
  unresolvable name makes the engine write no G-code at all. Current catalog: 159
  start, 119 end, 13 layer and 90 filament-start templates dropped; 319 of 428
  machines lose at least one of them and still slice, with the engine's own
  template in its place.
- **Brace expressions are kept, and their names are repaired.** `{...}` is part of
  the engine's own language and it evaluates. Three repairs are mechanical rather
  than invented: a renamed setting is renamed inside braces too, a bare reference
  to a per-tool setting gets `[0]` (the engine rejects an unindexed vector and the
  first tool is the only one these profiles mean), and a quoted literal
  (`{if filament_type[0] == "PLA"}`) is not mistaken for a setting name.
- **Percentages stay percentages.** A reference width of `110%` is 110% of the
  nozzle, not 110 mm; flattening it made the engine reject whole machines as
  unprintable.
- **Forced values.** `use_relative_e_distances` follows the flavour: absolute E
  writes no G-code on a Klipper machine and relative E writes none on a Marlin one.
  `single_extruder_multi_material` is always false, because in this engine it turns
  MMU printing on and an MMU slice with one tool exports nothing.

`G92 E0` is aligned with the addressing mode in both directions: it is removed
from `before_layer_gcode` under absolute E and added to `layer_gcode` under
relative E, matching the engine's own rule (`G92 E0.0` and a trailing comment are
resets too).

## Slicing state

`validate.py` resolves and slices every machine the catalog ships, and checks the
output file, its size and that the vendor's start G-code reached the output — a
preset can resolve, list and look right while the engine writes nothing.

```sh
python3 tools/printer-catalog/validate.py           # whole catalog, ~30 min serial
python3 tools/printer-catalog/validate.py --vendor Voron
python3 tools/printer-catalog/validate.py --names-only  # duplicates only, seconds
```

`--jobs` defaults to 1 and each slice peaks near 1.7 GB, so a parallel sweep on
this machine is what fills RAM; run it inside a capped scope:

```sh
setsid nohup systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=2G \
    -- python3 -u tools/printer-catalog/validate.py > /tmp/val.log 2>&1 &
```

It resolves presets under a throwaway `XDG_DATA_HOME`, because the app prefers
`~/.local/share/next-layer/presets`; `--data-home` points it at a real one when
needed. The copy the app keeps there is a real copy, refreshed on start whenever
the shipped bundle's index is newer, so it cannot shadow a regenerated bundle.

It sweeps the machines this bundle declares, read from each `vendor.yaml`'s
`printer_families` labels plus the vendor's tool name, not from `--dump-presets`.
The app's list also holds the engine's built-in vendor machines, whose
print profiles have their own names; sweeping those measures the built-in bundle
and reports failures the catalog cannot fix.

Measured on the current catalog: **428 machines, 290 processes, 319 filaments,
428/428 slice, 0 failures, 0 duplicate names** across the 460 hardware configs an
isolated preset home lists. One machine declares no start G-code at all, so there
is no marker to look for in its output. 30 machines have no convertible vendor
process and are published with this app's standard profile instead; the report
names each of them.

The reference's `Custom` vendor is deliberately not published: it holds printer
templates (`MyKlipper`, `MyMarlin`) meant for a user to copy, and next-layer
already owns that bundle name, so a machine published there can never resolve.

Two generation bugs made earlier measurements wrong, both now fixed:

- `slugify` drops punctuation, so `UltiMaker 2` and `UltiMaker 2+` both became
  `ultimaker-2`: the second machine overwrote the first one's preset file and
  vendor entry, and the survivor was published under the other machine's name.
  Every generated id is now unique within its vendor.
- `validate.py` split the column dump on double spaces. The dump pads names to 42
  columns and many printer names are longer, so it read the vendor as part of the
  name and reported 12 machines as unresolvable that resolve fine. It reads the
  app's `--dump-presets-json` now.

