// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

// The window header: the product, the document, and the two things a header
// always owes the user -- a way to reach the file commands and a way to switch
// between model and preview. Everything else lives in the tool strip below it.
Item {
    id: root

    property string mode: "model"
    property string documentName: ""
    property string version: ""
    property bool busy: false

    signal modeRequested(string mode)
    signal fileMenuRequested(real x, real y)
    signal savePresetRequested()

    readonly property int groupGap: Metrics.lg
    readonly property int dividerGap: Metrics.lg
    readonly property int dividerHeight: Metrics.controlHeight - Metrics.sm

    Rectangle {
        anchors.fill: parent
        color: Colors.surface
    }

    // Product lockup: mark, wordmark, version. One unit, never split by a
    // divider, so it reads as the app's name rather than a toolbar item.
    Row {
        id: lockup
        anchors.left: parent.left
        anchors.leftMargin: Metrics.panelPadding
        anchors.verticalCenter: parent.verticalCenter
        spacing: Metrics.md

        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/next-layer/brand/next-layer-mark.svg"
            sourceSize: Qt.size(Type.iconStandalone * 2, Type.iconStandalone * 2)
            fillMode: Image.PreserveAspectFit
            width: Type.iconStandalone * 2
            height: Type.iconStandalone * 2
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "NextLayer"
            font.family: Type.family
            font.pixelSize: Type.sizeHeading
            font.bold: true
            font.letterSpacing: 0.2
            color: Colors.textPrimary
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: versionLabel.implicitWidth + Metrics.sm * 2
            height: versionLabel.implicitHeight + Metrics.xxs * 2
            radius: Metrics.radiusSmall
            color: Colors.alpha(Colors.accent, 0.14)
            border.width: Metrics.borderWidth
            border.color: Colors.alpha(Colors.accent, 0.55)

            Text {
                id: versionLabel
                anchors.centerIn: parent
                text: root.version
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.accent
            }
        }
    }

    Rectangle {
        id: lockupDivider
        anchors.left: lockup.right
        anchors.leftMargin: root.dividerGap
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.borderWidth
        height: root.dividerHeight
        color: Colors.outlineVariant
    }

    Row {
        id: fileGroup
        anchors.left: lockupDivider.right
        anchors.leftMargin: root.groupGap
        anchors.verticalCenter: parent.verticalCenter
        spacing: Metrics.md

        Rectangle {
            id: fileButton
            width: fileRow.implicitWidth + Metrics.md * 2
            height: Metrics.controlHeight
            radius: Metrics.radiusSmall
            color: fileMouse.containsMouse ? Colors.hoverFill : Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: fileMouse.containsMouse ? Colors.edgeStrong : Colors.outlineVariant

            Row {
                id: fileRow
                anchors.centerIn: parent
                spacing: Metrics.sm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\uf0c9"
                    font.family: Type.family
                    font.pixelSize: Type.iconLead
                    color: Colors.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "File"
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    color: Colors.textPrimary
                }
            }

            MouseArea {
                id: fileMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Open on release: opening on press lets the same press be read
                // as "pressed outside" and closes the menu in the same frame.
                onReleased: {
                    const point = fileButton.mapToItem(null, 0, fileButton.height + Metrics.xs)
                    root.fileMenuRequested(point.x, point.y)
                }
            }
        }
    }

    Rectangle {
        id: fileDivider
        anchors.left: fileGroup.right
        anchors.leftMargin: root.dividerGap
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.borderWidth
        height: root.dividerHeight
        color: Colors.outlineVariant
    }

    // The document: what is open, and whether the engine is busy with it.
    Text {
        id: documentLabel
        anchors.left: fileDivider.right
        anchors.leftMargin: root.groupGap
        anchors.right: modeSwitch.left
        anchors.rightMargin: Metrics.xl
        anchors.verticalCenter: parent.verticalCenter
        text: root.documentName.length > 0 ? root.documentName : "no model open"
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: root.documentName.length > 0 ? Colors.textSecondary : Colors.textMuted
        elide: Text.ElideMiddle
    }

    // Saving the current settings as a preset is the one preset action that does
    // not belong behind a gear: a person who just changed a value looks at the
    // header, and the same three buttons are where they were already looking.
    Button {
        id: savePreset
        anchors.right: modeSwitch.left
        anchors.rightMargin: Metrics.md
        anchors.verticalCenter: parent.verticalCenter
        text: "Save preset"
        hint: "Save the settings in the open tab as a preset"
        onClicked: root.savePresetRequested()
    }

    Rectangle {
        id: modeSwitch
        anchors.right: parent.right
        anchors.rightMargin: Metrics.panelPadding
        anchors.verticalCenter: parent.verticalCenter
        width: switchRow.implicitWidth + Metrics.xxs * 2
        height: Metrics.controlHeight
        radius: Metrics.radiusSmall
        color: Colors.surfaceContainer
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant

        Row {
            id: switchRow
            anchors.fill: parent
            anchors.margins: Metrics.xxs
            spacing: Metrics.xxs

            Button {
                height: parent.height
                text: "Home"
                primary: root.mode === "home"
                onClicked: root.modeRequested("home")
            }

            Button {
                height: parent.height
                text: "Model"
                primary: root.mode === "model"
                onClicked: root.modeRequested("model")
            }

            Button {
                height: parent.height
                text: "Preview"
                primary: root.mode === "preview"
                onClicked: root.modeRequested("preview")
            }

            Button {
                height: parent.height
                text: "Device"
                primary: root.mode === "device"
                onClicked: root.modeRequested("device")
            }
        }
    }

    Separator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
