# Calibration meshes

The calibration tests print the same models OrcaSlicer prints, because "looks like
Orca's tower" only holds if it is Orca's tower. Those models live in Orca's own
resources as Draco-compressed meshes (`resources/calib/**.drc`), which this engine
cannot read, so they are exported once and committed here as STL.

| File | From | Used by |
|---|---|---|
| `temperature_tower.stl` | `calib/temperature_tower/temperature_tower.drc` | the temperature test |
| `retraction_tower.stl` | `calib/retraction/retraction_tower.drc` | (asset, not wired yet) |
| `pressure_advance_tower.stl` | `calib/pressure_advance/pressure_advance_test.drc` | (asset, not wired yet) |
| `vfa.stl` | `calib/vfa/vfa.drc` | (asset, not wired yet) |

## Regenerating

Orca's CLI decodes its own DRC files, so the export needs a checkout or an
installed Orca:

```sh
orca-slicer /opt/orca-slicer/resources/calib/temperature_tower/temperature_tower.drc \
    --export-stl --outputdir /tmp/calib
python3 tools/calib-meshes/decimate.py /tmp/calib/stl/obj_1_temperature_tower.drc.stl \
    upstream/src/slic3r-app-qt/resources/calib/temperature_tower.stl 0.3
```

`decimate.py` is vertex clustering: the exporter writes the full-resolution mesh
(212k triangles for the tower), and a printer does not need that. The cell size is
a compromise between file size in the repository and the detail a test print needs;
the temperature tower's band heights are 10 mm, so a fraction of a millimetre is
plenty.

## The tower's own contract

The temperature tower is cut, not generated. Orca's geometry contract, from
`Plater::calib_temp`:

- block `n` spans `[n * 10, (n + 1) * 10]` mm of the asset and prints at
  `500 - 5n` °C;
- the requested range is cut out of that 700 mm asset and dropped onto the bed;
- the layer G-code carries one `M104` per band boundary.

`PlateDocument::trimToBand` is the cut: it keeps the slab between two heights and
moves its vertices down so the band sits on the bed, which is what makes a middle
band printable at all.

## Licence

These meshes come from OrcaSlicer (AGPL-3.0), as does the engine this application
links; `upstream/` already carries PrusaSlicer under the same licence.
