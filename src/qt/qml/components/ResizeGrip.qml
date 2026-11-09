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

    implicitWidth: Metrics.gripSize + Metrics.xs
    implicitHeight: Metrics.gripSize + Metrics.xs
    z: 1000

    // A solid triangle in the corner, the shape every window manager has used for
    // this since forever, with two ribs inside it so it reads as a grip rather
    // than decoration. Drawn as geometry because a font glyph would be at the mercy
    // of whichever font resolved.
    Item {
        id: grip
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: Metrics.gripSize
        height: Metrics.gripSize

        readonly property color tint: area.containsMouse || area.pressed
                                      ? Colors.accent : Colors.textTertiary

        Canvas {
            id: canvas
            anchors.fill: parent
            // Quiet until reached for: a dialog usually has a button in this corner,
            // and a permanent bright wedge next to it reads as clutter.
            opacity: area.pressed ? 0.95 : (area.containsMouse ? 0.8 : 0.28)

            onPaint: {
                const context = getContext("2d")
                context.reset()
                const size = Math.min(width, height)

                context.beginPath()
                context.moveTo(size, 0)
                context.lineTo(size, size)
                context.lineTo(0, size)
                context.closePath()
                context.fillStyle = grip.tint
                context.fill()

                // Two ribs: the same triangle, cut out of the fill.
                context.strokeStyle = Colors.surfaceContainer
                context.lineWidth = 1.6
                for (let step = 1; step <= 2; ++step) {
                    const offset = step * (size / 3.2)
                    context.beginPath()
                    context.moveTo(size - offset, size)
                    context.lineTo(size, size - offset)
                    context.stroke()
                }
            }

            Connections {
                target: grip
                function onTintChanged() { canvas.requestPaint() }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: -Metrics.xs
            anchors.bottomMargin: -Metrics.xs
            width: Metrics.xxs
            height: Metrics.xxs
            radius: width / 2
            color: grip.tint
            opacity: 0.8
        }
    }

    // `preventStealing` is what makes this work at all: without it the popup takes
    // the pointer after the press, and the drag never reaches us.
    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true
        hoverEnabled: true

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
