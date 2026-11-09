// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import QtCore
import "../theme"
import "../components"

// What a new install opens on: not a form, a welcome. It says what next-layer
// is, what it does not do (no account, no cloud), and offers the one action a
// fresh install actually needs -- set up the printer -- with a plain way out for
// someone who would rather look around first.
Item {
    id: root

    property string version: ""

    signal setupRequested()
    signal skipRequested()
    signal imported(string printer)
    signal restartRequested()

    property var importResult: ({})
    property string importError: ""

    Settings {
        id: saved
        category: "welcome"
        property bool seen: false
    }

    property bool seen: saved.seen

    function open() {
        saved.seen = true
        visible = true
        forceActiveFocus()
    }

    function close() {
        visible = false
    }

    readonly property var highlights: [
        { title: "A real slicing engine", text: "The geometry, supports and G-code come from a mature engine library. NextLayer is the application around it, and everything you see here is ours." },
        { title: "Shapes, volumes, paint", text: "Add a box, cut it with a negative volume, force support where you want it, paint seams and tools." },
        { title: "Built for tool changers", text: "Per-tool colours and tool changes — implemented, and only printed on a single-extruder machine so far." },
        { title: "Local, always", text: "No account, no sign-in, no telemetry. Projects are one file on your disk." }
    ]

    visible: false
    z: 120
    focus: visible

    Keys.onEscapePressed: {
        root.close()
        root.skipRequested()
    }

    // The other slicers keep their presets in a directory named after the
    // application under the user's config home. Offering the one that exists
    // saves a filesystem hunt; the picker still takes any directory.
    function guessSlicerDirectory() {
        const dir = StandardPaths.writableLocation(StandardPaths.ConfigLocation).toString()
        return dir.startsWith("file://") ? dir.substring(7) : dir
    }

    function runImport(directory) {
        importError = ""
        importResult = presetStore.importSlicerPresets(directory)
        if (!importResult.ok) {
            importError = importResult.error
            return
        }
        if (importResult.count === 0) {
            importError = "Nothing in there this engine can express"
            return
        }
        importPanel.visible = true
    }

    Rectangle {
        id: sourcePanel

        visible: false
        anchors.centerIn: parent
        width: Math.min(parent.width - Metrics.panelMargin * 3, 560)
        height: sourceList.implicitHeight + Metrics.panelPadding * 3
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.edge
        z: 10

        Column {
            id: sourceList

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Metrics.panelPadding
            spacing: Metrics.sm

            Text {
                text: "Import presets from"
                font.family: Type.family
                font.pixelSize: Type.sizeTitle
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                width: parent.width
                text: "Machines, processes and filaments are read out of the other slicer's own configuration and written into yours, under their own names. Anything this engine has no equivalent for stays behind."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Repeater {
                model: presetStore.slicerSources()

                delegate: Item {
                    id: sourceRow

                    required property var modelData

                    width: sourceList.width
                    height: Metrics.controlHeight

                    Rectangle {
                        anchors.fill: parent
                        radius: Metrics.radiusSmall
                        color: sourceMouse.containsMouse ? Colors.hoverFill : "transparent"
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: Metrics.sm

                        Text {
                            text: sourceRow.modelData.name
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            color: Colors.textPrimary
                        }

                        Text {
                            text: sourceRow.modelData.path
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textMuted
                        }
                    }

                    MouseArea {
                        id: sourceMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            sourcePanel.visible = false
                            root.runImport(sourceRow.modelData.path)
                        }
                    }
                }
            }

            Text {
                visible: presetStore.slicerSources().length === 0
                width: parent.width
                text: "None found under " + root.guessSlicerDirectory() + ". Set up a printer instead, or type a folder below."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Row {
                width: parent.width
                spacing: Metrics.sm

                TextField {
                    id: customPath
                    width: parent.width - importButton.width - Metrics.sm
                    height: Metrics.controlHeight
                    placeholderText: "or the folder itself"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                    placeholderTextColor: Colors.textMuted
                    selectByMouse: true
                }

                Button {
                    id: importButton
                    text: "Import"
                    onClicked: {
                        sourcePanel.visible = false
                        root.runImport(customPath.text)
                    }
                }
            }

            Button {
                text: "Cancel"
                onClicked: sourcePanel.visible = false
            }
        }
    }

    Rectangle {
        id: importPanel

        visible: false
        anchors.centerIn: parent
        width: Math.min(parent.width - Metrics.panelMargin * 3, 520)
        height: importSummary.implicitHeight + Metrics.panelPadding * 3
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.edge
        z: 10

        Column {
            id: importSummary
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Metrics.panelPadding
            spacing: Metrics.sm

            Text {
                text: "Imported from that slicer"
                font.family: Type.family
                font.pixelSize: Type.sizeTitle
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                width: parent.width
                text: (root.importResult.printers ? root.importResult.printers.length : 0) + " printers, "
                      + (root.importResult.prints ? root.importResult.prints.length : 0) + " processes, "
                      + (root.importResult.filaments ? root.importResult.filaments.length : 0) + " filaments."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textPrimary
            }

            Text {
                width: parent.width
                visible: !!(root.importResult.refused && root.importResult.refused.length > 0)
                text: root.importResult.refused && root.importResult.refused.length > 0
                      ? "Left out: " + root.importResult.refused.slice(0, 3).join("; ")
                      : ""
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Text {
                width: parent.width
                visible: !!(root.importResult.elsewhere && root.importResult.elsewhere.length > 0)
                text: root.importResult.elsewhere && root.importResult.elsewhere.length > 0
                      ? root.importResult.elsewhere.length
                        + " of them live in another profile on this engine (retraction is per filament here), so they stay with the machine's own settings."
                      : ""
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Text {
                width: parent.width
                text: (root.importResult.dropped ? root.importResult.dropped.length : 0)
                      + " setting(s) were dropped: this engine has no equivalent, and a guessed value is worse than none."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            Text {
                width: parent.width
                visible: root.importResult.restartNeeded === true
                text: "The engine reads its preset bundle once per start, so these machines appear after NextLayer restarts."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }

            Row {
                spacing: Metrics.sm

                Button {
                    text: root.importResult.restartNeeded === true ? "Restart NextLayer" : "Use these printers"
                    primary: true
                    onClicked: {
                        importPanel.visible = false
                        root.close()
                        if (root.importResult.restartNeeded === true)
                            root.restartRequested()
                        else
                            root.imported(root.importResult.firstPrinter)
                    }
                }

                Button {
                    text: "Close"
                    onClicked: importPanel.visible = false
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Colors.alpha(Colors.background, 0.94)
    }

    Rectangle {
        id: card

        anchors.centerIn: parent
        width: Math.min(parent.width - Metrics.panelMargin * 4, 940)
        // The card is as tall as what is in it. A fixed 600 px cut the buttons off
        // the bottom edge as soon as the copy wrapped to one more line.
        height: Math.min(parent.height - Metrics.panelMargin * 3,
                         hero.implicitHeight + Metrics.panelPadding * 3)
        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.edge

        Column {
            id: hero
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Metrics.panelPadding * 1.5
            width: Math.min(420, parent.width * 0.46)
            spacing: Metrics.md

            Row {
                spacing: Metrics.md

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/next-layer/brand/next-layer-mark.svg"
                    sourceSize: Qt.size(Type.sizeMedium, Type.sizeMedium)
                    fillMode: Image.PreserveAspectFit
                    width: Type.sizeMedium
                    height: Type.sizeMedium
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Metrics.xxs

                    Text {
                        text: "NextLayer"
                        font.family: Type.family
                        font.pixelSize: Type.sizeXLarge
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        text: "slicing, on your machine"
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textTertiary
                    }
                }
            }

            Text {
                width: parent.width
                text: "Two minutes and you are slicing."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeLarge
                color: Colors.textSecondary
            }

            Column {
                width: parent.width
                spacing: Metrics.sm

                Repeater {
                    model: root.highlights

                    delegate: Item {
                        id: line

                        required property var modelData

                        width: parent.width
                        height: lineColumn.implicitHeight

                        Rectangle {
                            id: bullet
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.topMargin: Metrics.xs
                            width: Metrics.xs
                            height: Metrics.xs
                            radius: width / 2
                            color: Colors.accent
                        }

                        Column {
                            id: lineColumn
                            anchors.left: bullet.right
                            anchors.leftMargin: Metrics.sm
                            anchors.right: parent.right
                            spacing: 1

                            Text {
                                text: line.modelData.title
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                font.bold: true
                                color: Colors.textPrimary
                            }

                            Text {
                                width: parent.width
                                text: line.modelData.text
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textTertiary
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Metrics.sm }

            Flow {
                width: parent.width
                spacing: Metrics.sm

                Button {
                    text: "Set up my printer"
                    primary: true
                    onClicked: {
                        root.close()
                        root.setupRequested()
                    }
                }

                Button {
                    text: "Import my presets"
                    onClicked: sourcePanel.visible = true
                }
            }

            // A way out that is not a third button in the row: skipping is a
            // smaller decision than the other two and reads as one here.
            Text {
                text: "Skip for now and look around"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: skipMouse.containsMouse ? Colors.accent : Colors.textMuted
                font.underline: skipMouse.containsMouse

                MouseArea {
                    id: skipMouse
                    anchors.fill: parent
                    anchors.margins: -Metrics.xs
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.close()
                        root.skipRequested()
                    }
                }
            }

            Text {
                visible: root.importError.length > 0
                width: parent.width
                text: root.importError
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.urgent
            }
        }

        // The right half is what happens next: three steps, then the window. It
        // is a map of the product rather than decoration.
        Rectangle {
            id: nextPanel
            anchors.left: hero.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Metrics.panelPadding
            radius: Metrics.radiusMedium
            color: Colors.surfaceContainerLowest
            border.width: Metrics.borderWidth
            border.color: Colors.edge

            // Two columns, not one: the steps and the facts sit at the top and the
            // version line is pinned to the bottom edge, so adding a fact can never
            // push the footer out of the card.
            Column {
                id: topColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Metrics.panelPadding
                spacing: Metrics.md

                Text {
                    text: "WHAT HAPPENS NEXT"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Colors.textTertiary
                }

                Repeater {
                    model: [
                        { step: "1", title: "Pick your printer",
                          text: "459 machines in the engine's catalogue, with the bed and the extruders they really have." },
                        { step: "2", title: "Pick material and quality",
                          text: "The machine's own filaments and process profiles, so the temperatures and speeds are the vendor's." },
                        { step: "3", title: "Open a model and slice",
                          text: "Drag a model in, arrange it, slice, and read the toolpaths layer by layer." }
                    ]

                    delegate: Row {
                        id: stepRow

                        required property var modelData

                        width: parent.width
                        spacing: Metrics.md

                        Rectangle {
                            width: 26
                            height: 26
                            radius: 13
                            color: Colors.alpha(Colors.accent, 0.16)
                            border.width: Metrics.borderWidth
                            border.color: Colors.alpha(Colors.accent, 0.5)

                            Text {
                                anchors.centerIn: parent
                                text: stepRow.modelData.step
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                font.bold: true
                                color: Colors.accent
                            }
                        }

                        Column {
                            width: parent.width - 26 - Metrics.md
                            spacing: 1

                            Text {
                                text: stepRow.modelData.title
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                font.bold: true
                                color: Colors.textPrimary
                            }

                            Text {
                                width: parent.width
                                text: stepRow.modelData.text
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textTertiary
                            }
                        }
                    }
                }

                Item { width: 1; height: Metrics.sm }

                Rectangle {
                    width: parent.width
                    height: Metrics.hairline
                    color: Colors.edge
                }

                // What the app actually has in it, rather than more prose: the
                // machine list, the formats, and the import path for someone who
                // already has a slicer set up, which is most people arriving here.
                Text {
                    text: "WHAT IS IN IT"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Colors.textTertiary
                }

                Repeater {
                    model: [
                        { label: presetStore.printers.length + " printers",
                          text: "every one with its real bed, extruders and limits" },
                        { label: "STL · 3MF · STEP",
                          text: "plus OBJ in, G-code out, layer-by-layer preview" },
                        { label: "Orca, Prusa, Cura",
                          text: "import their printers, processes and filaments" }
                    ]

                    delegate: Row {
                        id: factRow

                        required property var modelData

                        width: parent.width
                        spacing: Metrics.md

                        Text {
                            width: 150
                            text: factRow.modelData.label
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            font.bold: true
                            color: Colors.textSecondary
                            elide: Text.ElideRight
                        }

                        Text {
                            width: factRow.width - 150 - Metrics.md
                            text: factRow.modelData.text
                            wrapMode: Text.WordWrap
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textTertiary
                        }
                    }
                }

            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Metrics.panelPadding
                spacing: Metrics.sm

                Rectangle {
                    width: parent.width
                    height: Metrics.hairline
                    color: Colors.edge
                }

                Text {
                    width: parent.width
                    text: "NextLayer " + root.version + "  ·  local, offline, no account"
                    wrapMode: Text.WordWrap
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
            }
        }
    }
}
