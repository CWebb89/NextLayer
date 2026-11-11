# Testing next-layer

How to find bugs in this application, in the order that finds the most per
minute. Every step says what a *pass* looks like as a number or a picture, not as
a feeling, because "it seems fine" is how regressions get signed off.

Two rules before anything else:

- **Never test against a stale binary.** `tools/check` refuses to run when the
  binary is older than its sources and names the files. If it says that, build.
- **A screenshot is evidence; a memory of a screenshot is not.** Use
  `tools/cockpit` so captures show the app alone and the agent terminal is never
  in frame.

## 1. The five-minute gate run

```sh
tools/check            # 31 gates, ~60 s once warm
tools/check --ui       # + a real window, captured and read
```

Read the *verdict lines*, not the exit code. Each gate prints a measured number:
coverage percentage, setting count, matched G-code lines, layer count. A gate
that prints `ok` with a suspiciously small number is a bug worth chasing
(`slice: 12 layers` where it used to be 400).

Then parity, which is the one that matters most:

```sh
python3 oracle/oracle.py verify --runner qt \
    --bin upstream/build/src/next-layer-qt/next-layer-qt
```

`12/12` or it is broken. A CHANGED case means the slicing path moved; the
report tells you which config and which file to diff.

### What CI runs, and what it cannot

`.github/workflows/check.yml` runs `tools/check --static` on every push and pull
request: the QML lint against its baseline and a `fish -n` parse of every script
in `tools/`. That is the whole of it, deliberately.

The rest of `tools/check` needs a binary built from the pinned engine, and the
engine's dependency build is hours and tens of gigabytes — a hosted runner has
neither the time nor a cache to make it cheap, and a workflow that pretends
otherwise is a red build nobody reads. So the binary gates, the oracle, the
headless frame and the printed evidence all stay local, where they run in a
minute against a real build. `--static` skips them and says so in a skip line
rather than passing quietly.

If CI is ever widened, it needs a self-hosted runner with a warm
`upstream/deps/build`, not a cleverer workflow.

## 2. Headless, no window: the fast loop

Use these while changing code. They exercise the same functions the UI does.

```sh
BIN=upstream/build/src/next-layer-qt/next-layer-qt

$BIN oracle/models/cube_20.stl --dump-presets                 # 462 machines, 67 vendors
$BIN oracle/models/cube_20.stl --dump-tabs                    # every settings tab
$BIN oracle/models/cube_20.stl --dump-subtab process,speed    # one sub-tab's items
$BIN oracle/models/cube_20.stl --printer 'Voron 2.4 350 0.4' \
     --process 'Standard' --filament 'Generic' --dump-config /tmp/c.json
$BIN oracle/models/cube_20.stl --slice /tmp/c.json,/tmp/c.gcode
# Without a dump, naming the triple opens the window on that setup.
$BIN oracle/models/cube_20.stl --printer 'Voron 2.4 350 0.4' \
     --process 'Standard' --filament 'Generic'
grep -c ';LAYER_CHANGE' /tmp/c.gcode                          # must be > 0
$BIN oracle/models/cube_20.stl --selftest-slice                # the app's own controller
```

A slice that "succeeds" and writes no file, or writes a file with zero layers, is
the single most common silent failure in this codebase. Always count layers.

Rendering, without a window on the desktop:

```sh
QT_QPA_PLATFORM=eglfs $BIN oracle/models/bridge_30.stl --smoke --shot /tmp/frame.png
```

It prints a coverage figure and exits non-zero if nothing drew. `offscreen`
produces a blank frame for 3D and looks like a pass — use `eglfs` only.

## 3. The window, driven for real

```sh
tools/cockpit/cockpit start next-layer --model oracle/models/cube_20.stl
tools/cockpit/cockpit shot /tmp/a.png
tools/cockpit/cockpit ocr                       # every word + its coordinates
tools/cockpit/cockpit click-text "Slice plate"
tools/cockpit/cockpit drag 1905 267 1905 600    # layer slider
tools/cockpit/cockpit stop
```

- **Pointer and keyboard are separate devices.** `cockpit` used to create one
  device with both a relative pointer and keys; libinput classified it as a
  pointer and the compositor delivered clicks while dropping every key, so
  typing and Escape silently did nothing. Each command now creates the device it
  needs, so `type` and `key` work in every text field.
- **First click often only focuses the window.** If a control does not respond,
  click the same coordinates again before assuming it is broken.
- `cockpit ocr` is the fastest way to find a control: read the coordinates off it
  instead of guessing window geometry.

To see first-run behaviour you need a clean data *and* config home, because both
the preset bundle and the "welcome seen" flag persist:

```sh
rm -rf /tmp/nl-test /tmp/nl-test-cfg && mkdir -p /tmp/nl-test /tmp/nl-test-cfg
XDG_DATA_HOME=/tmp/nl-test XDG_CONFIG_HOME=/tmp/nl-test-cfg next-layer \
    oracle/models/cube_20.stl
```

## 4. Preset import, end to end

The fixture in `oracle/import-fixture` is another slicer's directory shape, so
this works without any other app installed:

```sh
rm -rf /tmp/nl-imp && mkdir -p /tmp/nl-imp
XDG_DATA_HOME=/tmp/nl-imp $BIN oracle/models/cube_20.stl --import-slicer oracle/import-fixture
#   printers:  Gate Fixture 200
#   prints:    Gate Process
#   filaments: Gate Filament
#   N key(s) this engine has no setting for were dropped

XDG_DATA_HOME=/tmp/nl-imp $BIN oracle/models/cube_20.stl --dump-presets | grep 'Gate Fixture'
#   Gate Fixture 200 0.4   Custom   200x200 h=180 (1 extruder)   <- note the variant suffix

XDG_DATA_HOME=/tmp/nl-imp $BIN oracle/models/cube_20.stl \
    --printer 'Gate Fixture 200 0.4' --process 'Gate Process' --filament 'Gate Filament' \
    --dump-config /tmp/imp.json
XDG_DATA_HOME=/tmp/nl-imp $BIN oracle/models/cube_20.stl --slice /tmp/imp.json,/tmp/imp.gcode
grep -c ';LAYER_CHANGE' /tmp/imp.gcode        # 100 for the fixture
```

Against a real installation, point it at that slicer's own profile directory. In
the app: welcome → **Import my presets** → the detected folder → the summary.
When the summary says the machines appear after a restart, that is expected: the
engine reads its preset bundle once per process, so the app relaunches itself
rather than pretend the new vendor is loaded.

Every key that does not arrive is named. Two lists are printed:

- **dropped** — this engine has no such setting at all. Re-run with
  `NEXT_LAYER_DEBUG_PRESETS=1` to print each key. What is on that list today is
  the engine's own limit, not the importer's: per-role jerk, adaptive layer
  height, wall sequence, per-plate temperatures beyond one bed temperature, and
  the pressure-advance calibration pass.
- **belong to another profile** — the setting exists, but on a different profile:
  this engine keeps retraction and pressure advance on the filament, not on the
  process, so a process-level copy is reported rather than written into the
  wrong profile.

What does import through the name map, verified by importing and slicing: scarf
seams, arc fitting, the whole prime/wipe tower (width, brim, extra spacing and
flow), dynamic overhang speed and fan curves, tree/organic supports with their
branch angle, distance, tip diameter and top rate, plate temperature, and
pressure advance.

What to check by eye after an import:

1. The counts match what that slicer's own preset managers show, minus what was
   refused (each refusal is listed with a reason).
2. The bed dimensions of the imported machine equal the ones in the other
   slicer, in the printer list.
3. Slicing an imported triple writes layers, and the toolpaths look like that
   profile's (a 0.24 first layer, 3 perimeters, 15% grid infill for the fixture).
4. Temperatures in Preview match the filament you imported, not 200/60 defaults.

## 5. Manual passes worth running before a release

**Presets and settings**

- Change a setting in each of the three preset windows (printer, process,
  filament), then Save and Save as under a new name, restart, and check both come
  back with the value you set. Save as must report "saved <name>", never "nothing
  has been changed", and the copy must list print profiles like any other machine.
- Reset a sub-tab and a whole tab; both must return to the engine's defaults, and
  the settings panel must repaint without a restart.
- Add a printer via the welcome flow with a wrong bed size (1×1 mm) — the app must
  accept it and refuse to slice nothing; then delete it.

**Plate and objects**

- Drop two models, arrange them, slice: both must appear in the preview and the
  object list; the G-code must contain both.
- Rotate, scale, mirror, lay-flat: the preview bed and the sliced result must agree
  on where the object sits (`--transform` covers this headlessly).
- Add a negative volume and a support enforcer; the slice must show the cut and
  the forced supports.

**Preview**

- Drag the layer slider from top to bottom; the toolpath count must change at
  every layer, and the "moves" list must filter to the layer shown.
- Open a G-code file from disk (`--preview <file>`) and check the layer count
  matches `grep -c ';LAYER_CHANGE'`.
- Change a setting, re-slice, and confirm the preview reloads rather than showing
  the previous toolpaths.

**Robustness**

- Open a file that is not a model (a `.txt`), an empty `.stl`, and a 200 MB mesh.
  Each must produce a message, not a hang or a crash.
- Import a preset written by a *different* engine version (an old export from
  `--export-preset`): unknown keys must be dropped, not fatal.
- Disconnect the network and start the app: nothing may block on an update check.

**Window behaviour**

- Resize to 1280×720 and 2560×1440: no clipped controls, no overlap between the
  settings panel and the viewport, the plate toolbar still readable.
- Switch Model / Preview / Device repeatedly; no stale view, no double-drawn plate.
- Leave the app idle for a minute: CPU near zero (a busy loop here is a
  regression that ships quietly).

## 6. When something fails

1. Reproduce it headlessly if you can — `--slice`, `--dump-config`, `--smoke`
   reproduce almost everything without a window.
2. For a slice that produces nothing, bisect the *config*, not the code: dump two
   configs (one working, one not) and compare the `configuration` blocks key by
   key. More than one real bug in this project was a single value with the wrong
   unit — an extrusion width of `100` instead of `100%` slices to nothing at all,
   silently, exit code 0.
3. Save the failing input (model + config) somewhere before you dig; the config
   is the whole repro.
