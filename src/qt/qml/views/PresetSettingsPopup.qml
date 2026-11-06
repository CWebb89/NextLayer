pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NextLayer 1.0
import "../theme"
import "../components"

// The per-picker "extra settings" view Orca shows when the gear next to a
// preset dropdown is clicked: the whole resolved config for one tab, grouped and
// read-only. Editing stays in the main panel; this is the at-a-glance view of
// what a preset actually contains. A plain overlay rectangle rather than a
// Popup, so it lives inside the panel's own layout and needs no window overlay.
Rectangle {
    id: root

    property string tab: "printer"
    property string title: ""
    visible: false

    readonly property string presetKind: root.tab === "filament" ? "filament" : "print"
    readonly property string presetTab: root.tab === "filament" ? "filament" : "process"
    readonly property string currentPresetName: root.presetKind === "filament"
            ? presetStore.currentFilament : presetStore.currentPrint
    readonly property bool currentIsUser: presetStore.presetIsUser(root.presetKind,
                                                                  root.currentPresetName)
    property string actionMessage: ""

    function reloadWith(printName, filamentName) {
        presetStore.reload(presetStore.currentPrinter, printName, filamentName)
    }

    function savePreset() {
        const error = presetStore.saveUserPreset(root.presetKind, presetName.text, root.presetTab)
        root.actionMessage = error
        if (error.length > 0)
            return
        root.reloadWith(root.presetKind === "print" ? presetName.text : presetStore.currentPrint,
                        root.presetKind === "filament" ? presetName.text : presetStore.currentFilament)
    }

    function renamePreset() {
        const error = presetStore.renameUserPreset(root.presetKind, root.currentPresetName,
                                                  presetName.text)
        root.actionMessage = error
        if (error.length > 0)
            return
        root.reloadWith(root.presetKind === "print" ? presetName.text : presetStore.currentPrint,
                        root.presetKind === "filament" ? presetName.text : presetStore.currentFilament)
    }

    function deletePreset() {
        const error = presetStore.deleteUserPreset(root.presetKind, root.currentPresetName)
        root.actionMessage = error
        if (error.length > 0)
            return
        root.reloadWith(root.presetKind === "print" ? "" : presetStore.currentPrint,
                        root.presetKind === "filament" ? "" : presetStore.currentFilament)
    }

    function addPrinter() {
        const error = presetStore.createPrinter(printerName.text, bedWidth.value, bedDepth.value,
                                                bedHeight.value)
        root.actionMessage = error
        if (error.length > 0)
            return
        presetStore.reload(printerName.text, "Standard", "Generic")
    }

    color: Colors.surfaceContainerLow
    radius: Metrics.radiusMedium
    border.width: Metrics.borderWidth
    border.color: Colors.outlineVariant

    readonly property var groups: settingsModel.groups(root.tab, "", "")

    function displayValue(item) {
        const value = item.value
        if (item.type === "Boolean")
            return value ? "Yes" : "No"
        if (Array.isArray(value))
            return value.join(", ")
        if (value !== null && typeof value === "object")
            return String(value.value)
        return String(value)
    }

    function displayUnits(item) {
        if (item.type === "Percentage" || item.percent === true)
            return "%"
        if (item.units !== undefined && item.units !== null && item.units.length > 0)
            return String(item.units[0])
        return ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Metrics.lg
        spacing: Metrics.sm

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: root.title
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                font.bold: true
                color: Colors.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            ToolButton {
                icon: "close"
                hint: "Close"
                onClicked: root.visible = false
            }
        }

        Separator {
            Layout.fillWidth: true
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: column
                width: parent.width
                spacing: Metrics.lg

                Repeater {
                    model: root.groups

                    delegate: Column {
                        id: groupBlock
                        required property var modelData
                        width: column.width
                        spacing: Metrics.xxs

                        Text {
                            text: groupBlock.modelData.title
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            font.bold: true
                            color: Colors.textSecondary
                        }

                        Rectangle {
                            width: parent.width
                            height: Metrics.hairline
                            color: Colors.separator
                        }

                        Repeater {
                            model: groupBlock.modelData.items

                            delegate: RowLayout {
                                id: row
                                required property var modelData
                                width: column.width
                                spacing: Metrics.sm

                                Text {
                                    text: row.modelData.label
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeCaption
                                    color: Colors.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: column.width * 0.55
                                }

                                Text {
                                    text: root.displayValue(row.modelData)
                                          + (root.displayUnits(row.modelData).length > 0
                                             ? " " + root.displayUnits(row.modelData) : "")
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeCaption
                                    font.bold: true
                                    color: Colors.textPrimary
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideLeft
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Metrics.hairline
            color: Colors.separator
        }

        Text {
            Layout.fillWidth: true
            text: root.actionMessage.length > 0 ? root.actionMessage : presetStore.error
            visible: text.length > 0
            wrapMode: Text.WordWrap
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textSecondary
        }

        // Preset management for the tab this popup belongs to. Saving writes
        // only the values changed in the panel and inherits the preset the
        // panel was seeded from, which is how PrusaSlicer and Orca store user
        // presets; a printer is added as a whole machine instead.
        RowLayout {
            Layout.fillWidth: true
            spacing: Metrics.sm
            visible: root.tab !== "printer"

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Metrics.controlHeight
                radius: Metrics.radiusSmall
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: presetName.activeFocus ? Colors.primary : Colors.outlineVariant

                TextField {
                    id: presetName
                    anchors.fill: parent
                    anchors.leftMargin: Metrics.md
                    anchors.rightMargin: Metrics.md
                    background: Item {}
                    text: root.currentPresetName
                    placeholderText: "preset name"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    placeholderTextColor: Colors.textMuted
                    selectByMouse: true
                    onTextChanged: root.actionMessage = ""
                }
            }

            Button {
                text: "Save as"
                primary: true
                enabled: !presetStore.busy && presetName.text.length > 0
                onClicked: root.savePreset()
            }

            Button {
                text: "Rename"
                enabled: !presetStore.busy && root.currentIsUser && presetName.text.length > 0
                onClicked: root.renamePreset()
            }

            Button {
                text: "Delete"
                enabled: !presetStore.busy && root.currentIsUser
                onClicked: root.deletePreset()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Metrics.xs
            visible: root.tab === "printer"

            RowLayout {
                Layout.fillWidth: true
                spacing: Metrics.sm

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.controlHeight
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer
                    border.width: Metrics.borderWidth
                    border.color: printerName.activeFocus ? Colors.primary : Colors.outlineVariant

                    TextField {
                        id: printerName
                        anchors.fill: parent
                        anchors.leftMargin: Metrics.md
                        anchors.rightMargin: Metrics.md
                        background: Item {}
                        text: Host.printerModel
                        placeholderText: "printer name"
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textPrimary
                        placeholderTextColor: Colors.textMuted
                        selectByMouse: true
                        onTextChanged: root.actionMessage = ""
                    }
                }

                Button {
                    text: "Add printer"
                    primary: true
                    enabled: !presetStore.busy && printerName.text.length > 0
                    onClicked: root.addPrinter()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Metrics.sm

                NumberField {
                    id: bedWidth
                    Layout.fillWidth: true
                    value: Host.bedMax.x - Host.bedMin.x
                    from: 10
                    to: 2000
                    units: "W"
                }

                NumberField {
                    id: bedDepth
                    Layout.fillWidth: true
                    value: Host.bedMax.y - Host.bedMin.y
                    from: 10
                    to: 2000
                    units: "D"
                }

                NumberField {
                    id: bedHeight
                    Layout.fillWidth: true
                    value: Host.maxPrintHeight
                    from: 10
                    to: 2000
                    units: "H"
                }
            }
        }
    }
}
