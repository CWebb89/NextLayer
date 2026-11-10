// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import "../theme"
import "."

// The categories of a settings tab, on one line, with the accent underline the
// reference slicers use. Fourteen categories do not fit in a 490 px panel, so the
// strip scrolls: two chevrons appear when it does, and the one you are in is
// scrolled into view when it changes from somewhere else.
Item {
    id: root

    /// [{ key, title, modified }]
    property var categories: []
    property string current: ""
    property int rowHeight: Metrics.rowHeight

    signal activated(string key)

    // Compared against the strip's own width, not the flickable's: the
    // flickable gives up an end to the chevrons when it scrolls, so reading its
    // width here made the flag depend on itself and the panel logged a binding
    // loop on every load.
    readonly property bool scrollable: row.width > root.width + 1

    implicitHeight: root.rowHeight

    function ensureVisible() {
        const item = root.delegateAt(root.currentIndex())
        if (item === null)
            return
        if (item.x < strip.contentX)
            strip.contentX = item.x
        else if (item.x + item.width > strip.contentX + strip.width)
            strip.contentX = item.x + item.width - strip.width
    }

    /// The Repeater's own item sits in the same children list as the tabs it
    /// creates, so the delegates are counted around it rather than indexed into.
    function delegateAt(index) {
        if (index < 0)
            return null
        let seen = 0
        for (let i = 0; i < row.children.length; ++i) {
            const child = row.children[i]
            if (child === tabRepeater)
                continue
            if (seen === index)
                return child
            ++seen
        }
        return null
    }

    function currentIndex() {
        for (let i = 0; i < root.categories.length; ++i)
            if (root.categories[i].key === root.current)
                return i
        return -1
    }

    onCurrentChanged: root.ensureVisible()
    onWidthChanged: root.ensureVisible()

    Flickable {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        // The chevrons sit over the strip's ends, so the ends are given up to
        // them rather than drawn with the categories running underneath.
        anchors.rightMargin: root.scrollable ? Metrics.controlHeight : 0
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: row.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: root.scrollable

        Row {
            id: row
            height: strip.height
            spacing: Metrics.lg

            Repeater {
                id: tabRepeater
                model: root.categories

                delegate: Item {
                    id: tab

                    required property var modelData

                    readonly property bool active: root.current === tab.modelData.key

                    width: tabLabel.implicitWidth
                    height: strip.height

                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: tab.modelData.title
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: tab.active
                        color: tab.active ? Colors.textPrimary
                             : tabMouse.containsMouse ? Colors.textSecondary : Colors.textTertiary
                        Behavior on color { ColorAnimation { duration: 90 } }
                    }

                    Rectangle {
                        visible: tab.modelData.modified > 0
                        anchors.left: tabLabel.right
                        anchors.leftMargin: Metrics.xs
                        anchors.verticalCenter: tabLabel.verticalCenter
                        width: Metrics.xs
                        height: Metrics.xs
                        radius: width / 2
                        color: Colors.accent
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Metrics.xxs
                        radius: Metrics.xxs
                        color: Colors.accent
                        visible: tab.active
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        anchors.margins: -Metrics.xs
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activated(tab.modelData.key)
                    }
                }
            }
        }
    }

    // The strip fades out rather than ending in a hard cut, and the chevrons sit
    // on the side that has more.
    ToolButton {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.controlHeight
        height: Metrics.controlHeight
        icon: "chevron_right"
        visible: root.scrollable
        hint: "More categories"
        onClicked: strip.contentX = Math.min(strip.contentWidth - strip.width,
                                             strip.contentX + 160)
    }

    ToolButton {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.controlHeight
        height: Metrics.controlHeight
        icon: "chevron_left"
        visible: root.scrollable && strip.contentX > 1
        hint: "Previous categories"
        onClicked: strip.contentX = Math.max(0, strip.contentX - 160)

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Colors.surfaceContainerLow }
                GradientStop { position: 1.0; color: Colors.alpha(Colors.surfaceContainerLow, 0) }
            }
            z: -1
        }
    }
}
