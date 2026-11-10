// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components"

// Legend for the sliced preview: a view-type picker
// over either one row per extrusion role (line type mode) or one row per colour
// band of the active value range (speed, flow, height, layer time...), then the
// travel/retraction/seam options, then the totals the engine reports for the
// file. Feature and option rows toggle their geometry, so hiding infill or
// travel leaves only what matters.
Card {
    id: root

    property bool active: true
    property bool optionsOpen: false
    property bool collapsed: false

    readonly property var featureRows: toolpath.featureLegend !== undefined && toolpath.featureLegend !== null
                                       ? toolpath.featureLegend : []
    readonly property var optionRows: toolpath.optionLegend !== undefined && toolpath.optionLegend !== null
                                      ? toolpath.optionLegend : []
    readonly property var rangeRows: toolpath.rangeLegend !== undefined && toolpath.rangeLegend !== null
                                     ? toolpath.rangeLegend : []
    readonly property bool rangeMode: String(toolpath.viewType) !== "Feature type"
    readonly property var rows: root.rangeMode ? root.rangeRows : root.featureRows
    readonly property int rowHeight: Math.round(Type.sizeSmall * 1.7)

    implicitHeight: stack.implicitHeight + Metrics.panelPadding * 2
    visible: root.active && (root.collapsed || root.rows.length > 0 || root.optionRows.length > 0)
    title: ""

    function clock(seconds) {
        const total = Math.max(0, Math.round(Number(seconds)))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        // No space between the parts: the legend's time column is 54 px, and
        // "10m 37s" ran into the percentage beside it.
        return h > 0 ? h + "h" + m + "m" : (m > 0 ? m + "m" + s + "s" : s + "s")
    }

    function length(mm) {
        const value = Number(mm)
        return value < 1000 ? Math.round(value) + "mm" : (value / 1000).toFixed(2) + "m"
    }

    // The header: the view-type picker with a code-style icon, and a chevron
    // that folds the whole panel away so the toolpath is not covered by it.
    Column {
        id: stack

        width: parent.width
        spacing: Metrics.sm

    Item {
        id: header

        width: parent.width
        height: Metrics.rowHeight

        RowLayout {
            anchors.fill: parent
            spacing: Metrics.sm

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: "</>"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }

            ComboBox {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                choices: toolpath.viewTypes
                currentValue: toolpath.viewType
                onSelected: function(value) { toolpath.setViewType(value) }
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.collapsed ? "\u25b8" : "\u25be"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: parent.width - Metrics.huge
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            onClicked: root.collapsed = !root.collapsed
        }
    }

    Column {
        id: legendColumn

        width: parent.width
        visible: !root.collapsed
        spacing: Metrics.xxs

        RowLayout {
            width: parent.width
            spacing: Metrics.sm

            Text {
                Layout.fillWidth: true
                text: root.rangeMode ? "Color" : "Line type"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textTertiary
            }

            Text {
                Layout.preferredWidth: 66
                horizontalAlignment: Text.AlignRight
                visible: !root.rangeMode
                text: "Time"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textTertiary
            }

            Text {
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
                visible: !root.rangeMode
                text: "%"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textTertiary
            }

            Text {
                Layout.preferredWidth: 96
                horizontalAlignment: Text.AlignRight
                visible: !root.rangeMode
                text: "Usage"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textTertiary
            }

            Item { width: Type.sizeCaption; height: 1 }
        }

        Separator { width: parent.width }

        Repeater {
            model: root.rows
            delegate: Item {
                id: legendRow

                required property var modelData

                width: legendColumn.width
                height: root.rowHeight
                opacity: legendRow.modelData.visible === false ? 0.42 : 1.0

                Rectangle {
                    anchors.fill: parent
                    radius: Metrics.radiusSmall
                    color: legendMouse.containsMouse ? Colors.hoverFill : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Metrics.xs
                    anchors.rightMargin: Metrics.xs
                    spacing: Metrics.sm

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Metrics.sm + Metrics.xxs
                        Layout.preferredHeight: Metrics.sm + Metrics.xxs
                        radius: Metrics.xxs
                        color: legendRow.modelData.visible === false ? Colors.textMuted : legendRow.modelData.color
                    }

                    Text {
                        id: legendName
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: true
                        Layout.minimumWidth: 56
                        text: String(legendRow.modelData.name)
                        elide: Text.ElideRight
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        font.bold: true
                        color: legendRow.modelData.visible === false ? Colors.textMuted : Colors.textPrimary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 58
                        horizontalAlignment: Text.AlignRight
                        visible: !root.rangeMode
                        text: root.clock(Number(legendRow.modelData.seconds))
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textSecondary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                        visible: !root.rangeMode
                        text: Number(legendRow.modelData.percent).toFixed(1) + "%"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 96
                        horizontalAlignment: Text.AlignRight
                        visible: !root.rangeMode
                        text: root.length(legendRow.modelData.lengthMm) + " "
                              + (Number(legendRow.modelData.massG) < 1
                                 ? Number(legendRow.modelData.massG * 1000).toFixed(0) + "mg"
                                 : Number(legendRow.modelData.massG).toFixed(2) + "g")
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Type.sizeCaption
                        visible: !root.rangeMode
                        text: legendRow.modelData.visible === false ? "\u25cb" : "\u25c9"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: legendRow.modelData.visible === false ? Colors.textMuted : Colors.accent
                    }
                }

                MouseArea {
                    id: legendMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !root.rangeMode
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: toolpath.setFeatureVisible(legendRow.modelData.name,
                                                          legendRow.modelData.visible === false)
                }

                Tooltip {
                    target: legendRow
                    text: String(legendRow.modelData.name)
                    shown: legendMouse.containsMouse && legendName.truncated
                }
            }
        }

        Item {
            width: legendColumn.width
            height: root.rowHeight
            visible: root.optionRows.length > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.xs
                anchors.rightMargin: Metrics.xs
                spacing: Metrics.sm

                Text {
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                    text: "Options"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.textSecondary
                }

                Text {
                    text: root.optionsOpen ? "\u25be" : "\u25b8"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textSecondary
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.PointingHandCursor
                onClicked: root.optionsOpen = !root.optionsOpen
            }
        }

        Repeater {
            model: root.optionsOpen ? root.optionRows : []

            delegate: Item {
                id: optionRow

                required property var modelData

                width: legendColumn.width
                height: root.rowHeight
                opacity: optionRow.modelData.visible === false ? 0.42 : 1.0

                Rectangle {
                    anchors.fill: parent
                    radius: Metrics.radiusSmall
                    color: optionMouse.containsMouse ? Colors.hoverFill : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Metrics.xs
                    anchors.rightMargin: Metrics.xs
                    spacing: Metrics.sm

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Metrics.xxs + Metrics.xxs
                        Layout.preferredHeight: Metrics.xxs + Metrics.xxs
                        radius: width / 2.0
                        color: optionRow.modelData.visible === false ? Colors.textMuted : optionRow.modelData.color
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: true
                        text: String(optionRow.modelData.name)
                        elide: Text.ElideRight
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: optionRow.modelData.visible === false ? Colors.textMuted : Colors.textSecondary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 66
                        horizontalAlignment: Text.AlignRight
                        text: Number(optionRow.modelData.seconds) > 0
                              ? root.clock(Number(optionRow.modelData.seconds)) : ""
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textTertiary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Type.sizeCaption
                        text: optionRow.modelData.visible === false ? "\u25cb" : "\u25c9"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: optionRow.modelData.visible === false ? Colors.textMuted : Colors.accent
                    }
                }

                MouseArea {
                    id: optionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: toolpath.setOptionVisible(optionRow.modelData.name,
                                                         optionRow.modelData.visible === false)
                }
            }
        }

        Item { width: 1; height: Metrics.xs }

        Separator { width: parent.width }

        Text {
            width: parent.width
            text: "Total estimation"
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            font.bold: true
            color: Colors.textSecondary
        }

        Repeater {
            model: [
                { label: "Total filament", value: Number(toolpath.totalFilamentMm / 1000).toFixed(2)
                                                 + " m   " + Number(toolpath.totalFilamentG).toFixed(2) + " g" },
                { label: "Cost", value: toolpath.totalCost > 0 ? Number(toolpath.totalCost).toFixed(2) : "\u2014" },
                { label: "Prepare time", value: "<1s" },
                { label: "Model printing time", value: root.clock(toolpath.totalSeconds) },
                { label: "Total time", value: root.clock(toolpath.totalSeconds) }
            ]

            delegate: RowLayout {
                required property var modelData

                width: legendColumn.width
                spacing: Metrics.sm

                Text {
                    Layout.fillWidth: true
                    text: modelData.label
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textSecondary
                }

                Text {
                    text: modelData.value
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.textPrimary
                }
            }
        }
    }
    }
}
