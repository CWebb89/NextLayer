// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Checkbox. Caller-driven: renders `checked` and emits `toggled(!checked)` on a
// click, so a binding to the settings model is never overwritten. A square box
// with a check mark, because a settings row that reads "on / off" is a state to
// read, not a slider to drag.
Item {
    id: root

    property bool checked: false
    property bool hovered: area.containsMouse
    signal toggled(bool value)

    implicitWidth: 18
    implicitHeight: 18
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: root.checked ? Colors.accent
             : area.containsMouse ? Colors.hoverFill
             : Colors.surfaceContainer
        border.width: Metrics.borderWidth
        border.color: root.checked ? Colors.accent
             : area.containsMouse ? Colors.edgeStrong
             : Colors.edge
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    Icon {
        anchors.centerIn: parent
        name: "check"
        size: 14
        color: Colors.surfaceContainerLowest
        visible: root.checked
    }

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -Metrics.xs
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
