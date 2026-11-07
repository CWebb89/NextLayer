// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// Preset diff: every setting the panel has changed since the selected preset
// was loaded, before and after, grouped by tab and sub-tab. This is also the
// only honest answer to "what would saving this preset actually write", because
// saving writes exactly this list (SettingsModel::valuesYaml(changedOnly)).
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property string tab: ""
    property var entries: []

    readonly property int count: entries.length

    function refresh() {
        root.entries = settingsModel.modifiedValues(root.tab)
    }

    function openFor(tab) {
        root.tab = tab === undefined ? "" : tab
        root.refresh()
        root.open()
    }

    parent: Overlay.overlay
    width: Math.min(720, parent.width - Metrics.xl * 2)
    height: Math.min(620, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: root.refresh()

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

            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                spacing: Metrics.sm

                Text {
                    text: "Changed settings"
                    font.family: Type.family
                    font.pixelSize: Type.sizeTitle
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: root.count === 0 ? "nothing changed"
                                           : root.count + (root.count === 1 ? " setting" : " settings")
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                    Layout.fillWidth: true
                }

                Button {
                    text: "Reset all"
                    enabled: root.count > 0
                    onClicked: {
                        if (root.tab.length > 0)
                            settingsModel.resetTab(root.tab)
                        root.refresh()
                    }
                }

                ToolButton {
                    icon: "close"
                    label: "Close"
                    hint: "Close the diff"
                    onClicked: root.close()
                }
            }

            Divider { Layout.fillWidth: true }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Metrics.md
                clip: true
                spacing: Metrics.xs
                model: root.entries

                delegate: Rectangle {
                    id: row
                    required property var modelData

                    width: ListView.view.width
                    height: 52
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Metrics.sm
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Metrics.sm

                            Text {
                                text: row.modelData.label
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: row.modelData.subtab
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Metrics.sm

                            Text {
                                text: row.modelData.before === undefined ? ""
                                                                         : String(row.modelData.before)
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                                elide: Text.ElideRight
                                Layout.maximumWidth: 200
                            }

                            Text {
                                text: "\u2192"
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }

                            Text {
                                text: row.modelData.after === undefined ? ""
                                                                        : String(row.modelData.after)
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.accent
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            ToolButton {
                                icon: "undo"
                                label: "Reset"
                                hint: "Put this setting back to the preset's value"
                                onClicked: {
                                    settingsModel.reset(row.modelData.name)
                                    root.refresh()
                                }
                            }
                        }
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
