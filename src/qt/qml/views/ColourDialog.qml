// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// Colour picker for the engine settings that carry a colour: a filament colour
// swatch grid plus a free entry, because the value is a plain colour string the
// engine reads back.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property var setting: ({})
    property string value: "#000000"
    property string draft: "#000000"

    readonly property string settingName: root.setting.name === undefined ? ""
                                         : String(root.setting.name)

    readonly property var swatches: [
        "#000000", "#FFFFFF", "#8C8C8C", "#E53935", "#FB8C00", "#FDD835", "#43A047",
        "#00ACC1", "#1E88E5", "#3949AB", "#8E24AA", "#D81B60", "#6D4C41", "#F5F5F5",
        "#29B2B2", "#C0CA33", "#00897B", "#5E35B1", "#FF7043", "#7CB342", "#00BCD4",
        "#90A4AE", "#FFD54F", "#EF5350"
    ]

    parent: Overlay.overlay
    width: Math.min(460, parent.width - Metrics.xl * 2)
    height: Math.min(520, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(item) {
        root.setting = item
        root.value = item.value === undefined || item.value === null ? "" : String(item.value)
        root.draft = root.value.length > 0 ? root.value : "#000000"
        root.open()
    }

    function apply() {
        settingsModel.setValue(root.settingName, root.draft)
        root.close()
    }

    background: Rectangle {
        radius: Metrics.radiusMedium
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant
    }

    contentItem: Item {
        implicitWidth: dialogBody.implicitWidth
        implicitHeight: dialogBody.implicitHeight

        ColumnLayout {
            id: dialogBody
            anchors.fill: parent

            spacing: Metrics.sm

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.topMargin: Metrics.lg

                Text {
                    Layout.fillWidth: true
                    text: root.setting.label === undefined ? "Colour"
                                                           : String(root.setting.label)
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                    elide: Text.ElideRight
                }

                ToolButton {
                    icon: "close"
                    hint: "Close without applying"
                    onClicked: root.close()
                }
            }

            Rectangle {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.fillWidth: true
                Layout.preferredHeight: Metrics.controlHeight * 1.6
                radius: Metrics.radiusSmall
                color: root.draft
                border.width: Metrics.borderWidth
                border.color: Colors.outlineVariant
            }

            GridLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                columns: 8
                columnSpacing: Metrics.sm
                rowSpacing: Metrics.sm

                Repeater {
                    model: root.swatches

                    delegate: Rectangle {
                        id: swatch

                        required property string modelData

                        width: Metrics.controlHeight
                        height: Metrics.controlHeight
                        radius: Metrics.radiusSmall
                        color: swatch.modelData
                        border.width: root.draft.toUpperCase() === swatch.modelData.toUpperCase()
                                      ? Metrics.cardBorderWidth : Metrics.borderWidth
                        border.color: root.draft.toUpperCase() === swatch.modelData.toUpperCase()
                                      ? Colors.accent : Colors.outlineVariant

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.draft = swatch.modelData
                        }
                    }
                }
            }

            TextField {
                id: hexField
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.fillWidth: true
                Layout.preferredHeight: Metrics.controlHeight
                text: root.draft
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textPrimary
                selectByMouse: true
                onTextEdited: root.draft = hexField.text

                background: Rectangle {
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer
                    border.width: Metrics.borderWidth
                    border.color: hexField.activeFocus ? Colors.primary : Colors.outlineVariant
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.lg

                Button {
                    text: "Cancel"
                    onClicked: root.close()
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Apply"
                    primary: true
                    onClicked: root.apply()
                }
            }
    
        }

        ResizeGrip {
            dialog: root
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: Metrics.xs
            anchors.bottomMargin: Metrics.xs
        }
    }
}
