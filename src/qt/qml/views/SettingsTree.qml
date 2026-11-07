// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"
import "."

// The scroll of engine settings: one section per option group, one row per
// setting, label on the left and the typed editor on the right. Shared by the
// docked settings panel and the preset editor dialog so a setting renders the
// same way wherever it is edited, and owns the specialised editors the typed
// ones open.
Item {
    id: root

    property var groups: []
    property string filterText: ""
    property Item tipItem: null
    property string tipText: ""

    readonly property bool filtering: root.filterText.length > 0
    readonly property int editorWidth: Math.round(width * 0.46)
    readonly property bool empty: root.groups.length === 0

    function openDialog(kind, item) {
        switch (kind) {
        case "bedShape":
        case "points":
            pointDialog.openFor(item)
            break
        case "color":
            colourDialog.openFor(item)
            break
        case "substitutions":
            substitutionsDialog.openFor(item)
            break
        case "ramming":
        case "textList":
            stringListDialog.openFor(item)
            break
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: fieldColumn.implicitHeight + Metrics.md
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThemedScrollBar {}

        // A Flickable does not scroll on the wheel by itself, and a settings page
        // is exactly the place a person reaches for the wheel. The handler sits on
        // the flickable so it wins over anything behind the panel and stops the
        // viewport from zooming while the pointer is over a setting.
        WheelHandler {
            onWheel: (event) => {
                const limit = Math.max(0, flick.contentHeight - flick.height)
                flick.contentY = Math.max(0, Math.min(limit, flick.contentY - event.angleDelta.y / 2))
                event.accepted = true
            }
        }

        Column {
            id: fieldColumn
            width: flick.width
            spacing: Metrics.lg

            Text {
                visible: root.empty && root.filtering
                width: parent.width
                text: "no setting matches \"" + root.filterText + "\""
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textMuted
            }

            Repeater {
                model: root.groups

                delegate: Column {
                    id: groupBlock

                    required property var modelData

                    width: fieldColumn.width
                    spacing: Metrics.xxs

                    Item {
                        width: parent.width
                        height: sectionLabel.implicitHeight + Metrics.sm

                        Text {
                            id: sectionLabel
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            text: root.filtering
                                  ? groupBlock.modelData.tabTitle + " \u203a "
                                    + groupBlock.modelData.subtabTitle + " \u203a "
                                    + groupBlock.modelData.title
                                  : groupBlock.modelData.title
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            font.bold: true
                            color: Colors.textSecondary
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Metrics.hairline
                            color: Colors.separator
                        }
                    }

                    Repeater {
                        model: groupBlock.modelData.items

                        delegate: Item {
                            id: settingRow

                            required property var modelData

                            readonly property bool modified: Boolean(settingRow.modelData.modified)
                            readonly property bool multiline: settingRow.modelData.multiline === true

                            width: groupBlock.width
                            height: settingRow.multiline
                                    ? 132 : Math.max(Metrics.rowHeight,
                                                     rowLabel.implicitHeight + Metrics.md)

                            Rectangle {
                                anchors.fill: parent
                                radius: Metrics.radiusSmall
                                color: rowHover.containsMouse ? Colors.hoverFill : "transparent"
                            }

                            Text {
                                id: rowLabel
                                anchors.left: parent.left
                                anchors.leftMargin: Metrics.sm
                                anchors.right: editorSlot.left
                                anchors.rightMargin: Metrics.sm
                                anchors.verticalCenter: parent.verticalCenter
                                text: settingRow.modelData.label
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                font.bold: true
                                color: settingRow.modified ? Colors.accent : Colors.textPrimary
                            }

                            Rectangle {
                                visible: settingRow.modified
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Metrics.xxs
                                height: Metrics.xxs
                                radius: width / 2
                                color: Colors.accent
                            }

                            Item {
                                id: editorSlot
                                anchors.right: parent.right
                                anchors.rightMargin: Metrics.sm
                                anchors.verticalCenter: parent.verticalCenter
                                width: root.editorWidth
                                height: parent.height

                                Row {
                                    anchors.fill: parent
                                    spacing: Metrics.xs

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Metrics.md
                                        visible: settingRow.modified
                                        text: "\u27f2"
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeCaption
                                        color: revertMouse.containsMouse ? Colors.accent : Colors.textTertiary

                                        MouseArea {
                                            id: revertMouse
                                            anchors.fill: parent
                                            anchors.margins: -Metrics.xs
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: settingsModel.reset(settingRow.modelData.name)
                                        }
                                    }

                                    SettingEditor {
                                        width: parent.width - parent.spacing
                                               - (settingRow.modified ? Metrics.md : 0)
                                        height: parent.height
                                        item: settingRow.modelData
                                        onRequestDialog: (kind) => root.openDialog(kind, settingRow.modelData)
                                    }
                                }
                            }

                            // Behind the editor, not in front of it: a row-sized
                            // MouseArea on top would swallow every click meant for a
                            // field. `z: -1` costs it the label, where the text sits
                            // above it, and nothing else on the row is a hit area.
                            MouseArea {
                                id: rowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                                z: -1

                                onEntered: {
                                    const text = settingRow.modelData.tooltip === undefined
                                                 || settingRow.modelData.tooltip === null
                                                 ? "" : String(settingRow.modelData.tooltip)
                                    if (text.length === 0)
                                        return
                                    root.tipItem = settingRow
                                    root.tipText = text
                                    tipTimer.restart()
                                }

                                onExited: {
                                    root.tipItem = null
                                    root.tipText = ""
                                    tipTimer.stop()
                                    descriptionTip.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // The description of the setting under the pointer. Qt's attached tooltip
    // centres itself over the row, so a two-sentence description 650 px wide on
    // a 400 px row hung half of it off the window. This one is a fixed width,
    // wraps its text, and is placed under the row inside the panel.
    ToolTip {
        id: descriptionTip
        parent: root
        width: Math.min(380, Math.max(240, root.width - Metrics.xl * 2))
        height: tipLabel.implicitHeight + Metrics.md * 2
        padding: Metrics.md
        visible: false
        closePolicy: Popup.NoAutoClose

        contentItem: Text {
            id: tipLabel
            width: descriptionTip.width - Metrics.md * 2
            text: descriptionTip.text
            wrapMode: Text.WordWrap
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textSecondary
        }

        background: Rectangle {
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainerHigh
            border.width: Metrics.borderWidth
            border.color: Colors.edgeStrong
        }
    }

    Timer {
        id: tipTimer
        interval: 550
        onTriggered: {
            if (root.tipItem === null || root.tipText.length === 0)
                return
            const point = root.tipItem.mapToItem(root, 0, root.tipItem.height + Metrics.xs)
            const text = root.tipText
            descriptionTip.text = text
            descriptionTip.x = Math.max(Metrics.xs,
                                        Math.min(root.width - descriptionTip.width - Metrics.xs, point.x))
            descriptionTip.y = Math.max(Metrics.xs,
                                        Math.min(root.height - descriptionTip.height - Metrics.xs, point.y))
            descriptionTip.open()
        }
    }

    BedShapeDialog {
        id: pointDialog
    }

    ColourDialog {
        id: colourDialog
    }

    SubstitutionsDialog {
        id: substitutionsDialog
    }

    StringListDialog {
        id: stringListDialog
    }
}
