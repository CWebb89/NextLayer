// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// What is selected, in the viewport's own corner: its name, its measured size and
// where it stands. The object list carries the same numbers, but it is a drawer that
// is usually shut while the part is being placed, and "what am I looking at" is a
// question asked of the plate, not of a list.
Item {
    id: root

    /// One entry of `plateDocument.objects`.
    property var object: null
    property int selectedCount: 0

    signal cleared()

    width: 300
    height: metrics.implicitHeight + Metrics.lg * 2
    visible: root.object !== null

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusMedium
        color: Colors.alpha(Colors.surfaceContainer, 0.94)
        border.width: Metrics.borderWidth
        border.color: Colors.edge
    }

    Column {
        id: metrics

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Metrics.xl
        anchors.rightMargin: Metrics.xl
        anchors.topMargin: Metrics.lg
        spacing: Metrics.xs

        Row {
            width: parent.width
            spacing: Metrics.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "deployed_code"
                size: Type.iconLead
                color: Colors.accent
            }

            Text {
                width: parent.width - Type.iconLead - badge.width - Metrics.sm * 2
                anchors.verticalCenter: parent.verticalCenter
                text: root.object !== null ? root.object.name : ""
                elide: Text.ElideMiddle
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                color: Colors.textPrimary
            }

            Rectangle {
                id: badge

                anchors.verticalCenter: parent.verticalCenter
                visible: root.selectedCount > 1
                width: visible ? badgeText.implicitWidth + Metrics.md : 0
                height: Metrics.rowHeight - Metrics.xl
                radius: height / 2
                color: Colors.alpha(Colors.accent, 0.18)

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: root.selectedCount + " selected"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.accent
                }
            }
        }

        Text {
            width: parent.width
            text: root.object === null ? ""
                  : root.object.width.toFixed(1) + " × " + root.object.depth.toFixed(1)
                    + " × " + root.object.height.toFixed(1) + " mm"
            elide: Text.ElideRight
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textSecondary
        }

        Text {
            width: parent.width
            text: root.object === null ? ""
                  : "x " + root.object.x.toFixed(1) + "   y " + root.object.y.toFixed(1)
                    + "   z " + root.object.z.toFixed(1)
            elide: Text.ElideRight
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textMuted
        }
    }
}
