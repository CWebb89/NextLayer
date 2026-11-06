// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// G-code substitutions editor. The engine stores them as one flat string list,
// four entries per substitution (search, replacement, parameters, notes) — see
// GCodeFindReplace — so the dialog groups them by four in both directions.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property var setting: ({})
    property var rows: []
    property string message: ""

    readonly property string settingName: root.setting.name === undefined ? ""
                                         : String(root.setting.name)

    parent: Overlay.overlay
    width: Math.min(760, parent.width - Metrics.xl * 2)
    height: Math.min(640, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(item) {
        root.setting = item
        root.message = ""
        root.rows = []
        const value = Array.isArray(item.value) ? item.value : []
        for (let i = 0; i + 3 < value.length; i += 4)
            root.rows.push([String(value[i]), String(value[i + 1]), String(value[i + 2]),
                            String(value[i + 3])])
        root.open()
    }

    function apply() {
        const flat = []
        for (let i = 0; i < root.rows.length; ++i) {
            const row = root.rows[i]
            if (String(row[0]).length === 0 && String(row[1]).length === 0)
                continue
            flat.push(String(row[0]), String(row[1]), String(row[2]), String(row[3]))
        }
        settingsModel.setValue(root.settingName, flat)
        root.close()
    }

    function updateCell(rowIndex, column, text) {
        const next = root.rows.slice()
        const row = next[rowIndex].slice()
        row[column] = text
        next[rowIndex] = row
        root.rows = next
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
                    text: "G-code substitutions"
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                ToolButton {
                    icon: "close"
                    hint: "Close without applying"
                    onClicked: root.close()
                }
            }

            Text {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.fillWidth: true
                text: "Parameters: r = regular expression, i = case insensitive, w = whole word, "
                      + "s = single line. Notes are for you, the engine ignores them."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textMuted
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                contentWidth: width
                contentHeight: rowColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: rowColumn
                    width: parent.width
                    spacing: Metrics.sm

                    Repeater {
                        model: root.rows

                        delegate: Rectangle {
                            id: substitutionRow

                            required property int index

                            Layout.fillWidth: true
                            implicitHeight: rowLayout.implicitHeight + Metrics.md * 2
                            radius: Metrics.radiusSmall
                            color: Colors.surfaceContainer
                            border.width: Metrics.borderWidth
                            border.color: Colors.outlineVariant

                            ColumnLayout {
                                id: rowLayout
                                anchors.fill: parent
                                anchors.margins: Metrics.md
                                spacing: Metrics.xs

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "Search"
                                        Layout.preferredWidth: Metrics.sectionGap * 8
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeCaption
                                        color: Colors.textSecondary
                                    }

                                    TextField {
                                        Layout.fillWidth: true
                                        text: substitutionRow.index < root.rows.length
                                              ? String(root.rows[substitutionRow.index][0]) : ""
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeSmall
                                        color: Colors.textPrimary
                                        selectByMouse: true
                                        onEditingFinished: root.updateCell(substitutionRow.index, 0, text)

                                        background: Rectangle {
                                            radius: Metrics.radiusSmall
                                            color: Colors.surfaceContainerHigh
                                            border.width: Metrics.borderWidth
                                            border.color: Colors.outlineVariant
                                        }
                                    }

                                    ToolButton {
                                        icon: "close"
                                        hint: "Remove this substitution"
                                        onClicked: {
                                            const next = root.rows.slice()
                                            next.splice(substitutionRow.index, 1)
                                            root.rows = next
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "Replace"
                                        Layout.preferredWidth: Metrics.sectionGap * 8
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeCaption
                                        color: Colors.textSecondary
                                    }

                                    TextField {
                                        Layout.fillWidth: true
                                        text: substitutionRow.index < root.rows.length
                                              ? String(root.rows[substitutionRow.index][1]) : ""
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeSmall
                                        color: Colors.textPrimary
                                        selectByMouse: true
                                        onEditingFinished: root.updateCell(substitutionRow.index, 1, text)

                                        background: Rectangle {
                                            radius: Metrics.radiusSmall
                                            color: Colors.surfaceContainerHigh
                                            border.width: Metrics.borderWidth
                                            border.color: Colors.outlineVariant
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "Params"
                                        Layout.preferredWidth: Metrics.sectionGap * 8
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeCaption
                                        color: Colors.textSecondary
                                    }

                                    TextField {
                                        Layout.preferredWidth: Metrics.rowHeight
                                        text: substitutionRow.index < root.rows.length
                                              ? String(root.rows[substitutionRow.index][2]) : ""
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeSmall
                                        color: Colors.textPrimary
                                        selectByMouse: true
                                        onEditingFinished: root.updateCell(substitutionRow.index, 2, text)

                                        background: Rectangle {
                                            radius: Metrics.radiusSmall
                                            color: Colors.surfaceContainerHigh
                                            border.width: Metrics.borderWidth
                                            border.color: Colors.outlineVariant
                                        }
                                    }

                                    Text {
                                        text: "Notes"
                                        Layout.preferredWidth: Metrics.sectionGap * 6
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeCaption
                                        color: Colors.textSecondary
                                    }

                                    TextField {
                                        Layout.fillWidth: true
                                        text: substitutionRow.index < root.rows.length
                                              ? String(root.rows[substitutionRow.index][3]) : ""
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeSmall
                                        color: Colors.textPrimary
                                        selectByMouse: true
                                        onEditingFinished: root.updateCell(substitutionRow.index, 3, text)

                                        background: Rectangle {
                                            radius: Metrics.radiusSmall
                                            color: Colors.surfaceContainerHigh
                                            border.width: Metrics.borderWidth
                                            border.color: Colors.outlineVariant
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: "Add substitution"
                            onClicked: {
                                const next = root.rows.slice()
                                next.push(["", "", "", ""])
                                root.rows = next
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            RowLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.lg

                Text {
                    Layout.fillWidth: true
                    text: root.rows.length + " substitution" + (root.rows.length === 1 ? "" : "s")
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                Button {
                    text: "Cancel"
                    onClicked: root.close()
                }

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
