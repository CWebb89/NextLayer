// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// A section heading: the words, then the rule that carries them across the page.
Item {
    id: root

    property string text: ""

    implicitHeight: label.implicitHeight

    Text {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        textFormat: Text.PlainText
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        font.letterSpacing: 1.8
        font.bold: true
        color: Colors.textSecondary
    }

    Rectangle {
        anchors.left: label.right
        anchors.leftMargin: Metrics.lg
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: Metrics.borderWidth
        color: Colors.separator
    }
}
