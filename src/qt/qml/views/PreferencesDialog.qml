// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "../theme"
import "../components"

// Preferences: the application's own settings, separate from the engine's
// presets. Everything here changes something real the moment it is written --
// the grid and bed plate in the viewport, the camera's field of view, the
// units in the transform panel, whether changing a setting re-slices, and how
// long a status message stays up.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property bool autoslice: false
    property bool showGrid: true
    property bool showBed: true
    property real fieldOfView: 45
    property int statusSeconds: 6
    property string units: "mm"
    readonly property string appVersion: typeof appVersionString !== "undefined"
                                         ? appVersionString : ""

    signal preferencesChanged()

    Settings {
        id: saved
        category: "preferences"

        property bool autoslice: false
        property bool showGrid: true
        property bool showBed: true
        property real fieldOfView: 45
        property int statusSeconds: 6
        property string units: "mm"
    }

    function load() {
        root.autoslice = saved.autoslice
        root.showGrid = saved.showGrid
        root.showBed = saved.showBed
        root.fieldOfView = saved.fieldOfView
        root.statusSeconds = saved.statusSeconds
        root.units = saved.units
        Units.system = root.units
    }

    function store() {
        saved.autoslice = root.autoslice
        saved.showGrid = root.showGrid
        saved.showBed = root.showBed
        saved.fieldOfView = root.fieldOfView
        saved.statusSeconds = root.statusSeconds
        saved.units = root.units
        Units.system = root.units
        root.preferencesChanged()
    }

    function openWithCurrent() {
        root.load()
        root.open()
    }

    Component.onCompleted: root.load()

    onClosed: root.store()

    width: Math.min(620, parent.width - Metrics.xl * 2)
    // Never taller than the window, and never shorter than the content wants: the
    // scroll area takes the first hit, so the Done button and the last line of
    // text stay inside the border on any screen.
    height: Math.min(parent.height - Metrics.xl * 2, contentItem.implicitHeight)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

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
                id: header
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                spacing: Metrics.sm

                Text {
                    text: "Preferences"
                    font.family: Type.family
                    font.pixelSize: Type.sizeTitle
                    font.bold: true
                    color: Colors.textPrimary
                    Layout.fillWidth: true
                }

                ToolButton {
                    icon: "close"
                    label: "Close"
                    hint: "Close preferences"
                    onClicked: root.close()
                }
            }

            Divider { Layout.fillWidth: true }

            Flickable {
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                Layout.fillHeight: true
                Layout.minimumHeight: Metrics.rowHeight * 3
                Layout.preferredHeight: form.implicitHeight
                contentHeight: form.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThemedScrollBar {}

                ColumnLayout {
                    id: form
                    width: parent.width
                    spacing: Metrics.md

                    // A layout inside a flickable does not shrink to fit: one long
                    // sentence without a wrap pushed the column wider than the
                    // dialog, so the toggles and the update button were cut off at
                    // the right edge.
                    Layout.preferredWidth: parent.width

                    Text {
                        text: "Viewport"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: true
                        color: Colors.textMuted
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.sm

                        Text {
                            text: "Show the build plate"
                            font.family: Type.family
                            font.pixelSize: Type.sizeBody
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        Toggle {
                            checked: root.showBed
                            onToggled: root.showBed = !root.showBed
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.sm

                        Text {
                            text: "Show the grid"
                            font.family: Type.family
                            font.pixelSize: Type.sizeBody
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        Toggle {
                            checked: root.showGrid
                            onToggled: root.showGrid = !root.showGrid
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.md

                        Text {
                            text: "Field of view"
                            font.family: Type.family
                            font.pixelSize: Type.sizeBody
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        Slider {
                            Layout.preferredWidth: 220
                            from: 20
                            to: 80
                            step: 1
                            value: root.fieldOfView
                            onMoved: (value) => root.fieldOfView = value
                        }

                        Text {
                            text: Math.round(root.fieldOfView) + "\u00b0"
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textSecondary
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    Divider { Layout.fillWidth: true }

                    Text {
                        text: "Behaviour"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: true
                        color: Colors.textMuted
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.sm

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                Layout.fillWidth: true
                                text: "Slice again when a setting changes"
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeBody
                                color: Colors.textPrimary
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Runs the slicer after every edit once a plate has been sliced once."
                                wrapMode: Text.WordWrap
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }
                        }

                        Toggle {
                            checked: root.autoslice
                            onToggled: root.autoslice = !root.autoslice
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.md

                        Text {
                            text: "Status message"
                            font.family: Type.family
                            font.pixelSize: Type.sizeBody
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        Slider {
                            Layout.preferredWidth: 200
                            from: 2
                            to: 20
                            step: 1
                            value: root.statusSeconds
                            onMoved: (value) => root.statusSeconds = Math.round(value)
                        }

                        Text {
                            text: root.statusSeconds + " s"
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textSecondary
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.md

                        Text {
                            text: "Units"
                            font.family: Type.family
                            font.pixelSize: Type.sizeBody
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        ComboBox {
                            Layout.preferredWidth: 160
                            choices: ["mm", "inch"]
                            currentValue: root.units
                            onSelected: (value) => root.units = value
                        }
                    }

                    // Updates: the app checks a static channel -- one JSON manifest naming one
                    // artifact -- and installs by rename, so a running binary is never written
                    // into. The button sits beside the heading because it is the only thing
                    // here a person is meant to press.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.sm

                        SectionHeader {
                            text: "Updates"
                            Layout.fillWidth: true
                        }

                        Button {
                            text: updater.busy ? "working…" : "Check for updates"
                            enabled: !updater.busy
                            onClicked: updater.check()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: updater.status.length > 0 ? updater.status
                                                        : "version " + root.appVersion + " is installed"
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textSecondary
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: "Channel  " + updater.channel + (updater.notes.length > 0
                                                                 ? "  --  " + updater.notes : "")
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.sm
                        visible: updater.availableVersion.length > 0

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "Download " + updater.availableVersion
                            visible: !updater.downloaded
                            enabled: !updater.busy
                            onClicked: updater.download()
                        }

                        Button {
                            text: "Install and restart"
                            primary: true
                            visible: updater.canInstall
                            enabled: !updater.busy
                            onClicked: {
                                updater.install()
                                updater.relaunch()
                            }
                        }
                    }
                }
            }

            Divider { Layout.fillWidth: true }

            RowLayout {
                id: footer
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                spacing: Metrics.sm

                Item { Layout.fillWidth: true }

                Button {
                    text: "Done"
                    primary: true
                    onClicked: root.close()
                }
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
}
