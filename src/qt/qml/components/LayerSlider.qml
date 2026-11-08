// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// The preview's vertical layer slider: a track down
// the right edge of the viewport with two handles — the upper one is the top
// layer, the one the model is cut at, and the lower one is the first layer still
// drawn. Both handles carry their layer number and Z height, so the control
// tells you where you are without looking at the footer.
Item {
    id: root

    property real lowerValue: 0
    property real upperValue: 0
    property real from: 0
    property real to: 0
    property real step: 1
    property string unit: "Layer"
    property int labelOffset: 0
    property string upperDetail: ""
    property string lowerDetail: ""

    /// Heights that carry a marker (custom G-code, pause, tool change), so the
    /// track shows where they are: the ticks are the same in both.
    property var markers: []

    signal lowerMoved(real value)
    signal upperMoved(real value)
    signal stepped(real delta)
    signal contextRequested(real x, real y)

    property string draggingHandle: ""
    // Both handles on one value is the common start state (layer 1 only). Picking
    // "lower" there clamps it to the upper handle and the range can never grow,
    // so the first movement decides which handle the drag is taking.
    property bool coincidentDrag: false
    property real liveLower: root.lowerValue
    property real liveUpper: root.upperValue
    property real pressY: 0
    property real pressValue: 0

    readonly property real range: Math.max(0.0001, root.to - root.from)
    readonly property real lowerProgress: Math.max(0, Math.min(1, (root.liveLower - root.from) / root.range))
    readonly property real upperProgress: Math.max(0, Math.min(1, (root.liveUpper - root.from) / root.range))
    readonly property int travel: Math.max(0, height - Metrics.gaugeKnob - 2 * Metrics.xl)
    implicitWidth: 132
    implicitHeight: 320
    opacity: enabled ? 1.0 : 0.45

    onLowerValueChanged: if (root.draggingHandle !== "lower") root.liveLower = root.lowerValue
    onUpperValueChanged: if (root.draggingHandle !== "upper") root.liveUpper = root.upperValue

    function snap(v) {
        return root.step <= 0 ? v : Math.round(v / root.step) * root.step
    }

    function clamp(v) {
        return root.snap(Math.max(root.from, Math.min(root.to, v)))
    }

    function valueAtY(y) {
        if (root.travel <= 0)
            return root.from
        const offset = Math.max(0, Math.min(root.travel, y - Metrics.xl - Metrics.gaugeKnob / 2))
        return root.clamp(root.to - (offset / root.travel) * root.range)
    }

    function positionOf(progress) {
        return Metrics.xl + Metrics.gaugeKnob / 2 + root.travel * (1 - progress)
    }

    readonly property point upperHandlePoint: Qt.point(width - Metrics.gaugeKnob / 2,
                                                       positionOf(upperProgress))
    readonly property point lowerHandlePoint: Qt.point(width - Metrics.gaugeKnob / 2,
                                                       positionOf(lowerProgress))

    function applyValue(value) {
        if (root.draggingHandle === "lower") {
            const clamped = Math.min(value, root.liveUpper)
            if (clamped === root.liveLower)
                return
            root.liveLower = clamped
            root.lowerMoved(clamped)
        } else if (root.draggingHandle === "upper") {
            const clamped = Math.max(value, root.liveLower)
            if (clamped === root.liveUpper)
                return
            root.liveUpper = clamped
            root.upperMoved(clamped)
        }
    }

    Rectangle {
        id: track
        width: Metrics.gaugeTrack
        radius: width / 2
        color: Colors.selectedFill
        x: root.width - Metrics.gaugeKnob / 2 - width / 2
        y: root.positionOf(1)
        height: root.travel
        opacity: 0.55
    }

    Rectangle {
        anchors.horizontalCenter: track.horizontalCenter
        width: Metrics.gaugeTrack
        radius: width / 2
        color: Colors.accent
        y: root.positionOf(root.upperProgress)
        height: Math.max(0, (root.upperProgress - root.lowerProgress) * root.travel)
    }

    Rectangle {
        id: upperHandle
        width: Metrics.gaugeKnob
        height: Metrics.gaugeKnob
        radius: width / 2
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: root.width - width
        y: root.positionOf(root.upperProgress) - height / 2
        scale: input.containsMouse && root.draggingHandle === "upper" ? 1.12 : 1.0
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: lowerHandle
        width: Metrics.gaugeKnob
        height: Metrics.gaugeKnob
        radius: width / 2
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: root.width - width
        y: root.positionOf(root.lowerProgress) - height / 2
        scale: input.containsMouse && root.draggingHandle === "lower" ? 1.12 : 1.0
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    Column {
        anchors.right: parent.right
        anchors.rightMargin: Metrics.gaugeKnob + Metrics.sm
        anchors.verticalCenter: upperHandle.verticalCenter
        width: parent.width - Metrics.gaugeKnob - Metrics.sm
        spacing: 0

        Text {
            anchors.right: parent.right
            text: root.unit + " " + (Math.round(root.liveUpper) + root.labelOffset)
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            font.bold: true
            color: Colors.textPrimary
        }

        Text {
            anchors.right: parent.right
            visible: root.upperDetail.length > 0
            text: root.upperDetail
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textTertiary
        }
    }

    Column {
        anchors.right: parent.right
        anchors.rightMargin: Metrics.gaugeKnob + Metrics.sm
        anchors.verticalCenter: lowerHandle.verticalCenter
        width: parent.width - Metrics.gaugeKnob - Metrics.sm
        spacing: 0

        Text {
            anchors.right: parent.right
            text: root.unit + " " + (Math.round(root.liveLower) + root.labelOffset)
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            font.bold: true
            color: Colors.textSecondary
        }

        Text {
            anchors.right: parent.right
            visible: root.lowerDetail.length > 0
            text: root.lowerDetail
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textTertiary
        }
    }

    // Marker ticks, one per height that carries custom G-code.
    Repeater {
        model: root.markers

        delegate: Rectangle {
            id: tick

            required property var modelData

            readonly property real fraction: {
                const total = toolpath.layerCount > 0
                            ? toolpath.layerZAt(toolpath.layerCount - 1) : 0
                return total > 0 ? Math.max(0, Math.min(1, Number(modelData.z) / total)) : 0
            }

            x: root.width - Metrics.gaugeKnob - Metrics.sm * 2 + Metrics.sm
            y: root.positionOf(tick.fraction) - height / 2
            width: Metrics.gaugeKnob + Metrics.sm * 2
            height: Metrics.xxs
            radius: height / 2
            color: modelData.type === "pause" ? Colors.tertiary : Colors.accent
        }
    }

    MouseArea {
        id: contextArea
        anchors.fill: input
        acceptedButtons: Qt.RightButton
        hoverEnabled: true
        onClicked: (mouse) => {
            const scene = mapToItem(null, mouse.x, mouse.y)
            root.contextRequested(scene.x, scene.y)
        }
    }

    MouseArea {
        id: input
        x: root.width - Metrics.gaugeKnob - Metrics.sm * 2
        width: Metrics.gaugeKnob + Metrics.sm * 4
        y: Metrics.xl - Metrics.sm
        height: root.travel + Metrics.gaugeKnob + Metrics.sm * 2
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.SizeVerCursor
        preventStealing: true

        onPressed: function(mouse) {
            const value = root.valueAtY(mapToItem(root, mouse.x, mouse.y).y)
            root.coincidentDrag = Math.abs(root.liveLower - root.liveUpper)
                                  < Math.max(root.step, 0.001) * 0.5
            root.draggingHandle = root.coincidentDrag
                                  ? ""
                                  : (Math.abs(value - root.liveLower) <= Math.abs(value - root.liveUpper)
                                     ? "lower" : "upper")
            root.pressY = mapToItem(root, mouse.x, mouse.y).y
            root.pressValue = value
            if (!root.coincidentDrag)
                root.applyValue(value)
        }

        onPositionChanged: function(mouse) {
            if (root.travel <= 0)
                return
            const local = mapToItem(root, mouse.x, mouse.y).y
            const target = root.clamp(root.pressValue
                                      - (local - root.pressY) * root.range / root.travel)
            if (root.coincidentDrag) {
                if (target > root.liveUpper + root.step * 0.25)
                    root.draggingHandle = "upper"
                else if (target < root.liveLower - root.step * 0.25)
                    root.draggingHandle = "lower"
                else
                    return
                root.coincidentDrag = false
            }
            if (root.draggingHandle.length === 0)
                return
            root.applyValue(target)
        }

        onReleased: {
            root.draggingHandle = ""
            root.coincidentDrag = false
            root.liveLower = root.lowerValue
            root.liveUpper = root.upperValue
        }

        onWheel: (wheel) => {
            if (wheel.angleDelta.y === 0)
                return
            root.stepped(wheel.angleDelta.y > 0 ? 1 : -1)
            wheel.accepted = true
        }
    }
}
