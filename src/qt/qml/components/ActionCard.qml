// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// A full-width row that offers one way into the app: a glyph tile, a title with a
// line of explanation, and a chevron. The accent variant marks the one action a
// first run is meant to take, so the page has a starting point instead of three
// equal buttons.
Item {
    id: root

    property string icon: ""
    property string title: ""
    property string subtitle: ""
    property string hint: ""
    property bool accent: false
    signal clicked()

    readonly property bool hovered: mouse.containsMouse

    implicitHeight: Math.max(Metrics.controlHeight * 1.9, content.height + Metrics.lg * 2)
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusMedium
        color: mouse.pressed ? Colors.pressedFill
             : root.hovered ? Colors.surfaceContainerHigh
             : Colors.surfaceContainer
        border.width: Metrics.borderWidth
        border.color: root.hovered ? Colors.alpha(Colors.accent, 0.75)
                    : root.accent ? Colors.alpha(Colors.accent, 0.35)
                    : Colors.edge
        Behavior on color { ColorAnimation { duration: 100 } }
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    Row {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        spacing: Metrics.lg

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Metrics.controlHeight + Metrics.md
            height: width
            radius: Metrics.radiusSmall
            color: root.accent || root.hovered ? Colors.alpha(Colors.accent, 0.16)
                                               : Colors.surfaceContainerHigh

            Icon {
                anchors.centerIn: parent
                name: root.icon
                size: Type.iconStandalone
                color: root.accent || root.hovered ? Colors.accent : Colors.textTertiary
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Metrics.controlHeight - Metrics.md - Metrics.lg * 4
                   - Type.iconStandalone
            spacing: Metrics.xxs

            Text {
                width: parent.width
                text: root.title
                elide: Text.ElideRight
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                width: parent.width
                text: root.subtitle
                elide: Text.ElideRight
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textMuted
            }
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron_right"
            size: Type.iconLead
            color: root.hovered ? Colors.accent : Colors.textMuted
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Tooltip {
        target: root
        text: root.hint
        shown: mouse.containsMouse && root.hint !== ""
        delay: 700
    }
}
