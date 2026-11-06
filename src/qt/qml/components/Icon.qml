// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Material Symbols glyph. The family is installed system-wide and its ligature
// table maps the name to the glyph, so `name` is the Material name verbatim.
Text {
    id: root

    property string name: ""
    property int size: Type.iconLead
    property bool filled: false

    text: root.name
    font.family: "Material Symbols Outlined"
    font.pixelSize: root.size
    font.weight: root.filled ? Font.Medium : Font.Normal
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
    renderType: Text.NativeRendering
    color: Colors.textSecondary
}
