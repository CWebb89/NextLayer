# next-layer

A 3D printer slicer of our own, built on an open-source slicing engine used as a
**library**. We write the interface and the application; the engine does
the geometry and G-code. `THIRD-PARTY.md` names the engine and its licence.

Deeper context lives outside this repo, in the agent memory store:
`~/.pi/agent/projects-memory/next-layer/MEMORY.md` — read it first. It carries
the decisions, measured facts, dead ends, and the reasoning behind each.

## Layout

- `src/` — ours, all of it. `src/qt` is the application; `src/cli` is the
  headless slicing harness the oracle's `app` runner drives.
- `patches/` — the single patch that makes the engine build our sources. It
  replaces two `add_subdirectory` lines in the engine's `src/CMakeLists.txt`
  with two that point here, from outside the engine tree.
- `upstream/` — the slicing engine, fetched at a pinned commit and patched in
  place by `tools/bootstrap`. Gitignored: it is a dependency, never edited by
  hand and never committed. The patch is the whole of our footprint in it.
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
~/dev/next-layer/tools/check            # every gate, including the oracle
~/dev/next-layer/tools/check --fast     # skip the oracle
~/dev/next-layer/tools/check --ui       # + a real window, captured and read
~/dev/next-layer/tools/check --ui --with-orca   # + the reference slicer drives
```

It does not build — a binary older than its sources fails the first gate and
names the files, so a green run is never against a stale binary. Every gate reads
its verdict out of the tool's output (coverage %, setting count, match count,
layer count); nothing is inferred from an exit code alone. `--update-qmllint-baseline`
accepts the current QML warnings as known, and must be followed by reading the
diff of `tools/qmllint-baseline`. `docs/testing.md` is the manual walkthrough that
sits on top of it.

**Reference the real thing before writing UI.** A checkout of a mature reference
slicer, its source and its profile bundle are recorded in the memory store under
"Reference checkouts"; read its settings UI, its G-code viewer and its plate
chrome when a control, a measurement or a workflow is in question. Match that
behaviour, naming and layout, adapting its code where it fits and writing new
code only where it cannot be reused. The ledger this is measured against is
`docs/features.md`.

**See the change, don't guess at it.** `tools/cockpit/cockpit` launches an app
alone on an unused Hyprland workspace and drives it with real pointer and
keyboard input (`start`, `shot`, `ocr`, `click-text`, `drag`, `key`, `stop`). Use
it for anything visual: slice through the UI, drag the sliders, open a reference
slicer beside it, and compare the captures. `tools/cockpit/README.md` has the
details.

Slicing parity (the important one):

```sh
cd ~/dev/next-layer
python3 oracle/oracle.py capture                                    # stock engine → baselines
python3 oracle/oracle.py verify --runner app \
    --bin upstream/build/src/next-layer-cli/next-layer-spike        # spike binary vs baselines
python3 oracle/oracle.py verify --runner qt \
    --bin upstream/build/src/next-layer-qt/next-layer-qt            # app binary vs baselines
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
`src/qt/README.md`. `docs/features.md` is the
feature ledger: every tab, tool and setting marked done / staged / needs-engine,
so "do we have it yet" is a lookup rather than a guess.

Viewport rendering, headless (no window on the desktop, no screen capture):

```sh
QT_QPA_PLATFORM=eglfs upstream/build/src/next-layer-qt/next-layer-qt \
    oracle/models/bridge_30.stl --smoke --shot /tmp/frame.png
```

`--smoke` renders one frame, reports coverage and exits non-zero if nothing
drew. `eglfs` is the only platform that gives a real GL context headlessly —
`offscreen` renders 2D fine but silently produces a blank frame for 3D, which
looks like a pass if you only check the exit code.

The app's own pipeline, without a window or a mouse:

```sh
BIN=upstream/build/src/next-layer-qt/next-layer-qt
$BIN oracle/models/cube_20.stl --selftest-slice     # SliceController path
$BIN oracle/models/cube_20.stl --dump-config /tmp/c.json
$BIN oracle/models/cube_20.stl --preview oracle/baseline/gcode/cube_default.gcode --smoke --shot /tmp/preview.png
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

One repository. One command:

```sh
tools/save "what changed"   # commits, or says there is nothing to save
tools/status                # what is uncommitted, and what state the engine is in
```

- `~/dev/next-layer` (branch `main`) — everything: `src/`, `patches/`, `tools/`,
  `oracle/`, `docs/`, `AGENTS.md`. `.gitignore` is `/upstream/` and `/dist/`.
- `upstream/` is not a repository of ours. `tools/bootstrap` fetches it from the
  engine's own repository at the pinned commit and applies `patches/`, so it can
  always be discarded and rebuilt. Nothing in it is ever staged.

Bumping the engine pin: fetch the new commit, re-export `patches/` against it
(`git format-patch` in a scratch checkout), re-run `tools/bootstrap` and the
oracle. The patch failing to apply is the signal that the engine's build files
moved and need a look — that is deliberate, not a nuisance.

## Building

`upstream/build/` and `upstream/deps/build/` are populated on this machine, so
`tools/bootstrap` has nothing to fetch and a rebuild is incremental:

```sh
cmake --build upstream/build --target next-layer-qt -j4
```

On a fresh machine, order is: `tools/bootstrap`, then one cmake configure of
`upstream/` into `upstream/build/`, which fetches the engine's dependencies (hours
and tens of GB), then the build. Use the memory-capped scope for anything heavier
than an incremental link. QML under `src/qt/qml/` is loaded from disk at runtime —
editing it needs no rebuild.

Launching: `~/.local/bin/next-layer` is a fish wrapper around the build-tree
binary, so the app starts from any directory and with any arguments. It resolves
QML through `NEXT_LAYER_QML_DIR` (baked in at configure time), never the cwd.
