// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"

// Tooltip that belongs to this theme. Qt's attached tooltip takes its colours
// from the platform style, and under qt6ct that is a white box with dark text on
// a dark window -- the one piece of chrome in the app that did not match. It also
// centres itself on its parent, so a long hint slid half way off the window.
//
// This one is placed over its target, clamped inside the overlay, and wraps.
Popup {
    id: root

    property Item target: null
    property string text: ""
    property bool shown: false
    property int delay: 450

    readonly property real maxWidth: 320

    parent: Overlay.overlay
    width: Math.min(root.maxWidth, tipLabel.implicitWidth + Metrics.md * 2)
    height: tipLabel.implicitHeight + Metrics.sm * 2
    padding: 0
    closePolicy: Popup.NoAutoClose
    visible: false

    function place() {
        if (root.target === null)
            return
        const bounds = root.parent
        if (!bounds)
            return
        const point = root.target.mapToItem(bounds, 0, 0)
        const left = point.x + (root.target.width - root.width) / 2
        const above = point.y - root.height - Metrics.sm
        root.x = Math.max(Metrics.xs, Math.min(bounds.width - root.width - Metrics.xs, left))
        root.y = above >= Metrics.xs
                 ? above
                 : Math.min(bounds.height - root.height - Metrics.xs,
                            point.y + root.target.height + Metrics.sm)
    }

    contentItem: Text {
        id: tipLabel
        width: root.width - Metrics.md * 2
        text: root.text
        wrapMode: Text.WordWrap
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        color: Colors.textPrimary
    }

    background: Rectangle {
        radius: Metrics.radiusSmall
        color: Colors.surfaceContainerHighest
        border.width: Metrics.borderWidth
        border.color: Colors.edgeStrong
    }

    Timer {
        id: showTimer
        interval: root.delay
        onTriggered: {
            if (!root.shown || root.text.length === 0 || root.target === null)
                return
            root.place()
            root.open()
        }
    }

    onShownChanged: {
        if (root.shown) {
            showTimer.restart()
        } else {
            showTimer.stop()
            root.close()
        }
    }
}
