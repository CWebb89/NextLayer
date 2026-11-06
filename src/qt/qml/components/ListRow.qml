// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// List entry: label left, value right, accent text and a selectedFill wash when
// selected, hoverFill on hover.
Item {
    id: root

    property string label: ""
    property string value: ""
    property bool selected: false
    signal clicked()

    implicitWidth: 240
    implicitHeight: Metrics.rowHeight
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: root.selected ? Colors.selectedFill
             : mouse.containsMouse ? Colors.hoverFill
             : "transparent"
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Rectangle {
        visible: root.selected
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Metrics.xxs
        radius: width / 2
        color: Colors.accent
    }

    Text {
        id: labelText
        anchors.left: parent.left
        anchors.leftMargin: Metrics.md + (root.selected ? Metrics.xxs : 0)
        anchors.right: valueText.visible ? valueText.left : parent.right
        anchors.rightMargin: Metrics.sm
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: root.selected ? Colors.accent : Colors.textPrimary
    }

    Text {
        id: valueText
        visible: root.value !== ""
        anchors.right: parent.right
        anchors.rightMargin: Metrics.md
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        color: root.selected ? Colors.accent : Colors.textTertiary
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
