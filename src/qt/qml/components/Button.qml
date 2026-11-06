// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"

// Action button. Plain: outline edge, textPrimary label. Primary: accent text
// on a selectedFill wash with an accent border. Hover steps the fill to
// hoverFill, press to pressedFill, and the whole control scales down slightly
// so the press reads without moving layout siblings.
Item {
    id: root

    property string text: ""
    property string hint: ""
    property bool primary: false
    signal clicked()

    readonly property bool pressed: mouse.pressed
    readonly property bool hovered: mouse.containsMouse

    implicitWidth: Math.max(Metrics.controlHeight * 2,
                            label.implicitWidth + Metrics.controlPaddingX * 2)
    implicitHeight: Metrics.controlHeight
    opacity: enabled ? 1.0 : 0.45

    scale: mouse.pressed && enabled ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: !enabled ? "transparent"
             : mouse.pressed ? Colors.pressedFill
             : mouse.containsMouse ? Colors.hoverFill
             : root.primary ? Colors.selectedFill
             : "transparent"
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: "transparent"
        border.width: Metrics.borderWidth
        border.color: !enabled ? Colors.alpha(Colors.outlineVariant, 0.5)
             : root.primary ? Colors.alpha(Colors.accent, mouse.containsMouse ? 0.8 : 0.4)
             : (mouse.containsMouse || mouse.pressed) ? Colors.edgeStrong
             : Colors.edge
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        textFormat: Text.PlainText
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        font.bold: true
        font.letterSpacing: 0.8
        color: !enabled ? Colors.textMuted
             : root.primary ? Colors.accent
             : Colors.textPrimary
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ToolTip.visible: mouse.containsMouse && root.hint !== ""
    ToolTip.text: root.hint
    ToolTip.delay: 400
    ToolTip.timeout: 10000
}
