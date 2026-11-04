# next-layer

A 3D printer slicer of our own, built on the PrusaSlicer 3.0 slicing engine used
as a **library**. We write the interface and the application; the engine does
the geometry and G-code.

Deeper context lives outside this repo, in the agent memory store:
`~/.pi/agent/projects-memory/next-layer/MEMORY.md` — read it first. It carries
the decisions, measured facts, dead ends, and the reasoning behind each.

## Layout

- `upstream/` — PrusaSlicer 3.0.0-alpha12 at a pinned commit. **Treat as
  upstream.** Our code lives in two directories of our own
  (`src/slic3r-app-spike`, `src/slic3r-app-qt`) plus two `add_subdirectory`
  lines inside the existing `if (SLIC3R_GUI)` block in `src/CMakeLists.txt`.
  Nothing else is modified. Keeping it that way is what makes pulling engine
  updates cheap — do not edit upstream files to solve our problems.
- `oracle/` — the G-code oracle: model corpus, config profiles, baselines, and
  `oracle.py`. This is the regression harness for the slicing engine.

## Hard rules

- **Every build runs inside a memory-capped cgroup.** This machine has 16 GB and
  zram-only swap; an uncapped build already froze the desktop once. Invoking
  pattern is in the memory file under "Build operations" — read it before
  running cmake. Never `-jN` at the top level of `deps/build`; dependencies
  parallelise internally.
- **Never change a model in `oracle/models/` without re-capturing baselines.**
  Models are inputs to fingerprints; a changed mesh silently invalidates its
  baseline.
- Coordinate convention is **printer Z-up**, everywhere except the one transform
  into Qt Quick 3D's Y-up world. Mesh data, camera maths, and every future
  feature stay in printer coordinates.

## Verifying a change

One command answers it:

```sh
~/dev/next-layer/tools/check            # 23 s: 7 gates
~/dev/next-layer/tools/check --fast     # 8 s: skip the oracle
~/dev/next-layer/tools/check --ui       # + a real window, captured and read
```

It does not build — a binary older than its sources fails the first gate and
names the files, so a green run is never against a stale binary. Every gate reads
its verdict out of the tool's output (coverage %, setting count, match count,
layer count); nothing is inferred from an exit code alone. `--update-qmllint-baseline`
accepts the current QML warnings as known, and must be followed by reading the
diff of `tools/qmllint-baseline`.

**Reference the real thing before writing UI.** OrcaSlicer is checked out at
`~/dev/reference/OrcaSlicer`; read its `src/slic3r/GUI` (GLCanvas3D, GCodeViewer,
IMSlider, the ImGui panels) and its `resources/profiles` when a control, a
measurement or a workflow is in question. Match Orca or PrusaSlicer behaviour,
naming and layout, adapting their code where it fits and writing new code only
where theirs cannot be reused. The parity ledger this is measured against is
`docs/orca-parity.md`.

**See the change, don't guess at it.** `tools/cockpit/cockpit` launches an app
alone on an unused Hyprland workspace and drives it with real pointer and
keyboard input (`start`, `shot`, `ocr`, `click-text`, `drag`, `key`, `stop`). Use
it for anything visual: slice through the UI, drag the sliders, open Orca beside
it, and compare the captures. `tools/cockpit/README.md` has the details.

Slicing parity (the important one):

```sh
cd ~/dev/next-layer
python3 oracle/oracle.py capture                                    # stock engine → baselines
python3 oracle/oracle.py verify --runner app \
    --bin upstream/build/src/slic3r-app-spike/next-layer-spike      # spike binary vs baselines
python3 oracle/oracle.py verify --runner qt \
    --bin upstream/build/src/slic3r-app-qt/next-layer-qt            # app binary vs baselines
```

`verify` exits 0 only when every case is byte-identical after header
normalisation. Current state: 12/12 on both binaries (the twelfth case resolves a
printer/process/filament triple, so preset resolution is covered too). The `qt`
runner is the one that matters for the GUI: it slices through the same `--slice`
path the app uses, so a regression in the settings model or the slice controller
shows up here before it reaches a window.

`verify` compares the sha256 in `oracle/baseline/manifest.json`. The `.gcode`
files in that directory are the human-readable copy a CHANGED report tells you to
diff against, and nothing reads them; `python3 oracle/baseline_check.py` is what
keeps them honest, and `tools/check` runs it.

The UI's own headless knobs (`--dump-tabs`, `--dump-subtab`, `--settings-tab`,
`--panel-left`, `--transform`) are documented in
`upstream/src/slic3r-app-qt/README.md`. `docs/orca-parity.md` is the Orca/Prusa
feature ledger: every tab, tool and setting marked done / staged / needs-engine,
so "do we have it yet" is a lookup rather than a guess.

Viewport rendering, headless (no window on the desktop, no screen capture):

```sh
cd upstream/src/slic3r-app-qt
QT_QPA_PLATFORM=eglfs ../../build/src/slic3r-app-qt/next-layer-qt \
    ../../../oracle/models/bridge_30.stl --smoke --shot /tmp/frame.png
```

`--smoke` renders one frame, reports coverage and exits non-zero if nothing
drew. `eglfs` is the only platform that gives a real GL context headlessly —
`offscreen` renders 2D fine but silently produces a blank frame for 3D, which
looks like a pass if you only check the exit code.

The app's own pipeline, without a window or a mouse:

```sh
cd upstream/src/slic3r-app-qt
BIN=../../build/src/slic3r-app-qt/next-layer-qt
$BIN ../../../oracle/models/cube_20.stl --selftest-slice     # SliceController path
$BIN ../../../oracle/models/cube_20.stl --dump-config /tmp/c.json
$BIN ../../../oracle/models/cube_20.stl --preview ../../../oracle/baseline/gcode/cube_default.gcode --smoke --shot /tmp/preview.png
```

## Preset fixtures

`~/.local/share/next-layer/presets/` holds the custom `Voron 2.4 350` bundle and
the user presets the preset code is tested against. They are live files, not in
the repo, so they are captured there:

```sh
tools/fixtures list                  # saved fixtures (currently: regression)
tools/fixtures diff regression       # live vs the saved copy
tools/fixtures save NAME             # snapshot the live presets
tools/fixtures restore NAME --yes    # put a snapshot back (backs up first)
```

Only `presets/local/next-layer-custom` and `presets/user` are captured; the vendor
presets are the engine's own bundle under `upstream/resources`. A behaviour change
that moves these files deliberately means saving a new fixture, not editing the
old one.

## Saving work

Two repositories. One command:

```sh
tools/save "what changed"   # commits both, or reports there is nothing to save
tools/status                # what is uncommitted, either side
```

- `~/dev/next-layer` (branch `main`) — the harness: oracle corpus and baselines,
  `tools/`, `docs/`, `AGENTS.md`, `.pi/plans/`. `.gitignore` is `/upstream/`.
- `upstream/` (branch `next-layer`) — our code only: `src/slic3r-app-qt`,
  `src/slic3r-app-spike`, and the two `add_subdirectory` lines. One commit on top
  of the pinned engine tag.

Our code cannot live in the root repo: git records any directory containing
`.git` as a gitlink instead of tracking inside it, so a repo above `upstream/`
can never track files below it. `tools/save` stages only those three paths and
reports — without committing — any other change under `upstream/`, which is the
"never edit upstream files" rule turned into a check.

Nothing is pushed and `upstream`'s `origin` is still Prusa's repository. Bumping
the engine pin is a rebase of the `next-layer` commit onto the new tag; do not
re-clone, the branch lives only here.

## Building

Configure/build already done: `upstream/build/` and `upstream/deps/build/` are
populated. To rebuild after a C++ change:

```sh
cmake --build upstream/build --target next-layer-qt -j4
```

Use the cgroup wrapper from the memory file for anything heavier than an
incremental link. QML under `upstream/src/slic3r-app-qt/qml/` is loaded from disk
at runtime — editing it needs no rebuild.

Launching: `~/.local/bin/next-layer` is a fish wrapper around the build-tree
binary, so the app starts from any directory and with any arguments. It resolves
QML through `NEXT_LAYER_QML_DIR` (baked in at configure time), never the cwd.
