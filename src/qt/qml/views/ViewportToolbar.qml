pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The single strip over the viewport, laid out like Orca's: plate tabs, a
// divider, the object actions, a divider, the manipulation tools, then the
// slice and export pills on the right. One panel, icon-only, tooltips carry the
// labels.
Item {
    id: root

    property var plates: []
    property int activePlate: 0
    property var actions: []
    property var tools: []

    /// The same tools, split into the groups the strip draws a divider between.
    /// Ids are matched against the flat list, so adding a tool to `tools` and not
    /// here leaves it out rather than drawing it twice.
    readonly property var toolGroups: {
        const byId = ({})
        for (let i = 0; i < root.tools.length; ++i)
            byId[root.tools[i].id] = root.tools[i]

        const order = [
            { ids: ["move", "rotate", "scale", "face"], divider: true },
            { ids: ["cut", "measure"], divider: true },
            { ids: ["text", "fuzzy"], divider: true },
            { ids: ["support", "seam", "colour"], divider: true },
            { ids: ["layers", "assemble", "calibrate"], divider: false }
        ]

        const groups = []
        for (let g = 0; g < order.length; ++g) {
            const entries = []
            for (let i = 0; i < order[g].ids.length; ++i) {
                const tool = byId[order[g].ids[i]]
                if (tool !== undefined)
                    entries.push(tool)
            }
            if (entries.length > 0)
                groups.push({ tools: entries, divider: order[g].divider })
        }
        return groups
    }
    property string activeTool: ""
    property bool slicing: false
    property bool canSlice: false

    signal plateSelected(int index)
    signal plateAdded()
    signal plateRemoved(int index)
    signal plateLockToggled(int index)
    signal plateCloned(int index)
    signal plateMenuRequested(int index, real x, real y)
    signal actionTriggered(string id)
    signal addRequested()
    signal toolTriggered(string id)
    signal sliceRequested()

    implicitHeight: Metrics.rowHeight + Metrics.md * 2

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusMedium
        color: Colors.alpha(Colors.surfaceContainer, 0.94)
        border.width: Metrics.borderWidth
        border.color: Colors.edge
    }

    Row {
        id: plateRow
        anchors.left: parent.left
        anchors.leftMargin: Metrics.sm
        anchors.verticalCenter: parent.verticalCenter
        spacing: Metrics.xxs

        Repeater {
            model: root.plates

            delegate: Item {
                id: plateChip

                required property var modelData
                required property int index

                readonly property bool current: root.activePlate === index

                width: plateChipInner.implicitWidth + (plateChip.current ? Metrics.md * 3 + 10
                                                                          : Metrics.md * 2)
                height: Metrics.rowHeight

                Rectangle {
                    anchors.fill: parent
                    radius: Metrics.radiusSmall
                    color: plateChip.current ? Colors.selectedFill
                         : plateMouse.containsMouse ? Colors.hoverFill : "transparent"
                }

                Row {
                    id: plateChipInner
                    anchors.centerIn: parent
                    spacing: Metrics.xs

                    Icon {
                        visible: plateChip.modelData.locked
                        width: Type.sizeCaption
                        height: Type.sizeCaption
                        size: Type.sizeCaption
                        anchors.verticalCenter: parent.verticalCenter
                        name: "lock"
                        color: plateChip.current ? Colors.accent : Colors.textTertiary
                    }

                    Text {
                        id: plateLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: plateChip.modelData.label === undefined
                              ? plateChip.modelData.name : plateChip.modelData.label
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: true
                        color: plateChip.current ? Colors.accent
                             : plateChip.modelData.locked ? Colors.textTertiary
                                                          : Colors.textSecondary
                    }
                }

                // The active plate carries its own options button, as Orca's
                // plate tab does: everything that belongs to that plate rather
                // than to an object on it.
                Text {
                    id: plateOptions
                    visible: plateChip.current
                    anchors.right: parent.right
                    anchors.rightMargin: Metrics.xs
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u25be"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: plateOptionsMouse.containsMouse ? Colors.accent : Colors.textSecondary

                    MouseArea {
                        id: plateOptionsMouse
                        anchors.fill: parent
                        anchors.margins: -Metrics.xs
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const scene = mapToItem(null, mouse.x, mouse.y)
                            root.plateMenuRequested(plateChip.index, scene.x, scene.y)
                        }
                    }
                }

                MouseArea {
                    id: plateMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton && root.plates.length > 1)
                            root.plateRemoved(plateChip.index)
                        else
                            root.plateSelected(plateChip.index)
                    }
                }

                ToolTip.visible: plateMouse.containsMouse
                ToolTip.text: "Plate " + (plateChip.index + 1) + " — middle click to remove"
                ToolTip.delay: 500
            }
        }

        // Orca's plate cluster, beside the plate you just selected: the four
        // things you do to a plate rather than to what is on it.
        ToolButton {
            icon: "add"
            hint: "Add a build plate"
            onClicked: root.plateAdded()
        }

        ToolButton {
            icon: "content_copy"
            hint: "Clone this plate with everything on it"
            onClicked: root.plateCloned(root.activePlate)
        }

        ToolButton {
            icon: root.plates.length > root.activePlate && root.plates[root.activePlate].locked
                  ? "lock" : "lock_open"
            hint: root.plates.length > root.activePlate && root.plates[root.activePlate].locked
                  ? "Unlock this plate"
                  : "Lock this plate so nothing can be changed on it"
            active: root.plates.length > root.activePlate
                    && root.plates[root.activePlate].locked
            onClicked: root.plateLockToggled(root.activePlate)
        }

        ToolButton {
            icon: "delete"
            hint: "Delete this plate"
            enabled: root.plates.length > 1
            onClicked: root.plateRemoved(root.activePlate)
        }
    }

    Divider {
        id: plateDivider
        anchors.left: plateRow.right
        anchors.leftMargin: Metrics.sm
        anchors.verticalCenter: parent.verticalCenter
    }

    Row {
        id: actionRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: plateDivider.right
        anchors.leftMargin: Metrics.md
        spacing: 0

        // Add is first because it is the first thing a plate needs: a shape of
        // its own, or a volume that changes what a shape becomes.
        ToolButton {
            icon: "add_box"
            hint: "Add a shape or a modifier volume"
            onClicked: root.addRequested()
        }

        Divider {
            width: Metrics.borderWidth
            height: Metrics.controlHeight - Metrics.xs * 2
            anchors.verticalCenter: parent.verticalCenter
        }

        Repeater {
            model: root.actions

            delegate: ToolButton {
                required property var modelData

                icon: modelData.icon
                hint: modelData.hint
                enabled: modelData.enabled
                danger: modelData.danger === true
                onClicked: root.actionTriggered(modelData.id)
            }
        }
    }

    Divider {
        id: actionDivider
        anchors.left: actionRow.right
        anchors.leftMargin: Metrics.sm
        anchors.verticalCenter: parent.verticalCenter
    }

    // The tools in three groups, separated, so the strip reads as "shape it,
    // paint it, measure it" instead of one undifferentiated run of icons.
    Row {
        id: toolRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: actionDivider.right
        anchors.leftMargin: Metrics.md
        anchors.right: sliceRow.left
        anchors.rightMargin: Metrics.sm
        clip: true
        spacing: 0

        Repeater {
            model: root.toolGroups

            delegate: Row {
                id: toolGroup
                required property var modelData

                spacing: 0

                Repeater {
                    model: toolGroup.modelData.tools

                    delegate: ToolButton {
                        required property var modelData

                        icon: modelData.icon
                        hint: modelData.hint
                        enabled: modelData.state === "ready"
                        active: root.activeTool === modelData.id
                        onClicked: root.toolTriggered(modelData.id)
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: toolGroup.modelData.divider
                    width: Metrics.borderWidth
                    height: Metrics.rowHeight - Metrics.lg
                    color: Colors.outlineVariant
                }
            }
        }
    }

    Row {
        id: sliceRow
        anchors.right: parent.right
        anchors.rightMargin: Metrics.sm
        anchors.verticalCenter: parent.verticalCenter
        spacing: Metrics.sm

        Button {
            text: root.slicing ? "Slicing…" : "Slice plate"
            primary: true
            enabled: root.canSlice && !root.slicing
            onClicked: root.sliceRequested()
        }

        Button {
            text: "Export G-code"
            enabled: root.canSlice
            onClicked: root.actionTriggered("export")
        }
    }
}
