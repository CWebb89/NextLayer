// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "../theme"
import "../components"

// The alpha notice, shown once and dismissed for good. An alpha binary goes onto
// printers nobody here has seen, and the G-code has been printed on one machine:
// the one thing a new user has to be told before the first print is to watch the
// first layer, not after it lifts off the bed.
Item {
    id: root

    property string version: ""

    signal dismissed()

    Settings {
        id: saved
        category: "alphaNotice"
        property bool dismissed: false
    }

    readonly property bool seen: saved.dismissed

    function close() {
        saved.dismissed = true
        root.dismissed()
    }

    visible: false
    z: 130

    Rectangle {
        anchors.fill: parent
        color: Colors.scrim
        opacity: 0.6
    }

    Card {
        anchors.centerIn: parent
        width: Math.min(root.width - Metrics.panelMargin * 4, 560)
        height: bodyTop + lines.implicitHeight + Metrics.panelPadding
        title: "Alpha " + root.version

        Column {
            id: lines
            width: parent.width
            spacing: Metrics.md

            Text {
                width: lines.width
                wrapMode: Text.WordWrap
                text: "This build slices and prints, but its G-code has been printed on one machine so far. Before you trust it with a long job:"
                color: Colors.textPrimary
                font.pixelSize: Type.sizeBody
            }

            Repeater {
                model: [
                    "Watch the whole first layer before you leave the printer.",
                    "Check that the printer, process and filament are the ones you meant — nothing here knows your machine until you tell it.",
                    "Keep a hand near the power switch for the first print with any new model or profile."
                ]

                Text {
                    required property string modelData
                    width: lines.width
                    wrapMode: Text.WordWrap
                    text: "· " + modelData
                    color: Colors.textSecondary
                    font.pixelSize: Type.sizeBody
                }
            }

            Text {
                width: lines.width
                wrapMode: Text.WordWrap
                text: "Prints that worked, and the ones that did not, are recorded in docs/real-prints.md in the source tree — that record is how the next person knows what to expect."
                color: Colors.textSecondary
                font.pixelSize: Type.sizeSmall
            }

            RowLayout {
                width: lines.width

                Item { Layout.fillWidth: true }

                Button {
                    text: "I understand"
                    primary: true
                    onClicked: root.close()
                }
            }
        }
    }

    Keys.onEscapePressed: root.close()
}
