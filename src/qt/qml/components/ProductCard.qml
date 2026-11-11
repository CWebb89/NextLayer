// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// A sibling project on the home page: a glyph, its name, what it does in one
// sentence, and the two or three facts that place it. Static by design -- nothing
// here is a button, because a home page that offers a link it cannot open is worse
// than one that only says what exists.
Item {
    id: root

    property string icon: ""
    property string name: ""
    property string tagline: ""
    property var tags: []

    implicitHeight: content.implicitHeight + Metrics.lg * 2

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusMedium
        color: Colors.surfaceContainer
        border.width: Metrics.borderWidth
        border.color: Colors.edge
    }

    Column {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.lg
        spacing: Metrics.sm

        Row {
            width: parent.width
            spacing: Metrics.md

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Metrics.controlHeight - Metrics.md
                height: width
                radius: Metrics.radiusSmall
                color: Colors.alpha(Colors.accent, 0.14)

                Icon {
                    anchors.centerIn: parent
                    name: root.icon
                    size: Type.iconLead
                    color: Colors.accent
                }
            }

            Text {
                width: parent.width - Metrics.controlHeight - Metrics.md
                anchors.verticalCenter: parent.verticalCenter
                text: root.name
                elide: Text.ElideRight
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                font.bold: true
                color: Colors.textPrimary
            }
        }

        Text {
            width: parent.width
            text: root.tagline
            wrapMode: Text.WordWrap
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            lineHeight: 1.15
            color: Colors.textSecondary
        }

        Flow {
            width: parent.width
            spacing: Metrics.xs

            Repeater {
                model: root.tags

                delegate: Rectangle {
                    id: tag

                    required property string modelData

                    width: tagText.implicitWidth + Metrics.md * 2
                    height: tagText.implicitHeight + Metrics.xxs * 3
                    radius: height / 2
                    color: Colors.surfaceContainerHigh

                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: tag.modelData
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }
                }
            }
        }
    }
}
