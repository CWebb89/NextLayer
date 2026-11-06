// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// The calibration suite: pick a test, then
// set the parameters that shape it before it goes on the plate. Each row is a
// test plate this app builds itself -- the geometry from the engine's primitives,
// the per-band change from the plate's own layer G-code -- so nothing here
// depends on a checked-in model.
Popup {
    id: root

    property string message: ""
    property var selected: null
    property var activeParams: []
    property var values: ({})
    property string flavour: "klipper"
    property real applyValue: 0

    parent: Overlay.overlay
    width: Math.min(880, (parent ? parent.width : 1280) - 140)
    height: Math.min(620, (parent ? parent.height : 800) - 110)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant
    }

    /// The values the plate's own print setup supplies, so the dialog opens on the
    /// machine's nozzle and speed rather than on a 0.4 mm reference.
    function settingNumber(name, fallback) {
        const value = Number(settingsModel.value(name))
        return isNaN(value) || value <= 0 ? fallback : value
    }

    function setParam(name, value) {
        const next = Object.assign({}, root.values)
        next[name] = value
        root.values = next
    }

    function select(test) {
        root.selected = test
        root.activeParams = test.params
        const next = ({})
        for (const param of test.params) {
            next[param.name] = param.value
            if (param.name === "nozzle")
                next.nozzle = root.settingNumber("nozzle_diameter", 0.4)
            else if (param.name === "speed")
                next.speed = root.settingNumber("print_speed", 100)
        }
        root.values = next
        const flavour = String(settingsModel.value("gcode_flavor"))
        root.flavour = flavour.length > 0 ? flavour : "klipper"
        root.applyValue = 0
    }

    function paramValues() {
        return Object.assign({ "flavour": root.flavour }, root.values)
    }

    function build() {
        root.message = calibration.build(root.selected.kind, root.paramValues())
        root.close()
    }

    function applyResult() {
        const setting = root.selected ? root.selected.apply : ""
        if (setting === "" || root.applyValue <= 0)
            return
        settingsModel.setValue(setting, root.applyValue * root.selected.applyScale)
        root.message = "saved " + root.applyValue + " to " + setting
        root.close()
    }

    contentItem: Item {
        implicitWidth: dialogBody.implicitWidth
        implicitHeight: dialogBody.implicitHeight

        RowLayout {
            id: dialogBody
            anchors.fill: parent
            spacing: 0

            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.margins: Metrics.lg
                    Layout.bottomMargin: Metrics.sm

                    Text {
                        anchors.left: parent.left
                        anchors.baseline: closeButton.baseline
                        text: "Calibration"
                        font.family: Type.family
                        font.pixelSize: Type.sizeBody
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    ToolButton {
                        id: closeButton
                        anchors.right: parent.right
                        anchors.top: parent.top
                        icon: "close"
                        hint: "Close"
                        onClicked: root.close()
                    }
                }

                Separator { Layout.fillWidth: true }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Metrics.sm
                    clip: true
                    spacing: Metrics.xxs
                    model: calibration.tests

                    delegate: Rectangle {
                        id: testRow
                        required property var modelData

                        width: ListView.view.width
                        height: 34
                        radius: Metrics.radiusSmall
                        color: root.selected !== null && root.selected.kind === modelData.kind
                               ? Colors.accentDim
                               : (testMouse.containsMouse ? Colors.hoverFill : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Metrics.sm
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Metrics.sm * 2
                            text: testRow.modelData.title
                            elide: Text.ElideRight
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            color: Colors.textPrimary
                        }

                        MouseArea {
                            id: testMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.select(testRow.modelData)
                        }
                    }
                }

                Component.onCompleted: {
                    const tests = calibration.tests
                    if (tests.length > 0)
                        root.select(tests[0])
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: Metrics.borderWidth
                color: Colors.outlineVariant
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Metrics.lg
                spacing: Metrics.md

                Text {
                    Layout.fillWidth: true
                    text: root.selected ? root.selected.title : ""
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    Layout.fillWidth: true
                    text: root.selected ? root.selected.instruction : ""
                    wrapMode: Text.WordWrap
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textTertiary
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: form.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: form
                        width: parent.width
                        spacing: Metrics.xs

                        Repeater {
                            model: root.activeParams

                            delegate: RowLayout {
                                id: paramRow
                                required property var modelData

                                Layout.fillWidth: true
                                spacing: Metrics.sm

                                Text {
                                    Layout.preferredWidth: 230
                                    text: paramRow.modelData.label
                                    elide: Text.ElideRight
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeSmall
                                    color: Colors.textSecondary
                                }

                                NumberField {
                                    visible: paramRow.modelData.kind === "number"
                                    Layout.preferredWidth: 170
                                    from: paramRow.modelData.min === undefined ? -1e9 : paramRow.modelData.min
                                    to: paramRow.modelData.max === undefined ? 1e9 : paramRow.modelData.max
                                    decimals: paramRow.modelData.decimals === undefined ? 2 : paramRow.modelData.decimals
                                    units: paramRow.modelData.unit === undefined ? "" : paramRow.modelData.unit
                                    value: Number(root.values[paramRow.modelData.name])
                                    onEdited: (number) => root.setParam(paramRow.modelData.name, number)
                                }

                                ComboBox {
                                    visible: paramRow.modelData.kind === "choice"
                                    Layout.preferredWidth: 250
                                    choices: paramRow.modelData.choices === undefined ? []
                                                                                    : paramRow.modelData.choices
                                    currentValue: root.values[paramRow.modelData.name]
                                    onSelected: (choice) => {
                                        root.setParam(paramRow.modelData.name, choice)
                                        const entry = paramRow.modelData.choices.find(
                                                        c => c.value === choice)
                                        if (entry === undefined || entry.start === undefined)
                                            return
                                        root.setParam("start", entry.start)
                                        root.setParam("end", entry.end)
                                    }
                                }

                                Toggle {
                                    visible: paramRow.modelData.kind === "bool"
                                    checked: root.values[paramRow.modelData.name] === true
                                    onToggled: (checked) => root.setParam(paramRow.modelData.name, checked)
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        RowLayout {
                            visible: root.selected !== null && root.selected.apply !== ""
                            Layout.fillWidth: true
                            Layout.topMargin: Metrics.sm
                            spacing: Metrics.sm

                            Text {
                                Layout.preferredWidth: 230
                                text: "Apply to " + (root.selected ? root.selected.apply : "")
                                elide: Text.ElideRight
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: Colors.textSecondary
                            }

                            NumberField {
                                Layout.preferredWidth: 170
                                from: 0
                                to: 1000
                                decimals: 3
                                value: root.applyValue
                                onEdited: (number) => root.applyValue = number
                            }

                            Button {
                                text: "Apply"
                                onClicked: root.applyResult()
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.sm

                    Text {
                        Layout.fillWidth: true
                        text: root.selected !== null && root.selected.apply !== ""
                              ? "Measure the plate, type what you measured, then add it to the settings."
                              : "This test is read by eye; the value it gives goes into the printer's own configuration."
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }

                    Button {
                        text: "Build the plate"
                        enabled: root.selected !== null
                        onClicked: root.build()
                    }
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
