// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Square-cornered grouped section: an accent-washed header band with the title
// centred, an outlineVariant frame shared with the header, and the same
// padding-inset content slot as Card.
Item {
    id: root

    property string title: ""
    default property alias content: body.data

    readonly property int headerHeight: Metrics.controlHeight + Metrics.xs
    readonly property int bodyTop: root.headerHeight + Metrics.md

    implicitWidth: 320
    implicitHeight: 200

    Rectangle {
        anchors.fill: parent
        color: Colors.surfaceContainerLow
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: Metrics.cardBorderWidth
        border.color: hover.hovered ? Colors.edgeStrong : Colors.edge
        Behavior on border.color { ColorAnimation { duration: 140 } }
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.headerHeight

        Rectangle {
            anchors.fill: parent
            color: Colors.alpha(Colors.accent, 0.12)
            border.width: Metrics.cardBorderWidth
            border.color: Colors.alpha(Colors.accent, 0.40)
        }

        Text {
            anchors.fill: parent
            anchors.leftMargin: Metrics.md
            anchors.rightMargin: Metrics.md
            text: root.title
            textFormat: Text.PlainText
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Type.family
            font.pixelSize: Type.sizeSmall
            font.bold: true
            font.letterSpacing: 1.1
            color: Colors.textPrimary
        }
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: Metrics.panelPadding
        anchors.rightMargin: Metrics.panelPadding
        anchors.topMargin: root.bodyTop
        anchors.bottomMargin: Metrics.panelPadding
        clip: true
    }

    HoverHandler {
        id: hover
    }
}
