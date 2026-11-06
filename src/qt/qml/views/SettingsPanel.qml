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

    function resetSubtab() {
        settingsModel.resetSubtab(root.activeTab, root.activeSubtab)
    }

    function activeSubtabModified() {
        const tab = root.currentTab
        for (let i = 0; i < (tab ? tab.subtabs.length : 0); ++i)
            if (tab.subtabs[i].key === root.activeSubtab)
                return tab.subtabs[i].modified
        return 0
    }

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

    // A printer is always selected: an empty bar reads as a broken app, and the
    // first node of the bundle is what is shown before anything is
    // chosen. The setup flow overrides this the moment it resolves a triple.
    Connections {
        target: presetStore

        // Last used machine first, then the first of the bundle: the panel must
        // never open empty, and it should open on the machine that was last
        // sliced with rather than on whatever sorts first in the catalogue.
        function onPrintersChanged() {
            if (presetBar.printer.length > 0)
                return
            const printers = presetStore.printers
            if (printers.length === 0)
                return
            let wanted = rememberedSetup.printer
            let found = false
            for (let i = 0; i < printers.length; ++i) {
                if (printers[i].name === wanted) {
                    found = true
                    break
                }
            }
            presetBar.choosePrinter(found ? wanted : printers[0].name)
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

    Item {
        id: subtabStrip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabSeparator.bottom
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.xs
        // The chips wrap instead of scrolling sideways: a group's strip is three
        // to five chips, and a clipped one reads as a cut-off label rather than
        // as something that can be scrolled.
        height: subtabFlow.implicitHeight + Metrics.xs * 2
        visible: !root.filtering

        Flow {
            id: subtabFlow
            y: Metrics.xs
            width: parent.width - (subtabReset.visible ? subtabReset.width + Metrics.sm : 0)
            spacing: Metrics.xxs

            Repeater {
                model: root.currentSubtabs

                delegate: Item {
                    id: subtabChip

                    required property var modelData

                    readonly property bool active: root.activeSubtab === modelData.key

                    width: subtabText.implicitWidth + Metrics.md * 2 + (subtabChip.modelData.modified > 0
                                                                       ? Metrics.sm : 0)
                    height: Metrics.rowHeight - Metrics.xs * 2

                    Rectangle {
                        anchors.fill: parent
                        radius: Metrics.radiusSmall
                        color: subtabChip.active ? Colors.selectedFill
                             : subtabMouse.containsMouse ? Colors.hoverFill : "transparent"
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: Metrics.xs

                        Text {
                            id: subtabText
                            anchors.verticalCenter: parent.verticalCenter
                            text: subtabChip.modelData.title
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            font.bold: true
                            color: subtabChip.active ? Colors.accent
                                 : subtabMouse.containsMouse ? Colors.textPrimary : Colors.textSecondary
                        }

                        Rectangle {
                            visible: subtabChip.modelData.modified > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: Metrics.xs + Metrics.xxs
                            height: width
                            radius: width / 2
                            color: Colors.accent
                        }
                    }

                    MouseArea {
                        id: subtabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectSubtab(subtabChip.modelData.key)
                    }
                }
            }
        }
    }

    ToolButton {
        id: subtabReset
        anchors.right: parent.right
        anchors.rightMargin: Metrics.lg
        anchors.top: subtabStrip.top
        anchors.topMargin: Metrics.xs * 2
        icon: "restart_alt"
        hint: "Reset every change in this group"
        visible: !root.filtering
        enabled: root.activeSubtabModified() > 0
        onClicked: root.resetSubtab()
    }

    Item {
        id: searchRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: subtabStrip.visible ? subtabStrip.bottom : tabSeparator.bottom
        anchors.leftMargin: Metrics.lg
        anchors.rightMargin: Metrics.lg
        anchors.topMargin: Metrics.sm
        height: Metrics.controlHeight

        Rectangle {
            anchors.fill: parent
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: filterField.activeFocus ? Colors.primary : Colors.outlineVariant
        }

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

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: searchRow.bottom
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
