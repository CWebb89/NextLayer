#!/usr/bin/env python3
"""Write the Bambu/Orca-shaped project a foreign-import gate opens.

The point of the fixture is the dialect, not the geometry: those slicers keep an
object's settings as plain metadata in ``Metadata/model_settings.config``, under
their own setting names, and they do not write a ``Slic3r_PE_model.config`` at all.
The settings here cover every outcome a gate has to tell apart: names this engine
accepts as written (``layer_height``), names that mean something here under a
different word (``wall_loops``, ``sparse_infill_density``, ``top_shell_layers``,
``inner_wall_speed``, ``outer_wall_line_width``, ``initial_layer_print_height``,
``enable_support``, ``support_type``), names whose values are spelled differently on
the two sides (``seam_position=back``, ``enable_support=1``), names whose behaviour
this engine does not have (``skeleton_infill_density``), and a name nothing here
knows (``made_up_setting``). The last two are dropped without a word: nothing a
person opening a downloaded project could do about them. ``support_object_xy_distance``
is the one that used to be dropped and now lands: this engine states the same gap as
a distance in millimetres or a share of the perimeter width.

    python3 oracle/fixtures/bambu_project.py OUT.3mf
"""

from __future__ import annotations

import sys
import zipfile

CUBE = """<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US"
       xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
  <resources>
    <object id="2" type="model">
      <mesh>
        <vertices>
          <vertex x="0" y="0" z="0"/><vertex x="20" y="0" z="0"/>
          <vertex x="20" y="20" z="0"/><vertex x="0" y="20" z="0"/>
          <vertex x="0" y="0" z="20"/><vertex x="20" y="0" z="20"/>
          <vertex x="20" y="20" z="20"/><vertex x="0" y="20" z="20"/>
        </vertices>
        <triangles>
          <triangle v1="0" v2="2" v3="1"/><triangle v1="0" v2="3" v3="2"/>
          <triangle v1="4" v2="5" v3="6"/><triangle v1="4" v2="6" v3="7"/>
          <triangle v1="0" v2="1" v3="5"/><triangle v1="0" v2="5" v3="4"/>
          <triangle v1="1" v2="2" v3="6"/><triangle v1="1" v2="6" v3="5"/>
          <triangle v1="2" v2="3" v3="7"/><triangle v1="2" v2="7" v3="6"/>
          <triangle v1="3" v2="0" v3="4"/><triangle v1="3" v2="4" v3="7"/>
        </triangles>
      </mesh>
    </object>
  </resources>
  <build>
    <item objectid="2" transform="1 0 0 0 1 0 0 0 1 100 100 0"/>
  </build>
</model>
"""

SETTINGS = """<?xml version="1.0" encoding="UTF-8"?>
<config>
  <object id="2">
    <metadata key="name" value="cube"/>
    <metadata key="extruder" value="1"/>
    <metadata key="layer_height" value="0.12"/>
    <metadata key="initial_layer_print_height" value="0.2"/>
    <metadata key="wall_loops" value="3"/>
    <metadata key="top_shell_layers" value="4"/>
    <metadata key="bottom_shell_layers" value="3"/>
    <metadata key="sparse_infill_density" value="15%"/>
    <metadata key="sparse_infill_pattern" value="gyroid"/>
    <metadata key="inner_wall_speed" value="150"/>
    <metadata key="outer_wall_line_width" value="0.42"/>
    <metadata key="enable_support" value="1"/>
    <metadata key="support_type" value="tree(auto)"/>
    <metadata key="support_threshold_angle" value="40"/>
    <metadata key="support_object_xy_distance" value="0.4"/>
    <metadata key="brim_type" value="outer_only"/>
    <metadata key="seam_position" value="back"/>
    <metadata key="skeleton_infill_density" value="25%"/>
    <metadata key="made_up_setting" value="1"/>
    <part id="1" subtype="normal_part">
      <metadata key="name" value="cube"/>
      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>
      <metadata key="source_file" value="cube.stl"/>
    </part>
  </object>
  <plate>
    <metadata key="plater_id" value="1"/>
    <metadata key="plater_name" value=""/>
    <model_instance>
      <metadata key="object_id" value="2"/>
      <metadata key="instance_id" value="0"/>
    </model_instance>
  </plate>
</config>
"""

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>
"""

RELS = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Target="/3D/3dmodel.model" Id="rel0"
                Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    with zipfile.ZipFile(argv[1], "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", CONTENT_TYPES)
        archive.writestr("_rels/.rels", RELS)
        archive.writestr("3D/3dmodel.model", CUBE)
        archive.writestr("Metadata/model_settings.config", SETTINGS)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
