// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The screen the app lands on: the product, the three ways to start, and what was
// open last. Modelled on the reference slicers' home pages, minus their account
// column -- there is nothing to sign in to here, so the space carries the artwork
// and the brand instead. Nothing on this page touches a network.
Item {
    id: root

    property string version: ""
    /// [{ path, name, when, kind }], newest first, written by the window whenever a
    /// project or a model is opened or saved.
    property var recent: []

    signal newProjectRequested()
    signal openProjectRequested()
    signal openModelRequested()
    signal recentRequested(string path)
    signal clearRecentRequested()

    readonly property int columnGap: Metrics.huge * 2

    Rectangle {
        anchors.fill: parent
        color: Colors.surface
    }

    Row {
        id: columns
        anchors.fill: parent
        anchors.margins: Metrics.panelMargin + Metrics.xxl
        spacing: root.columnGap

        // What this is, in one column: the machine, the mark, and the name. The
        // artwork is red and black on black while the app is warm grey and peach, so
        // it sits in a card of its own rather than floating as a rectangle.
        Column {
            id: identity
            width: Math.min(420, columns.width * 0.34)
            spacing: Metrics.xl

            Rectangle {
                width: parent.width
                height: Math.round(width * 0.667)
                radius: Metrics.radiusMedium
                color: "#0a0a0a"
                border.width: Metrics.borderWidth
                border.color: Colors.edge
                clip: true

                Image {
                    anchors.fill: parent
                    source: "qrc:/next-layer/brand/next-layer-hero.png"
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                }
            }

            Row {
                spacing: Metrics.lg

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/next-layer/brand/next-layer-mark.svg"
                    sourceSize.width: 44
                    sourceSize.height: 44
                    width: 44
                    height: 44
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Metrics.xxs

                    Text {
                        text: "NextLayer"
                        font.family: Type.family
                        font.pixelSize: 27
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        text: root.version.length > 0
                              ? "version " + root.version + " — local, no account, no cloud"
                              : "local, no account, no cloud"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Metrics.md

                Button {
                    width: parent.width
                    primary: true
                    text: "New Project"
                    hint: "Start with an empty plate (Ctrl+N)"
                    onClicked: root.newProjectRequested()
                }

                Button {
                    width: parent.width
                    text: "Open Project…"
                    hint: "Open a .nextproj project (Ctrl+Shift+O)"
                    onClicked: root.openProjectRequested()
                }

                Button {
                    width: parent.width
                    text: "Open Model…"
                    hint: "Add an STL, 3MF, OBJ or STEP to the plate (Ctrl+O)"
                    onClicked: root.openModelRequested()
                }
            }

            Text {
                width: parent.width
                text: "A project is one file on your disk. Nothing here is uploaded, "
                      + "and there is nothing to sign in to."
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textMuted
            }
        }

        // What was open last, newest first, with the one button a stuck list needs.
        Column {
            id: recentColumn
            width: columns.width - identity.width - root.columnGap
            spacing: Metrics.lg

            Item {
                width: parent.width
                height: Math.max(Metrics.rowHeight, clearAll.height)

                SectionHeader {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Recently opened"
                }

                Button {
                    id: clearAll
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Clear all"
                    hint: "Forget the recently opened list"
                    enabled: root.recent.length > 0
                    onClicked: root.clearRecentRequested()
                }
            }

            Text {
                visible: root.recent.length === 0
                text: "Nothing opened yet. Start a new project, or open a project or a model."
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                color: Colors.textMuted
            }

            Flow {
                width: parent.width
                spacing: Metrics.lg

                Repeater {
                    model: root.recent

                    delegate: Item {
                        id: card
                        required property var modelData
                        width: 232
                        height: 148

                        Rectangle {
                            anchors.fill: parent
                            radius: Metrics.radiusMedium
                            color: cardMouse.containsMouse
                                 ? Colors.surfaceContainerHigh : Colors.surfaceContainer
                            border.width: Metrics.borderWidth
                            border.color: cardMouse.containsMouse ? Colors.accent : Colors.edge
                            Behavior on color { ColorAnimation { duration: 100 } }

                            Icon {
                                id: cardIcon
                                anchors.top: parent.top
                                anchors.topMargin: Metrics.lg
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: card.modelData.kind === "project" ? "folder_special"
                                                                         : "deployed_code"
                                size: 34
                                color: cardMouse.containsMouse ? Colors.accent : Colors.textTertiary
                            }

                            Text {
                                id: nameText
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: cardIcon.bottom
                                anchors.topMargin: Metrics.md
                                anchors.leftMargin: Metrics.md
                                anchors.rightMargin: Metrics.md
                                text: card.modelData.name
                                elide: Text.ElideMiddle
                                horizontalAlignment: Text.AlignHCenter
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                font.bold: true
                                color: Colors.textPrimary
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: nameText.bottom
                                anchors.topMargin: Metrics.xxs
                                anchors.leftMargin: Metrics.md
                                anchors.rightMargin: Metrics.md
                                text: root.stamp(card.modelData.when)
                                horizontalAlignment: Text.AlignHCenter
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: Colors.textMuted
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.recentRequested(card.modelData.path)
                        }

                        Tooltip {
                            target: card
                            text: card.modelData.path
                            shown: cardMouse.containsMouse
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
