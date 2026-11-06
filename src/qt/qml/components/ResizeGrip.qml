// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Draggable corner for a Popup. QML popups have no native resize, so this sits in
// the dialog's content and writes to the popup's own width/height: assigning to a
// bound property breaks the binding, which is exactly right — from the first drag
// on the size is the user's, not the layout's. Clamped to the overlay so a dialog
// can never be dragged off the window.
//
// Handlers rather than a MouseArea: inside a modal popup the pointer handlers are
// the reliable path, and this way hover, cursor and the drag itself are three
// declarative pieces instead of one piece of event plumbing.
Item {
    id: root

    // A Popup is a QObject, not an Item, so this cannot be typed `Item`.
    property var dialog

    property real minWidth: 420
    property real minHeight: 260
    property real margin: Metrics.md

    readonly property bool ready: root.dialog !== null && root.dialog !== undefined
    readonly property real startWidth: root.ready ? root.dialog.width : 0
    readonly property real startHeight: root.ready ? root.dialog.height : 0

    implicitWidth: 18
    implicitHeight: 18
    z: 1000

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Metrics.xxs
        anchors.bottomMargin: Metrics.xxs
        spacing: 2

        Repeater {
            model: 3

            delegate: Rectangle {
                required property int index

                width: Metrics.xs
                height: 1
                y: (2 - index) * Metrics.xxs
                color: Colors.textTertiary
                opacity: 0.45 + index * 0.2
            }
        }
    }

    // `preventStealing` is what makes this work at all: without it the popup takes
    // the pointer after the press, and the drag never reaches us.
    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true

        property real fromWidth: 0
        property real fromHeight: 0
        property real pressX: 0
        property real pressY: 0

        onPressed: (mouse) => {
            area.fromWidth = root.startWidth
            area.fromHeight = root.startHeight
            area.pressX = mouse.x
            area.pressY = mouse.y
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !root.ready)
                return
            const bounds = root.dialog.parent
            const maxWidth = bounds ? bounds.width - root.margin : Infinity
            const maxHeight = bounds ? bounds.height - root.margin : Infinity
            root.dialog.width = Math.max(root.minWidth,
                                         Math.min(maxWidth, area.fromWidth + mouse.x - area.pressX))
            root.dialog.height = Math.max(root.minHeight,
                                          Math.min(maxHeight, area.fromHeight + mouse.y - area.pressY))
        }
    }

    HoverHandler {
        cursorShape: Qt.SizeFDiagCursor
    }
}
