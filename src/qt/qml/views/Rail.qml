// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"
import "../components"

Item {
    id: root

    property string currentView: "model"
    property bool objectListVisible: false
    property int objectCount: 0

    signal viewRequested(string view)
    signal objectListRequested()
    signal resetViewRequested()
    signal setupRequested()

    readonly property var views: [
        { id: "model", glyph: "◆", label: "Model" },
        { id: "preview", glyph: "≡", label: "Preview" }
    ]

    Rectangle {
        anchors.fill: parent
        color: Colors.surfaceContainerLow
    }

    Column {
        anchors.top: parent.top
        anchors.topMargin: Metrics.lg
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Metrics.sm

        Repeater {
            model: root.views

            delegate: RailButton {
                required property var modelData
                glyph: modelData.glyph
                active: root.currentView === modelData.id
                onClicked: root.viewRequested(modelData.id)
            }
        }

        // The object list lives here with the view switches, because it is one:
        // where the objects are is a view of the plate, not a tool that acts on
        // it. The count rides under the glyph the way an unread badge would.
        Item {
            width: Metrics.railWidth
            height: Metrics.railWidth + Metrics.sm

            RailButton {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: "☰"
                active: root.objectListVisible
                onClicked: root.objectListRequested()
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                text: root.objectCount
                visible: root.objectCount > 0
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: root.objectListVisible ? Colors.accent : Colors.textMuted
            }
        }
    }

    RailButton {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.lg + Metrics.controlHeight + Metrics.sm
        anchors.horizontalCenter: parent.horizontalCenter
        glyph: "⚙"
        active: false
        onClicked: root.setupRequested()
    }

    RailButton {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.lg
        anchors.horizontalCenter: parent.horizontalCenter
        glyph: "⟲"
        active: false
        onClicked: root.resetViewRequested()
    }
}
