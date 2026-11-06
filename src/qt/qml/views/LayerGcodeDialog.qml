// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The layer-slider right-click: attach a custom
// G-code snippet, a pause or a tool change to one height of the print. The
// engine emits it at that layer on the next slice, so the dialog says so.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property double layerZ: 0
    property int layerIndex: 0
    property string kind: "gcode"

    signal submitted(string kind, string text)

    parent: Overlay.overlay
    width: 560
    height: 340
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(z, index, what) {
        root.layerZ = z
        root.layerIndex = index
        root.kind = what
        editor.text = what === "pause" ? "Paused by next-layer" : ""
        root.open()
        editor.forceActiveFocus()
    }

    readonly property string title: root.kind === "pause" ? "Pause at layer "
                                     : root.kind === "tool" ? "Tool change at layer "
                                                            : "Custom G-code at layer "

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.edge

        Column {
            anchors.fill: parent
            anchors.margins: Metrics.panelPadding
            spacing: Metrics.md

            Text {
                text: root.title + (root.layerIndex + 1)
                      + "   \u00b7   " + root.layerZ.toFixed(2) + " mm"
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                width: parent.width
                text: root.kind === "pause"
                      ? "The printer stops here and shows this message. M601 is written into the G-code."
                      : "Written verbatim into the G-code at the start of this layer. It is emitted by the engine, so it survives a re-slice."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Rectangle {
                width: parent.width
                height: 150
                radius: Metrics.radiusSmall
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: editor.activeFocus ? Colors.accent : Colors.edge

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: Metrics.sm

                    TextArea {
                        id: editor
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textPrimary
                        wrapMode: TextArea.Wrap
                        background: null
                        selectByMouse: true
                        placeholderText: root.kind === "pause" ? "Message for the printer display"
                                                                : "G1 X10 Y10 F3000"
                    }
                }
            }

            Item { width: 1; height: Metrics.xs }

            Row {
                spacing: Metrics.sm

                Button {
                    text: "Add marker"
                    primary: true
                    enabled: editor.text.trim().length > 0
                    onClicked: {
                        root.submitted(root.kind, editor.text)
                        root.close()
                    }
                }

                Button {
                    text: "Cancel"
                    onClicked: root.close()
                }
            }
        }
    }
}
