// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// Text as an object: a line of glyphs, a height and a thickness. The dialog lists
// the fonts this machine actually has, because the engine's own font enumeration
// is a no-op off Windows and fontconfig is what answers here.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property var fonts: []
    property string text: "next-layer"
    property real textHeight: 10
    property real depth: 1.2
    property int fontIndex: 0
    property string filterText: ""

    readonly property var shownFonts: {
        const all = root.fonts
        if (root.filterText.length === 0)
            return all
        const matches = []
        for (let i = 0; i < all.length; ++i) {
            if (String(all[i].family).toLowerCase().indexOf(root.filterText.toLowerCase()) >= 0)
                matches.push(all[i])
        }
        return matches
    }

    signal textRequested(string text, real height, real depth, string fontPath)

    modal: true
    dim: true
    padding: 0
    parent: Overlay.overlay
    width: 480
    height: Math.min(620, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    closePolicy: Popup.CloseOnEscape

    onOpened: fontIndex = 0

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
                text: "Text"
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                font.bold: true
                color: Colors.textPrimary
            }

            Field {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                label: "Text"

                TextField {
                    width: parent.width
                    text: root.text
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    selectByMouse: true
                    onTextEdited: root.text = text
                }
            }

            Row {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                spacing: Metrics.sm

                Field {
                    width: (parent.width - Metrics.sm) / 2
                    label: "Height"

                    NumberField {
                        width: parent.width
                        value: root.textHeight
                        from: 1
                        to: 200
                        decimals: 1
                        isLength: true
                        onEdited: (value) => root.textHeight = value
                    }
                }

                Field {
                    width: (parent.width - Metrics.sm) / 2
                    label: "Thickness"

                    NumberField {
                        width: parent.width
                        value: root.depth
                        from: 0.2
                        to: 20
                        decimals: 1
                        isLength: true
                        onEdited: (value) => root.depth = value
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                text: root.shownFonts.length + " fonts"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textMuted
            }

            TextField {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                placeholderText: "filter by family"
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textPrimary
                selectByMouse: true
                onTextEdited: root.filterText = text
            }

            ListView {
                id: fontList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                clip: true
                model: root.shownFonts
                spacing: 1

                delegate: Rectangle {
                    id: fontRow

                    required property int index
                    required property var modelData

                    width: fontList.width
                    height: 32
                    radius: Metrics.radiusSmall
                    color: fontRow.index === root.fontIndex ? Colors.surfaceContainerHigh
                                                            : (rowMouse.containsMouse
                                                               ? Colors.surfaceContainer : "transparent")

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Metrics.sm
                        anchors.rightMargin: Metrics.sm
                        verticalAlignment: Text.AlignVCenter
                        text: fontRow.modelData.family
                        elide: Text.ElideRight
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textSecondary
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.fontIndex = fontRow.index
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                spacing: Metrics.sm

                Item { Layout.fillWidth: true }

                Button {
                    text: "cancel"
                    onClicked: root.close()
                }

                Button {
                    text: "add"
                    primary: true
                    enabled: root.text.trim().length > 0 && root.shownFonts.length > 0
                    onClicked: {
                        const font = root.shownFonts[Math.min(root.fontIndex,
                                                              root.shownFonts.length - 1)]
                        root.textRequested(root.text, root.textHeight, root.depth,
                                           font === undefined ? "" : String(font.path))
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
