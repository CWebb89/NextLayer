pragma ComponentBehavior: Bound
import QtQuick
import "../theme"

// The preview's move slider, laid out like OrcaSlicer's second slider: it scrubs
// the moves inside the current layer, so a print can be walked forward one
// extrusion at a time instead of one layer at a time. The tick marks are the
// layer's own moves and the label reads the move index within it.
Item {
    id: root

    property real lowerValue: 0
    property real upperValue: 0
    property real from: 0
    property real to: 0
    property real step: 1
    property int tickCount: 0
    property string hint: ""

    signal lowerMoved(real value)
    signal upperMoved(real value)

    property string draggingHandle: ""
    property real liveLower: root.lowerValue
    property real liveUpper: root.upperValue
    property real pressX: 0
    property real pressValue: 0

    readonly property real range: Math.max(0.0001, root.to - root.from)
    readonly property real lowerProgress: Math.max(0, Math.min(1, (root.liveLower - root.from) / root.range))
    readonly property real upperProgress: Math.max(0, Math.min(1, (root.liveUpper - root.from) / root.range))
    readonly property real usable: Math.max(0, width - Metrics.gaugeKnob)

    implicitWidth: 320
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
        if (root.usable <= 0)
            return root.from
        const offset = Math.max(0, Math.min(root.usable, x - Metrics.gaugeKnob / 2))
        return root.clamp(root.from + (offset / root.usable) * root.range)
    }

    function positionOf(progress) {
        return Metrics.gaugeKnob / 2 + root.usable * progress
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
        opacity: 0.9
    }

    Repeater {
        model: root.tickCount > 0 && root.tickCount <= 240 ? root.tickCount : 0

        delegate: Rectangle {
            required property int index

            width: Metrics.hairline
            height: Metrics.sm
            anchors.verticalCenter: parent.verticalCenter
            x: root.positionOf(root.tickCount > 1 ? index / (root.tickCount - 1) : 1)
            color: Colors.textTertiary
            opacity: 0.45
        }
    }

    Rectangle {
        anchors.verticalCenter: track.verticalCenter
        height: Metrics.gaugeTrack
        radius: height / 2
        color: Colors.accent
        x: root.positionOf(root.lowerProgress)
        width: Math.max(0, (root.upperProgress - root.lowerProgress) * root.usable)
    }

    Rectangle {
        id: lowerKnob
        width: Metrics.gaugeKnob - Metrics.xs
        height: Metrics.gaugeKnob - Metrics.xs
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: root.positionOf(root.lowerProgress) - width / 2
        scale: input.containsMouse && root.draggingHandle === "lower" ? 1.15 : 1.0
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: upperKnob
        width: Metrics.gaugeKnob - Metrics.xs
        height: Metrics.gaugeKnob - Metrics.xs
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        color: Colors.accent
        border.width: Metrics.xxs
        border.color: Colors.surfaceContainerHigh
        x: root.positionOf(root.upperProgress) - width / 2
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
            if (root.draggingHandle.length === 0 || root.usable <= 0)
                return
            root.applyValue(root.clamp(root.pressValue
                                       + (mouse.x - root.pressX) * root.range / root.usable))
        }

        onReleased: {
            root.draggingHandle = ""
            root.liveLower = root.lowerValue
            root.liveUpper = root.upperValue
        }
    }
}
