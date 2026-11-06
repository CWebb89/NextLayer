import QtQuick
import "../theme"

// 5px track, 19px knob, fill and knob animated 140 ms OutCubic. Dragging and
// the wheel emit `moved(value)`; `value` is not assigned here, so the caller's
// binding stays live. A step of 0 (or less) means continuous.
Item {
    id: root

    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    signal moved(real value)

    property real liveValue: root.value
    property bool dragging: false

    readonly property real range: Math.max(0.0001, root.to - root.from)
    readonly property real progress: Math.max(0, Math.min(1, (root.liveValue - root.from) / root.range))

    implicitWidth: 240
    implicitHeight: Metrics.gaugeKnob + Metrics.md
    opacity: enabled ? 1.0 : 0.45

    onValueChanged: if (!root.dragging) root.liveValue = root.value

    function snap(v) {
        if (root.step <= 0)
            return v
        return Math.round(v / root.step) * root.step
    }

    function clamp(v) {
        return root.snap(Math.max(root.from, Math.min(root.to, v)))
    }

    function valueAtX(x) {
        var usable = root.width - Metrics.gaugeKnob
        if (usable <= 0)
            return root.from
        var offset = Math.max(0, Math.min(usable, x - Metrics.gaugeKnob / 2))
        return root.clamp(root.from + (offset / usable) * root.range)
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
        anchors.left: track.left
        anchors.verticalCenter: track.verticalCenter
        height: Metrics.gaugeTrack
        radius: height / 2
        color: Colors.accent
        width: (root.width - Metrics.gaugeKnob) * root.progress
        Behavior on width {
            enabled: !root.dragging
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        id: knob
        width: Metrics.gaugeKnob
        height: Metrics.gaugeKnob
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: Metrics.gaugeKnob / 2 + (root.width - Metrics.gaugeKnob) * root.progress
        scale: area.containsMouse || root.dragging ? 1.15 : 1.0
        Behavior on x {
            enabled: !root.dragging
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: function(mouse) {
            root.dragging = true
            root.liveValue = root.valueAtX(mouse.x)
            root.moved(root.liveValue)
        }
        onPositionChanged: function(mouse) {
            if (!root.dragging)
                return
            root.liveValue = root.valueAtX(mouse.x)
            root.moved(root.liveValue)
        }
        onReleased: {
            root.dragging = false
            root.liveValue = root.value
        }
    }

    WheelHandler {
        onWheel: function(event) {
            var increment = root.step > 0 ? root.step : root.range / 100
            var next = root.clamp(root.liveValue + (event.angleDelta.y > 0 ? increment : -increment))
            root.liveValue = next
            root.moved(next)
            event.accepted = true
        }
    }
}
