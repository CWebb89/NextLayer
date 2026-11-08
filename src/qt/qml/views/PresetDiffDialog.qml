// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// Two answers from one window.
//
// "Changes" lists every setting the panel has edited since the selected preset
// was loaded, before and after -- the only honest answer to "what would saving
// this preset actually write", because saving writes exactly this list
// (SettingsModel::valuesYaml(changedOnly)).
//
// "Compare" puts two presets of the same kind side by side: every value they set
// differently. Inherited values are not shown, because the files do not carry
// them, so this answers "how far did my copy drift from the one it came from".
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property string mode: "changes"
    property string tab: ""
    property string kind: ""
    property string aName: ""
    property string bName: ""
    property var entries: []

    readonly property bool comparing: root.mode === "compare"
    readonly property int count: entries.length

    readonly property string title: root.comparing
                                  ? (root.kind === "printer" ? "Compare machines"
                                   : root.kind === "filament" ? "Compare filaments"
                                                              : "Compare processes")
                                  : "Changed settings"

    readonly property var choices: {
        if (root.kind === "printer") {
            const out = []
            const printers = presetStore.userPrinters.concat(presetStore.catalogPrinters)
            for (let i = 0; i < printers.length; ++i)
                out.push({ value: printers[i].name, label: printers[i].name })
            return out
        }
        return root.kind === "filament" ? presetStore.filamentProfiles
                                        : presetStore.printProfiles
    }

    function refresh() {
        if (!root.comparing) {
            root.entries = settingsModel.modifiedValues(root.tab)
            return
        }
        root.entries = presetStore.comparePresets(root.kind, root.aName, root.bName)
    }

    function openFor(tab) {
        root.mode = "changes"
        root.tab = tab === undefined ? "" : tab
        root.refresh()
        root.open()
    }

    function openForKind(kind, currentName) {
        root.mode = "compare"
        root.kind = kind
        root.aName = currentName === undefined ? "" : currentName
        root.bName = ""
        const options = root.choices
        for (let i = 0; i < options.length; ++i) {
            const value = options[i].value === undefined ? options[i] : options[i].value
            if (value !== root.aName) {
                root.bName = value
                break
            }
        }
        root.refresh()
        root.open()
    }

    width: Math.min(860, parent.width - Metrics.xl * 2)
    height: Math.min(680, parent.height - Metrics.xl * 2)
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
                    text: root.title
                    font.family: Type.family
                    font.pixelSize: Type.sizeTitle
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    Layout.fillWidth: true
                    text: root.count === 0
                          ? (root.comparing ? "identical" : "nothing changed")
                          : root.count + (root.count === 1 ? " setting" : " settings")
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                Button {
                    visible: !root.comparing
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
                    hint: "Close this window"
                    onClicked: root.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                Layout.bottomMargin: Metrics.md
                spacing: Metrics.sm
                visible: root.comparing

                ComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    choices: root.choices
                    currentValue: root.aName
                    searchable: true
                    popupWidth: 380
                    onSelected: (value) => {
                        root.aName = value
                        root.refresh()
                    }
                }

                Text {
                    text: "vs"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                ComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    choices: root.choices
                    currentValue: root.bName
                    searchable: true
                    popupWidth: 380
                    onSelected: (value) => {
                        root.bName = value
                        root.refresh()
                    }
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
                    height: root.comparing ? 44 : 52
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
                                Layout.fillWidth: true
                                text: row.modelData.label !== undefined
                                      ? row.modelData.label : row.modelData.key
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textPrimary
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: row.modelData.subtab !== undefined
                                text: row.modelData.subtab === undefined ? "" : row.modelData.subtab
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Metrics.sm

                            Text {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                text: root.comparing
                                      ? (row.modelData.a.length > 0 ? row.modelData.a : "not set")
                                      : (row.modelData.before === undefined ? ""
                                                                            : String(row.modelData.before))
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "\u2192"
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                text: root.comparing
                                      ? (row.modelData.b.length > 0 ? row.modelData.b : "not set")
                                      : (row.modelData.after === undefined ? ""
                                                                           : String(row.modelData.after))
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.accent
                                elide: Text.ElideRight
                            }

                            ToolButton {
                                visible: !root.comparing
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
