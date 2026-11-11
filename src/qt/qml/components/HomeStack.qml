// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// The other two projects that make up the stack this app is one third of. Wide, the
// two cards sit side by side so the group reads as a set; narrow, they stack.
Column {
    id: root

    property string blurb: ""

    readonly property bool sideBySide: width >= 720
    readonly property int cardHeight: Math.max(klipshell.implicitHeight, layermind.implicitHeight)

    spacing: Metrics.lg

    SectionTitle {
        width: parent.width
        text: "The rest of the stack"
    }

    Text {
        width: Math.min(parent.width, 620)
        text: root.blurb
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        lineHeight: 1.25
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        color: Colors.textSecondary
    }

    Item {
        width: parent.width
        height: root.sideBySide ? root.cardHeight
                                : klipshell.implicitHeight + Metrics.lg + layermind.implicitHeight

        ProductCard {
            id: klipshell

            width: root.sideBySide ? (parent.width - Metrics.lg) / 2 : parent.width
            height: root.sideBySide ? root.cardHeight : implicitHeight
            icon: "dashboard"
            name: "klipshell"
            tagline: "A desktop control surface for Klipper printers: dashboard, console, "
                     + "print files, history, bed mesh, G-code viewer and machine health, "
                     + "in one keyboard-driven window over Moonraker."
            tags: ["Quickshell · QML", "Klipper / Moonraker", "8 views"]
        }

        ProductCard {
            id: layermind

            anchors.top: root.sideBySide ? parent.top : klipshell.bottom
            anchors.topMargin: root.sideBySide ? 0 : Metrics.lg
            anchors.right: parent.right
            width: root.sideBySide ? (parent.width - Metrics.lg) / 2 : parent.width
            height: root.sideBySide ? root.cardHeight : implicitHeight
            icon: "monitor_heart"
            name: "LayerMind"
            tagline: "Sits above Klipper and Moonraker and turns every print into a data "
                     + "point: hardware health over time, failure patterns, and diagnostics "
                     + "that end in a recommendation rather than a chart."
            tags: ["Rust", "252 tests", "AI diagnostics"]
        }
    }
}
