#!/usr/bin/env python3
"""Write a two-plate project 3MF for the plate-assignment gate.

The cube is written twice: both objects are identical and sit at the same local
origin, so an importer that ignored the project's plate records would produce one
plate holding two objects and the gate could not tell the difference from the
model alone. Nothing else in the application reads this file.
"""

import sys
import zipfile

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>
"""

RELS = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""

MODEL = """<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US"
       xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
 <resources>
  <object id="1" type="model">
   <mesh>
    <vertices>%(vertices)s</vertices>
    <triangles>%(triangles)s</triangles>
   </mesh>
  </object>
  <object id="2" type="model">
   <mesh>
    <vertices>%(vertices)s</vertices>
    <triangles>%(triangles)s</triangles>
   </mesh>
  </object>
 </resources>
 <build>
  <item objectid="1" transform="1 0 0 0 1 0 0 0 1 60 60 0"/>
  <item objectid="2" transform="1 0 0 0 1 0 0 0 1 600 600 0"/>
 </build>
</model>
"""

SETTINGS = """<?xml version="1.0" encoding="UTF-8"?>
<config>
 <plate>
  <metadata key="plater_id" value="1"/>
  <metadata key="plater_name" value=""/>
  <model_instance>
   <metadata key="object_id" value="1"/>
   <metadata key="instance_id" value="0"/>
  </model_instance>
 </plate>
 <plate>
  <metadata key="plater_id" value="2"/>
  <metadata key="plater_name" value=""/>
  <model_instance>
   <metadata key="object_id" value="2"/>
   <metadata key="instance_id" value="0"/>
  </model_instance>
 </plate>
</config>
"""


def cube(origin, size):
    x, y, z = origin
    corners = [
        (x, y, z), (x + size, y, z), (x + size, y + size, z), (x, y + size, z),
        (x, y, z + size), (x + size, y, z + size), (x + size, y + size, z + size),
        (x, y + size, z + size),
    ]
    vertices = "".join(
        '<vertex x="%.4f" y="%.4f" z="%.4f"/>' % corner for corner in corners
    )
    faces = [
        (1, 3, 2), (1, 4, 3), (5, 6, 7), (5, 7, 8),
        (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6),
        (3, 4, 8), (3, 8, 7), (4, 1, 5), (4, 5, 8),
    ]
    triangles = "".join(
        '<triangle v1="%d" v2="%d" v3="%d"/>' % (a - 1, b - 1, c - 1) for a, b, c in faces
    )
    return vertices, triangles


def main():
    if len(sys.argv) != 2:
        print("usage: make.py OUT.3mf", file=sys.stderr)
        return 2

    vertices, triangles = cube((0.0, 0.0, 0.0), 20.0)
    with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", CONTENT_TYPES)
        archive.writestr("_rels/.rels", RELS)
        archive.writestr("3D/3dmodel.model",
                         MODEL % {"vertices": vertices, "triangles": triangles})
        archive.writestr("Metadata/model_settings.config", SETTINGS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
