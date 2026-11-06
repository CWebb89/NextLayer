// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Numeric editor: typed value with a unit suffix and minus/plus steppers.
// Caller-driven: `value` is rendered, never assigned, so a binding to the
// settings model survives; every committed change is clamped to [from, to],
// quantised to `decimals` places, and emitted as `edited(value)`. Edits commit
// on focus loss, Return, or a stepper; Up/Down and the wheel step by the
// smallest representable increment.
Item {
    id: root

    property real value: 0
    property real from: -1e9
    property real to: 1e9
    property string units: root.isLength ? Units.lengthSuffix : ""
    property int decimals: root.isLength ? Units.lengthDecimals : 2
    /// `value` is always millimetres; the field shows the user's system.
    property bool isLength: false
    signal edited(real value)

    readonly property real shownValue: root.isLength ? Units.fromMillimetres(root.value) : root.value
    readonly property real shownFrom: root.isLength ? Units.fromMillimetres(root.from) : root.from
    readonly property real shownTo: root.isLength ? Units.fromMillimetres(root.to) : root.to
    readonly property real perShownUnit: root.isLength ? Units.toMillimetres(1) : 1

    readonly property real increment: Math.pow(10, -Math.max(0, root.decimals))

    implicitWidth: 200
    implicitHeight: Metrics.controlHeight
    opacity: enabled ? 1.0 : 0.45

    function format(v) {
        return Number(v).toFixed(Math.max(0, root.decimals))
    }

    function quantize(v) {
        var scale = Math.pow(10, Math.max(0, root.decimals))
        return Math.round(v * scale) / scale
    }

    function commit() {
        var parsed = parseFloat(input.text)
        if (isNaN(parsed))
            parsed = root.shownValue
        const next = root.quantize(Math.max(root.shownFrom, Math.min(root.shownTo, parsed)))
        input.text = root.format(next)
        const millimetres = next * root.perShownUnit
        if (millimetres !== root.value)
            root.edited(millimetres)
    }

    function bump(direction) {
        const next = root.quantize(Math.max(root.shownFrom,
            Math.min(root.shownTo, root.shownValue + direction * root.increment)))
        input.text = root.format(next)
        const millimetres = next * root.perShownUnit
        if (millimetres !== root.value)
            root.edited(millimetres)
    }

    onValueChanged: if (!input.activeFocus) input.text = root.format(root.shownValue)
    onShownValueChanged: if (!input.activeFocus) input.text = root.format(root.shownValue)

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: Colors.surfaceContainerHigh
        border.width: Metrics.borderWidth
        border.color: input.activeFocus ? Colors.edgeStrong : Colors.edge
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    Text {
        id: minus
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Metrics.xxl
        text: "−"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: Type.family
        font.pixelSize: Type.sizeBody
        font.bold: true
        color: minusMouse.containsMouse ? Colors.accent : Colors.textSecondary

        MouseArea {
            id: minusMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.bump(-1)
        }
    }

    Text {
        id: plus
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Metrics.xxl
        text: "+"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: Type.family
        font.pixelSize: Type.sizeBody
        font.bold: true
        color: plusMouse.containsMouse ? Colors.accent : Colors.textSecondary

        MouseArea {
            id: plusMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.bump(1)
        }
    }

    Text {
        id: unitsText
        // The suffix is a courtesy; the number is the field. A long one
        // ("money/kg") used to squeeze the value into a few pixels, so it only
        // appears when the number still has room beside it.
        visible: root.units !== "" && root.width >= 130 + unitsText.implicitWidth
        anchors.right: plus.left
        anchors.rightMargin: Metrics.xs
        anchors.verticalCenter: parent.verticalCenter
        text: root.units
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        color: Colors.textTertiary
    }

    TextInput {
        id: input
        anchors.left: minus.right
        anchors.leftMargin: Metrics.xs
        anchors.right: unitsText.visible ? unitsText.left : plus.left
        anchors.rightMargin: Metrics.xs
        anchors.verticalCenter: parent.verticalCenter
        text: root.format(root.shownValue)
        color: Colors.textPrimary
        selectionColor: Colors.accentDim
        selectedTextColor: Colors.textPrimary
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        onActiveFocusChanged: if (!activeFocus) root.commit()
        onEditingFinished: root.commit()
        Keys.onUpPressed: root.bump(1)
        Keys.onDownPressed: root.bump(-1)
        Keys.onReturnPressed: root.commit()
        Keys.onEnterPressed: root.commit()
    }

    WheelHandler {
        onWheel: function(event) {
            root.bump(event.angleDelta.y > 0 ? 1 : -1)
            event.accepted = true
        }
    }
}
