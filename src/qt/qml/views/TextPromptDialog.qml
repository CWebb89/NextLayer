// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// One line, one answer. Used where a name has to be typed: renaming a plate.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property string title: ""
    property string value: ""
    property string placeholder: ""
    property string action: "Apply"

    signal submitted(string text)

    width: 460
    height: 200
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openWith(heading, current, hint, actionLabel) {
        root.title = heading
        root.value = current
        root.placeholder = hint
        root.action = actionLabel === undefined || actionLabel.length === 0
                      ? "Apply" : actionLabel
        field.text = current
        root.open()
        field.forceActiveFocus()
        field.selectAll()
    }

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

            Row {
                width: parent.width

                Text {
                    width: parent.width - promptClose.width
                    text: root.title
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                ToolButton {
                    id: promptClose
                    icon: "close"
                    hint: "Close"
                    onClicked: root.close()
                }
            }

            Rectangle {
                width: parent.width
                height: Metrics.controlHeight + Metrics.sm
                radius: Metrics.radiusSmall
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: field.activeFocus ? Colors.accent : Colors.edge

                TextInput {
                    id: field
                    anchors.fill: parent
                    anchors.margins: Metrics.sm
                    verticalAlignment: TextInput.AlignVCenter
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    selectionColor: Colors.accent
                    selectByMouse: true

                    onAccepted: {
                        root.submitted(text.trim())
                        root.close()
                    }
                    Keys.onEscapePressed: root.close()
                }
            }

            Row {
                spacing: Metrics.sm

                Button {
                    text: root.action
                    primary: true
                    enabled: field.text.trim().length > 0
                    onClicked: {
                        root.submitted(field.text.trim())
                        root.close()
                    }
                }

                Button {
                    text: "Cancel"
                    onClicked: root.close()
                }
            }
        }

        ResizeGrip {
            dialog: root
            minWidth: 380
            minHeight: 190
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Metrics.xs
        }
    }
}
