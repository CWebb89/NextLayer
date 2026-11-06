// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// Numeric transform panel for the tools that need a value rather than a drag
// gizmo: rotate, scale, move, cut, duplicate. Each row commits straight to the
// transform controller, which runs the engine and reloads the resulting mesh.
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

    property real rotateX: 0
    property real rotateY: 0
    property real rotateZ: 45
    property real scaleFactor: 1.5
    property real moveX: 125
    property real moveY: 110
    property real cutZ: 10
    property int duplicates: 2

    readonly property var titles: ({
        rotate: "Rotate",
        scale: "Scale",
        move: "Move",
        cut: "Cut at Z",
        duplicate: "Duplicate"
    })

    visible: root.tool !== ""
    width: 300
    height: body.implicitHeight + Metrics.panelPadding * 2

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
                text: root.titles[root.tool] !== undefined ? root.titles[root.tool] : ""
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textPrimary
            }

            Item { width: parent.width - 120; height: 1 }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.busy ? "working…" : "esc closes"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textMuted
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
                    onEdited: (value) => root.rotateX = value
                }

                NumberField {
                    width: (parent.width - Metrics.xs * 2) / 3
                    value: root.rotateY
                    decimals: 1
                    units: "°"
                    onEdited: (value) => root.rotateY = value
                }

                NumberField {
                    width: (parent.width - Metrics.xs * 2) / 3
                    value: root.rotateZ
                    decimals: 1
                    units: "°"
                    onEdited: (value) => root.rotateZ = value
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
                onEdited: (value) => root.scaleFactor = value
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
                    onEdited: (value) => root.moveX = value
                }

                NumberField {
                    width: (parent.width - Metrics.xs) / 2
                    value: root.moveY
                    units: "y"
                    isLength: true
                    onEdited: (value) => root.moveY = value
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
                onEdited: (value) => root.cutZ = value
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
                onEdited: (value) => root.duplicates = Math.round(value)
            }
        }

        Row {
            width: parent.width
            spacing: Metrics.sm

            Button {
                text: "apply"
                primary: true
                enabled: !root.busy
                onClicked: {
                    if (root.tool === "rotate")
                        root.rotateRequested(root.rotateX, root.rotateY, root.rotateZ)
                    else if (root.tool === "scale")
                        root.scaleRequested(root.scaleFactor)
                    else if (root.tool === "move")
                        root.moveRequested(root.moveX, root.moveY)
                    else if (root.tool === "cut")
                        root.cutRequested(root.cutZ)
                    else if (root.tool === "duplicate")
                        root.duplicateRequested(root.duplicates)
                }
            }

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
