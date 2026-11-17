# Competitive gap analysis, 2026-11-15

Measured against two references on this machine:

| | |
|---|---|
| next-layer | working tree at `526f28d` + uncommitted viewport/shader edits; engine PrusaSlicer `3.0.0-alpha12` @ `30ef591` + 6 patches |
| PrusaSlicer | `2.9.6+arch5`, installed `/usr/bin/prusa-slicer`; source comparison against the engine tree (`upstream/`, which *is* PrusaSlicer 3.0-alpha) |
| OrcaSlicer | `~/dev/reference/OrcaSlicer` @ `e8ebe345`, 2026-11-02 (Bambu-derived fork of PrusaSlicer), plus its 999 option names vendored at `oracle/fixtures/orca-options.txt` |

Numbers below were produced, not remembered: `--dump-settings-json` for our surface, `prusa-slicer --help-fff` for theirs, `add("name"` extraction from each tree's `PrintConfig.cpp` for the engines, `find | xargs wc -l` for size. Oracle re-run on the working tree during this pass: **12/12 match baseline `engine-3.0.0-alpha12`**, so the dirty viewport/shader edits do not touch toolpaths.

## 1. Scale, so the ambition is priced

| | Lines of C++ | GUI |
|---|---|---|
| OrcaSlicer | 845,817 (`libslic3r` 339k + `slic3r/GUI` 464k + `Utils` 43k) | wxWidgets, 511 files in `slic3r/GUI`, 24 gizmos |
| Engine (PrusaSlicer 3.0-a12) | 398,912 (`libslic3r` 131k + `slic3r-shared` 263k + app 4.7k) | wx-free ImGui plater (~77 `.cpp` in `App/Plater`, one host: `PlaterRenderModule`) |
| next-layer (ours) | 26,078 C++ + 20,218 QML | Qt Quick 3D, 145 source files, 25 dialogs |

We are 5 % of Orca's size and 12 % of the engine's. Any premise of "match Orca feature for feature" is a 3–5 year solo project. The only winnable premise is "be the best slicer for a defined user, and never worse in the workflows that user runs daily".

## 2. Settings surface

| | Count |
|---|---|
| next-layer surfaced (process 219 / filament 68 / printer 65) | **352** |
| engine option names defined (`add(...)`) | 556 (includes SLA, CLI flags, runtime placeholders) |
| PrusaSlicer 2.9 config options exposed via `--help-fff` | 411 lines |
| OrcaSlicer option names | **999** |
| Orca names with no engine counterpart | 701 — of which 109 are already rename-mapped by `ForeignSettings`, and a large remainder is Bambu/AMS/IMEX/belt/cloud-only |

User-relevant engine settings **not surfaced in our UI** (verified against the dump): `nozzle_diameter`, `nozzle_high_flow`, `elefant_foot_min_width`, `filament_flush_speed`, `filament_flush_volume`, `wiping_volumes_matrix`, `wiping_volumes_use_custom_matrix`, `wipe_into_infill`, `wipe_into_objects`, `single_extruder_multi_material_priming`, `mmu_segmented_region_max_width`, `mmu_segmented_region_interlocking_depth`, `extruder_colour`, `support_points_density_relative`, `tower_hop_height`, `tower_speed`. (The `wipe_tower_*` family *is* surfaced — 11 of them.) Nozzle diameter is the sharpest: a machine's nozzle can only be changed by editing an imported preset file.

## 3. Engine capability gaps — the fork tax

These are things OrcaSlicer's `libslic3r` does that ours cannot, verified by name absence in our `PrintConfig.cpp`. They are print-quality issues, not cosmetics, and none of them is a UI job:

- `adaptive_layer_height` and family — adaptive height is Orca's headline time-vs-quality feature
- `xy_hole_compensation`, `xy_contour_compensation` — hole fit on printed parts
- `only_one_wall_top`, `only_one_wall_first_layer`
- `gap_fill_flow_ratio`, `gap_fill_target`
- `thick_internal_bridges`, `internal_bridge_density/angle/flow/accel/line_width` (we patched an internal-bridge *role*; the settings are absent)
- `spiral_mode_smooth/max_xy_smoothing`, `spiral_starting_flow_ratio` (our `spiral_vase` is the plain one)
- `elefant_foot_compensation_layers`, `elefant_foot_layers_density`
- `overhang_reverse` (+ internal-only/threshold), `overhang_flow_ratio`
- `support_critical_regions_only`, `support_base_pattern`, `support_with_infill`, `independent_support_layer_height`, `support_air_filtration`
- `accel_to_decel_enable/factor`, `emit_machine_limits_to_gcode` (relevant: we deliberately do not emit limits for Klipper)
- `adaptive_pressure_advance` (+ bridges/overhangs/model)
- `input_shaping_freq_x/y`, `input_shaping_type`, `input_shaping_emit` — Klipper users set these in the slicer; we only do them through the calibration G-code
- `exclude_object`, `exclude_object_tolerance`, `skip_objects` — cancel one object on a Klipper bed
- `time_lapse_gcode`, `timelapse_type`
- `wipe_tower_type/rotation/fillet/rib_*`, `purge_in_prime_tower`, `flush_volumes_matrix/vector`, `flush_into_infill/objects/support` — the MMU/multi-nozzle purge model
- plate-type bed temperatures (`cool_/hot_/textured_/eng_/supertack_plate_temp`)
- `activate_air_filtration*`, chamber exhaust
- `fan_kickstart`, `fan_speedup_time/overhangs`, `auxiliary_fan`, `part_cooling_fan_min_pwm`
- `small_area_infill_flow_compensation`
- `infill_direction`, `solid_infill_direction`, `symmetric_infill_y_axis`, `separated_infills`, `minimum_sparse_infill_area`, `infill_lock_depth`
- `top_surface_density`, `top_solid_infill_flow_ratio`, `bottom_surface_density/pattern`, top/bottom fill order
- `make_overhang_printable` (+angle/hole), `hole_to_polyhole`
- `wall_direction`, `wall_sequence`, `dont_slow_down_outer_wall`, `slow_down_layers`, `detect_overhang_wall`
- Orca-only patterns: `lightning_infill*`; `skeleton/skin_infill_*` (Bambu)

The honest read: our engine is PrusaSlicer's, whose quality stack is good but *narrower* than Orca's fork, which has 200k more `libslic3r` lines. Matching them means owning patches to `libslic3r` permanently, not just UI work. We already do this (`patches/0002`–`0006`); the question is whether we want that to be a standing cost.

## 4. Prepare/tool gaps (the richest UI deficit)

Engine-side host code for **all** of these already exists wx-free in `upstream/src/slic3r-shared/src/Slic3r/App/Plater/` (`CutGizmo`, `CutDialog` with connectors, `PaintOnFuzzySkinGizmo`, `HeightRangeGizmo`, `SimplifyGizmo`, `SvgGizmo`, `TextGizmo`, `VariableLayerHeightGizmo`, `RotationGizmo`, `ScaleGizmo`, `TranslationGizmo`, `MeasureGizmo`, `ArrangeGizmo`, `SelectionHandler`, `PlaterRenderModule`). The ledger's "Why the tool rail is one job" analysis stands: a Qt equivalent of `WXRenderCanvas` (1,247 lines of wx) unlocks them together. We instead reimplemented six of them at partial depth.

Missing or partial, verified in our tree:

| Tool | next-layer | Orca | Prusa 2.9 |
|---|---|---|---|
| Cut | plane only, capped halves | `GLGizmoCut` + `CutDialog`: connectors/plugs, x/y/grid cuts | connectors |
| Mesh boolean | absent | `GLGizmoMeshBoolean` | union/difference/intersection (CGAL) |
| Simplify | absent | `GLGizmoSimplify` | yes |
| Emboss text | done (baked mesh) | parametric emboss + font list | yes (Emboss) |
| Emboss SVG | absent (SVG imports as geometry only) | `GLGizmoSVG` | yes (Emboss SVG) |
| Variable layer height | bands only | smooth curve + adaptive | smooth curve + per-layer edit |
| Adaptive layer height | absent | yes | no |
| Height-range modifier volumes | absent | yes | yes |
| Rotate/scale gizmos + snapping | numeric only; single-axis move gizmo | full gizmo + snap + alignment | full gizmo + snap |
| Measure | 2-point only | `GLGizmoMeasure`, multiple modes | Measure with multiple modes |
| Brim ears | absent | `GLGizmoBrimEars` | no |
| Per-facet fuzzy skin painting | absent (setting only) | `GLGizmoFuzzySkin` | yes (verified in the binary) |
| Alignment / multi-body assembly | absent | `GLGizmoAlignment`, `GLGizmoAssembly` | no |
| Texture displacement | absent | `GLGizmoTextureDisplacement` | no |
| Repair / inspect mesh | absent | `repair`, `inspect_mesh` options | "Fix through netfabb" |
| Exclude object | absent | `exclude_object` | no |
| Export STL/OBJ of an object | absent | yes | yes |
| Reload object from disk | absent | yes | yes (verified in the binary) |

## 5. Product surface gaps

| Area | next-layer today | Orca | PrusaSlicer 2.9 |
|---|---|---|---|
| Printer control | Device tab is an embedded Mainsail/Fluidd page + "send/print" upload | full Device tab — `DeviceCore/` carries axis jog, bed/chamber control, extruder/filament system, firmware and health state, calibration, camera | none (upload only) |
| Print monitoring | web page only | native + camera (`Camera.cpp`) | none |
| Print statistics | per-object geometry stats only | yes | yes (Help ▸ Print statistics) |
| Autosave / crash recovery | none | `autosave` option + notification centre | config-file recovery only; no project autosave (no autosave string in the 2.9.6 binary) |
| Log file | stderr only (known issue) | log file | log file |
| Localization | 0 `qsTr` calls, English only | localized upstream (catalogs not in this checkout) | **136** `.po` catalogs in `upstream/` |
| Theme | single dark palette (`Colors.qml`), no light/system | light/dark, per-platform | light/dark |
| Plugins / network backends | none | plugin interface, cloud, network plugins | none |
| Preset validity (`compatible_printers`) | not surfaced | yes | yes |
| Multiple instances | none | single-instance forwarding | single-instance forwarding |
| Session restore | none (recents only) | yes | yes |
| Sequential printing | settings only, no arrangement | arranged by NFP with clearance | settings only, arrange gap |
| Multi-plate | yes, in project + slice-all | yes | **no** |
| Project interop | reads/writes Prusa 3MF, reads/writes Bambu/Orca dialect, `.nextproj` | own only | own + Prusa 3MF |
| Keyboard | 60 bindings + filterable map (counted in `main.qml`) | large, per-workspace | large |
| Platforms | Linux x86-64 only (AppImage/AUR) | Win/mac/Linux | Win/mac/Linux |

Already ahead of both, and worth saying out loud: multi-plate projects; byte-identical headless CLI (`oracle` 12/12 on both binaries); 428-machine catalog generated with each vendor's own process/filament; host offsets applied to both export and upload; per-feature paint performance gating; no account, no telemetry, no cloud.

## 6. Defects that would sink a first impression

From `docs/known-issues.md` and the audit, these are the ones a reviewer hits in ten minutes:

1. **A machine saved with "Save as" cannot be selected** (list comes from `printers.json`).
2. **Scaling pivots on the object's own bbox min**, so parts grow toward +X/+Y.
3. **Typing in the transform panel re-slices/rebuilds per keystroke.**
4. **Organic supports + variable layer height**: saving a Bambu project invents a layer-height profile, then refuses to slice it.
5. **Saving a project that came from a 3MF changes the toolpath** (digest differs, one extra override).
6. No autosave, no crash log — a crash loses the plate silently.
7. Painting has had no usability pass (concave radius, erase path, hard edges).
8. The Orca process drive in `tools/check` is flaky 1-in-3, so the UI gate's Orca half is not trustworthy yet.

## 7. What "better" has to mean

Feature-count superiority is not reachable and would not be the deciding factor even if it were: Orca already loses users to its own complexity, Bambu-centric defaults, cloud prompts and stability, and PrusaSlicer loses users to its cadence. People switch slicers when **their** machine, **their** profiles and **their** files work on day one, and stay when the daily loop is faster or less annoying.

So "better than Orca" resolves to three concrete claims, each of which is testable:

1. **Opens everything, prints the same.** Their Orca/Prusa presets, their Bambu/MakerWorld 3MF, their plates; sliced output that matches PrusaSlicer's (already proven by the oracle) and is credible against Orca's for the same settings. Today this breaks at nozzle editing, profile fidelity, MMU/AMS, and the two data-integrity defects above.
2. **The daily loop is faster and calmer.** Startup, slice latency, preview smoothness, no modal soup, no account, no cloud, keyboard-first, undo that works, autosave that works. This is winnable with Qt Quick and is the only place the architecture is a genuine advantage.
3. **It is the best slicer for Klipper-class FFF and tool changers.** Native input shaping and pressure advance *as settings*, `EXCLUDE_OBJECT_DEFINE` emission + cancel-one-object, time-lapse, host offsets (done), Moonraker job control, per-nozzle data. Orca treats Klipper as a generic firmware; PrusaSlicer barely acknowledges it. This is the defensible niche — and it is Curtis's own hardware, so it can be printed and verified instead of guessed.

Everything else (AMS/cloud/MakerWorld, SLA, plugins SDK, colour mixing, belt printers, Windows/macOS) should be explicitly declared out of scope in the UI and the docs, not half-shipped.

## 8. Order of work

**P0 — finish what is claimed (no new features).** Save-as machine list; scale/rotate pivot; transform keystroke storm; the two project data-integrity defects; autosave + a log file; nozzle diameter (and `nozzle_high_flow`) in the Printer tab; hide or arrange sequential printing; fix the Orca drive so the UI gate means something; regenerate `docs/features.md`'s gap table (it is stale: multi-slot editing, compare-presets and `.3mf` projects are all listed missing and are all done).

**P1 — table stakes a switcher checks.** Cut connectors; mesh boolean; simplify; SVG emboss; smooth + adaptive layer height; height-range modifiers; rotate/scale gizmos with snapping; measure modes; per-facet fuzzy painting; exclude-object end to end (G-code + Moonraker cancel); native device panel with job control and history; multi-material for tool changers actually printed; import fidelity report (what was dropped, per setting); `compatible_printers` conditions; export STL/OBJ and reload-from-disk.

**P2 — the engine, where parity actually lives.** `xy_hole/contour_compensation`, `only_one_wall_top`, `gap_fill_flow_ratio`, `thick_internal_bridges` + internal-bridge settings, `spiral_mode_smooth`, elephant-foot layers, support critical regions/base pattern/independent support LH, `accel_to_decel` + `emit_machine_limits`, adaptive pressure advance and input-shaping settings, `lightning_infill`, plate-type bed temperatures, flush/wipe family for MMU-class machines.

**P3 — the wedge, and it is cheap.** CLI job specs and JSON reports (nobody else has them); `EXCLUDE_OBJECT` + time-lapse as first-class Klipper features; published startup/slice benchmarks next to Orca and PrusaSlicer; i18n scaffolding; light theme.

**Deliberately skip:** Bambu AMS/cloud/MakerWorld, SLA (already skipped), plugin SDK, colour mixing, belt printers, Windows/macOS packaging, a preset visibility-mode system.

**The structural decision still open:** keep reimplementing plater tools in Qt, or host `PlaterRenderModule` behind a Qt `AbstractRenderCanvas` and inherit twenty tools plus gizmo interactions at once. Hosting ends the reimplementation treadmill and would make P1's tool list nearly free, at the cost of the Qt Quick 3D viewport identity and a hard integration (GL surface, ImGui input, framebuffer handoff). If the goal is "better than Orca", the second option is the only one that gets there in a plausible number of months; if the goal is "a clean slicer for Klipper people", the first one is honest and the wedge in P3 is where the differentiation lives.

## 9. Verdict

next-layer today is a credible *engine* and an incomplete *product*: excellent slicing parity and project interop, strong preview and calibration, and a prepare/machine-side surface that is roughly a third of what Orca offers — with a handful of defects that would read as "unfinished" within minutes. It cannot be better than Orca in breadth, and should stop implying that (`docs/what-is-next-layer.md` should say what it does not do). It can be better for the user it currently fits — Klipper/Voron/tool-changer, keyboard-driven, no cloud — provided P0 and P1 are finished, printer control is real, and multi-material is verified on hardware.
