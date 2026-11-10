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
// It is drawn in the window's overlay rather than opened as a popup window: a
// popup that sits over a control swallows the press meant for it, so hovering a
// settings row and then clicking the field under the hint did nothing until the
// hint was dismissed. An item that is not enabled takes no input, so the press
// goes to the control beneath it, which is what a tooltip is for.
Item {
    id: root

    property Item target: null
    property string text: ""
    property bool shown: false
    property int delay: 450

    readonly property real maxWidth: 320

    parent: Overlay.overlay
    width: Math.min(root.maxWidth, tipLabel.implicitWidth + Metrics.md * 2)
    height: tipLabel.implicitHeight + Metrics.sm * 2
    enabled: false
    visible: false
    z: 1e6

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

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: Colors.surfaceContainerHighest
        border.width: Metrics.borderWidth
        border.color: Colors.edgeStrong
    }

    Text {
        id: tipLabel

        x: Metrics.md
        y: Metrics.sm
        width: root.width - Metrics.md * 2
        text: root.text
        wrapMode: Text.WordWrap
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        color: Colors.textPrimary
    }

    Timer {
        id: showTimer
        interval: root.delay
        onTriggered: {
            if (!root.shown || root.text.length === 0 || root.target === null)
                return
            root.place()
            root.visible = true
        }
    }

    onShownChanged: {
        if (root.shown) {
            showTimer.restart()
        } else {
            showTimer.stop()
            root.visible = false
        }
    }

    onVisibleChanged: if (!root.visible) showTimer.stop()
}
