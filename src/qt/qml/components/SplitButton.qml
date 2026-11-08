// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// One pill with two actions: the label runs the common case, the chevron opens
// the menu that holds the rest. The chevron is part of the pill rather than a
// second button beside it, so a strip with two actions per control still reads
// as two controls.
Item {
    id: root

    property string text: ""
    property string hint: ""
    property string menuHint: ""
    property bool primary: false
    signal clicked()
    /// The chevron was pressed, with the point under its bottom-right corner in
    /// window coordinates, so the menu lands on the chevron rather than on the
    /// strip's corner.
    signal menuRequested(real x, real y)

    readonly property bool pressed: labelMouse.pressed || chevronMouse.pressed
    readonly property bool hovered: labelMouse.containsMouse || chevronMouse.containsMouse

    implicitWidth: Math.max(Metrics.controlHeight * 2,
                            label.implicitWidth + Metrics.controlPaddingX * 2)
                + chevron.width
    implicitHeight: Metrics.controlHeight
    opacity: enabled ? 1.0 : 0.45

    scale: root.pressed && enabled ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: !root.enabled ? "transparent"
             : root.pressed ? Colors.pressedFill
             : root.hovered ? Colors.hoverFill
             : root.primary ? Colors.selectedFill
             : "transparent"
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: "transparent"
        border.width: Metrics.borderWidth
        border.color: !root.enabled ? Colors.alpha(Colors.outlineVariant, 0.5)
             : root.primary ? Colors.alpha(Colors.accent, root.hovered ? 0.8 : 0.4)
             : (root.hovered || root.pressed) ? Colors.edgeStrong
             : Colors.edge
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Metrics.controlPaddingX
        anchors.right: chevron.left
        anchors.rightMargin: Metrics.xs
        text: root.text
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        font.bold: true
        font.letterSpacing: 0.8
        color: !root.enabled ? Colors.textMuted
             : root.primary ? Colors.accent
             : Colors.textPrimary
    }

    Rectangle {
        id: rule
        anchors.right: chevron.left
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.borderWidth
        height: parent.height * 0.4
        color: Colors.outlineVariant
    }

    Item {
        id: chevron
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Metrics.md + Metrics.sm

        Icon {
            anchors.centerIn: parent
            name: "expand_more"
            size: Type.sizeCaption
            color: chevronMouse.containsMouse && root.enabled ? Colors.textPrimary
                                                              : Colors.textSecondary
        }

        MouseArea {
            id: chevronMouse
            anchors.fill: parent
            enabled: root.enabled
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                const point = chevron.mapToItem(null, chevron.width, chevron.height)
                root.menuRequested(point.x, point.y)
            }
        }
    }

    MouseArea {
        id: labelMouse
        anchors.left: parent.left
        anchors.right: rule.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        enabled: root.enabled
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Tooltip {
        target: root
        text: root.hint
        shown: labelMouse.containsMouse && root.enabled && root.hint !== ""
    }

    Tooltip {
        target: chevron
        text: root.menuHint
        shown: chevronMouse.containsMouse && root.enabled && root.menuHint !== ""
    }
}
