// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import "../theme"

// A small pop-up menu drawn over the whole window: a list of labelled entries
// with an optional hint, group separators, and one click anywhere else to
// dismiss it. It is an overlay rather than a Popup because a Popup opened from a
// toolbar button gets closed again by the same press that opened it, and this has
// to work every time.
Item {
    id: root

    property var entries: []
    property real anchorX: 0
    property real anchorY: 0
    property int menuWidth: 320

    signal triggered(string id)
    signal dismissed()

    function entryCount() {
        let visible = 0
        for (let i = 0; i < root.entries.length; ++i) {
            const entry = root.entries[i]
            if (!entry || entry.disabled === true)
                continue
            ++visible
        }
        return visible
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.dismissed()
    }

    Rectangle {
        id: panel
        x: root.anchorX
        y: root.anchorY
        width: root.menuWidth
        height: column.implicitHeight + Metrics.sm * 2
        radius: Metrics.radiusMedium
        color: Colors.surfaceContainerLow
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant

        MouseArea {
            anchors.fill: parent
        }

        Column {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Metrics.sm
            spacing: 0

            Repeater {
                model: root.entries

                delegate: Item {
                    id: row

                    required property var modelData

                    readonly property bool separator: (row.modelData.id || "").indexOf("sep") === 0
                    readonly property bool disabled: row.modelData.disabled === true

                    width: root.menuWidth
                    height: row.separator ? Metrics.md : 32

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: Metrics.radiusSmall
                        color: rowMouse.containsMouse && !row.separator && !row.disabled
                               ? Colors.alpha(Colors.accent, 0.16) : "transparent"
                    }

                    Rectangle {
                        visible: row.separator
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Metrics.md
                        anchors.rightMargin: Metrics.md
                        height: Metrics.borderWidth
                        color: Colors.outlineVariant
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Metrics.md
                        anchors.rightMargin: Metrics.md
                        spacing: Metrics.sm
                        visible: !row.separator

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - hintLabel.width - Metrics.sm
                            text: row.modelData.label || ""
                            elide: Text.ElideRight
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            color: row.disabled ? Colors.textMuted : Colors.textPrimary
                        }

                        Text {
                            id: hintLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.hint || ""
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignRight
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textMuted
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !row.separator && !row.disabled
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.dismissed()
                            root.triggered(row.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
