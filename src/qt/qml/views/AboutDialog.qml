// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// About: the product, the engine it runs on, and where its files live. Every
// line is a fact the binary can answer, so this cannot drift from the build.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property string version: ""
    property string engineVersion: ""
    property string dataDir: ""
    property string presetDir: ""

    parent: Overlay.overlay
    width: Math.min(560, parent.width - Metrics.xl * 2)
    height: content.implicitHeight + Metrics.xl * 2
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: Metrics.radiusMedium
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        ColumnLayout {
            id: content
            anchors.fill: parent

            spacing: Metrics.md
            Layout.margins: Metrics.xl

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.xl
                Layout.rightMargin: Metrics.xl
                Layout.topMargin: Metrics.xl
                spacing: Metrics.md

                Image {
                    source: "qrc:/next-layer/brand/next-layer-mark.svg"
                    sourceSize: Qt.size(Type.sizeXLarge, Type.sizeXLarge)
                    fillMode: Image.PreserveAspectFit
                    Layout.preferredWidth: Type.sizeXLarge
                    Layout.preferredHeight: Type.sizeXLarge
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        text: "next-layer"
                        font.family: Type.family
                        font.pixelSize: Type.sizeHeading
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        text: "a slicer built for tool changers"
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textSecondary
                    }
                }

                ToolButton {
                    icon: "close"
                    label: "Close"
                    hint: "Close"
                    onClicked: root.close()
                }
            }

            Divider { Layout.fillWidth: true }

            GridLayout {
                Layout.leftMargin: Metrics.xl
                Layout.rightMargin: Metrics.xl
                columns: 2
                columnSpacing: Metrics.lg
                rowSpacing: Metrics.sm

                Text {
                    text: "Version"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
                Text {
                    text: root.version
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textPrimary
                }

                Text {
                    text: "Engine"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
                Text {
                    text: root.engineVersion
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textPrimary
                }

                Text {
                    text: "Licence"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
                Text {
                    text: "AGPL-3.0-or-later"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textPrimary
                }

                Text {
                    text: "Data"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
                Text {
                    text: root.dataDir
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textSecondary
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Text {
                    text: "Presets"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
                Text {
                    text: root.presetDir
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textSecondary
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillHeight: true; Layout.preferredHeight: Metrics.sm }

            Text {
                Layout.leftMargin: Metrics.xl
                Layout.rightMargin: Metrics.xl
                Layout.fillWidth: true
                text: "This program is free software under the GNU Affero General Public License, version 3 or later, with no warranty. The full licence text and every third-party notice ship with the source, as LICENSE and THIRD-PARTY.md."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Item { Layout.fillHeight: true; Layout.preferredHeight: Metrics.xs }

            RowLayout {
                Layout.leftMargin: Metrics.xl
                Layout.rightMargin: Metrics.xl
                Layout.bottomMargin: Metrics.xl

                Item { Layout.fillWidth: true }

                Button {
                    text: "Keyboard shortcuts"
                    onClicked: {
                        root.close()
                        shortcutMap.openWithCurrent()
                    }
                }

                Button {
                    text: "Close"
                    primary: true
                    onClicked: root.close()
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
