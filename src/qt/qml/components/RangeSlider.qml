import QtQuick
import "../theme"

// The preview's layer range: one track, two knobs. The right knob is the top
// layer — the one the model is cut at — and the left is the first layer still
// drawn, which is how a print is inspected without
// hiding the layers above it. A single mouse area handles both knobs: the one
// grabbed is the nearer of the two, and because the area does not move with the
// knob every pointer step maps to exactly one layer.
Item {
    id: root

    property real lowerValue: 0
    property real upperValue: 100
    property real from: 0
    property real to: 100
    property real step: 1

    signal lowerMoved(real value)
    signal upperMoved(real value)
    signal stepped(real delta)

    property string draggingHandle: ""
    property real liveLower: root.lowerValue
    property real liveUpper: root.upperValue
    property real pressX: 0
    property real pressValue: 0

    readonly property real range: Math.max(0.0001, root.to - root.from)
    readonly property real lowerProgress: Math.max(0, Math.min(1, (root.liveLower - root.from) / root.range))
    readonly property real upperProgress: Math.max(0, Math.min(1, (root.liveUpper - root.from) / root.range))

    readonly property point lowerHandlePoint: Qt.point(positionOf(lowerProgress), height / 2)
    readonly property point upperHandlePoint: Qt.point(positionOf(upperProgress), height / 2)

    implicitWidth: 240
    implicitHeight: Metrics.gaugeKnob + Metrics.md
    opacity: enabled ? 1.0 : 0.45

    onLowerValueChanged: if (root.draggingHandle !== "lower") root.liveLower = root.lowerValue
    onUpperValueChanged: if (root.draggingHandle !== "upper") root.liveUpper = root.upperValue

    function snap(v) {
        return root.step <= 0 ? v : Math.round(v / root.step) * root.step
    }

    function clamp(v) {
        return root.snap(Math.max(root.from, Math.min(root.to, v)))
    }

    function valueAtX(x) {
        const usable = root.width - Metrics.gaugeKnob
        if (usable <= 0)
            return root.from
        const offset = Math.max(0, Math.min(usable, x - Metrics.gaugeKnob / 2))
        return root.clamp(root.from + (offset / usable) * root.range)
    }

    function positionOf(progress) {
        return Metrics.gaugeKnob / 2 + (root.width - Metrics.gaugeKnob) * progress
    }

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
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Metrics.gaugeKnob / 2
        anchors.rightMargin: Metrics.gaugeKnob / 2
        height: Metrics.gaugeTrack
        radius: height / 2
        color: Colors.selectedFill
    }

    Rectangle {
        anchors.verticalCenter: track.verticalCenter
        height: Metrics.gaugeTrack
        radius: height / 2
        color: Colors.accent
        x: root.positionOf(root.lowerProgress)
        width: Math.max(0, (root.upperProgress - root.lowerProgress) * (root.width - Metrics.gaugeKnob))
    }

    Rectangle {
        id: lowerKnob
        width: Metrics.gaugeKnob
        height: Metrics.gaugeKnob
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: root.positionOf(root.lowerProgress)
        scale: input.containsMouse && root.draggingHandle === "lower" ? 1.15 : 1.0
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: upperKnob
        width: Metrics.gaugeKnob
        height: Metrics.gaugeKnob
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: root.positionOf(root.upperProgress)
        scale: input.containsMouse && root.draggingHandle === "upper" ? 1.15 : 1.0
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: input
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        preventStealing: true

        onPressed: function(mouse) {
            const value = root.valueAtX(mouse.x)
            root.draggingHandle = Math.abs(value - root.liveLower) <= Math.abs(value - root.liveUpper)
                                  ? "lower" : "upper"
            root.pressX = mouse.x
            root.pressValue = value
            root.applyValue(value)
        }

        onPositionChanged: function(mouse) {
            if (root.draggingHandle.length === 0)
                return
            const usable = root.width - Metrics.gaugeKnob
            if (usable <= 0)
                return
            root.applyValue(root.clamp(root.pressValue
                                       + (mouse.x - root.pressX) * root.range / usable))
        }

        onReleased: {
            root.draggingHandle = ""
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
