pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../components"
import "."

// Orca's preset editor: the gear next to a preset dropdown opens the whole
// resolved configuration of that preset kind in one window, with its own preset
// toolbar (select, revert, save, save as, rename, delete, add a machine) and the
// same typed editors the docked panel uses. Edits apply to the live settings
// immediately; Save writes them into the engine's preset tree.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property string tab: "process"
    property string filterText: ""
    property string nameField: ""
    property string message: ""
    property bool showAddPrinter: false
    property bool confirmingDelete: false

    readonly property string presetKind: root.tab === "filament" ? "filament"
                                       : root.tab === "printer" ? "printer" : "print"
    readonly property bool printerTab: root.tab === "printer"
    readonly property string currentName: {
        if (root.tab === "filament")
            return presetStore.currentFilament
        if (root.tab === "printer")
            return presetStore.currentPrinter
        return presetStore.currentPrint
    }
    readonly property bool currentIsUser: !root.printerTab
                                          && presetStore.presetIsUser(root.presetKind,
                                                                      root.currentName)
    readonly property int modified: settingsModel.modifiedCount(root.tab)
    readonly property var groups: root.filterText.length > 0
                                  ? settingsModel.groups("", "", root.filterText)
                                  : settingsModel.groups(root.tab, "", "")

    readonly property var tabChoices: {
        if (root.tab === "filament")
            return presetStore.filamentProfiles
        if (root.tab === "printer")
            return root.printerChoices()
        return presetStore.printProfiles
    }

    parent: Overlay.overlay
    width: Math.min(1180, parent.width - Metrics.xl * 2)
    height: Math.min(860, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function printerChoices() {
        const out = []
        for (let i = 0; i < presetStore.printers.length; ++i)
            out.push({ value: presetStore.printers[i].name, label: presetStore.printers[i].name })
        return out
    }

    function openOn(tabKey) {
        root.tab = tabKey
        root.filterText = ""
        root.message = ""
        root.showAddPrinter = false
        root.nameField = root.currentName
        root.open()
    }

    function title() {
        return root.tab === "filament" ? "Filament settings"
             : root.tab === "printer" ? "Printer settings" : "Print settings"
    }

    function reloadWith(printName, filamentName) {
        presetStore.reload(presetStore.currentPrinter, printName, filamentName)
    }

    function selectPreset(name) {
        if (name.length === 0)
            return
        if (root.tab === "printer") {
            presetStore.selectPrinter(name)
            return
        }
        root.nameField = name
        root.reloadWith(root.tab === "process" ? name : presetStore.currentPrint,
                        root.tab === "filament" ? name : presetStore.currentFilament)
    }

    function save() {
        if (root.printerTab) {
            root.message = "A second printer preset would duplicate this machine. "
                         + "Add a printer instead."
            return
        }
        if (!root.currentIsUser) {
            root.message = "This preset ships with the engine, so it cannot be "
                         + "overwritten. Give it your own name to save a copy."
            return
        }
        const error = presetStore.saveUserPreset(root.presetKind, root.currentName, root.tab)
        root.message = error
        if (error.length > 0)
            return
        root.reloadWith(presetStore.currentPrint, presetStore.currentFilament)
    }

    function saveAs() {
        if (root.printerTab) {
            root.message = "A second printer preset would duplicate this machine. "
                         + "Add a printer instead."
            return
        }
        const name = root.nameField.trim()
        if (name.length === 0) {
            root.message = "Give the preset a name first"
            return
        }
        const error = presetStore.saveUserPreset(root.presetKind, name, root.tab)
        root.message = error
        if (error.length > 0)
            return
        root.reloadWith(root.tab === "process" ? name : presetStore.currentPrint,
                        root.tab === "filament" ? name : presetStore.currentFilament)
    }

    function rename() {
        if (!root.currentIsUser) {
            root.message = "Only a preset you saved can be renamed"
            return
        }
        const name = root.nameField.trim()
        const error = presetStore.renameUserPreset(root.presetKind, root.currentName, name)
        root.message = error
        if (error.length > 0)
            return
        root.reloadWith(root.tab === "process" ? name : presetStore.currentPrint,
                        root.tab === "filament" ? name : presetStore.currentFilament)
    }

    function remove() {
        if (!root.currentIsUser) {
            root.message = "This preset ships with the engine and cannot be deleted"
            return
        }
        root.confirmingDelete = true
        root.message = "Delete " + root.currentName + "? The file is removed from disk."
    }

    function removeConfirmed() {
        root.confirmingDelete = false
        const error = presetStore.deleteUserPreset(root.presetKind, root.currentName)
        root.message = error
        if (error.length > 0)
            return
        root.reloadWith(root.tab === "process" ? "" : presetStore.currentPrint,
                        root.tab === "filament" ? "" : presetStore.currentFilament)
    }

    function addPrinter() {
        const error = presetStore.createPrinter(printerName.text, printerWidth.value,
                                                printerDepth.value, printerHeight.value)
        root.message = error
        if (error.length > 0)
            return
        root.showAddPrinter = false
        presetStore.reload(printerName.text, "Standard", "Generic")
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

            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Metrics.lg
                spacing: Metrics.sm

                Text {
                    text: root.title()
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: root.modified > 0
                    text: root.modified + " changed"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.accent
                }

                ToolButton {
                    icon: "restart_alt"
                    hint: "Revert every change on this tab"
                    enabled: root.modified > 0
                    onClicked: settingsModel.resetTab(root.tab)
                }

                ToolButton {
                    icon: "close"
                    hint: "Close"
                    onClicked: root.close()
                }
            }

            Separator {
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.topMargin: Metrics.sm
                Layout.bottomMargin: Metrics.sm
                spacing: Metrics.sm

                Repeater {
                    model: [
                        { key: "process", title: "Print" },
                        { key: "filament", title: "Filament" },
                        { key: "printer", title: "Printer" }
                    ]

                    delegate: Item {
                        id: stepTab

                        required property var modelData

                        readonly property bool active: root.tab === stepTab.modelData.key

                        Layout.preferredWidth: tabText.implicitWidth + Metrics.lg * 2
                        Layout.preferredHeight: Metrics.controlHeight

                        Rectangle {
                            anchors.fill: parent
                            radius: Metrics.radiusSmall
                            color: stepTab.active ? Colors.selectedFill
                                 : stepMouse.containsMouse ? Colors.hoverFill : "transparent"
                        }

                        Text {
                            id: tabText
                            anchors.centerIn: parent
                            text: stepTab.modelData.title
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            font.bold: true
                            color: stepTab.active ? Colors.accent : Colors.textSecondary
                        }

                        MouseArea {
                            id: stepMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.tab = stepTab.modelData.key
                                root.nameField = root.currentName
                                root.message = ""
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                TextField {
                    Layout.preferredWidth: 260
                    Layout.preferredHeight: Metrics.controlHeight
                    text: root.nameField
                    placeholderText: "preset name"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    placeholderTextColor: Colors.textMuted
                    selectByMouse: true
                    onTextChanged: {
                        root.nameField = text
                        root.message = ""
                    }

                    background: Rectangle {
                        radius: Metrics.radiusSmall
                        color: Colors.surfaceContainer
                        border.width: Metrics.borderWidth
                        border.color: root.activeFocus ? Colors.primary : Colors.outlineVariant
                    }
                }

                Button {
                    text: "Save"
                    hint: "Write the changed values into this preset"
                    primary: true
                    enabled: root.modified > 0 && !root.printerTab
                    onClicked: root.save()
                }

                Button {
                    text: "Save as"
                    hint: "Save these values under the name in the field"
                    enabled: !root.printerTab
                    onClicked: root.saveAs()
                }

                Button {
                    text: "Rename"
                    enabled: root.currentIsUser
                    onClicked: root.rename()
                }

                Button {
                    text: root.confirmingDelete ? "Confirm delete" : "Delete"
                    enabled: root.currentIsUser
                    onClicked: root.confirmingDelete ? root.removeConfirmed() : root.remove()
                }

                Button {
                    text: "Share…"
                    hint: "Write this preset out as a file you can hand to someone else"
                    enabled: root.currentIsUser
                    onClicked: sharePresetDialog.open()
                }

                Button {
                    text: "Import…"
                    hint: "Read a shared preset file into your presets"
                    onClicked: importPresetDialog.open()
                }

                Button {
                    text: "Add printer"
                    hint: "Create a new machine from the current settings"
                    visible: root.printerTab
                    onClicked: root.showAddPrinter = !root.showAddPrinter
                }
            }

            // A tool changer loads one material per tool. Comma-separated preset
            // names would be unreadable for an eight-tool machine, so each tool gets
            // its own picker here and the choice feeds the slice's filament list.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.sm
                spacing: Metrics.sm
                visible: presetStore.filaments.length > 1

                Text {
                    text: "Tools"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    font.bold: true
                    color: Colors.textSecondary
                }

                Repeater {
                    model: presetStore.filaments.length

                    delegate: RowLayout {
                        id: toolRow
                        required property int index

                        spacing: Metrics.xs

                        Text {
                            text: "T" + (toolRow.index + 1)
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            color: Colors.textSecondary
                        }

                        ComboBox {
                            Layout.preferredWidth: 190
                            choices: presetStore.filamentProfiles
                            currentValue: presetStore.filaments[toolRow.index]
                            searchable: true
                            popupWidth: 320
                            onSelected: (value) => presetStore.setFilamentForTool(toolRow.index + 1, value)
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.sm
                spacing: Metrics.sm
                visible: root.showAddPrinter

                TextField {
                    id: printerName
                    Layout.preferredWidth: 220
                    Layout.preferredHeight: Metrics.controlHeight
                    placeholderText: "printer name"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    placeholderTextColor: Colors.textMuted
                    selectByMouse: true

                    background: Rectangle {
                        radius: Metrics.radiusSmall
                        color: Colors.surfaceContainer
                        border.width: Metrics.borderWidth
                        border.color: printerName.activeFocus ? Colors.primary : Colors.outlineVariant
                    }
                }

                NumberField {
                    id: printerWidth
                    Layout.preferredWidth: 130
                    value: {
                        const shape = settingsModel.value("bed_shape")
                        return shape !== undefined && shape !== null && shape.length > 1
                               ? Math.abs(Number(shape[1][0]) - Number(shape[0][0])) : 250
                    }
                    from: 50
                    to: 2000
                    units: "x"
                }

                NumberField {
                    id: printerDepth
                    Layout.preferredWidth: 130
                    value: {
                        const shape = settingsModel.value("bed_shape")
                        return shape !== undefined && shape !== null && shape.length > 2
                               ? Math.abs(Number(shape[2][1]) - Number(shape[1][1])) : 210
                    }
                    from: 50
                    to: 2000
                    units: "y"
                }

                NumberField {
                    id: printerHeight
                    Layout.preferredWidth: 130
                    value: {
                        const height = Number(settingsModel.value("max_print_height"))
                        return height > 0 ? height : 250
                    }
                    from: 20
                    to: 2000
                    units: "z"
                }

                Button {
                    text: "Create machine"
                    primary: true
                    onClicked: root.addPrinter()
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.sm
                spacing: Metrics.sm

                Text {
                    text: root.printerTab ? "Machine" : "Preset"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                ComboBox {
                    Layout.preferredWidth: 320
                    choices: root.tabChoices
                    currentValue: root.currentName
                    onSelected: (value) => root.selectPreset(String(value))
                }

                Text {
                    Layout.fillWidth: true
                    text: root.message.length > 0 ? root.message
                          : presetStore.error.length > 0 ? presetStore.error : ""
                    elide: Text.ElideRight
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: root.message.length > 0 || presetStore.error.length > 0
                           ? Colors.error : Colors.textMuted
                }

                Text {
                    text: root.currentIsUser ? "user preset" : "engine preset"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textTertiary
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.sm
                spacing: Metrics.sm

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.controlHeight
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer
                    border.width: Metrics.borderWidth
                    border.color: searchField.activeFocus ? Colors.primary : Colors.outlineVariant

                    Icon {
                        id: searchIcon
                        anchors.left: parent.left
                        anchors.leftMargin: Metrics.md
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: Type.sizeSmall
                        color: Colors.textMuted
                    }

                    TextField {
                        id: searchField
                        anchors.left: searchIcon.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Metrics.sm
                        anchors.rightMargin: Metrics.md
                        background: Item {}
                        placeholderText: "search every tab"
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textPrimary
                        placeholderTextColor: Colors.textMuted
                        selectByMouse: true
                        onTextChanged: root.filterText = text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
            }

            SettingsTree {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.lg
                filterText: root.filterText
                groups: root.groups
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

    FileDialog {
        id: sharePresetDialog
        title: "Share the preset"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "yaml"
        nameFilters: ["Preset (*.yaml)", "All files (*)"]
        onAccepted: {
            const error = presetStore.exportPreset(root.presetKind, root.currentName,
                                                   selectedFile)
            root.message = error.length > 0 ? error
                                            : "wrote " + root.currentName + " to " + selectedFile
        }
    }

    FileDialog {
        id: importPresetDialog
        title: "Import a shared preset"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Preset (*.yaml)", "All files (*)"]
        onAccepted: {
            const error = presetStore.importPreset(root.presetKind, selectedFile)
            if (error.length > 0) {
                root.message = error
                return
            }
            root.message = "imported " + presetStore.importedPresetName()
        }
    }
}
