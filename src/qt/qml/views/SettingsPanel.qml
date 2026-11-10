// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtCore
import "../theme"
import "../components"

// The settings surface: what will slice the plate, the process categories, and
// one scroll of two-column setting rows (label left, editor right). Only the
// active category is instantiated, so 219 process settings cost a few dozen items.
//
// Process only, as the reference slicers have it. A filament and a machine are
// edited in their own window, because they are their own presets: they carry
// settings a process does not have, they outlive the process in use, and three
// sets of tabs in one 490 px column made every one of them a cramped half-page.
// The gears beside the Filament and Printer rows open those windows.
Item {
    id: root

    signal machineChooserRequested()
    signal changesRequested(string tab)
    /// The printer's connection, from the machine settings: the same window the
    /// File menu opens, reachable where a person is editing the machine.
    signal networkRequested()

    property string activeSubtab: ""
    property string filterText: ""

    property var tabs: []
    property var visibleGroups: []
    property var lastSubtab: ({})

    readonly property bool filtering: root.filterText.length > 0
    readonly property string activeTab: "process"
    readonly property var currentTab: root.tabData(root.activeTab)
    readonly property var currentSubtabs: root.currentTab ? root.currentTab.subtabs : []
    readonly property int modifiedTotal: settingsModel.modifiedCount(root.activeTab)

    function tabData(key) {
        for (let i = 0; i < root.tabs.length; ++i)
            if (root.tabs[i].key === key)
                return root.tabs[i]
        return null
    }

    function showSubtab(tab, subtab) {
        const remembered = Object.assign({}, root.lastSubtab)
        remembered[tab] = subtab
        root.lastSubtab = remembered
        root.activeSubtab = subtab
        root.refresh()
    }

    function openPresetEditor(tab) {
        presetEditor.openOn(tab)
    }

    function selectSubtab(key) {
        root.activeSubtab = key
        const next = Object.assign({}, root.lastSubtab)
        next[root.activeTab] = key
        root.lastSubtab = next
        root.refresh()
    }

    /// The categories of the process tab, for the strip. A category with changed
    /// settings carries a dot, so the one place that lists them all also says
    /// which of them is not at its preset value.
    function categoryEntries() {
        const out = []
        for (let i = 0; i < root.currentSubtabs.length; ++i) {
            const entry = root.currentSubtabs[i]
            out.push({ key: entry.key, title: entry.title, modified: entry.modified })
        }
        return out
    }

    function ensureSubtab() {
        const tab = root.tabData(root.activeTab)
        if (!tab)
            return
        for (let i = 0; i < tab.subtabs.length; ++i)
            if (tab.subtabs[i].key === root.activeSubtab)
                return
        root.activeSubtab = tab.subtabs.length > 0 ? tab.subtabs[0].key : ""
    }

    function refresh() {
        root.tabs = settingsModel.tabList()
        root.ensureSubtab()
        root.visibleGroups = root.filtering
                ? settingsModel.groups("", "", root.filterText)
                : settingsModel.groups(root.activeTab, root.activeSubtab, "")
    }

    /// The machine remembered from the last session is not in the bundle any
    /// more, so nothing resolved and the app has no bed. Named rather than
    /// silent: a regenerated printer catalog renames machines, and this is the
    /// normal way to arrive at that state.
    signal rememberedPrinterMissing(string name)

    Settings {
        id: rememberedSetup
        category: "presets"

        property string printer: ""
        property string process: ""
        property string filament: ""
    }

    Component.onCompleted: {
        // A startup flag naming a filament or a printer sub-tab opens the window
        // that edits it: this panel no longer has those tabs, and silently landing
        // on the process tab would be the flag doing nothing.
        const start = typeof startupSettingsTab !== "undefined" ? String(startupSettingsTab) : ""
        const parts = start.length > 0 ? start.split("/") : []
        if (parts.length > 0 && (parts[0] === "filament" || parts[0] === "printer")) {
            presetEditor.openOn(parts[0])
            if (parts.length > 1)
                presetEditor.showSubtab(parts[0], parts[1])
        } else if (parts.length > 1) {
            const remembered = {}
            remembered[parts[0]] = parts[1]
            root.lastSubtab = remembered
            root.activeSubtab = parts[1]
        }
        root.refresh()
        presetStore.refreshPrinters()
        const dialog = typeof startupPresetDialog !== "undefined" ? String(startupPresetDialog) : ""
        if (dialog.length > 0)
            presetEditor.openOn(dialog)
    }

    Connections {
        target: presetStore

        // Restore the machine last sliced with, and nothing else. There is no
        // default printer: on a fresh install nothing is chosen, the panel stays
        // empty, and the setup flow is what picks a machine — never the order a
        // catalog happens to be sorted in.
        function onPrintersChanged() {
            if (presetBar.printer.length > 0)
                return
            const printers = presetStore.printers
            if (printers.length === 0)
                return
            const wanted = rememberedSetup.printer
            if (wanted.length === 0)
                return
            for (let i = 0; i < printers.length; ++i) {
                if (printers[i].name === wanted) {
                    presetBar.choosePrinter(wanted)
                    return
                }
            }
            rememberedSetup.printer = ""
            root.rememberedPrinterMissing(wanted)
        }

        function onResolved() {
            rememberedSetup.printer = presetStore.currentPrinter
            rememberedSetup.process = presetStore.currentPrint
            rememberedSetup.filament = presetStore.currentFilament
        }

        function onCurrentPrinterChanged() {
            if (presetBar.printer.length === 0 && presetStore.currentPrinter.length > 0)
                presetBar.printer = presetStore.currentPrinter
            if (presetBar.printProfile.length === 0 && presetStore.currentPrint.length > 0)
                presetBar.printProfile = presetStore.currentPrint
            if (presetBar.filament.length === 0 && presetStore.currentFilament.length > 0)
                presetBar.filament = presetStore.currentFilament
        }
    }

    Connections {
        target: settingsModel

        function onChanged() {
            root.refresh()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Colors.surfaceContainerLow
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Metrics.hairline
        color: Colors.separator
    }

    Item {
        id: presetBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.sm
        readonly property int presetLabelWidth: 66
        readonly property int rowCount: 2 + Math.max(1, presetBar.toolCount)

        height: Metrics.controlHeight * presetBar.rowCount
                + Metrics.sm * (presetBar.rowCount - 1)

        property string printer: ""
        property string printProfile: ""
        property string filament: ""
        property bool autoResolve: false

        readonly property var printerChoices: {
            const out = []
            const printers = presetStore.userPrinters
            for (let i = 0; i < printers.length; ++i)
                out.push({ value: printers[i].name, label: printers[i].name })
            // The machine in force is always listed: it may be one from the
            // catalogue, chosen in the setup flow, and a bar that cannot show
            // what it is slicing with is worse than a list of one.
            if (presetBar.printer.length > 0) {
                let listed = false
                for (let i = 0; i < out.length; ++i)
                    if (out[i].value === presetBar.printer)
                        listed = true
                if (!listed)
                    out.unshift({ value: presetBar.printer, label: presetBar.printer })
            }
            return out
        }

        readonly property int toolCount: {
            const printers = presetStore.printers
            for (let i = 0; i < printers.length; ++i)
                if (printers[i].name === presetBar.printer)
                    return Math.max(1, printers[i].extruders)
            return 1
        }

        // One material per tool, which is what a tool changer prints with. The
        // single list is the machine's own; a second tool is a second row here
        // and a second entry in the slice's filament list.
        function filamentFor(tool) {
            return presetStore.filaments.length > tool ? presetStore.filaments[tool] : presetBar.filament
        }

        function chooseFilament(tool, value) {
            if (tool === 0) {
                presetBar.filament = value
                presetBar.resolveIfReady()
                return
            }
            presetStore.setFilamentForTool(tool + 1, value)
        }

        function resolveIfReady() {
            if (presetBar.printer.length > 0 && presetBar.printProfile.length > 0
                    && presetBar.filament.length > 0)
                presetStore.resolve(presetBar.printer, presetBar.printProfile, presetBar.filament)
        }

        function choosePrinter(name) {
            if (presetBar.printer === name)
                return
            presetBar.printer = name
            presetBar.printProfile = ""
            presetBar.filament = ""
            presetBar.autoResolve = true
            presetStore.selectPrinter(name)
        }

        Connections {
            target: presetStore

            function onResolved() {
                presetBar.autoResolve = false
                presetBar.printer = presetStore.currentPrinter
                presetBar.printProfile = presetStore.currentPrint
                presetBar.filament = presetStore.currentFilament
            }

            // Picking a printer clears the two dependent presets; this is what
            // fills them back in, at the moment the engine reports the list. The
            // remembered pair wins when the machine still offers it.
            function onPrintProfilesChanged() {
                if (!presetBar.autoResolve)
                    return
                presetBar.autoResolve = false

                const profiles = presetStore.printProfiles
                if (profiles.length === 0)
                    return   // the engine has not answered yet; another signal will

                let print = profiles[0]
                for (let i = 0; i < profiles.length; ++i)
                    if (profiles[i] === rememberedSetup.process)
                        print = profiles[i]
                presetBar.printProfile = print

                const filaments = presetStore.filamentProfiles
                let filament = filaments.length > 0 ? filaments[0] : ""
                for (let i = 0; i < filaments.length; ++i)
                    if (filaments[i] === rememberedSetup.filament)
                        filament = filaments[i]
                presetBar.filament = filament

                presetBar.resolveIfReady()
            }
        }

        // The three presets in force, one per row: a third of a 420 px panel per
        // preset truncated every name to "...0.4", which is the one thing this bar
        // must never do -- it is the answer to "what will this slice with".
        Column {
            anchors.fill: parent
            spacing: Metrics.sm

            Row {
                width: parent.width
                spacing: Metrics.sm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: presetBar.presetLabelWidth
                    text: "Printer"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.textSecondary
                }

                ComboBox {
                    width: parent.width - presetBar.presetLabelWidth - printerGear.width
                           - machinePicker.width - Metrics.sm * 3
                    choices: presetBar.printerChoices
                    currentValue: presetBar.printer
                    searchable: true
                    popupWidth: Math.min(460, root.width)
                    onSelected: (value) => presetBar.choosePrinter(value)
                }

                ToolButton {
                    id: machinePicker
                    width: Metrics.controlHeight
                    height: Metrics.controlHeight
                    icon: "grid_view"
                    hint: "All machines (the catalogue and your own)"
                    onClicked: root.machineChooserRequested()
                }

                ToolButton {
                    id: printerGear
                    width: Metrics.controlHeight
                    height: Metrics.controlHeight
                    icon: "settings"
                    hint: "Edit this printer preset"
                    onClicked: root.openPresetEditor("printer")
                }
            }

            Row {
                width: parent.width
                spacing: Metrics.sm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: presetBar.presetLabelWidth
                    text: "Process"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.textSecondary
                }

                ComboBox {
                    width: parent.width - presetBar.presetLabelWidth - processGear.width - Metrics.sm * 2
                    enabled: presetBar.printer.length > 0
                    choices: presetStore.printProfiles
                    currentValue: presetBar.printProfile
                    searchable: true
                    popupWidth: Math.min(460, root.width)
                    onSelected: (value) => {
                        if (presetBar.printProfile === value)
                            return
                        presetBar.printProfile = value
                        presetBar.resolveIfReady()
                    }
                }

                ToolButton {
                    id: processGear
                    width: Metrics.controlHeight
                    height: Metrics.controlHeight
                    icon: "settings"
                    hint: "Edit this process preset"
                    onClicked: root.openPresetEditor("process")
                }
            }

            Repeater {
                model: Math.max(1, presetBar.toolCount)

                delegate: Row {
                    id: filamentRow

                    required property int index

                    width: parent.width
                    spacing: Metrics.sm

                    readonly property string presetName: presetBar.filamentFor(index)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: presetBar.presetLabelWidth
                        text: filamentRow.index === 0 ? "Filament"
                              : presetBar.toolCount > 1 ? "T" + (filamentRow.index + 1) : "Filament"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: true
                        color: Colors.textSecondary
                    }

                    ComboBox {
                        width: parent.width - presetBar.presetLabelWidth - Metrics.controlHeight
                               - Metrics.sm * 2
                        enabled: presetBar.printer.length > 0
                        choices: presetStore.filamentProfiles
                        currentValue: filamentRow.presetName
                        searchable: true
                        popupWidth: Math.min(460, root.width)
                        onSelected: (value) => {
                            if (filamentRow.presetName === value)
                                return
                            presetBar.chooseFilament(filamentRow.index, value)
                        }
                    }

                    ToolButton {
                        visible: filamentRow.index === 0
                        width: Metrics.controlHeight
                        height: Metrics.controlHeight
                        icon: "settings"
                        hint: "Edit this filament preset"
                        onClicked: root.openPresetEditor("filament")
                    }

                    Item {
                        visible: filamentRow.index > 0
                        width: Metrics.controlHeight
                        height: Metrics.controlHeight
                    }
                }
            }

        }
    }

    Separator {
        id: presetSeparator
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: presetBar.bottom
        anchors.topMargin: Metrics.sm
    }

    // The categories of the process, as the strip the reference slicers put in the
    // same place, and the filter over every setting on the right.
    Item {
        id: categoryRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: presetSeparator.bottom
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.xs
        height: Metrics.rowHeight

        TabStrip {
            id: categoryStrip
            anchors.left: parent.left
            anchors.right: categoryTail.left
            anchors.rightMargin: Metrics.sm
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.filtering
            categories: root.categoryEntries()
            current: root.activeSubtab
            onActivated: (key) => root.selectSubtab(key)
        }

        Rectangle {
            id: searchBox
            anchors.left: categoryStrip.visible ? categoryStrip.right : parent.left
            anchors.leftMargin: categoryStrip.visible ? Metrics.sm : 0
            anchors.right: categoryTail.left
            anchors.rightMargin: Metrics.sm
            anchors.verticalCenter: parent.verticalCenter
            height: Metrics.controlHeight
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: filterField.activeFocus ? Colors.primary : Colors.outlineVariant

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
                id: filterField
                anchors.left: searchIcon.right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Metrics.sm
                anchors.rightMargin: Metrics.md
                background: Item {}
                placeholderText: "search"
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textPrimary
                placeholderTextColor: Colors.textMuted
                selectByMouse: true
                onTextChanged: {
                    root.filterText = text
                    root.refresh()
                }
            }
        }

        // What has been changed and the way back to the preset, where the strip
        // leaves room for them: the count is the press, the icon reverts.
        Row {
            id: categoryTail
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Metrics.xs

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.modifiedTotal > 0
                text: root.modifiedTotal + " changed"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.accent

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Metrics.xs
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.changesRequested(root.activeTab)
                }
            }

            ToolButton {
                icon: "restart_alt"
                hint: "Reset every change in this process"
                enabled: root.modifiedTotal > 0
                onClicked: settingsModel.resetTab(root.activeTab)
            }
        }
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: categoryRow.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: Metrics.sm
        anchors.bottomMargin: Metrics.sm
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg

        SettingsTree {
            anchors.fill: parent
            groups: root.visibleGroups
            filterText: root.filterText
        }
    }

    PresetEditorDialog {
        id: presetEditor
        onNetworkRequested: root.networkRequested()
    }
}
