// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "../theme"
import "../components"

// next-layer's first-run setup. The engine's onboarding is not this app's — an
// account prompt, an online/offline question, release notes — so this app owns
// its own: machine, material, print quality, review. No network, no account, and
// the chosen triple is remembered for the next launch.
Item {
    id: root

    signal closed()

    Settings {
        id: saved
        category: "setup"

        property bool done: false
        property bool dismissed: false
        property string machine: ""
        property string material: ""
        property string quality: ""
    }

    property int step: 0
    property string machine: ""
    property string material: ""
    property string quality: ""
    property string filter: ""
    property string vendor: ""
    property bool customOpen: false
    property string customMessage: ""

    readonly property var steps: ["Machine", "Material", "Quality", "Review"]
    readonly property bool ready: machine !== "" && material !== "" && quality !== ""
    readonly property bool nextEnabled: step === 0 ? machine !== ""
                                         : step === 1 ? material !== ""
                                         : step === 2 ? quality !== ""
                                         : ready

    readonly property var vendorChoices: {
        const out = [{ value: "", label: "All vendors (" + presetStore.printers.length + ")" }]
        const vendors = root.vendors
        for (let i = 0; i < vendors.length; ++i)
            out.push({ value: vendors[i].name, label: vendors[i].name + " (" + vendors[i].count + ")" })
        return out
    }

    readonly property var vendors: {
        const counts = ({})
        const order = []
        for (let i = 0; i < presetStore.printers.length; ++i) {
            const name = presetStore.printers[i].vendor
            if (name === "")
                continue
            if (counts[name] === undefined) {
                counts[name] = 0
                order.push(name)
            }
            counts[name] += 1
        }
        order.sort()
        const out = []
        for (let i = 0; i < order.length; ++i)
            out.push({ name: order[i], count: counts[order[i]] })
        return out
    }

    readonly property var machines: {
        const needle = root.filter.toLowerCase()
        const out = []
        for (let i = 0; i < presetStore.printers.length; ++i) {
            const entry = presetStore.printers[i]
            if (root.vendor !== "" && entry.vendor !== root.vendor)
                continue
            if (needle !== "" && entry.name.toLowerCase().indexOf(needle) < 0
                && entry.vendor.toLowerCase().indexOf(needle) < 0)
                continue
            out.push(entry)
        }
        return out
    }

    readonly property var materials: {
        const needle = root.filter.toLowerCase()
        const out = []
        for (let i = 0; i < presetStore.filamentProfiles.length; ++i) {
            const name = presetStore.filamentProfiles[i]
            if (needle !== "" && name.toLowerCase().indexOf(needle) < 0)
                continue
            out.push(name)
        }
        return out
    }

    readonly property var qualities: {
        const needle = root.filter.toLowerCase()
        const out = []
        for (let i = 0; i < presetStore.printProfiles.length; ++i) {
            const name = presetStore.printProfiles[i]
            if (needle !== "" && name.toLowerCase().indexOf(needle) < 0)
                continue
            out.push(name)
        }
        return out
    }

    readonly property var selectedMachine: {
        for (let i = 0; i < presetStore.printers.length; ++i)
            if (presetStore.printers[i].name === root.machine)
                return presetStore.printers[i]
        return null
    }

    function open() {
        visible = true
        root.filter = ""
        root.customOpen = false
        root.customMessage = ""
        if (presetStore.printers.length === 0)
            presetStore.refreshPrinters()
    }

    // A machine the catalog does not carry: the user knows its bed, so the bed is
    // the whole question. This writes a printer preset into the local bundle and
    // nothing is inherited from any vendor's profile.
    function createCustom(name, width, depth, height) {
        const error = presetStore.createPrinter(name, width, depth, height)
        if (error.length > 0) {
            root.customMessage = error
            return
        }
        presetStore.refreshPrinters()
        root.customOpen = false
        root.customMessage = ""
        root.chooseMachine(name)
        root.step = 1
    }

    function close() {
        visible = false
        closed()
    }

    function reopen() {
        saved.dismissed = false
        root.open()
    }

    function goTo(index) {
        if (index < 0 || index > 3)
            return
        if (index > 0 && root.machine === "")
            return
        if (index > 1 && root.material === "")
            return
        if (index > 2 && root.quality === "")
            return
        root.step = index
        root.filter = ""
    }

    function chooseMachine(name) {
        if (root.machine === name)
            return
        root.machine = name
        root.material = ""
        root.quality = ""
        presetStore.selectPrinter(name)
    }

    function apply() {
        presetStore.reload(root.machine, root.quality, root.material)
        saved.machine = root.machine
        saved.material = root.material
        saved.quality = root.quality
        saved.done = true
        root.close()
    }

    function firstRun() {
        return !saved.done && !saved.dismissed
    }

    // "Not now" is an answer: it stops the flow from opening on every launch and
    // leaves Ctrl+, and the rail gear as the way back in.
    function dismiss() {
        saved.dismissed = true
        root.close()
    }

    function remember() {
        if (saved.machine === "")
            return
        presetStore.reload(saved.machine, saved.quality, saved.material)
    }

    function stepReached(index) {
        if (index === 0)
            return true
        if (index === 1)
            return root.machine !== ""
        if (index === 2)
            return root.material !== ""
        return root.quality !== ""
    }

    visible: false
    z: 100
    focus: visible

    Keys.onEscapePressed: root.close()
    Keys.onReturnPressed: if (root.nextEnabled) root.step === 3 ? root.apply() : root.goTo(root.step + 1)
    Keys.onUpPressed: if (root.step === 3) root.goTo(root.step - 1)
    Keys.onDownPressed: if (root.step === 3) root.goTo(root.step + 1)

    Rectangle {
        anchors.fill: parent
        color: Colors.alpha(Colors.background, 0.92)
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Metrics.panelMargin * 4, 1120)
        height: Math.min(parent.height - Metrics.panelMargin * 3, 680)
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.edge

        Rectangle {
            id: side
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 292
            radius: Metrics.radiusLarge
            color: Colors.surfaceContainerLowest

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.radius
                color: Colors.surfaceContainerLowest
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Metrics.borderWidth
                color: Colors.edge
            }

            Column {
                id: sideColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Metrics.panelPadding
                spacing: Metrics.lg

                Column {
                    width: parent.width
                    spacing: Metrics.xxs

                    Text {
                        text: "next-layer"
                        textFormat: Text.PlainText
                        font.family: Type.family
                        font.pixelSize: Type.sizeHeading
                        font.bold: true
                        color: Colors.accent
                    }

                    Text {
                        text: "printer setup"
                        textFormat: Text.PlainText
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }
                }

                Column {
                    width: parent.width
                    spacing: Metrics.xs

                    Repeater {
                        model: root.steps

                        delegate: Item {
                            id: stepRow
                            required property string modelData
                            required property int index

                            width: parent.width
                            height: Metrics.rowHeight + Metrics.xs

                            Rectangle {
                                anchors.fill: parent
                                radius: Metrics.radiusSmall
                                color: root.step === index ? Colors.selectedFill
                                     : stepMouse.containsMouse && root.stepReached(index)
                                       ? Colors.hoverFill
                                     : "transparent"
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Metrics.xxs
                                height: parent.height - Metrics.md * 2
                                radius: width / 2
                                color: root.step === index ? Colors.accent
                                     : root.stepReached(index) ? Colors.edgeStrong
                                     : Colors.edge
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Metrics.md + Metrics.sm
                                anchors.verticalCenter: parent.verticalCenter
                                text: (index + 1) + "   " + modelData
                                textFormat: Text.PlainText
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: root.step === index ? Colors.accent
                                     : root.stepReached(index) ? Colors.textPrimary
                                     : Colors.textMuted
                            }

                            MouseArea {
                                id: stepMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.stepReached(index)
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.goTo(index)
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Metrics.hairline
                    color: Colors.edge
                }

                Column {
                    width: parent.width
                    spacing: Metrics.sm

                    Text {
                        width: parent.width
                        text: "CHOSEN"
                        textFormat: Text.PlainText
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: true
                        font.letterSpacing: 1.2
                        color: Colors.textTertiary
                    }

                    Text {
                        width: parent.width
                        text: root.machine !== "" ? root.machine : "no machine yet"
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: root.machine !== "" ? Colors.textPrimary : Colors.textMuted
                    }

                    Text {
                        width: parent.width
                        text: root.material !== "" ? root.material : "no material yet"
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: root.material !== "" ? Colors.textPrimary : Colors.textMuted
                    }

                    Text {
                        width: parent.width
                        text: root.quality !== "" ? root.quality : "no quality yet"
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: root.quality !== "" ? Colors.textPrimary : Colors.textMuted
                    }
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Metrics.panelPadding
                text: presetStore.busy
                      ? "reading the catalogue…"
                      : presetStore.error !== ""
                        ? presetStore.error
                        : presetStore.printers.length + " machines · " + root.vendors.length + " vendors"
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: presetStore.error !== "" ? Colors.error : Colors.textTertiary
            }
        }

        Item {
            id: body
            anchors.left: side.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: footer.top
            anchors.leftMargin: Metrics.panelPadding
            anchors.rightMargin: Metrics.panelPadding
            anchors.topMargin: Metrics.panelPadding
            anchors.bottomMargin: Metrics.md

            ColumnLayout {
                anchors.fill: parent
                spacing: Metrics.sm

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: stepTitle.implicitHeight

                    Text {
                        id: stepTitle
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.steps[root.step]
                        textFormat: Text.PlainText
                        font.family: Type.family
                        font.pixelSize: Type.sizeLarge
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.step + 1) + " / " + root.steps.length
                        textFormat: Text.PlainText
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }
                }

                Text {
                    id: blurb
                    Layout.fillWidth: true
                    Layout.preferredHeight: blurb.implicitHeight
                    width: parent.width
                    text: root.step === 0
                          ? "The machine you print on. Every entry is a printer profile from the engine's bundle, so the bed, the extruders and the limits are the real definition."
                          : root.step === 1
                          ? "The material loaded now. This is the machine's own compatible filament set, with its own temperatures, cooling and flow."
                          : root.step === 2
                          ? "Print quality. These are the machine's own process profiles: every layer height, speed and shell count follows from this choice."
                          : "That is what next-layer will slice with. Nothing leaves this machine: no account, no cloud, no telemetry."
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textTertiary
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.controlHeight
                    width: parent.width
                    height: Metrics.controlHeight
                    visible: root.step !== 3

                Rectangle {
                    width: parent.width
                    height: Metrics.controlHeight
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer
                    border.width: Metrics.borderWidth
                    border.color: searchInput.activeFocus ? Colors.accent : Colors.edge

                    TextInput {
                        id: searchInput
                        anchors.fill: parent
                        anchors.leftMargin: Metrics.controlPaddingX
                        anchors.rightMargin: Metrics.controlPaddingX
                        verticalAlignment: TextInput.AlignVCenter
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textPrimary
                        selectionColor: Colors.primaryContainer
                        selectedTextColor: Colors.primaryContainerText
                        clip: true
                        text: root.filter
                        onTextChanged: root.filter = text
                    }

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Metrics.controlPaddingX
                        verticalAlignment: Text.AlignVCenter
                        visible: searchInput.text === ""
                        text: root.step === 0 ? "search " + root.machines.length + " machines by name or vendor"
                                              : root.step === 1 ? "search " + root.materials.length + " materials"
                                              : "search " + root.qualities.length + " quality profiles"
                        textFormat: Text.PlainText
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textTertiary
                    }
                }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    width: parent.width

                    Item {
                        id: machinePane
                        anchors.fill: parent
                        visible: root.step === 0 && !root.customOpen

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Metrics.sm

                            Row {
                                id: chips
                                Layout.fillWidth: true
                                Layout.preferredHeight: Metrics.controlHeight
                                width: parent.width
                                height: Metrics.controlHeight
                                spacing: Metrics.sm

                                ComboBox {
                                    width: parent.width * 0.42
                                    choices: root.vendorChoices
                                    currentValue: root.vendor
                                    searchable: true
                                    onSelected: (value) => root.vendor = value
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width * 0.33 - parent.spacing
                                    text: root.machines.length + " machines"
                                            + (root.vendor === "" ? "" : " from " + root.vendor)
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeCaption
                                    color: Colors.textTertiary
                                }

                                Button {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width * 0.25 - parent.spacing
                                    text: "Not listed?"
                                    hint: "Create a printer of my own"
                                    onClicked: root.customOpen = true
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                width: parent.width
                                clip: true
                                model: root.machines
                                spacing: Metrics.xxs

                                delegate: Item {
                                    id: machineRow
                                    required property var modelData

                                    width: ListView.view.width
                                    height: Metrics.rowHeight + Metrics.sm

                                    readonly property bool current: root.machine === modelData.name

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Metrics.radiusSmall
                                        color: machineRow.current ? Colors.selectedFill
                                             : machineMouse.containsMouse ? Colors.hoverFill
                                             : "transparent"
                                    }

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: Metrics.md
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width * 0.52
                                        text: machineRow.modelData.name
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeSmall
                                        color: machineRow.current ? Colors.accent : Colors.textPrimary
                                    }

                                    Text {
                                        anchors.right: parent.right
                                        anchors.rightMargin: Metrics.md
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: machineRow.modelData.vendor
                                              + "   " + Math.round(machineRow.modelData.width)
                                              + "×" + Math.round(machineRow.modelData.height)
                                              + "×" + Math.round(machineRow.modelData.maxPrintHeight)
                                              + " mm   " + machineRow.modelData.extruders
                                              + (machineRow.modelData.extruders === 1 ? " tool" : " tools")
                                        textFormat: Text.PlainText
                                        font.family: Type.family
                                        font.pixelSize: Type.sizeCaption
                                        color: machineRow.current ? Colors.accent : Colors.textTertiary
                                    }

                                    MouseArea {
                                        id: machineMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.chooseMachine(machineRow.modelData.name)
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        id: customPane
                        anchors.fill: parent
                        visible: root.step === 0 && root.customOpen

                        Column {
                            anchors.fill: parent
                            spacing: Metrics.md

                            Text {
                                width: parent.width
                                text: "A printer the catalog does not carry. Give it a name and the size it actually prints on — this writes a printer preset of your own, inherited from no vendor profile."
                                textFormat: Text.PlainText
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textTertiary
                            }

                            Rectangle {
                                width: parent.width
                                height: Metrics.controlHeight
                                radius: Metrics.radiusSmall
                                color: Colors.surfaceContainer
                                border.width: Metrics.borderWidth
                                border.color: customName.activeFocus ? Colors.accent : Colors.edge

                                TextInput {
                                    id: customName
                                    anchors.fill: parent
                                    anchors.leftMargin: Metrics.controlPaddingX
                                    anchors.rightMargin: Metrics.controlPaddingX
                                    verticalAlignment: TextInput.AlignVCenter
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeSmall
                                    color: Colors.textPrimary
                                    selectionColor: Colors.primaryContainer
                                    selectedTextColor: Colors.primaryContainerText
                                    clip: true
                                    onTextChanged: root.customMessage = ""
                                }

                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: Metrics.controlPaddingX
                                    verticalAlignment: Text.AlignVCenter
                                    visible: customName.text === ""
                                    text: "printer name"
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeSmall
                                    color: Colors.textTertiary
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: Metrics.sm

                                NumberField {
                                    id: customWidth
                                    width: (parent.width - parent.spacing * 2) / 3
                                    value: 350
                                    from: 10
                                    to: 2000
                                    units: "W"
                                }

                                NumberField {
                                    id: customDepth
                                    width: (parent.width - parent.spacing * 2) / 3
                                    value: 350
                                    from: 10
                                    to: 2000
                                    units: "D"
                                }

                                NumberField {
                                    id: customHeight
                                    width: (parent.width - parent.spacing * 2) / 3
                                    value: 330
                                    from: 10
                                    to: 2000
                                    units: "H"
                                }
                            }

                            Row {
                                spacing: Metrics.sm

                                Button {
                                    text: "Create this printer"
                                    primary: true
                                    enabled: !presetStore.busy && customName.text.length > 0
                                    onClicked: root.createCustom(customName.text, customWidth.value,
                                                                 customDepth.value, customHeight.value)
                                }

                                Button {
                                    text: "Back to the list"
                                    onClicked: {
                                        root.customOpen = false
                                        root.customMessage = ""
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                visible: root.customMessage !== ""
                                text: root.customMessage
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.error
                            }
                        }
                    }

                    ListView {
                        id: materialPane
                        anchors.fill: parent
                        visible: root.step === 1
                        clip: true
                        model: root.materials
                        spacing: Metrics.xxs

                        delegate: ListRow {
                            required property string modelData
                            width: ListView.view.width
                            label: modelData
                            selected: root.material === modelData
                            onClicked: root.material = modelData
                        }
                    }

                    ListView {
                        id: qualityPane
                        anchors.fill: parent
                        visible: root.step === 2
                        clip: true
                        model: root.qualities
                        spacing: Metrics.xxs

                        delegate: ListRow {
                            required property string modelData
                            width: ListView.view.width
                            label: modelData
                            selected: root.quality === modelData
                            onClicked: root.quality = modelData
                        }
                    }

                    Column {
                        anchors.fill: parent
                        visible: root.step === 3
                        spacing: Metrics.md

                        Repeater {
                            model: [
                                { heading: "Machine", detail: root.machine },
                                { heading: "Material", detail: root.material },
                                { heading: "Print quality", detail: root.quality }
                            ]

                            delegate: Item {
                                required property var modelData
                                width: parent.width
                                height: Metrics.rowHeight

                                Text {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.heading
                                    textFormat: Text.PlainText
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeSmall
                                    color: Colors.textTertiary
                                }

                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width * 0.62
                                    text: modelData.detail
                                    textFormat: Text.PlainText
                                    elide: Text.ElideLeft
                                    horizontalAlignment: Text.AlignRight
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeSmall
                                    color: Colors.textPrimary
                                }

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: Metrics.hairline
                                    color: Colors.edge
                                }
                            }
                        }

                        Item {
                            width: parent.width
                            height: Metrics.md
                        }

                        Text {
                            width: parent.width
                            text: root.selectedMachine !== null
                                  ? "Bed " + Math.round(root.selectedMachine.width) + " × "
                                    + Math.round(root.selectedMachine.height) + " × "
                                    + Math.round(root.selectedMachine.maxPrintHeight) + " mm · "
                                    + root.selectedMachine.vendor
                                    + " — everything else stays editable in the process, filament and printer tabs."
                                  : ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textTertiary
                        }
                    }
                }
            }
        }

        Row {
            id: footer
            anchors.left: side.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Metrics.panelPadding
            anchors.rightMargin: Metrics.panelPadding
            anchors.bottomMargin: Metrics.panelPadding
            height: Metrics.controlHeight
            spacing: Metrics.md

            Button {
                text: root.step === 0 ? "Not now" : "Back"
                onClicked: {
                    if (root.step === 0)
                        root.dismiss()
                    else
                        root.goTo(root.step - 1)
                }
            }

            Item {
                width: Math.max(8, parent.width - 240 * 3 - Metrics.md * 4)
                height: 1
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.step === 3 && !root.ready ? "pick a machine, a material and a quality first" : ""
                textFormat: Text.PlainText
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textMuted
            }

            Button {
                primary: root.nextEnabled
                enabled: root.nextEnabled
                text: root.step === 3 ? "Start slicing" : "Continue"
                onClicked: {
                    if (root.step === 3)
                        root.apply()
                    else
                        root.goTo(root.step + 1)
                }
            }
        }
    }
}
