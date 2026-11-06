// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"

// Labelled settings row: caption-bold label on the textSecondary rung, the editor
// supplied by the caller in the default property stretched to the field width,
// and the engine's own tooltip text on hover rather than inline.
Column {
    id: root

    property string label: ""
    property string tooltip: ""
    property real editorHeight: Metrics.controlHeight
    default property alias editor: slot.data

    spacing: Metrics.xs
    opacity: enabled ? 1.0 : 0.45

    Text {
        id: labelText
        width: parent.width
        visible: root.label !== ""
        text: root.label
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        font.bold: true
        color: Colors.textPrimary
    }

    Item {
        id: slot
        width: parent.width
        height: root.editorHeight
    }

    HoverHandler {
        id: hover
    }

    ToolTip.visible: hover.hovered && root.tooltip !== ""
    ToolTip.text: root.tooltip
    ToolTip.delay: 500
    ToolTip.timeout: 20000
}
