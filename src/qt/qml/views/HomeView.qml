// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The screen the app lands on: what this program is for, the three ways to start,
// what was open last, and which machine will slice it -- with the artwork, the
// pitch and the two sibling projects framed together in the aside on the right.
// Modelled on the reference slicers' home pages, minus their account column --
// there is nothing to sign in to here. Nothing on this page touches a network.
Item {
    id: root

    /// The printer artwork. A transparent PNG, so the panel colour shows through and
    /// the drawing reads as part of the card rather than as a picture in a frame.
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
    readonly property int gutter: Metrics.huge * 2
    readonly property int columnWidth: root.wide
                                        ? Math.max(400, Math.min(560, width * 0.36))
                                        : Math.min(width - root.pageMargin * 2, 720)
    /// Short windows give the aside less room than its content and its artwork want
    /// together, so the frame and the gutters tighten rather than the card scrolling.
    readonly property bool compactAside: height < 900
    readonly property int asidePad: root.compactAside ? Metrics.huge * 1.25 : Metrics.huge * 2

    /// Two columns need room for a readable pitch beside a plate of actions; below
    /// that the pitch and the stack rejoin the column and the artwork steps aside.
    readonly property bool wide: width >= 1240 && root.heroSource !== ""

    readonly property string pitchEyebrow: "LOCAL SLICING, END TO END"
    readonly property string pitchHeadline: "Slice on your own machine."
    readonly property string pitchBody: "A slicer of our own: this interface, an open slicing "
                                        + "engine, and every toolpath calculated here. Your models, "
                                        + "your settings and your G-code stay on this disk, in "
                                        + "formats another slicer can read."
    readonly property string stackBlurb: "next-layer plans the paths. The two projects below run "
                                         + "the machine and watch it, and the three share one data "
                                         + "file format: the G-code."

    /// The three promises the whole application keeps. They are the answer to "why
    /// another slicer", so they sit under the sentence rather than in a settings
    /// page nobody opens.
    readonly property var promises: ["No account", "No telemetry", "G-code on your disk"]

    readonly property string printerName: presetStore.currentPrinter
    readonly property string processName: presetStore.currentPrint
    readonly property string filamentName: presetStore.currentFilament

    /// The home page is a landing screen, not a file manager: six entries is what
    /// fits beside the aside, and the rest are one `Ctrl+Shift+O` away.
    readonly property var recentShown: recent.slice(0, root.compactAside ? 4 : 6)

    Rectangle {
        anchors.fill: parent
        color: Colors.surface
    }

    // The plate of actions, and the machine they will run on.
    Flickable {
        id: page

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.wide
                            ? root.pageMargin
                            : Math.max(root.pageMargin,
                                       (root.width - root.columnWidth) / 2)
        width: root.columnWidth
        contentWidth: width
        contentHeight: body.height + body.topPadding + body.bottomPadding
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body

            width: page.width
            topPadding: Math.max(Metrics.xl,
                                 Math.min(Metrics.huge * 3, (page.height - body.implicitHeight) * 0.4))
            bottomPadding: Metrics.huge * 2
            spacing: Metrics.lg

            HomeIntro {
                width: parent.width
                visible: !root.wide
                eyebrow: root.pitchEyebrow
                headline: root.pitchHeadline
                body: root.pitchBody
                chips: root.promises
            }

            Item {
                width: parent.width
                height: root.wide ? 0 : Metrics.md
                visible: !root.wide
            }

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
                    model: root.recentShown

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

            Item {
                width: parent.width
                height: root.wide ? 0 : Metrics.md
                visible: !root.wide
            }

            HomeStack {
                width: parent.width
                visible: !root.wide
                blurb: root.stackBlurb
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

    // The artwork, the pitch and the stack, framed as one card. The card is the same
    // height as the window so the page reads as two balanced columns rather than a
    // column and a floating picture.
    Rectangle {
        id: aside

        visible: root.wide
        anchors.left: page.right
        anchors.leftMargin: root.gutter
        anchors.right: parent.right
        anchors.rightMargin: root.pageMargin
        anchors.top: parent.top
        anchors.topMargin: Metrics.xl
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.xl

        radius: Metrics.radiusLarge
        color: Colors.surfaceContainerLow
        border.width: Metrics.cardBorderWidth
        border.color: Colors.edge
        clip: true

        Flickable {
            id: asideScroll

            anchors.fill: parent
            contentWidth: width
            contentHeight: asideBody.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: asideBody

                width: asideScroll.width

                Item {
                    id: artFrame

                    width: parent.width
                    // Whatever the pitch and the stack do not need, so the card never
                    // scrolls at a size the window actually opens at.
                    height: Math.max(root.compactAside ? Metrics.huge * 8 : Metrics.huge * 13,
                                     Math.min(parent.width * 0.42,
                                              aside.height - asidePanel.height))

                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Colors.alpha(Colors.accent, 0.13) }
                            GradientStop { position: 0.75; color: Colors.alpha(Colors.accent, 0.02) }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }

                    Image {
                        anchors.fill: parent
                        anchors.margins: root.compactAside ? Metrics.lg : Metrics.huge
                        anchors.bottomMargin: Metrics.sm
                        source: root.heroSource
                        fillMode: Image.PreserveAspectFit
                        verticalAlignment: Image.AlignBottom
                        asynchronous: true
                        smooth: true
                        mipmap: true
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.height * 0.22
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: Colors.surfaceContainerLow }
                        }
                    }
                }

                Item {
                    id: asidePanel

                    width: asideBody.width
                    height: asideColumn.implicitHeight + root.asidePad * 2

                    Column {
                        id: asideColumn

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: root.asidePad
                        anchors.rightMargin: root.asidePad
                        anchors.topMargin: Metrics.md
                        spacing: root.compactAside ? Metrics.xl : Metrics.huge

                        HomeIntro {
                            width: parent.width
                            eyebrow: root.pitchEyebrow
                            headline: root.pitchHeadline
                            body: root.pitchBody
                            chips: root.promises
                        }

                        Rectangle {
                            width: parent.width
                            height: Metrics.borderWidth
                            color: Colors.separator
                        }

                        HomeStack {
                            width: parent.width
                            blurb: root.stackBlurb
                        }
                    }
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
}
