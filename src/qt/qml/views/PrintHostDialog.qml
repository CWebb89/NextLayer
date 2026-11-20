// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextLayer 1.0
import "../theme"
import "../components"

// Send the sliced plate to the machine, through the engine's print-host family.
// The connection is stored next to the presets, so it is set once and the buttons
// afterwards are Test and Send.
Popup {
    id: root

    property string message: ""
    property bool messageOk: false

    width: Math.min(620, (parent ? parent.width : 1280) - 140)
    height: Math.min(parent ? parent.height - Metrics.xl * 2 : 460, 470)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant
    }

    onOpened: {
        root.message = ""
        PrinterLink.load()
    }

    function report(text) {
        root.message = text
        root.messageOk = text.startsWith("connected") || text.startsWith("sent")
    }

    contentItem: Item {
        implicitWidth: dialogBody.implicitWidth
        implicitHeight: dialogBody.implicitHeight

        ColumnLayout {
            id: dialogBody
            anchors.fill: parent

            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.lg
                spacing: Metrics.sm

                Text {
                    text: "Send to printer"
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    icon: "close"
                    hint: "Close"
                    onClicked: root.close()
                }
            }

            Separator { Layout.fillWidth: true }

            GridLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.lg
                columns: 2
                columnSpacing: Metrics.sm
                rowSpacing: Metrics.sm

                Text {
                    text: "Host"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textSecondary
                }

                TextField {
                    id: hostField
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.controlHeight
                    text: PrinterLink.host
                    placeholderText: "voron-24.local"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    selectByMouse: true
                    onTextChanged: {
                        PrinterLink.host = text
                        PrinterLink.save()
                    }
                }

                Text {
                    text: "Type"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textSecondary
                }

                ComboBox {
                    Layout.fillWidth: true
                    choices: PrinterLink.kinds().map((entry) => entry.label)
                    currentValue: {
                        const kinds = PrinterLink.kinds()
                        for (let i = 0; i < kinds.length; ++i)
                            if (kinds[i].id === PrinterLink.kind)
                                return kinds[i].label
                        return kinds.length > 0 ? kinds[0].label : ""
                    }
                    onSelected: (value) => {
                        const kinds = PrinterLink.kinds()
                        for (let i = 0; i < kinds.length; ++i) {
                            if (kinds[i].label === value) {
                                PrinterLink.kind = kinds[i].id
                                PrinterLink.save()
                            }
                        }
                    }
                }

                Text {
                    text: "API key"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textSecondary
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.controlHeight
                    text: PrinterLink.apiKey
                    placeholderText: "from moonraker.asvc, if one is set"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    echoMode: TextInput.Password
                    selectByMouse: true
                    onTextChanged: {
                        PrinterLink.apiKey = text
                        PrinterLink.save()
                    }
                }

                // Host offsets: the compensation table for a machine that is a
                // few hundredths out of alignment. It is a printer property, not a
                // print setting, so it lives with the connection and travels with
                // the file -- export and upload both carry it.
                Text {
                    text: "Offsets"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textSecondary
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.xs

                    Repeater {
                        model: [
                            { axis: "X", value: PrinterLink.offsetX },
                            { axis: "Y", value: PrinterLink.offsetY },
                            { axis: "Z", value: PrinterLink.offsetZ }
                        ]

                        delegate: RowLayout {
                            id: offsetField

                            required property var modelData

                            spacing: Metrics.xs

                            Text {
                                text: offsetField.modelData.axis
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }

                            NumberField {
                                Layout.preferredWidth: 92
                                value: offsetField.modelData.value
                                from: -50
                                to: 50
                                decimals: 3
                                isLength: true
                                onEdited: (value) => {
                                    if (offsetField.modelData.axis === "X")
                                        PrinterLink.offsetX = value
                                    else if (offsetField.modelData.axis === "Y")
                                        PrinterLink.offsetY = value
                                    else
                                        PrinterLink.offsetZ = value
                                    PrinterLink.save()
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Item {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 1
                }

                Text {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    visible: PrinterLink.hasOffsets
                    text: "Written into the G-code as " + PrinterLink.offsetsLine
                          + " before the first extrusion. Klipper applies it; other "
                          + "firmware ignores the command."
                    wrapMode: Text.WordWrap
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                visible: root.message.length > 0
                text: root.message
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: root.messageOk ? Colors.accent : Colors.error
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.lg
                spacing: Metrics.sm

                Button {
                    text: "Test"
                    hint: "Ask the printer for its version"
                    enabled: PrinterLink.configured
                    onClicked: root.report(PrinterLink.test())
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Send"
                    hint: SliceController.gcodePath
                    primary: true
                    enabled: PrinterLink.configured && SliceController.gcodePath.length > 0
                    onClicked: root.report(PrinterLink.upload(SliceController.gcodePath, false))
                }

                Button {
                    text: "Send and print"
                    hint: "Upload, then start the job on the machine"
                    enabled: PrinterLink.configured && SliceController.gcodePath.length > 0
                    onClicked: root.report(PrinterLink.upload(SliceController.gcodePath, true))
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
