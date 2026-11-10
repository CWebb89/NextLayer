// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The screen the app lands on: the machine, the three ways to start, and what was
// open last. Modelled on the reference slicers' home pages, minus their account
// column -- there is nothing to sign in to here, so the whole page is the artwork,
// the three actions and the recent files. Nothing on this page touches a network.
Item {
    id: root

    /// The illustration on the right. Swapping it for another SVG is this one line.
    property string heroSource: "qrc:/next-layer/brand/next-layer-hero.png"
    /// [{ path, name, when, kind }], newest first, written by the window whenever a
    /// project or a model is opened or saved.
    property var recent: []

    signal newProjectRequested()
    signal openProjectRequested()
    signal openModelRequested()
    signal recentRequested(string path)
    signal forgetRequested(string path)
    signal clearRecentRequested()

    readonly property int pageMargin: Metrics.huge * 2
    readonly property int columnWidth: Math.max(420, Math.min(600, width * 0.46))
    /// The artwork is drawn only when the asset carries its own transparency. The
    /// shipped PNG is a drawing on opaque black, and a black rectangle on a warm grey
    /// page is worse than no picture: point `heroSource` at an SVG in the brand
    /// directory and the page fills itself.
    readonly property bool artVisible: width > 980 && root.heroSource.slice(-4) === ".svg"

    Rectangle {
        anchors.fill: parent
        color: Colors.surface
    }

    // The artwork, off the right edge and behind everything: a backdrop rather than
    // a framed picture. The drawing is a raster on opaque black and this page is warm
    // grey, so the page colour is laid over its left edge in a gradient and the
    // picture fades in instead of starting at a border. An SVG on a transparent
    // ground does not need that, and putting one at `heroSource` leaves the fade
    // reading as a vignette rather than a patch.
    Image {
        id: hero

        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: -width * 0.06
        width: Math.min(root.width * 0.62, 900)
        height: Math.min(root.height * 0.86, width * 0.72)
        visible: root.artVisible
        source: root.heroSource
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
    }

    Rectangle {
        anchors.fill: hero
        visible: root.artVisible
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Colors.surface }
            GradientStop { position: 0.16; color: Colors.alpha(Colors.surface, 0.85) }
            GradientStop { position: 0.38; color: Colors.alpha(Colors.surface, 0.0) }
            GradientStop { position: 1.0; color: Colors.alpha(Colors.surface, 0.0) }
        }
    }

    Flickable {
        id: page

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.pageMargin
        anchors.rightMargin: root.pageMargin
        width: root.columnWidth
        contentWidth: width
        contentHeight: body.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body
            width: page.width
            topPadding: Math.max(Metrics.huge, (page.height - body.implicitHeight) * 0.16)
            bottomPadding: Metrics.huge
            spacing: Metrics.lg

            SectionHeader {
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

            Item {
                width: parent.width
                height: Metrics.huge

                SectionHeader {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Recently opened"
                }

                Button {
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
        }
    }

    function stamp(when) {
        const date = new Date(when)
        return isNaN(date.getTime()) ? "" : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm")
    }
}
