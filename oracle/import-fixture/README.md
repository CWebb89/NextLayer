# Import fixture

A stand-in for another slicer's configuration directory, used by the
`preset import` gate in `tools/check`. It is deliberately small: one machine,
one process and one filament, in the `user/default/<kind>/*.json` layout the
importer reads, with the source key names it translates from.

It is not a real printer profile and nothing slices with it by hand. The gate
imports it into a scratch data directory, then resolves and slices the three
imported presets, so the check covers the whole path: read another slicer's
JSON, translate the keys this engine has, write a bundle the engine loads, and
slice from what was written.

The process carries the settings whose translation is easy to get subtly wrong,
and the gate asserts on the toolpaths rather than the JSON: arc fitting (arc
moves in the G-code), tree supports (support material under the bridge model's
overhangs), scarf seams and a three-perimeter wall. A key that arrives with the
right name and the wrong shape produces no toolpath, which is what these
assertions catch.
