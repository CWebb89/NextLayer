// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// Numeric transform panel for the tools that take a value as well as a drag:
// rotate, scale, move, cut, duplicate. A value commits as it is typed -- there is
// no Apply to press, because the field you just edited is the thing you meant.
// It shows only for those tools: opening an empty panel with a dead Apply button
// for a tool that takes no numbers is worse than showing nothing.
Item {
    id: root

    property string tool: ""
    property bool busy: false

    signal rotateRequested(real x, real y, real z)
    signal scaleRequested(real factor)
    signal moveRequested(real x, real y)
    signal cutRequested(real z)
    signal duplicateRequested(int count)
    signal centerRequested()
    signal dropRequested()
    signal dismissed()

    property real rotateX: 0
    property real rotateY: 0
    property real rotateZ: 45
    property real scaleFactor: 1.5
    property real moveX: 125
    property real moveY: 110
    property real cutZ: 10
    property int duplicates: 2

    // Written as property reads, not as a helper call: QML only tracks the
    // dependencies a binding names directly, so `titleFor(root.tool)` would have
    // been evaluated once with an empty tool and never again -- the panel stayed
    // hidden for every tool.
    readonly property bool hasFields: root.tool === "rotate" || root.tool === "scale"
                                      || root.tool === "move" || root.tool === "cut"
                                      || root.tool === "duplicate"

    objectName: "transformPanel"
    visible: root.hasFields
    width: 300
    height: 220

    function commitRotate() {
        root.rotateRequested(root.rotateX, root.rotateY, root.rotateZ)
    }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusMedium
        color: Colors.surfaceContainer
        border.width: Metrics.borderWidth
        border.color: Colors.edge
    }

    Column {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Metrics.panelPadding
        spacing: Metrics.md

        Row {
            width: parent.width
            spacing: Metrics.sm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - closeButton.width - Metrics.sm
                text: root.busy ? "working…"
                                : root.tool === "rotate" ? "Rotate"
                                : root.tool === "scale" ? "Scale"
                                : root.tool === "move" ? "Move"
                                : root.tool === "cut" ? "Cut at Z"
                                : root.tool === "duplicate" ? "Duplicate" : ""
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textPrimary
            }

            ToolButton {
                id: closeButton
                icon: "close"
                hint: "Close this panel"
                onClicked: root.dismissed()
            }
        }

        Field {
            width: parent.width
            visible: root.tool === "rotate"
            label: "X / Y / Z degrees"

            Row {
                width: parent.width
                spacing: Metrics.xs

                NumberField {
                    width: (parent.width - Metrics.xs * 2) / 3
                    value: root.rotateX
                    decimals: 1
                    units: "°"
                    onEdited: (value) => {
                        root.rotateX = value
                        root.commitRotate()
                    }
                }

                NumberField {
                    width: (parent.width - Metrics.xs * 2) / 3
                    value: root.rotateY
                    decimals: 1
                    units: "°"
                    onEdited: (value) => {
                        root.rotateY = value
                        root.commitRotate()
                    }
                }

                NumberField {
                    width: (parent.width - Metrics.xs * 2) / 3
                    value: root.rotateZ
                    decimals: 1
                    units: "°"
                    onEdited: (value) => {
                        root.rotateZ = value
                        root.commitRotate()
                    }
                }
            }
        }

        Field {
            width: parent.width
            visible: root.tool === "scale"
            label: "Uniform factor"

            NumberField {
                width: parent.width
                value: root.scaleFactor
                from: 0.05
                to: 50
                onEdited: (value) => {
                    root.scaleFactor = value
                    root.scaleRequested(value)
                }
            }
        }

        Field {
            width: parent.width
            visible: root.tool === "move"
            label: "Centre on X / Y"

            Row {
                width: parent.width
                spacing: Metrics.xs

                NumberField {
                    width: (parent.width - Metrics.xs) / 2
                    value: root.moveX
                    units: "x"
                    isLength: true
                    onEdited: (value) => {
                        root.moveX = value
                        root.moveRequested(value, root.moveY)
                    }
                }

                NumberField {
                    width: (parent.width - Metrics.xs) / 2
                    value: root.moveY
                    units: "y"
                    isLength: true
                    onEdited: (value) => {
                        root.moveY = value
                        root.moveRequested(root.moveX, value)
                    }
                }
            }
        }

        Field {
            width: parent.width
            visible: root.tool === "cut"
            label: "Cut plane Z"

            NumberField {
                width: parent.width
                value: root.cutZ
                from: 0.1
                units: "mm"
                isLength: true
                onEdited: (value) => {
                    root.cutZ = value
                    root.cutRequested(value)
                }
            }
        }

        Field {
            width: parent.width
            visible: root.tool === "duplicate"
            label: "Copies"

            NumberField {
                width: parent.width
                value: root.duplicates
                from: 1
                to: 100
                decimals: 0
                onEdited: (value) => {
                    root.duplicates = Math.round(value)
                    root.duplicateRequested(root.duplicates)
                }
            }
        }

        Row {
            width: parent.width
            spacing: Metrics.sm
            visible: root.tool === "move" || root.tool === "rotate"

            Button {
                text: "centre"
                visible: root.tool === "move"
                enabled: !root.busy
                onClicked: root.centerRequested()
            }

            Button {
                text: "drop"
                visible: root.tool === "move" || root.tool === "rotate"
                enabled: !root.busy
                onClicked: root.dropRequested()
            }
        }
    }
}
