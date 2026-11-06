// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Switch. Caller-driven: renders `checked` and emits `toggled(!checked)` on a
// click, so a binding to the settings model is never overwritten. Knob travel
// is animated 140 ms OutCubic.
Item {
    id: root

    property bool checked: false
    signal toggled(bool value)

    implicitWidth: 44
    implicitHeight: 24
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Colors.alpha(Colors.accent, 0.28)
             : area.containsMouse ? Colors.hoverFill
             : Colors.selectedFill
        border.width: Metrics.borderWidth
        border.color: root.checked ? Colors.accent
             : area.containsMouse ? Colors.edgeStrong
             : Colors.edge
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }
    }

    Rectangle {
        id: knob
        width: parent.height - Metrics.sm
        height: width
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? parent.width - width - Metrics.xxs - Metrics.hairline
                        : Metrics.xxs + Metrics.hairline
        color: root.checked ? Colors.accent : Colors.textSecondary
        scale: area.containsMouse ? 1.08 : 1.0
        Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
