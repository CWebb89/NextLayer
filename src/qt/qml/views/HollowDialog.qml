// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../theme"
import "../components"

// Hollow: how thick the inner wall should be. Two millimetres is the default
// and what a 0.4 mm nozzle prints as five perimeters.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property int objectId: 0
    property real thickness: 2.0

    signal hollowRequested(int objectId, real thickness)

    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: 0

    function openFor(id) {
        root.objectId = id
        root.open()
    }

    background: Rectangle {
        radius: Metrics.radiusLarge
        color: Colors.surface
        border.width: Metrics.borderWidth
        border.color: Colors.edgeStrong
    }

    contentItem: Item {
        implicitWidth: dialogBody.implicitWidth
        implicitHeight: dialogBody.implicitHeight

        ColumnLayout {
            id: dialogBody
            anchors.fill: parent

            spacing: Metrics.md

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                Layout.topMargin: Metrics.md
                text: "Hollow"
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                text: "A wall this thick is grown inside the surface; the interior prints as "
                      + "perimeters only. The object keeps its outer size."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textSecondary
            }

            Field {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                label: "Wall thickness"

                NumberField {
                    width: parent.width
                    value: root.thickness
                    from: 0.4
                    to: 20
                    decimals: 1
                    units: "mm"
                    onEdited: (value) => root.thickness = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.md

                Item { Layout.fillWidth: true }

                ToolButton {
                    icon: "close"
                    hint: "Close"
                    onClicked: root.close()
                }

                Button {
                    text: "cancel"
                    onClicked: root.close()
                }

                Button {
                    text: "hollow"
                    primary: true
                    onClicked: {
                        root.hollowRequested(root.objectId, root.thickness)
                        root.close()
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
