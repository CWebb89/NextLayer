// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

// Dropdown over a value list. `choices` accepts either a list of scalars or a
// list of {value, label} objects (SettingsModel's enum shape). The control is
// caller-driven: it shows `currentValue` and emits `selected(value)`; it does
// not assign currentValue itself, so a binding to the settings model survives.
// The popup closes on Escape, on a click anywhere outside it, and on a pick.
Item {
    id: root

    property var choices: []
    property var currentValue: undefined
    property bool searchable: false
    property int popupWidth: 0
    /// Preset names put the distinguishing word last ("Generic PETG",
    /// "0.20mm SPEED @MK4 0.4"), so a preset picker keeps the tail; an enum value
    /// is a phrase, so it keeps the head.
    property int elideMode: Text.ElideLeft
    signal selected(var value)

    readonly property int selectedIndex: root.indexOfValue(root.currentValue)
    readonly property string currentLabel: root.selectedIndex >= 0
        ? root.labelAt(root.selectedIndex) : ""

    implicitWidth: 200
    implicitHeight: Metrics.controlHeight
    opacity: enabled ? 1.0 : 0.45
    activeFocusOnTab: true

    function entryCount() {
        return root.choices ? root.choices.length : 0
    }

    function valueAt(i) {
        var entry = root.choices[i]
        return (entry !== null && typeof entry === "object") ? entry.value : entry
    }

    function labelAt(i) {
        var entry = root.choices[i]
        if (entry !== null && typeof entry === "object")
            return entry.label !== undefined ? String(entry.label) : String(entry.value)
        return String(entry)
    }

    function indexOfValue(v) {
        for (var i = 0; i < root.entryCount(); ++i)
            if (root.valueAt(i) === v)
                return i
        return -1
    }

    function choose(i) {
        if (i < 0 || i >= root.entryCount())
            return
        var v = root.valueAt(i)
        popup.close()
        root.selected(v)
    }

    function step(direction) {
        var count = root.entryCount()
        if (count === 0)
            return
        var base = root.selectedIndex >= 0 ? root.selectedIndex : 0
        root.choose(Math.max(0, Math.min(count - 1, base + direction)))
    }

    property var visibleIndices: []

    function refilter() {
        var needle = search.text.trim().toLowerCase()
        var kept = []
        for (var i = 0; i < root.entryCount(); ++i) {
            if (needle === "" || root.labelAt(i).toLowerCase().indexOf(needle) >= 0)
                kept.push(i)
        }
        root.visibleIndices = kept
    }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: area.pressed ? Colors.pressedFill
             : area.containsMouse ? Colors.hoverFill
             : Colors.surfaceContainerHigh
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusSmall
        color: "transparent"
        border.width: Metrics.borderWidth
        border.color: root.activeFocus || popup.opened ? Colors.edgeStrong : Colors.edge
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    Text {
        id: label
        anchors.left: parent.left
        anchors.leftMargin: Metrics.md
        anchors.right: chevron.left
        anchors.rightMargin: Metrics.sm
        anchors.verticalCenter: parent.verticalCenter
        text: root.currentLabel
        textFormat: Text.PlainText
        elide: root.elideMode
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: root.currentLabel === "" ? Colors.textMuted : Colors.textPrimary
    }

    // A truncated value is still readable in full on hover, which is the whole
    // point of truncating it rather than shrinking the font.
    Controls.ToolTip.visible: area.containsMouse && label.truncated
    Controls.ToolTip.delay: 400
    Controls.ToolTip.text: root.currentLabel

    Text {
        id: chevron
        anchors.right: parent.right
        anchors.rightMargin: Metrics.md
        anchors.verticalCenter: parent.verticalCenter
        text: "\u25BE"
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        color: area.containsMouse ? Colors.accent : Colors.textTertiary
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (popup.opened)
                popup.close()
            else
                popup.open()
        }
    }

    Keys.onSpacePressed: popup.opened ? popup.close() : popup.open()
    Keys.onReturnPressed: popup.opened ? popup.close() : popup.open()
    Keys.onUpPressed: root.step(-1)
    Keys.onDownPressed: root.step(1)

    Controls.Popup {
        id: popup
        parent: root
        x: 0
        y: root.height + Metrics.xxs
        width: root.popupWidth > 0 ? Math.max(root.width, root.popupWidth) : root.width
        padding: Metrics.xs
        modal: false
        dim: false
        focus: true
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside

        onOpened: {
            search.text = ""
            root.refilter()
            var row = Math.max(0, root.visibleIndices.indexOf(root.selectedIndex))
            list.currentIndex = row
            list.positionViewAtIndex(row, ListView.Contain)
            if (root.searchable)
                search.forceActiveFocus()
            else
                list.forceActiveFocus()
        }
        onClosed: root.forceActiveFocus()

        background: Rectangle {
            radius: Metrics.radiusMedium
            color: Colors.surfaceContainerHigh
            border.width: Metrics.cardBorderWidth
            border.color: Colors.edgeStrong
        }

        contentItem: Column {
            id: content
            spacing: Metrics.xs

            Controls.TextField {
                id: search
                visible: root.searchable
                width: content.width
                height: visible ? Metrics.controlHeight : 0
                placeholderText: "Search"
                onTextChanged: root.refilter()
                onAccepted: {
                    if (list.count > 0)
                        root.choose(root.visibleIndices[list.currentIndex])
                }
                Keys.onDownPressed: list.forceActiveFocus()
                Keys.onEscapePressed: popup.close()
            }

            ListView {
                id: list
                width: content.width
                implicitHeight: Math.min(count, 8) * Metrics.rowHeight
                height: implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.visibleIndices

                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onReturnPressed: root.choose(root.visibleIndices[list.currentIndex])
                Keys.onEnterPressed: root.choose(root.visibleIndices[list.currentIndex])
                Keys.onEscapePressed: popup.close()

                delegate: ListRow {
                    required property int modelData
                    width: list.width
                    label: root.labelAt(modelData)
                    selected: root.valueAt(modelData) === root.currentValue
                    onClicked: root.choose(modelData)
                }
            }
        }
    }
}
