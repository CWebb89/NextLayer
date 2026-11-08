#!/usr/bin/env python3
"""Write the project files a slicer has to lay out exactly as their author saved them.

A project is more than a pile of meshes. Its objects sit at coordinates its author
chose, and a project with several plates keeps them in one space, plates hundreds of
millimetres apart, with a record naming the plate each object belongs to. A reader
that centres every object on itself loses both, and the plate comes back as a single
point with every part of the project stacked on it.

    python3 tools/project-fixtures.py --out /tmp/nl-project

The files are generated rather than committed so the layout being tested is readable
here, and so the numbers in the gate and the numbers in the file cannot drift apart.
"""

import argparse
import struct
import zipfile
from pathlib import Path

CUBE_MM = 20.0

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
 <Default Extension="config" ContentType="text/xml"/>
</Types>
"""

RELS = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel0"
   Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""


def cube(identifier, size=CUBE_MM):
    half = size / 2.0
    corners = [(x, y, z)
               for z in (0.0, size)
               for y in (-half, half)
               for x in (-half, half)]
    faces = [(0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6),
             (0, 1, 4), (1, 5, 4), (2, 6, 3), (3, 6, 7),
             (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5)]
    vertices = "\n".join(
        '        <vertex x="%.4f" y="%.4f" z="%.4f"/>' % corner for corner in corners)
    triangles = "\n".join(
        "        <triangle v1=\"%d\" v2=\"%d\" v3=\"%d\"/>" % face for face in faces)
    return f"""   <object id="{identifier}" type="model">
    <mesh>
     <vertices>
{vertices}
     </vertices>
     <triangles>
{triangles}
     </triangles>
    </mesh>
   </object>
"""


def scene(objects):
    """`objects` is a list of (id, x, y, z), each placed by its build item."""
    resources = "".join(cube(identifier) for identifier, _, _, _ in objects)
    items = "\n".join(
        '   <item objectid="%d" transform="1 0 0 0 1 0 0 0 1 %.4f %.4f %.4f" printable="1"/>'
        % (identifier, x, y, z) for identifier, x, y, z in objects)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US"
 xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
 <resources>
{resources} </resources>
 <build>
{items}
 </build>
</model>
"""


def settings(plates, object_plates):
    """`plates` maps a plater id to the object ids on it."""
    parts = []
    for identifier, plate in object_plates:
        parts.append(f"""  <object id="{identifier}">
    <metadata key="name" value="part {identifier}"/>
    <metadata key="plater_id" value="{plate}"/>
   </object>
""")
    for plate in sorted(plates):
        instances = "\n".join(
            f"""    <model_instance>
     <metadata key="object_id" value="{identifier}"/>
     <metadata key="instance_id" value="0"/>
     <metadata key="identify_id" value="{identifier}"/>
    </model_instance>"""
            for identifier in plates[plate])
        parts.append(f"""  <plate>
    <metadata key="plater_id" value="{plate}"/>
    <metadata key="plater_name" value=""/>
    <metadata key="locked" value="false"/>
{instances}
   </plate>
""")
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<config>\n" + "".join(parts) + "</config>\n"


def write_project(path, objects, plate_of, plates):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", CONTENT_TYPES)
        archive.writestr("_rels/.rels", RELS)
        archive.writestr("3D/3dmodel.model", scene(objects))
        archive.writestr("Metadata/model_settings.config", settings(plates, plate_of))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="/tmp/nl-project", help="directory to write into")
    arguments = parser.parse_args()
    out = Path(arguments.out)

    two_plates = [(1, 60.0, 60.0, 0.0), (2, 100.0, 60.0, 0.0),
                  (3, 460.0, 60.0, 0.0), (4, 540.0, 60.0, 0.0)]
    write_project(out / "two-plates.3mf", two_plates,
                  plate_of=[(1, 1), (2, 1), (3, 2), (4, 2)],
                  plates={1: [1, 2], 2: [3, 4]})

    one_plate = [(1, 40.0, 40.0, 0.0), (2, 90.0, 40.0, 0.0), (3, 140.0, 40.0, 0.0)]
    write_project(out / "one-plate.3mf", one_plate,
                  plate_of=[(1, 1), (2, 1), (3, 1)],
                  plates={1: [1, 2, 3]})

    stacked = [(1, 50.0, 50.0, 0.0), (2, 50.0, 50.0, 25.0)]
    write_project(out / "stacked.3mf", stacked,
                  plate_of=[(1, 1), (2, 1)],
                  plates={1: [1, 2]})

    print(f"wrote {len(list(out.glob('*.3mf')))} project file(s) to {out}")


if __name__ == "__main__":
    main()
