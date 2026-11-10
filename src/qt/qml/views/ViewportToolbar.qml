// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The single strip over the viewport: plate tabs, a divider, the object actions,
// a divider, then the slice and export pills on the right. One row, icon-only,
// tooltips carry the labels. The manipulation tools are not here: fourteen of
// them plus ten actions did not fit one row, and the second row they were given
// read as a wall of glyphs. They live in the tool rail down the left of the
// viewport (ToolRail.qml), next to the model they act on.
Item {
    id: root

    property var plates: []
    property int activePlate: 0
    property var actions: []
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
    signal sliceRequested()
    /// The slice button's chevron: the same two actions plus the whole project.
    signal sliceMenuRequested(real x, real y)
    /// The export button's own menu, whose entries live in the window: a menu at the
    /// top of the strip has to be drawn over the whole window, not cropped by it.
    signal exportMenuRequested(real x, real y)

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

        // The rest of a plate's own actions: rename, duplicate, clear. There used
        // to be a column of six icons standing on the bed for this; at a low camera
        // angle they read as loose glyphs floating over the plate, and every one of
        // them but this menu is in the bar already.
        ToolButton {
            id: plateMenuButton
            icon: "more_vert"
            hint: "Plate settings: rename, duplicate, lock, clear"
            onClicked: {
                const point = plateMenuButton.mapToItem(null, plateMenuButton.width,
                                                        plateMenuButton.height)
                root.plateMenuRequested(root.activePlate, point.x, point.y)
            }
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
            onClicked: {
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

    Row {
        id: sliceRow
        anchors.right: parent.right
        anchors.rightMargin: Metrics.sm
        anchors.verticalCenter: plateRow.verticalCenter
        spacing: Metrics.sm

        SplitButton {
            text: root.slicing ? "Slicing…" : "Slice plate"
            primary: true
            hint: root.slicing ? "Slicing the plate" : "Slice the active plate (Ctrl+B)"
            menuHint: root.plates.length > 1
                      ? "Slice the active plate, or all " + root.plates.length + " plates"
                      : "Slice the active plate"
            enabled: root.canSlice && !root.slicing
            onClicked: root.sliceRequested()
            onMenuRequested: (x, y) => root.sliceMenuRequested(x, y)
        }

        SplitButton {
            text: "Export G-code"
            hint: "Write the sliced G-code to a file (Ctrl+E)"
            menuHint: "Export the G-code to a file, or send it to the printer"
            enabled: root.canSlice
            onClicked: root.actionTriggered("export")
            onMenuRequested: (x, y) => root.exportMenuRequested(x, y)
        }
    }
}
