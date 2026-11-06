# Orca preset import

next-layer runs the PrusaSlicer engine, whose own preset bundle carries Prusa
machines and nothing else. A Voron, a RatRig or a Trident therefore does not
exist unless it is imported — and a slice without one falls back to a Prusa bed
and a generic filament, which is exactly how a 20 mm cube once went out on a
350 mm Voron at 230 °C against a 220 mm plate.

This tool imports **your own** OrcaSlicer presets:

```sh
tools/orca-import/import.py                                     # Voron, from ~/.config/OrcaSlicer
tools/orca-import/import.py --list                              # what it would take
tools/orca-import/import.py --vendor Voron --nozzle 0.4 --dry-run
tools/orca-import/import.py --out DIR --bundle-id orca-catalog
```

It resolves each of your user presets through the Orca installation you actually
run (`system/` vendor bundles plus your `user/default/` overrides), translates
the settings with `tools/orca-convert`, and writes the result into the
`orca-catalog` bundle next-layer already reads. Preset names are kept, so the
printer, process and filament you pick by name in Orca are the names that appear
in next-layer.

## What it does to a preset

- **Printers** go through `tools/orca-catalog/generate.py`, which resolves the
  machine, translates its settings, drops what this engine cannot express, and
  writes a printer model, a printer config and the settings preset for it. Your
  machine's own values — bed shape and height, G-code flavour, macros, machine
  limits — come along. Machine presets are limited to `--nozzle` (default 0.4):
  run it again with `--nozzle 0.6` for a 0.6 machine.
- **Process and filament** presets are converted key by key. Every key that
  cannot be translated is counted and reported; nothing is invented.
- Every printer/process/filament combination the import produced is then
  resolved and sliced headlessly. A preset that cannot slice is renamed to
  `.yaml.disabled` and named in the output, so the bundle only publishes presets
  that work.

## Things this engine cannot take from Orca

Reported by the tool, not silently dropped:

- **Pressure advance** as a number. The engine's filament block has a
  `pressure_advance` enum, not a value; Klipper owns PA on these machines, and
  the calibration tests write `SET_PRESSURE_ADVANCE` themselves.
- **Chamber temperature** control, AMS/flush behaviour and other
  Bambu-specific settings.
- **Shrinkage compensation** per axis, whose Orca shape (one object per
  extruder) aborts the slice in `std::length_error` when copied through.
- Zero-valued speeds, accelerations and widths: Orca writes `0` to mean "use the
  inherited value", and a literal zero reaches the time estimator and kills the
  slice.

## Requirements

`import.py` needs the app binary in the build tree, the model it slices for
verification, and the engine source for the option list and enum vocabularies —
all defaulted from the repository, so it runs from a checkout with no arguments.
