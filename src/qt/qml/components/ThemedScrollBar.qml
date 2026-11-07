// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"

// Scrollbar that belongs to this theme. The stock control takes its colours from
// the platform style, and under qt6ct that produced a bar that was invisible on
// this background -- a long settings page looked like it simply ended. Handle
// only, no track: the thin line appearing at the edge is the affordance, and a
// groove behind it would cut the panel in two.
ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded
    width: Metrics.xs + Metrics.xxs
    minimumSize: 0.08
    padding: 0

    contentItem: Rectangle {
        implicitWidth: root.width
        radius: width / 2
        color: root.pressed ? Colors.accent
             : root.hovered ? Colors.textTertiary
             : Colors.separator
        opacity: root.policy === ScrollBar.AlwaysOff ? 0 : 1

        Behavior on color { ColorAnimation { duration: 100 } }
    }

    background: Item {}
}
