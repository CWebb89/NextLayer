// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The object list, as a drawer on the inner edge of the settings panel: one
// row per object with its visibility, name, size and bed position, and one set
// of buttons acting on the selection. Selecting a row selects the object in the
// document, which is what the viewport tint, the transform tools and the slice
// all read.
Rectangle {
    id: root

    readonly property var objects: plateDocument.objects
    readonly property int selectedId: plateDocument.selectedId

    signal closeRequested()
    /// The list stowed into its chip rather than closed: the same hide, but it leaves
    /// a square behind that brings it back with one click.
    signal collapseRequested()
    /// A right-click on a row: the panel says which object and where in window
    /// coordinates, because the menu is a window overlay, not a child here.
    signal contextRequested(int objectId, real x, real y)
    signal volumeSettingsRequested(int objectId, int volumeId)

    width: Metrics.drawerWidth
    color: Colors.surfaceContainer
    border.width: Metrics.borderWidth
    border.color: Colors.edge

    Row {
        id: titleRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Metrics.md
        anchors.rightMargin: Metrics.sm
        anchors.topMargin: Metrics.md
        spacing: Metrics.sm

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Objects"
            font.family: Type.family
            font.pixelSize: Type.sizeBody
            font.bold: true
            color: Colors.textPrimary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: plateDocument.count
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textMuted
        }

        Item { width: parent.width - 200; height: 1 }

        ToolButton {
            icon: "unfold_less"
            label: "Collapse"
            hint: "Collapse the object list to a chip you can drag anywhere"
            onClicked: root.collapseRequested()
        }

        ToolButton {
            icon: "close"
            label: "Close"
            hint: "Hide the object list (Ctrl+L)"
            onClicked: root.closeRequested()
        }
    }

    Divider {
        id: topDivider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleRow.bottom
        anchors.topMargin: Metrics.sm
    }

    Column {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Metrics.md
        spacing: Metrics.sm

        Text {
            width: parent.width
            text: plateDocument.selectedCount > 1
                  ? plateDocument.selectedCount + " objects selected"
                  : plateDocument.selectedCount === 1
                    ? (plateDocument.objectInfo(plateDocument.selectedId).name || "")
                    : "no object selected"
            elide: Text.ElideMiddle
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textMuted
        }

        Row {
            width: parent.width
            spacing: Metrics.xs

            ToolButton {
                icon: "filter_center_focus"
                label: "Centre"
                hint: "Centre the selection on the bed"
                onClicked: plateDocument.centreSelection()
            }

            ToolButton {
                icon: "vertical_align_bottom"
                label: "Drop"
                hint: "Drop the selection onto the bed"
                onClicked: plateDocument.dropSelection()
            }

            ToolButton {
                icon: "content_copy"
                label: "Duplicate"
                hint: "Duplicate the selection"
                onClicked: plateDocument.duplicateSelection()
            }

            ToolButton {
                icon: "delete"
                label: "Delete"
                hint: "Delete the selection"
                danger: true
                onClicked: plateDocument.removeSelection()
            }
        }
    }

    Divider {
        id: bottomDivider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        anchors.bottomMargin: Metrics.sm
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topDivider.bottom
        anchors.bottom: bottomDivider.top
        anchors.topMargin: Metrics.sm
        anchors.bottomMargin: Metrics.sm
        model: root.objects
        spacing: Metrics.xs
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // A plate of eighteen objects is taller than the drawer: without the bar the
        // list simply looked like it ended at whichever row the clip fell on.
        ScrollBar.vertical: ThemedScrollBar { policy: ScrollBar.AlwaysOn }

        delegate: Rectangle {
            id: row

            required property var modelData

            readonly property var volumeList: row.modelData.volumes || []

            width: list.width
            height: 56 + row.volumeList.length * 26
            radius: Metrics.radiusSmall
            color: modelData.selected ? Colors.selectedFill : "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                radius: 1.5
                visible: modelData.selected
                color: Colors.accent
            }

            // Ctrl toggles one row in and out of the selection, Shift extends the
            // selection from the anchor, a plain click replaces it -- the same
            // convention object lists share.
            TapHandler {
                acceptedModifiers: Qt.NoModifier
                onTapped: plateDocument.selectObject(row.modelData.id)
                onDoubleTapped: {
                    nameField.text = row.modelData.name
                    nameField.visible = true
                    nameField.forceActiveFocus()
                    nameField.selectAll()
                }
            }
            TapHandler {
                acceptedModifiers: Qt.ControlModifier
                onTapped: plateDocument.selectObject(row.modelData.id, true)
            }
            TapHandler {
                acceptedModifiers: Qt.ShiftModifier
                onTapped: plateDocument.selectRange(row.modelData.id)
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Metrics.sm
                anchors.rightMargin: Metrics.sm
                spacing: Metrics.xxs

                Row {
                    width: parent.width
                    spacing: Metrics.sm

                    // The tool an object prints with, cycled with a click. Only
                    // a tool changer has more than one, so a single-tool printer
                    // never sees this.
                    Rectangle {
                        width: Metrics.controlHeight
                        height: Metrics.controlHeight
                        radius: Metrics.radiusSmall
                        visible: plateDocument.toolCount > 1
                        color: toolMouse.containsMouse ? Colors.hoverFill : "transparent"
                        border.width: Metrics.borderWidth
                        border.color: plateDocument.objectTool(row.modelData.id) > 1
                                      ? plateDocument.toolColour(plateDocument.objectTool(row.modelData.id))
                                      : Colors.outlineVariant

                        Text {
                            anchors.centerIn: parent
                            text: "T" + plateDocument.objectTool(row.modelData.id)
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            font.bold: true
                            color: Colors.textPrimary
                        }

                        MouseArea {
                            id: toolMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const next = plateDocument.objectTool(row.modelData.id) % plateDocument.toolCount + 1
                                plateDocument.setObjectTool(row.modelData.id, next)
                            }
                        }
                    }

                    ToolButton {
                        width: Metrics.controlHeight
                        height: Metrics.controlHeight
                        icon: row.modelData.visible ? "visibility" : "visibility_off"
                        label: row.modelData.visible ? "Hide" : "Show"
                        hint: row.modelData.visible ? "Hide this object" : "Show this object"
                        onClicked: plateDocument.setVisible(row.modelData.id, !row.modelData.visible)
                    }

                    // Double-click the name to rename the object: the name is the
                    // only thing here that is the user's rather than the file's.
                    Text {
                        id: nameLabel
                        width: parent.width - Metrics.controlHeight * 2 - Metrics.sm * 2
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !nameField.visible
                        text: row.modelData.name
                        elide: Text.ElideMiddle
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        font.bold: row.modelData.selected
                        color: row.modelData.selected ? Colors.accent : Colors.textPrimary
                    }

                    TextInput {
                        id: nameField
                        width: parent.width - Metrics.controlHeight * 2 - Metrics.sm * 2
                        anchors.verticalCenter: parent.verticalCenter
                        visible: false
                        text: row.modelData.name
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textPrimary
                        selectByMouse: true
                        selectionColor: Colors.accent

                        Keys.onReturnPressed: {
                            plateDocument.renameObject(row.modelData.id, nameField.text)
                            nameField.visible = false
                        }
                        Keys.onEscapePressed: nameField.visible = false
                        onActiveFocusChanged: if (!activeFocus) nameField.visible = false
                    }
                }

                // Size and position on one muted line: the numbers are reference,
                // the name above is what the eye needs to find.
                Text {
                    width: parent.width
                    leftPadding: Metrics.controlHeight + Metrics.sm
                    text: row.modelData.width.toFixed(1) + " x "
                          + row.modelData.depth.toFixed(1) + " x "
                          + row.modelData.height.toFixed(1) + " mm   at "
                          + row.modelData.x.toFixed(1) + ", " + row.modelData.y.toFixed(1)
                    elide: Text.ElideRight
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                // The volumes attached to this object, nested under it, because a
                // modifier is part of the object, not another thing on the plate.
                Repeater {
                    model: row.volumeList

                    delegate: Item {
                        id: volumeRow

                        required property var modelData

                        width: row.width - Metrics.sm * 2
                        height: 26

                        readonly property color badge: modelData.type === "negative" ? "#e8503f"
                                                     : modelData.type === "modifier" ? "#e0a83a"
                                                     : modelData.type === "blocker" ? "#9a5ce8"
                                                     : "#54c96a"
                        readonly property bool selected:
                            plateDocument.selectedVolume === modelData.id

                        Rectangle {
                            anchors.fill: parent
                            radius: Metrics.radiusSmall
                            color: volumeRow.selected
                                   ? Colors.alpha(Colors.accent, 0.16) : "transparent"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: plateDocument.selectVolume(row.modelData.id,
                                                                  volumeRow.modelData.id)
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 6
                            height: 6
                            radius: 3
                            color: volumeRow.badge
                            opacity: volumeRow.modelData.visible ? 1.0 : 0.35
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Metrics.md
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Metrics.md * 2 - 22
                            text: volumeRow.modelData.type + "  " + volumeRow.modelData.name
                                  + "  " + volumeRow.modelData.width.toFixed(0) + " mm"
                            elide: Text.ElideRight
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textSecondary
                        }

                        ToolButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 24
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            icon: "settings"
                            label: "Volume settings"
                            hint: "This " + volumeRow.modelData.type + " volume's own settings"
                            onClicked: root.volumeSettingsRequested(row.modelData.id,
                                                                    volumeRow.modelData.id)
                        }

                        ToolButton {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            icon: "delete"
                            label: "Delete volume"
                            hint: "Delete this " + volumeRow.modelData.type + " volume"
                            danger: true
                            onClicked: plateDocument.removeVolume(row.modelData.id,
                                                                  volumeRow.modelData.id)
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: (mouse) => {
                    const point = row.mapToItem(null, mouse.x, mouse.y)
                    root.contextRequested(row.modelData.id, point.x, point.y)
                    plateDocument.selectObject(row.modelData.id)
                }
            }
        }
    }
}
