// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Vertical rail entry: one glyph, at the rail's full width, centred. Active
// reads as accent glyph on a selectedFill wash with an accent edge marker.
Item {
    id: root

    property string glyph: ""
    property bool active: false
    signal clicked()

    implicitWidth: Metrics.railWidth
    implicitHeight: Metrics.railWidth
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        color: root.active ? Colors.selectedFill
             : area.containsMouse ? Colors.hoverFill
             : "transparent"
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Rectangle {
        visible: root.active
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Metrics.xxs
        color: Colors.accent
    }

    Text {
        anchors.centerIn: parent
        text: root.glyph
        font.family: Type.family
        font.pixelSize: Type.iconStandalone
        color: root.active ? Colors.accent
             : area.containsMouse ? Colors.textPrimary
             : Colors.textTertiary
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
