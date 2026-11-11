// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// What this program is, in one breath: an eyebrow, the sentence that names it, the
// paragraph that qualifies it, and the promises that make it credible. Sized from
// its own width so it reads the same in the home aside and in the narrow column.
Column {
    id: root

    property string eyebrow: ""
    property string headline: ""
    property string body: ""
    property var chips: []

    readonly property int measure: Math.min(width, 620)
    readonly property int headlineSize: Math.min(46, Math.max(Type.sizeLarge,
                                                             width * 0.046))

    spacing: Metrics.lg

    Text {
        width: parent.width
        text: root.eyebrow
        textFormat: Text.PlainText
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        font.letterSpacing: 2.4
        color: Colors.accent
    }

    Text {
        width: parent.width
        text: root.headline
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.family: Type.family
        font.pixelSize: root.headlineSize
        font.bold: true
        color: Colors.textPrimary
    }

    Text {
        width: root.measure
        text: root.body
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        lineHeight: 1.34
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: Colors.textSecondary
    }

    Flow {
        width: parent.width
        spacing: Metrics.sm

        Repeater {
            model: root.chips

            delegate: Rectangle {
                id: chip

                required property string modelData

                width: chipText.implicitWidth + Metrics.lg * 2
                height: chipText.implicitHeight + Metrics.sm
                radius: height / 2
                color: Colors.alpha(Colors.accent, 0.10)
                border.width: Metrics.borderWidth
                border.color: Colors.alpha(Colors.accent, 0.28)

                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.modelData
                    textFormat: Text.PlainText
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.accent
                }
            }
        }
    }
}
