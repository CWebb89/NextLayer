// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The single strip over the viewport: plate tabs, a
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
    /// The plate selector was opened: the window owns the list, because a menu at
    /// the top of the strip has to be drawn over the whole window.
    signal plateListRequested(real x, real y)
    signal actionTriggered(string id)
    /// The Add button was pressed, with the point under its bottom-left corner in
    /// window coordinates, so the menu it opens lands on the button rather than in
    /// a corner of the window.
    signal addRequested(real x, real y)
    signal toolTriggered(string id)
    signal sliceRequested()
    signal sliceAllRequested()

    // Two rows, because one cannot hold them: a plate cluster, nine object
    // actions, fourteen tools and the slice controls came to roughly 1300 px
    // against 1150 available, and the overflow silently clipped the last six
    // tools -- support, seam and colour painting, variable layer height,
    // assemble and calibration -- off the end of the strip. Plate and object
    // actions share the top row with the slice controls; the tools own the
    // second row and always fit.
    implicitHeight: Metrics.rowHeight + Metrics.md * 2 + Metrics.rowHeight + Metrics.xs

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
        anchors.top: parent.top
        anchors.topMargin: Metrics.md
        spacing: Metrics.xxs

        // One control for the whole plate list. A project can hold fourteen plates
        // and a strip of fourteen chips pushed everything else off the bar, so the
        // plates live behind a selector that names where you are and opens the list.
        Rectangle {
            id: plateSelector

            readonly property var current: root.activePlate >= 0
                                                && root.activePlate < root.plates.length
                                            ? root.plates[root.activePlate] : null

            width: selectorRow.implicitWidth + Metrics.md * 2
            height: Metrics.rowHeight
            radius: Metrics.radiusSmall
            color: selectorMouse.containsMouse ? Colors.hoverFill : Colors.selectedFill

            Row {
                id: selectorRow
                anchors.centerIn: parent
                spacing: Metrics.xs

                Icon {
                    visible: plateSelector.current !== null && plateSelector.current.locked
                    width: Type.sizeCaption
                    height: Type.sizeCaption
                    size: Type.sizeCaption
                    anchors.verticalCenter: parent.verticalCenter
                    name: "lock"
                    color: Colors.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (plateSelector.current !== null
                           ? plateSelector.current.name
                           : "Plate " + (root.activePlate + 1)) + "  \u25be"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.accent
                }
            }

            MouseArea {
                id: selectorMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    const scene = mapToItem(null, mouse.x, mouse.y)
                    root.plateListRequested(scene.x, scene.y)
                }
            }

            Tooltip {
                target: plateSelector
                text: "Build plates \u2014 pick the one to work on"
                shown: selectorMouse.containsMouse
                delay: 500
            }
        }

        // The plate cluster, beside the selector: the four things you do to a
        // plate rather than to what is on it.
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
        vertical: true
        anchors.left: plateRow.right
        anchors.leftMargin: Metrics.sm
        anchors.verticalCenter: plateRow.verticalCenter
    }

    Row {
        id: actionRow
        anchors.verticalCenter: plateRow.verticalCenter
        anchors.left: plateDivider.right
        anchors.leftMargin: Metrics.md
        spacing: 0

        // Add is first because it is the first thing a plate needs: a shape of
        // its own, or a volume that changes what a shape becomes.
        ToolButton {
            id: addButton
            icon: "add_box"
            hint: "Add a shape or a modifier volume"
            onClicked: (mouse) => {
                const point = addButton.mapToItem(null, 0, addButton.height + Metrics.xs)
                root.addRequested(point.x, point.y)
            }
        }

        Divider {
            vertical: true
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
        vertical: true
        anchors.left: actionRow.right
        anchors.leftMargin: Metrics.sm
        anchors.verticalCenter: plateRow.verticalCenter
    }

    // The tools in three groups, separated, so the strip reads as "shape it,
    // paint it, measure it" instead of one undifferentiated run of icons.
    Row {
        id: toolRow
        anchors.left: parent.left
        anchors.leftMargin: Metrics.md
        anchors.right: parent.right
        anchors.rightMargin: Metrics.md
        anchors.top: plateRow.bottom
        anchors.topMargin: Metrics.md
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
        anchors.verticalCenter: plateRow.verticalCenter
        spacing: Metrics.sm

        Button {
            text: root.slicing ? "Slicing…" : "Slice plate"
            primary: true
            enabled: root.canSlice && !root.slicing
            onClicked: root.sliceRequested()
        }

        // The other half of the slice action: a project of fourteen plates is
        // sliced as one job from here, and each plate keeps its own toolpath.
        ToolButton {
            icon: "layers"
            hint: root.plates.length > 1
                  ? "Slice all " + root.plates.length + " plates"
                  : "Slice all plates"
            enabled: root.canSlice && !root.slicing && root.plates.length > 1
            onClicked: root.sliceAllRequested()
        }

        Button {
            text: "Export G-code"
            enabled: root.canSlice
            onClicked: root.actionTriggered("export")
        }
    }
}
