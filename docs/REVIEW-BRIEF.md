# Review brief

Two repositories, one goal: next-layer is a Qt/qml slicer application whose backend is
being moved from a vendored C++ engine (`next-layer/upstream/`, a PrusaSlicer/Orca
descendant) to HFW, a Rust slicing engine in `~/dev/HFW`. The C++ engine leaves by being
ported file by file; when the last file goes, `upstream/` is deleted.

This file exists so a reviewer can start from the current state instead of reconstructing
it. Read the two authoritative documents first — they are maintained, this is a signpost.

| read | for |
| --- | --- |
| `docs/rust-engine-plan.md` | what each phase replaces, and the per-phase ledger of what has landed |
| `docs/port-graph.md` | the module map: dependencies, levels, the five-file cycle, and "Where the port stands" |
| `~/dev/HFW/docs/NEXTLAYER-BRIDGE.md` | the ABI contract, the four concepts that cross it, and the five things next-layer cannot port past yet |
| `~/dev/HFW/docs/PREVIEW-RENDERER.md` | the costed options for what draws the toolpath once libvgcode goes |

## Verify, do not trust

```
cd ~/dev/next-layer && tools/check          # 56 gates, oracle 12/12 byte-identical
cd ~/dev/HFW       && fish scripts/test-hfw.fish   # 86 sections, ~690 tests
```

Both are green at the commits described below. `tools/check` also gates the port's
direction: a new `.cpp`/`.hpp` under `src/qt/src` fails it, and a deleted one must leave
`tools/cpp-baseline` in the same commit.

Measured at the time of writing: `fish tools/rust-port` reads **57 C++ files, 23,837
lines** against **24 Rust modules, 15,500 lines**; 19 of 39 modules are Rust.

## The pattern every port follows

`src/qt/src/GcodeBrand.hpp` is the reference. Logic moves to `src/app/src/<Name>.rs`
behind one `#[cxx::bridge]` in `src/app/src/bridge.rs`; the C++ header stays as inline
Qt-typed adapters keeping every spelling its callers use; the `.cpp` and its
`tools/cpp-baseline` line leave in the same commit. The header dies when its last C++
includer is ported.

## What to scrutinise, in the order it matters

1. **Is the adapter-header pattern sound, or is it two sources of truth?** Read one landed
   module end to end — `SettingsModel` (1,568 C++ lines to Rust, 21 invokables, 14 QML
   files) or `HfwEngine` (1,008 lines over the C ABI) — and decide whether the split is
   reviewable or whether behaviour can hide in the seam.
2. **Does the gate actually prevent drift?** `tools/check` fails on new C++ and on an
   un-baselined deletion. Ask what it cannot see: it does not check that a ported module
   behaves the same, only that the file moved. Which gate would catch a wrong port?
   `--plate-document-test`, `--plate-readback-test` and the 12-file oracle are the real
   ones.
3. **Behaviours claimed preserved.** These were verified by tests: paint as one `uint8_t`
   per triangle per channel, placement as a 16×`f64` row-major matrix where a shear or a
   collapsed axis is refused rather than approximated, layer heights as interleaved
   `(z, height)` pairs with zero pairs meaning uniform, the foreign-setting dialect's 109
   rows in the same order, and G-code byte-identical across the whole oracle.
4. **Behaviours deliberately changed, and whether each was right.** `ForeignSettings` now
   picks the weakest match by table order, not `QHash` iteration order — the old choice
   was not stable across process runs. `Settings` no longer prints `55` as
   `55.00000000000001` (a real defect, fixed and pinned). `displayName()` no longer reads
   out of bounds on an empty plate.
5. **Unresolved, and answerable by reading code.** HFW emits `;TYPE:Internal solid
   infill` while `PreviewData::classifyBottomSurface` retags `;TYPE:Solid infill`, so the
   bottom-surface legend row will disappear and per-role totals shift once the preview
   ports; nothing gates it. `Arranger` is deliberately not ported: HFW packs bounding
   boxes with MaxRects where the C++ packs projected silhouettes with an NFP kernel, and
   the only gate uses cubes.
6. **The renderer is the largest thing not built.** `GcodeView` draws through the vendored
   `libvgcode` with its own desktop-GL context; when `upstream/` goes, nothing draws the
   toolpath. `HFW/docs/PREVIEW-RENDERER.md` costs four options, recommends Qt Quick 3D
   geometry with quads per bead, and measures the numbers. `hfw_plate_preview_buffer`
   already hands over the buffer that needs. Review the decision, not the code.
7. **`PlateDocument` is the wall.** 4,479 lines, 102 invokables, the last file in the
   five-file cycle. Both design slices are drafted in
   `~/.pi/agent/projects-memory/next-layer/drafts/nl-drafts5/PlateDocument.slice1.md` and
   `nl-drafts10/PlateDocument.slice2.md`. Nothing is implemented.

## Known trade-offs, stated so they are not mistaken for oversights

- Four of the six level-0 "leaves" are header-only, so they move no line off
  `tools/cpp-baseline`: a header dies with its last C++ includer, not when its logic moves.
- `main.cpp` is permanent (Definition of Done item 5): it registers 13 singletons and is
  an edit site for every port, never a port target.
- `GlewWrap` cannot carry its `-Wl,--wrap=glewInit` into the cargo link line; the flag
  stays at `src/qt/CMakeLists.txt:171`.
- The six phase-7 files (`SceneSelftest`, `SliceRuntime`, `Paint`, `Text`, `Cut`,
  `Hollow` — 5,118 lines) are deletions, not ports, and wait on their `tools/check` gates
  being re-homed and on the four plate structs leaving `SliceRuntime.hpp`.
