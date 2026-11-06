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
        ScrollBar.vertical: ScrollBar {}

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

                            MouseArea {
                                id: rowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                                z: -1
                            }

                            ToolTip.visible: rowHover.containsMouse
                                             && settingRow.modelData.tooltip !== undefined
                                             && settingRow.modelData.tooltip !== null
                                             && String(settingRow.modelData.tooltip).length > 0
                            ToolTip.text: String(settingRow.modelData.tooltip)
                            ToolTip.delay: 700
                        }
                    }
                }
            }
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
