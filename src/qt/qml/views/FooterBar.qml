// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Layouts
import NextLayer 1.0
import "../theme"
import "../components"

Item {
    id: root

    property string lastError: ""
    property bool haveResult: false

    readonly property bool slicing: sliceController.busy
    readonly property bool hasLayers: toolpath.layerCount > 0

    readonly property string statusText: {
        if (root.slicing)
            return "Slicing..."
        if (root.lastError.length > 0)
            return root.lastError
        if (session.error.length > 0)
            return session.error
        if (!root.haveResult)
            return ""

        // What the plate costs, in the order a person asks it: how long, how
        // much plastic, and then the numbers that are only interesting when
        // something looks wrong.
        const parts = []
        if (sliceController.estimatedTime.length > 0)
            parts.push(sliceController.estimatedTime)
        if (sliceController.filamentGrams > 0)
            parts.push(sliceController.filamentGrams.toFixed(1) + " g")
        parts.push(sliceController.layerCount + " layers")
        parts.push(sliceController.moveCount + " moves")
        parts.push(Math.round(sliceController.elapsedMs) + " ms")
        return parts.join("  ·  ")
    }

    readonly property color statusColor: root.lastError.length > 0 || session.error.length > 0
                                         ? Colors.error : Colors.textSecondary

    signal sliceRequested()
    signal previewReady()

    function startSlice() {
        if (root.slicing)
            return
        root.lastError = ""
        root.haveResult = false
        root.sliceRequested()
        sliceController.slice()
    }

    Connections {
        target: sliceController

        function onFinished() {
            root.haveResult = true
            if (toolpath.load(sliceController.gcodePath) && toolpath.layerCount > 0) {
                toolpath.setLayer(toolpath.layerCount - 1)
                root.previewReady()
            }
        }

        function onFailed(message) {
            root.haveResult = false
            root.lastError = String(message)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Colors.surface
    }

    Separator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Metrics.panelPadding
        anchors.rightMargin: Metrics.panelPadding
        anchors.topMargin: Metrics.md
        anchors.bottomMargin: Metrics.md
        spacing: Metrics.lg

        // The footer carries status only. The layer slider lives on the
        // viewport's right edge and its view mode sits on top of that slider,
        // so nothing here duplicates the preview controls.
        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.statusText
            elide: Text.ElideRight
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            font.bold: true
            color: root.statusColor
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 92
            horizontalAlignment: Text.AlignRight
            visible: root.hasLayers
            text: toolpath.layerZ.toFixed(2) + " mm Z"
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            font.bold: true
            color: Colors.textPrimary
        }
    }
}
