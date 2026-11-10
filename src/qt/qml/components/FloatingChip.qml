// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// A panel that has been stowed, standing in for it as a square over the window.
// Clicking restores the panel; dragging puts the square wherever it is out of the
// way, so a collapsed panel can be parked in a corner instead of sitting on top of
// the thing it was collapsed to reveal. The chip is parented to the window, not to
// the panel, because the panel is exactly what is not on screen.
Item {
    id: root

    property string icon: ""
    property string label: ""
    property string hint: ""

    signal restoreRequested()

    implicitWidth: Metrics.rowHeight + Metrics.sm * 2
    implicitHeight: implicitWidth
    width: implicitWidth
    height: implicitHeight

    readonly property real dragMinX: 0
    readonly property real dragMinY: 0
    readonly property real dragMaxX: parent ? parent.width - width : 0
    readonly property real dragMaxY: parent ? parent.height - height : 0

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: mouse.containsMouse || mouse.pressed
             ? Colors.surfaceContainerHigh : Colors.surfaceContainer
        border.width: Metrics.borderWidth
        border.color: mouse.containsMouse ? Colors.accent : Colors.edge
        Behavior on color { ColorAnimation { duration: 90 } }

        Icon {
            anchors.centerIn: parent
            name: root.icon
            size: Type.iconLead
            color: mouse.containsMouse ? Colors.accent : Colors.textSecondary
        }
    }

    // Dragging and clicking are the same gesture on one control: the panel is only
    // restored when the press ends where it began, so moving the chip never opens
    // what it stands for. The position is computed from the pointer's window
    // coordinates on every motion rather than accumulated, because the item moves
    // out from under the pointer and a delta would carry that movement along.
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        property real pressSceneX: 0
        property real pressSceneY: 0
        property real pressLocalX: 0
        property real pressLocalY: 0
        property real pressX: 0
        property real pressY: 0

        function scenePoint(event) {
            return mouse.mapToItem(null, event.x, event.y)
        }

        onPressed: (event) => {
            // A declarative `x:` in the window would put the chip back the moment it
            // re-evaluated, so the first press takes ownership of the position.
            root.x = root.x
            root.y = root.y
            const point = scenePoint(event)
            pressSceneX = point.x
            pressSceneY = point.y
            pressLocalX = event.x
            pressLocalY = event.y
            pressX = root.x
            pressY = root.y
        }
        onPositionChanged: (event) => {
            if (!pressed)
                return
            const point = scenePoint(event)
            root.x = Math.max(root.dragMinX,
                              Math.min(root.dragMaxX, pressX + point.x - pressSceneX))
            root.y = Math.max(root.dragMinY,
                              Math.min(root.dragMaxY, pressY + point.y - pressSceneY))
        }
        onReleased: (event) => {
            // Judged in window coordinates: the chip follows the pointer, so the
            // pointer sits at the same place inside it however far it was dragged.
            const point = scenePoint(event)
            if (Math.abs(point.x - pressSceneX) < 4 && Math.abs(point.y - pressSceneY) < 4)
                root.restoreRequested()
        }
    }

    Tooltip {
        target: root
        text: root.hint
        shown: mouse.containsMouse && root.hint !== ""
    }
}
