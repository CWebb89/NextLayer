// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../components"
import "."

// The preset editor: the gear next to a preset dropdown opens the whole
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
    property string subTab: ""
    property string filterText: ""
    property string nameField: ""
    property string message: ""
    property bool showAddPrinter: false
    property bool confirmingDelete: false

    /// Asks the window for the printer's connection window: the dialog itself has
    /// no business owning a second copy of it.
    signal networkRequested()

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
    readonly property bool currentIsUser: presetStore.presetIsUser(root.presetKind,
                                                                  root.currentName)
    /// The categories of this preset, the per-category dots and the changed count
    /// all come from the settings model, so the window re-reads them whenever the
    /// model says something moved. Opened once and never re-read, it showed the
    /// values it was opened with for as long as it stayed open: an edit was in the
    /// model but not on screen, and Save stayed disabled under it.
    property int modified: 0
    readonly property var groups: root.groupCache
    property var groupCache: []

    function refresh() {
        root.modified = settingsModel.modifiedCount(root.tab)
        root.groupCache = root.filterText.length > 0
                        ? settingsModel.groups("", "", root.filterText)
                        : settingsModel.groups(root.tab, root.subTab, "")
    }

    Connections {
        target: settingsModel

        function onChanged() { root.refresh() }
    }

    Connections {
        target: presetStore

        function onResolved() { root.refresh() }
    }

    /// The categories of the preset being edited -- Filament has material,
    /// cooling, tool changes; Printer has general, bed, machine limits -- which is
    /// what the reference slicers put in this window and what makes 68 settings
    /// readable at all.
    readonly property var categories: {
        const tabs = settingsModel.tabList()
        const wanted = root.tab === "filament" ? "filament"
                     : root.tab === "printer" ? "printer" : "process"
        for (let i = 0; i < tabs.length; ++i) {
            if (tabs[i].key !== wanted)
                continue
            const out = []
            for (let j = 0; j < tabs[i].subtabs.length; ++j) {
                const entry = tabs[i].subtabs[j]
                out.push({ key: entry.key, title: entry.title, modified: entry.modified })
            }
            return out
        }
        return []
    }

    readonly property var tabChoices: {
        if (root.tab === "filament")
            return presetStore.filamentProfiles
        if (root.tab === "printer") {
            const out = []
            const printers = presetStore.printers
            for (let i = 0; i < printers.length; ++i)
                out.push({ value: printers[i].name, label: printers[i].name })
            return out
        }
        return presetStore.printProfiles
    }

    // Drawn over the whole window, not inside the panel it is declared in: a Popup
    // is a visual child of its `parent`, so left at its default it was painted
    // under the preview cards while being sized against them.
    parent: Overlay.overlay

    // Sized and centred against the window, not against the item this popup is
    // declared in. It lives inside the settings panel, which is a third of the
    // window wide: sized from there it drew at the panel's width while its content
    // kept the width it was laid out for, and the right-hand half of every setting
    // landed on the viewport behind it.
    readonly property Item windowItem: Overlay.overlay !== null ? Overlay.overlay : parent

    // Wide enough for a two-column settings tree with its value column, and never
    // wider than the window: the tree's rows are as wide as this popup, so a
    // narrow window squeezes them rather than cutting them off.
    width: Math.min(1180, root.windowItem.width - Metrics.xl * 2)
    height: Math.min(880, root.windowItem.height - Metrics.xl * 2)
    x: Math.round((root.windowItem.width - width) / 2)
    y: Math.round((root.windowItem.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openOn(tabKey) {
        root.tab = tabKey
        root.subTab = root.firstCategory()
        root.filterText = ""
        root.message = ""
        root.showAddPrinter = false
        root.nameField = root.currentName
        root.refresh()
        root.open()
    }

    function showSubtab(tabKey, subTabKey) {
        root.tab = tabKey
        root.subTab = subTabKey
        root.filterText = ""
        root.open()
    }

    function firstCategory() {
        return root.categories.length > 0 ? root.categories[0].key : ""
    }

    onTabChanged: {
        root.subTab = root.firstCategory()
        root.refresh()
    }

    onSubTabChanged: root.refresh()

    onFilterTextChanged: root.refresh()

    onOpened: root.refresh()

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
        if (!root.currentIsUser) {
            root.message = root.printerTab
                         ? "This machine came from the catalogue, so it cannot be overwritten "
                           + "here. Give it a name to save it as your own machine."
                         : "This preset ships with the engine, so it cannot be "
                           + "overwritten. Give it your own name to save a copy."
            return
        }
        const error = presetStore.saveUserPreset(root.presetKind, root.currentName, root.tab)
        root.message = error.length > 0 ? error : "saved " + root.currentName
        if (error.length > 0)
            return
        root.reloadWith(presetStore.currentPrint, presetStore.currentFilament)
    }

    function saveAs() {
        const name = root.nameField.trim()
        if (name.length === 0) {
            root.message = "Give the preset a name first"
            return
        }
        // A new name is a new preset, and it is written whole: a copy that carried
        // only the moved values would depend on the parent it was copied from and
        // reading it here twice in a row would say "nothing changed" the second
        // time, because nothing had.
        const copy = name !== root.currentName
        const error = presetStore.saveUserPreset(root.presetKind, name, root.tab, !copy)
        root.message = error.length > 0 ? error
                                       : (copy ? "saved " + name : "saved " + root.currentName)
        if (error.length > 0)
            return
        // A saved machine is a machine: it has to become the one selected, or
        // the editor would keep editing the catalogue entry it was copied from.
        if (root.printerTab) {
            presetStore.selectPrinter(name)
            root.nameField = name
            return
        }
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

                // Each preset kind is its own window, and a window never offers a
                // way into another one: switching kind under a shared name field
                // and Save button is how a printer setting ends up written into a
                // process preset. The title says which kind this is.
                Text {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    horizontalAlignment: Text.AlignRight
                    text: "Name"
                    elide: Text.ElideRight
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                TextField {
                    Layout.preferredWidth: 200
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
                    enabled: root.modified > 0
                    onClicked: root.save()
                }

                Button {
                    text: "Save as"
                    hint: "Save these values under the name in the field"
                    onClicked: root.saveAs()
                }
            }

            // Wraps rather than running off the edge: nine controls do not fit on
            // one row at every window size, and the last of them used to be drawn
            // outside the dialog, over the viewport.
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.sm
                spacing: Metrics.sm

                Text {
                    width: 56
                    height: Metrics.controlHeight
                    verticalAlignment: Text.AlignVCenter
                    text: root.printerTab ? "Machine" : "Preset"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                ComboBox {
                    width: 300
                    height: Metrics.controlHeight
                    choices: root.tabChoices
                    currentValue: root.currentName
                    searchable: true
                    popupWidth: 420
                    onSelected: (value) => root.selectPreset(String(value))
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
                    text: "Compare…"
                    hint: "Another preset of this kind, setting by setting"
                    onClicked: compareDialog.openForKind(root.presetKind, root.currentName)
                }

                Button {
                    text: "Add printer"
                    hint: "Create a new machine from the current settings"
                    visible: root.printerTab
                    onClicked: root.showAddPrinter = !root.showAddPrinter
                }

                ToolButton {
                    icon: "wifi"
                    hint: "Network: host, API key and host offsets for this machine"
                    visible: root.printerTab
                    onClicked: {
                        // One modal at a time: the connection window is its own, and a
                        // popup opened on top of a popup lands behind it.
                        root.close()
                        root.networkRequested()
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.sm
                visible: text.length > 0
                text: root.message.length > 0 ? root.message
                      : presetStore.error.length > 0 ? presetStore.error : ""
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.error
            }

            PresetDiffDialog {
                id: compareDialog
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

                Separator { Layout.fillWidth: true }
            }

            // The categories of this preset, the way the reference slicers put
            // them at the top of the same window: material and cooling for a
            // filament, the bed and the machine limits for a printer. The search
            // sits on the end of the same row, where the reference puts it, rather
            // than on a row of its own above the tabs.
            Item {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.topMargin: Metrics.xs
                Layout.bottomMargin: Metrics.xs
                Layout.preferredHeight: Metrics.rowHeight

                TabStrip {
                    id: presetPages
                    anchors.left: parent.left
                    anchors.right: searchBox.left
                    anchors.rightMargin: Metrics.sm
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.filterText.length === 0
                    categories: root.categories
                    current: root.subTab
                    onActivated: (key) => root.subTab = key
                }

                Rectangle {
                    id: searchBox
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 220
                    height: Metrics.controlHeight
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
                        placeholderText: "search every page"
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textPrimary
                        placeholderTextColor: Colors.textMuted
                        selectByMouse: true
                        onTextChanged: root.filterText = text
                    }
                }
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
