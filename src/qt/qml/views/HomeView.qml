// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The screen the app lands on: what this program is for, the three ways to start,
// what was open last, which machine will slice it, and the two sibling projects
// that make up the rest of the stack. Modelled on the reference slicers' home
// pages, minus their account column -- there is nothing to sign in to here.
// Nothing on this page touches a network.
Item {
    id: root

    /// The printer artwork. A transparent PNG, so the page colour shows through and
    /// the drawing reads as part of the page rather than as a picture in a frame.
    property string heroSource: "qrc:/next-layer/brand/next-layer-printer.png"
    property string version: ""
    property string engineVersion: ""
    /// [{ path, name, when, kind }], newest first, written by the window whenever a
    /// project or a model is opened or saved.
    property var recent: []

    signal newProjectRequested()
    signal openProjectRequested()
    signal openModelRequested()
    signal recentRequested(string path)
    signal forgetRequested(string path)
    signal clearRecentRequested()

    readonly property int pageMargin: width > 1400 ? Metrics.huge * 3 : Metrics.huge * 2
    readonly property int columnWidth: Math.max(440, Math.min(720, width * 0.44))
    readonly property bool artVisible: width > 1080 && root.heroSource !== ""

    /// The three promises the whole application keeps. They are the answer to "why
    /// another slicer", so they sit under the sentence rather than in a settings
    /// page nobody opens.
    readonly property var promises: ["No account", "No telemetry", "G-code on your disk"]

    readonly property string printerName: presetStore.currentPrinter
    readonly property string processName: presetStore.currentPrint
    readonly property string filamentName: presetStore.currentFilament

    Rectangle {
        anchors.fill: parent
        color: Colors.surface
    }

    Image {
        id: hero

        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: Metrics.xl
        width: Math.min(root.width * 0.56, 860)
        height: Math.min(root.height * 0.84, width * 0.72)
        visible: root.artVisible
        source: root.artVisible ? root.heroSource : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        mipmap: true
    }

    Flickable {
        id: page

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.pageMargin
        width: root.columnWidth
        contentWidth: width
        contentHeight: body.height + body.topPadding + body.bottomPadding
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body

            width: page.width
            topPadding: Math.max(Metrics.huge * 2,
                                 Math.min(Metrics.huge * 3, (page.height - body.implicitHeight) * 0.3))
            bottomPadding: Metrics.huge * 2
            spacing: Metrics.lg

            // --- What this is -------------------------------------------------

            Text {
                width: parent.width
                text: "LOCAL SLICING, END TO END"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.letterSpacing: 2.4
                color: Colors.accent
            }

            Text {
                width: parent.width
                text: "Slice on your own machine."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Math.min(Type.sizeXLarge, Math.max(Type.sizeLarge,
                                                                  body.width * 0.062))
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                width: parent.width
                text: "A slicer of our own: this interface, an open slicing engine, and every "
                      + "toolpath calculated here. Your models, your settings and your G-code "
                      + "stay on this disk, in formats another slicer can read."
                wrapMode: Text.WordWrap
                lineHeight: 1.28
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textSecondary
            }

            Row {
                width: parent.width
                spacing: Metrics.sm

                Repeater {
                    model: root.promises

                    delegate: Rectangle {
                        id: promise

                        required property string modelData

                        width: promiseText.implicitWidth + Metrics.lg * 2
                        height: promiseText.implicitHeight + Metrics.sm
                        radius: height / 2
                        color: Colors.alpha(Colors.accent, 0.10)
                        border.width: Metrics.borderWidth
                        border.color: Colors.alpha(Colors.accent, 0.28)

                        Text {
                            id: promiseText
                            anchors.centerIn: parent
                            text: promise.modelData
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.accent
                        }
                    }
                }
            }

            Item { width: 1; height: Metrics.sm }

            // --- Start --------------------------------------------------------

            SectionTitle {
                width: parent.width
                text: "Start"
            }

            Column {
                width: parent.width
                spacing: Metrics.sm

                ActionCard {
                    width: parent.width
                    icon: "add_box"
                    title: "New project"
                    subtitle: "Start with an empty plate"
                    hint: "New plate (Ctrl+N)"
                    accent: true
                    onClicked: root.newProjectRequested()
                }

                ActionCard {
                    width: parent.width
                    icon: "folder_open"
                    title: "Open project"
                    subtitle: "Resume a saved .nextproj"
                    hint: "Open a project (Ctrl+Shift+O)"
                    onClicked: root.openProjectRequested()
                }

                ActionCard {
                    width: parent.width
                    icon: "deployed_code"
                    title: "Open model"
                    subtitle: "Add an STL, 3MF, OBJ or STEP to the plate"
                    hint: "Add a model (Ctrl+O)"
                    onClicked: root.openModelRequested()
                }
            }

            Item { width: 1; height: Metrics.md }

            // --- Recent -------------------------------------------------------

            Item {
                width: parent.width
                height: Metrics.controlHeight

                SectionTitle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - clearButton.width - Metrics.lg
                    text: "Recently opened"
                }

                Button {
                    id: clearButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Clear all"
                    hint: "Forget the recently opened list"
                    enabled: root.recent.length > 0
                    onClicked: root.clearRecentRequested()
                }
            }

            Rectangle {
                width: parent.width
                height: emptyColumn.height + Metrics.xxl * 2
                visible: root.recent.length === 0
                radius: Metrics.radiusMedium
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: Colors.edge

                Column {
                    id: emptyColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Metrics.xxl
                    spacing: Metrics.xs

                    Text {
                        width: parent.width
                        text: "Nothing opened yet"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Type.family
                        font.pixelSize: Type.sizeBody
                        font.bold: true
                        color: Colors.textSecondary
                    }

                    Text {
                        width: parent.width
                        text: "A project is one file on your disk. Nothing here is uploaded, "
                              + "and there is nothing to sign in to."
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textMuted
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Metrics.xxs
                visible: root.recent.length > 0

                Repeater {
                    model: root.recent

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        width: body.width
                        height: Metrics.rowHeight + Metrics.sm

                        readonly property bool project: modelData.kind === "project"

                        radius: Metrics.radiusSmall
                        color: rowMouse.containsMouse
                             ? Colors.surfaceContainerHigh : "transparent"
                        Behavior on color { ColorAnimation { duration: 90 } }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: Metrics.sm
                            anchors.right: forgetButton.left
                            anchors.rightMargin: Metrics.sm
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Metrics.md

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: row.project ? "folder_special" : "deployed_code"
                                size: Type.iconLead
                                color: rowMouse.containsMouse ? Colors.accent : Colors.textTertiary
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Type.iconLead - Metrics.md - stampItem.width
                                       - Metrics.md
                                spacing: Metrics.xxs

                                Text {
                                    width: parent.width
                                    text: row.modelData.name
                                    elide: Text.ElideMiddle
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeBody
                                    color: rowMouse.containsMouse ? Colors.textPrimary
                                                                 : Colors.textSecondary
                                }

                                Text {
                                    width: parent.width
                                    text: row.project ? "project" : "model  ·  " + row.modelData.path
                                    elide: Text.ElideMiddle
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeSmall
                                    color: Colors.textMuted
                                }
                            }

                            Text {
                                id: stampItem
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.stamp(row.modelData.when)
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: Colors.textMuted
                            }
                        }

                        ToolButton {
                            id: forgetButton
                            anchors.right: parent.right
                            anchors.rightMargin: Metrics.xs
                            anchors.verticalCenter: parent.verticalCenter
                            width: Metrics.controlHeight
                            height: Metrics.controlHeight
                            icon: "close"
                            hint: "Forget this file"
                            visible: rowMouse.containsMouse
                            onClicked: root.forgetRequested(row.modelData.path)
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            anchors.rightMargin: Metrics.controlHeight + Metrics.xs
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.recentRequested(row.modelData.path)
                        }

                        Tooltip {
                            target: row
                            text: row.modelData.path
                            shown: rowMouse.containsMouse
                        }
                    }
                }
            }

            Item { width: 1; height: Metrics.md }

            // --- This machine -------------------------------------------------

            SectionTitle {
                width: parent.width
                text: "This machine"
            }

            Rectangle {
                width: parent.width
                height: machineColumn.implicitHeight + Metrics.lg * 2
                radius: Metrics.radiusMedium
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: Colors.edge

                Column {
                    id: machineColumn

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Metrics.lg
                    anchors.rightMargin: Metrics.lg
                    spacing: Metrics.sm

                    Repeater {
                        model: [
                            { role: "Printer", value: root.printerName },
                            { role: "Process", value: root.processName },
                            { role: "Filament", value: root.filamentName }
                        ]

                        delegate: Row {
                            id: machineRow

                            required property var modelData

                            width: machineColumn.width
                            spacing: Metrics.md

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(90, machineColumn.width * 0.22)
                                text: machineRow.modelData.role
                                elide: Text.ElideRight
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                font.letterSpacing: 1.2
                                color: Colors.textMuted
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: machineRow.width - parent.spacing * 2
                                       - Math.max(90, machineColumn.width * 0.22)
                                text: machineRow.modelData.value.length > 0
                                      ? machineRow.modelData.value
                                      : "not resolved yet"
                                elide: Text.ElideRight
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: machineRow.modelData.value.length > 0
                                       ? Colors.textPrimary : Colors.textMuted
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Metrics.borderWidth
                        color: Colors.separator
                    }

                    Text {
                        width: parent.width
                        text: presetStore.printers.length + " printers · "
                              + presetStore.printProfiles.length + " processes · "
                              + presetStore.filamentProfiles.length + " filaments available, "
                              + "from the engine's bundle and your own presets"
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                    }
                }
            }

            Item { width: 1; height: Metrics.md }

            // --- The stack ----------------------------------------------------

            SectionTitle {
                width: parent.width
                text: "The rest of the stack"
            }

            Text {
                width: parent.width
                text: "next-layer plans the paths. The two projects below run the machine and "
                      + "watch it, and the three share one data file format: the G-code."
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }

            ProductCard {
                width: parent.width
                icon: "dashboard"
                name: "klipshell"
                tagline: "A desktop control surface for Klipper printers: dashboard, console, "
                         + "print files, history, bed mesh, G-code viewer and machine health, "
                         + "in one keyboard-driven window over Moonraker."
                tags: ["Quickshell · QML", "Klipper / Moonraker", "8 views"]
            }

            ProductCard {
                width: parent.width
                icon: "monitor_heart"
                name: "LayerMind"
                tagline: "Sits above Klipper and Moonraker and turns every print into a data "
                         + "point: hardware health over time, failure patterns, and diagnostics "
                         + "that end in a recommendation rather than a chart."
                tags: ["Rust", "252 tests", "AI diagnostics"]
            }

            Item { width: 1; height: Metrics.lg }

            Rectangle {
                width: parent.width
                height: Metrics.borderWidth
                color: Colors.separator
            }

            Row {
                width: parent.width
                spacing: Metrics.md

                Text {
                    text: root.version.length > 0 ? "version " + root.version : ""
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                Text {
                    text: "·"
                    visible: footerText.text.length > 0
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                Text {
                    id: footerText
                    width: parent.width - parent.spacing * 2
                           - (root.version.length > 0 ? root.version.length * Type.sizeCaption * 0.6 + 40 : 0)
                    text: "AGPL-3.0 · slicing engine " + engineLabel()
                    elide: Text.ElideRight
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }
            }
        }
    }

    function engineLabel() {
        return root.engineVersion.length > 0 ? root.engineVersion : "bundled"
    }

    function stamp(when) {
        const date = new Date(when)
        return isNaN(date.getTime()) ? "" : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm")
    }

    // A section heading: the words, then the rule that carries them across the page.
    component SectionTitle: Item {
        id: title

        property string text: ""

        implicitHeight: titleText.implicitHeight

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: title.text
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            font.letterSpacing: 1.8
            font.bold: true
            color: Colors.textSecondary
        }

        Rectangle {
            anchors.left: titleText.right
            anchors.leftMargin: Metrics.lg
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Metrics.borderWidth
            color: Colors.separator
        }
    }
}
