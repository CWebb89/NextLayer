# Real prints

A slicer is not verified by a diff of its G-code against itself. This page is the
record of models next-layer actually printed on real hardware, with the evidence
left where anyone can check it: the file on the printer, its hash, and the
Moonraker job record that says it finished.

Anything here was sliced by this engine, uploaded from the app, and printed
without editing the G-code by hand.

## What is verified, at a glance

The table is the whole of it. An empty cell is a machine nobody has printed on
with next-layer, which is most of them — the detailed entries below say what was
proven and what the print itself got wrong.

| Printer | Firmware | Tool count | Slicer version | Tester | Date | Result |
|---|---|---|---|---|---|---|
| Voron 2.4 R2 350 | Klipper + Moonraker | 1 | 0.1.0 | Curtis | 2026-11-06 | printed and measured, sliced on the default profile (see below) |

A row goes in here only after a print finished and was looked at. A print that
failed goes in too, with what failed — a table of successes alone is a table of
things nobody has tried.

## 2026-11-06 — 20 mm calibration cube, Voron 2.4 350

The first end-to-end print: model opened in next-layer, sliced in next-layer,
sent to the printer from the Device tab, printed to completion.

| | |
|---|---|
| Printer | Voron 2.4 R2 350 mm, Klipper + Moonraker at `10.0.0.22` |
| Nozzle | 0.4 mm |
| Model | `oracle/models/cube_20.stl`, 20 × 20 × 20 mm |
| G-code | `voron-cube20-next-layer.gcode`, 214 407 bytes |
| SHA-256 | `011024b74ce782ce2e9213de14bb45fbb4718d2122fc5524363a2176a980b7a4` |
| Slicer header | the engine's version string (attribution in `THIRD-PARTY.md`) |
| Layer height | 0.20 mm |
| Temperatures | nozzle 230 °C, bed 60 °C |
| Moonraker job | `0003E1`, status `completed` |
| Print duration | 958.6 s (15 min 59 s) |
| Filament | 1245.15 mm / 2.99 cm³ / 3.71 g |
| Object height in metadata | 20.0 mm |

### How to check it

The file is still on the printer, so the record can be re-read rather than
trusted:

```sh
curl -s 'http://10.0.0.22:7125/server/history/list?limit=3&order=desc'        # job 0003E1
curl -s 'http://10.0.0.22:7125/server/files/metadata?filename=voron-cube20-next-layer.gcode'
curl -s 'http://10.0.0.22:7125/server/files/gcodes/voron-cube20-next-layer.gcode' | sha256sum
```

### What it proved, and what it did not

It proved the pipeline is real: the app's slice output, its upload path, its
Moonraker client, the printer's own start macro, the toolpath, and a finished
part. The cube came off the bed at 20 mm.

It also proved the presets were wrong, which is worth writing down because that
is the failure this page exists to catch:

- The print was sliced against the **default profile**, not a Voron one: a
  220 mm bed, so the plate geometry did not match the machine.
- Temperatures came from `Generic PLA` at **230 °C**, 15 °C above what this
  filament wants, and the bed at 60 °C rather than the 55 °C textured PEI asks
  for. The part printed; it was not printed *well*.

The cause was the engine's own preset bundle: it ships one printer vendor only,
so no Voron exists unless one is imported — see
`tools/preset-import/README.md` and the section below.

### After the fix

`tools/preset-import` now imports a user's own printer, process and filament
presets from a reference slicer's config directory into the catalog bundle
next-layer ships, translating that slicer's setting names to this engine's. With
the same model, printer and `2.4 PLA`:

```sh
B=upstream/build/src/next-layer-qt/next-layer-qt
QT_QPA_PLATFORM=offscreen $B oracle/models/cube_20.stl \
    --printer 'Voron 2.4 350 0.4 nozzle - Copy 0.4' --process '2.4' \
    --filament '2.4 PLA' --dump-config /tmp/v.json
QT_QPA_PLATFORM=offscreen $B oracle/models/cube_20.stl --slice /tmp/v.json,/tmp/newcube.gcode
```

resolves to a 350 × 350 bed, 0.30 mm layers, `M190 S55` / `M109 S215`,
extrusion multiplier 0.982 and an 18 mm³/s volumetric limit — the user's own
numbers, not a generic default. 42 printer/process/filament combinations of the
imported presets slice, and the importer refuses to publish one that does not.

Those imported presets were removed from this machine afterwards, so the command
above no longer resolves. What ships today is next-layer's own bundle:

```sh
B=upstream/build/src/next-layer-qt/next-layer-qt
$B oracle/models/cube_20.stl --printer 'Voron 2.4 350 0.4' \
    --process 'Standard' --filament 'Generic' --dump-config /tmp/v.json
$B oracle/models/cube_20.stl --slice /tmp/v.json,dist/prints/voron-cube20.gcode
```

The next print should be the same cube with these presets; until that is recorded
here, the "wrong settings" column above stands for the presets next-layer
ships by default.
