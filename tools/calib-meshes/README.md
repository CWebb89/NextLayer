# Calibration meshes

**Nothing uses these files any more.** The temperature test is built from primitives
inside the app (`Calibration::build`, one block per band with a shelf cantilevered out
of its top, the temperature printed on the bed beside it), because the reference
slicer's test meshes carry that project's logo embossed in the geometry -- a printed
"ORCA" on the test piece -- and cutting a temperature range out of a fixed 700 mm
stack left the top band sliced through its own number.

The three other meshes were never wired up. They stay in the tree until someone
decides to drop them; see the note at the end.

---

The notes below describe how these were produced.

The calibration tests print the same models a reference slicer prints, because
"looks like the reference tower" only holds if it is that tower. Those models live
in that project's own resources as Draco-compressed meshes
(`resources/calib/**.drc`), which this engine cannot read, so they are exported
once and committed here as STL.

| File | From | Used by |
|---|---|---|
| `temperature_tower.stl` | `calib/temperature_tower/temperature_tower.drc` | the temperature test |
| `retraction_tower.stl` | `calib/retraction/retraction_tower.drc` | (asset, not wired yet) |
| `pressure_advance_tower.stl` | `calib/pressure_advance/pressure_advance_test.drc` | (asset, not wired yet) |
| `vfa.stl` | `calib/vfa/vfa.drc` | (asset, not wired yet) |

## Regenerating

The DRC format is decoded by the reference slicer's own CLI, which is not part of
this repository, so regenerating these meshes needs that toolchain installed
alongside: export each `.drc` to STL, then reduce it with `decimate.py`.

`decimate.py` is vertex clustering: the exporter writes the full-resolution mesh
(212k triangles for the tower), and a printer does not need that. The cell size is
a compromise between file size in the repository and the detail a test print needs;
the temperature tower's band heights are 10 mm, so a fraction of a millimetre is
plenty.

## The tower's own contract

The temperature tower is cut, not generated. The geometry contract, from
`Plater::calib_temp`:

- block `n` spans `[n * 10, (n + 1) * 10]` mm of the asset and prints at
  `500 - 5n` °C;
- the requested range is cut out of that 700 mm asset and dropped onto the bed;
- the layer G-code carries one `M104` per band boundary.

`PlateDocument::trimToBand` is the cut: it keeps the slab between two heights and
moves its vertices down so the band sits on the bed, which is what makes a middle
band printable at all.

## Licence

These meshes come from another AGPL-3.0 project; `THIRD-PARTY.md` carries the
attribution, alongside the engine this application links.

They are also the reason the temperature test stopped using them: the reference
prints its own name on its test models, and a test piece that carries another
slicer's logo is not something this app should ship. Nothing references the files
now; deleting the four `.stl` files (8 MB) would remove the last of it.
