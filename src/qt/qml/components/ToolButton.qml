// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"

// One entry in the viewport strip: a 40px hit area holding a Material Symbols
// icon, matching the icon-only toolbar. The tooltip carries the label, the
// shortcut, and the reason when the entry is disabled.
Item {
    id: root

    property string icon: ""
    property string label: ""
    property string hint: ""
    property bool active: false
    property bool danger: false
    property int iconSize: Type.iconLead
    signal clicked()

    implicitWidth: Metrics.rowHeight
    implicitHeight: Metrics.rowHeight
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: root.active ? Colors.selectedFill
             : mouse.containsMouse && root.enabled ? Colors.hoverFill : "transparent"
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: root.iconSize
        filled: root.active
        color: root.active ? Colors.accent
             : root.danger && mouse.containsMouse ? Colors.error
             : mouse.containsMouse && root.enabled ? Colors.textPrimary : Colors.textSecondary
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ToolTip.visible: mouse.containsMouse && root.hint !== ""
    ToolTip.text: root.hint
    ToolTip.delay: 400
    ToolTip.timeout: 10000
}
