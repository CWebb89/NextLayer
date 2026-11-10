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
//
// The field understands the unit as part of the number, which is how the
// reference slicers work: `50%`, `0.4mm` and a bare `0.4` are all a value, and
// there is no separate switch to say which one you meant. The suffix beside it
// names both possibilities (`mm or %`) rather than being a control.
Item {
    id: root

    property real value: 0
    property real from: -1e9
    property real to: 1e9
    /// The unit shown beside the number: "mm", "mm/s", "mm or %".
    property string units: root.isLength ? Units.lengthSuffix : ""
    property int decimals: root.isLength ? Units.lengthDecimals : 2
    /// `value` is always millimetres; the field shows the user's system.
    property bool isLength: false
    /// The value is a share, so the number carries its own `%`.
    property bool isPercent: false
    /// The typed value may carry its own unit: a trailing `%`, or the absolute
    /// unit this field is measured in.
    property bool acceptsUnit: false
    readonly property string absoluteUnits: root.isLength ? Units.lengthSuffix
                                             : root.units.replace(/ or %$/, "")
    signal edited(real value)
    /// The typed value named a percentage, so it is a share rather than a quantity.
    signal percentEdited(real value)

    readonly property real shownValue: root.isLength ? Units.fromMillimetres(root.value) : root.value
    readonly property real shownFrom: root.isLength ? Units.fromMillimetres(root.from) : root.from
    readonly property real shownTo: root.isLength ? Units.fromMillimetres(root.to) : root.to
    readonly property real perShownUnit: root.isLength ? Units.toMillimetres(1) : 1

    readonly property real increment: Math.pow(10, -Math.max(0, root.decimals))

    implicitWidth: 200
    implicitHeight: Metrics.controlHeight
    opacity: enabled ? 1.0 : 0.45

    function format(v) {
        if (root.isPercent)
            return Number(Number(v).toFixed(Math.max(0, root.decimals))) + "%"
        return Number(v).toFixed(Math.max(0, root.decimals))
    }

    function quantize(v) {
        var scale = Math.pow(10, Math.max(0, root.decimals))
        return Math.round(v * scale) / scale
    }

    function commit() {
        if (!root.acceptsUnit && !/^-?[0-9]*[.,]?[0-9]*$/.test(String(input.text).trim())) {
            input.text = root.format(root.shownValue)
            return
        }
        const typed = root.parseTyped(input.text)
        const next = root.quantize(isNaN(typed.value) ? root.shownValue
                                                       : Math.max(root.shownFrom,
                                                                  Math.min(root.shownTo, typed.value)))
        input.text = root.format(next)
        if (typed.percent) {
            root.percentEdited(next)
            return
        }
        const millimetres = next * root.perShownUnit
        if (millimetres !== root.value)
            root.edited(millimetres)
    }

    /// A typed number with an optional unit: `50%`, `50 mm/s` or a bare `50`, which
    /// keeps the field's current unit. A suffix the field does not accept is not a
    /// number, and the commit keeps the old value.
    function parseTyped(text) {
        const raw = String(text).trim()
        if (!root.acceptsUnit)
            return { value: parseFloat(raw), percent: false }
        if (raw.slice(-1) === "%") {
            const share = parseFloat(raw.slice(0, -1).replace(",", "."))
            return { value: share, percent: !isNaN(share) }
        }
        const suffix = root.absoluteUnits.trim()
        if (suffix.length > 0 && raw.slice(-suffix.length) === suffix) {
            const absolute = parseFloat(raw.slice(0, -suffix.length))
            return { value: absolute, percent: false }
        }
        return { value: parseFloat(raw.replace(",", ".")), percent: false }
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
