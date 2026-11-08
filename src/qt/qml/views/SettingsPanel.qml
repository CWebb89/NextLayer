// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtCore
import "../theme"
import "../components"

// Settings surface: Process / Filament / Printer
// tabs on top, a horizontal strip of the engine's own option groups under them,
// then one scroll of two-column rows (label left, editor right). Only the active
// group is instantiated, so 335 engine settings cost a few dozen items.
Item {
    id: root

    property string activeTab: "process"
    property string activeSubtab: ""
    property string filterText: ""

    property var tabs: []
    property var visibleGroups: []
    property var lastSubtab: ({})

    readonly property bool filtering: root.filterText.length > 0
    readonly property var currentTab: root.tabData(root.activeTab)
    readonly property var currentSubtabs: root.currentTab ? root.currentTab.subtabs : []
    readonly property int modifiedTotal: settingsModel.modifiedCount("")

    function tabData(key) {
        for (let i = 0; i < root.tabs.length; ++i)
            if (root.tabs[i].key === key)
                return root.tabs[i]
        return null
    }

    function selectTab(key) {
        root.activeTab = key
        root.activeSubtab = root.lastSubtab[key] !== undefined ? root.lastSubtab[key] : ""
        root.refresh()
    }

    function showSubtab(tab, subtab) {
        const remembered = Object.assign({}, root.lastSubtab)
        remembered[tab] = subtab
        root.lastSubtab = remembered
        root.activeTab = tab
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

    /// The categories of the open tab, for the selector row. A category with
    /// changed settings carries a dot in its label, so the one place that lists
    /// them all also says which ones are not at their preset value.
    function subtabChoices() {
        const out = []
        for (let i = 0; i < root.currentSubtabs.length; ++i) {
            const entry = root.currentSubtabs[i]
            out.push({ value: entry.key,
                       label: entry.modified > 0 ? entry.title + "  \u2022" : entry.title })
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
        const start = typeof startupSettingsTab !== "undefined" ? String(startupSettingsTab) : ""
        if (start.length > 0) {
            const parts = start.split("/")
            root.activeTab = parts[0]
            if (parts.length > 1) {
                const remembered = {}
                remembered[parts[0]] = parts[1]
                root.lastSubtab = remembered
                root.activeSubtab = parts[1]
            }
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
        height: Metrics.controlHeight * 3 + Metrics.sm * 2

        readonly property int presetLabelWidth: 66

        property string printer: ""
        property string printProfile: ""
        property string filament: ""
        property bool autoResolve: false

        readonly property var printerChoices: {
            const out = []
            const printers = presetStore.printers
            for (let i = 0; i < printers.length; ++i)
                out.push({ value: printers[i].name, label: printers[i].name })
            return out
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
                    width: parent.width - presetBar.presetLabelWidth - printerGear.width - Metrics.sm * 2
                    choices: presetBar.printerChoices
                    currentValue: presetBar.printer
                    searchable: true
                    popupWidth: Math.min(460, root.width)
                    onSelected: (value) => presetBar.choosePrinter(value)
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

            Row {
                width: parent.width
                spacing: Metrics.sm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: presetBar.presetLabelWidth
                    text: "Filament"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.textSecondary
                }

                ComboBox {
                    width: parent.width - presetBar.presetLabelWidth - filamentGear.width - Metrics.sm * 2
                    enabled: presetBar.printer.length > 0
                    choices: presetStore.filamentProfiles
                    currentValue: presetBar.filament
                    searchable: true
                    popupWidth: Math.min(460, root.width)
                    onSelected: (value) => {
                        if (presetBar.filament === value)
                            return
                        presetBar.filament = value
                        presetBar.resolveIfReady()
                    }
                }

                ToolButton {
                    id: filamentGear
                    width: Metrics.controlHeight
                    height: Metrics.controlHeight
                    icon: "settings"
                    hint: "Edit this filament preset"
                    onClicked: root.openPresetEditor("filament")
                }
            }

        }
    }

    Item {
        id: tabStrip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: presetBar.bottom
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.sm
        height: Metrics.rowHeight

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Metrics.sm

            Repeater {
                model: root.tabs

                delegate: Item {
                    id: tabButton

                    required property var modelData

                    readonly property bool active: root.activeTab === modelData.key

                    width: tabLabel.implicitWidth + Metrics.lg * 2
                    height: tabStrip.height

                    Rectangle {
                        anchors.fill: parent
                        radius: Metrics.radiusSmall
                        color: tabMouse.containsMouse && !tabButton.active ? Colors.hoverFill
                             : "transparent"
                    }

                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: tabButton.modelData.title
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        font.bold: true
                        color: tabButton.active ? Colors.textPrimary : Colors.textTertiary
                    }

                    Rectangle {
                        visible: tabButton.modelData.modified > 0
                        anchors.right: parent.right
                        anchors.rightMargin: Metrics.xs
                        anchors.top: parent.top
                        anchors.topMargin: Metrics.xs
                        width: Metrics.sm
                        height: Metrics.sm
                        radius: width / 2
                        color: Colors.accent
                    }

                    Rectangle {
                        visible: tabButton.active
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Metrics.xxs
                        radius: Metrics.xxs
                        color: Colors.accent
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectTab(tabButton.modelData.key)
                    }
                }
            }
        }

        Row {
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
            }

            ToolButton {
                icon: "restart_alt"
                hint: "Reset every change on this tab"
                enabled: root.modifiedTotal > 0
                onClicked: settingsModel.resetTab(root.activeTab)
            }
        }
    }

    Separator {
        id: tabSeparator
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabStrip.bottom
    }

    // The categories of the open tab and the filter over every tab, on one row.
    //
    // They were two: a wrapping strip of category chips that took up to four rows
    // of a 490 px panel, and a search field under it. Fourteen chips cannot be
    // read at a glance anyway, so the strip is a selector that names the category
    // you are in and lists all of them, and the space it used to take belongs to
    // the settings.
    Item {
        id: categoryRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabSeparator.bottom
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.sm
        height: Metrics.controlHeight

        ComboBox {
            id: categoryPicker
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 158
            visible: !root.filtering
            elideMode: Text.ElideRight
            choices: root.subtabChoices()
            currentValue: root.activeSubtab
            onSelected: (value) => root.selectSubtab(String(value))
        }

        Rectangle {
            id: searchBox
            anchors.left: categoryPicker.visible ? categoryPicker.right : parent.left
            anchors.leftMargin: categoryPicker.visible ? Metrics.sm : 0
            anchors.right: parent.right
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
            placeholderText: "search all settings"
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
    }
}
