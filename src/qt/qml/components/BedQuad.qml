// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick3D

// A rectangle standing in the bed plane, textured from a Qt Quick item.
//
// The plate chrome is text and glyphs, so it only exists as a Qt Quick item, and
// it has to be hidden by whatever stands in front of it the way the reference
// hides its own labels: with depth. An item drawn in the 2D scene is composited
// over the 3D content whatever the depth says, which is why the plate's buttons
// used to read straight through a part standing on the plate. A quad inside the
// scene is depth tested against the model for free, and one placed in the bed
// plane projects exactly where the 2D decal was, so the chrome keeps its look and
// loses the see-through.
//
// Coordinates are printer millimetres, so this belongs inside a node that is in
// printer space. `#Rectangle` is 100 units across, hence the scale.
Model {
    id: bedQuad

    /// The item whose rendering becomes this quad's texture. A null item hides
    /// the quad, which is how a decal behind the camera or off the plate
    /// disappears.
    property Item sourceItem: null
    property real x0: 0
    property real y0: 0
    property real x1: 0
    property real y1: 0
    /// Height above the bed. The chrome rides a fraction of a millimetre up so it
    /// wins the depth test against the plate top rather than z-fighting with it.
    property real lift: 0.25

    source: "#Rectangle"
    visible: bedQuad.sourceItem !== null
    position: Qt.vector3d((bedQuad.x0 + bedQuad.x1) / 2, (bedQuad.y0 + bedQuad.y1) / 2, bedQuad.lift)
    scale: Qt.vector3d(Math.max(0.0001, (bedQuad.x1 - bedQuad.x0) / 100),
                       Math.max(0.0001, (bedQuad.y1 - bedQuad.y0) / 100), 1.0)

    materials: PrincipledMaterial {
        lighting: PrincipledMaterial.NoLighting
        alphaMode: PrincipledMaterial.Blend
        cullMode: Material.NoCulling
        baseColorMap: Texture {
            // No flip on either axis: the built-in rectangle's UVs already run with the
            // item's own axes, u to printer +x and v to printer +y, so the plate's name
            // reads left to right with the bed seen from the front. A flip on one axis
            // is a mirror and on both a half turn -- invisible on a symmetric glyph,
            // which is how the name stayed upside down.
            sourceItem: bedQuad.sourceItem
        }
    }
}
