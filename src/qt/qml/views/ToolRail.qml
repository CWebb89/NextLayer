// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import "../theme"
import "../components"

// The tools, as a column down the left edge of the viewport.
//
// They used to share the strip over the viewport with the plate and object
// actions, which put fourteen tools and ten actions on two rows of icons and left
// nothing to read: the strip is one row for the plate, the actions and the slice
// buttons now, and the tools live here where a person shapes and paints a model.
// Groups are separated, so the column reads as "place it, cut it, decorate it,
// support it, tune it" rather than as one run of glyphs.
Item {
    id: root

    property var groups: []
    property string activeTool: ""

    signal toolTriggered(string id)

    implicitWidth: Metrics.rowHeight + Metrics.sm * 2
    implicitHeight: column.implicitHeight + Metrics.sm * 2

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusMedium
        color: Colors.alpha(Colors.surfaceContainer, 0.92)
        border.width: Metrics.borderWidth
        border.color: Colors.edge
    }

    Column {
        id: column

        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: root.groups

            delegate: Column {
                id: group
                required property var modelData
                required property int index

                spacing: 0

                Repeater {
                    model: group.modelData.tools

                    delegate: ToolButton {
                        required property var modelData

                        icon: modelData.icon
                        hint: modelData.hint
                        active: root.activeTool === modelData.id
                        onClicked: root.toolTriggered(modelData.id)
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: group.modelData.divider && group.index < root.groups.length - 1
                    width: Metrics.rowHeight - Metrics.md
                    height: Metrics.borderWidth
                    color: Colors.outlineVariant
                }
            }
        }
    }
}
