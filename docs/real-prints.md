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
| Voron 2.4 R2 350 | Klipper + Moonraker | 1 | 0.1.0 | Curtis | 2026-11-08 | 3DBenchy, printed and judged by eye against Orca's own print of the same model: no visible difference |
| Voron 2.4 R2 350 | Klipper + Moonraker | 1 | 0.1.2 | Curtis | 2026-11-17 | 3DBenchy again, after the retraction, Z-hop and brim fixes: job `0003F4`, 42 min, 9.99 g, "looks very good" |
| Voron 2.4 R2 350 | Klipper + Moonraker | 1 | 0.1.2 | Curtis | 2026-11-17 | two 20 mm cubes on one plate, the arrange-spacing fix: job `0003F6`, 19 min, 6.3 g, "very clean and separate" |

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

## 2026-11-08 — 3DBenchy, Voron 2.4 350

The first print of a model with the kind of geometry that exposes a slicer: an
overhanging bow, a smooth hull, a small cabin, lettering on the stern.

| | |
|---|---|
| Printer | Voron 2.4 R2 350 mm, Klipper + Moonraker |
| Model | 3DBenchy, downloaded by Curtis, opened from the Downloads directory |
| Sliced in | next-layer, `0.1.0` |
| Result | Printed to completion. Curtis' verdict, in his own words: "looks very good and just as good as if I printed it on orca". |

What this entry does **not** have, and the cube entry does: the G-code file and
its hash, plus the Moonraker job record. The printers were unreachable when this
was written, so those fields are missing rather than checked. It is still the
first print of a real model, and the first comparison against the same model
sliced by OrcaSlicer on the same machine, so it is recorded as that and no more.

What it establishes: overhangs, bridges, retraction and cooling on a shape that
shows them, from a project this app opened and sliced. What it does not establish:
dimensional accuracy (nothing was measured with calipers), a tool change (one
tool), or any of the file-format round trips this repository now knows are broken.

## 2026-11-17 — 3DBenchy again, and a two-cube plate, on 0.1.2

Both jobs completed on `voron-24` and are in Moonraker's own history, which is
where the figures below come from. Both files carry `; generated by next-layer
0.1.2`, which means the **bundled C++ engine** sliced them: routing a slice to
HFW needs `NEXT_LAYER_ENGINE=hfw` in the environment, and the app was started
without it.

| Job | File | Duration | Filament | Result |
|---|---|---|---|---|
| `0003F4` | `benchy-app.gcode` (4.04 MB) | 42 min | 9.99 g | completed, Curtis' verdict "looks very good" |
| `0003F6` | `two-cubes.gcode` (0.34 MB) | 19 min | 6.3 g | completed, "very clean and separate" — the plate that proved the object spacing fix |

`0003F5` is the same two-cube file cancelled three minutes in, from the Z-hop
investigation; `0003F6` is the run that finished.

The benchy file is also on this machine at `~/benchy-app.gcode`, sha256
`df7fdcc45cec7f9ae104cbd91c1e675d4f1b96a22eb42b77ece72c65a38d69df`, written one
minute before the job started, so that hash is the printed G-code.

**The two-cube file has no hash here.** Moonraker's copy was uploaded at 15:21 EST
and `~/two-cubes.gcode` was written at 16:13 EST, so the local file is a later
slice — the first one carrying the per-flavour velocity-limit fix, as
`grep -c SAVE_VELOCITY_LIMIT` returning 0 shows — and not the file that printed.
The printer's own copy is the authority; the MCP server for `voron-24` has no
metadata call, so its hash is not recorded.

What these two prints establish: the retraction, `retract_lift` and `brim_type`
fixes produce a printable plate, and two objects arranged on a plate stay separate.
They do not establish anything about HFW — no print has come out of it through the
app yet.

