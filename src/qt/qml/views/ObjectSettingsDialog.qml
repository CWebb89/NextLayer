// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// Everything that belongs to one object rather than to the plate: its own
// settings (the print settings this engine lets an object override), its layer
// height profile, and its painted facets. All three reach the slicer through the
// plate slice path, so this panel edits exactly what the slice reads.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property int objectId: 0
    property string paintKind: "supports"
    property real brushRadius: 4.0
    property bool erasing: false
    property bool settingsTab: true
    property bool painting: false
    property int paintTool: 2
    property string statusText: ""

    signal paintRequested(int objectId, string kind, real radius, bool erase, int tool)

    /// A settings surface for one thing: an object, or -- when volumeId is set --
    /// one modifier volume inside it. The engine keeps the two in different
    /// boxes, so the dialog reads and writes whichever one it was opened on.
    property int volumeId: 0

    readonly property bool volumeMode: volumeId > 0

    property var overrides: volumeMode && objectId > 0
                            ? plateDocument.volumeSettings(objectId, volumeId)
                            : objectId > 0 ? plateDocument.objectSettings(objectId) : ({})

    readonly property var info: objectId > 0 ? plateDocument.objectInfo(objectId) : ({})
    readonly property var definitions: objectId > 0 ? settingsModel.objectSettingDefs("") : []
    readonly property var profile: objectId > 0 ? plateDocument.layerHeightProfile(objectId) : []
    /// Settings the object's file carried that this engine cannot apply. They are
    /// kept in the document and written back out on export, so what this list says
    /// is "not used here" and never "lost": the object's file still holds them.
    readonly property var keptForeign: objectId > 0 && !volumeMode
                                      ? plateDocument.objectForeignSettings(objectId) : ({})

    function overrideValue(name, fallback) {
        const value = root.overrides[name]
        return value === undefined ? fallback : value
    }

    function refresh() {
        root.refreshVersion++
        root.overrides = root.volumeMode && objectId > 0
                         ? plateDocument.volumeSettings(objectId, volumeId)
                         : objectId > 0 ? plateDocument.objectSettings(objectId) : ({})
    }

    function setOverride(name, value) {
        if (root.volumeMode)
            plateDocument.setVolumeSetting(root.objectId, root.volumeId, name, value)
        else
            plateDocument.setObjectSetting(root.objectId, name, value)
    }

    function clearOverride(name) {
        if (root.volumeMode)
            plateDocument.clearVolumeSetting(root.objectId, root.volumeId, name)
        else
            plateDocument.clearObjectSetting(root.objectId, name)
    }

    property int refreshVersion: 0

    function openFor(id) {
        root.volumeId = 0
        root.objectId = id
        root.painting = false
        root.settingsTab = true
        root.refresh()
        root.open()
    }

    function togglePainting() {
        root.painting = !root.painting
        root.paintRequested(root.painting ? root.objectId : 0, root.paintKind,
                            root.brushRadius, root.erasing, root.paintTool)
    }

    function labelsForKind(kind) {
        if (kind === "tool")
            return plateDocument.paintedFacetCount(root.objectId, "tool") + " painted triangle(s)"
        return plateDocument.paintedFacetCount(root.objectId, kind) + " painted triangle(s)"
    }

    width: Math.min(720, parent.width - Metrics.xl * 2)
    height: Math.min(680, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: false
    dim: false
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
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                spacing: Metrics.sm

                Text {
                    text: root.info.name === undefined ? "Object" : root.info.name
                    font.family: Type.family
                    font.pixelSize: Type.sizeTitle
                    font.bold: true
                    color: Colors.textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: Object.keys(root.overrides).length + " override(s)"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                ToolButton {
                    icon: "close"
                    label: "Close"
                    hint: "Close the object panel"
                    onClicked: root.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                spacing: Metrics.sm

                Button {
                    text: "Settings"
                    primary: root.settingsTab
                    onClicked: root.settingsTab = true
                }

                Button {
                    text: "Layer height"
                    primary: !root.settingsTab
                    onClicked: root.settingsTab = false
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Run this object alone"
                    visible: false
                }

                Button {
                    text: "Drop every override"
                    enabled: Object.keys(root.overrides).length > 0
                    onClicked: {
                        if (root.volumeMode) {
                            const keys = Object.keys(root.overrides)
                            for (let i = 0; i < keys.length; ++i)
                                plateDocument.clearVolumeSetting(root.objectId, root.volumeId, keys[i])
                        } else {
                            plateDocument.clearObjectSettings(root.objectId)
                        }
                        root.refresh()
                    }
                }
            }

            Divider { Layout.fillWidth: true; Layout.topMargin: Metrics.sm }

            // Settings: every print setting this engine allows an object to override,
            // each row switching between "inherits the preset" and "this object only".
            ListView {
                id: settingsList
                visible: root.settingsTab
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Metrics.md
                clip: true
                spacing: Metrics.xs
                model: root.definitions

                // What the file carried and this engine cannot use. Read-only, and
                // said plainly: these are neither applied nor lost -- they go back
                // into the project when it is saved, so a file from another slicer
                // keeps everything it arrived with. Part of the list rather than a
                // panel under it, so the two cannot overlap and it scrolls with the
                // settings it belongs to.
                footer: Item {
                    visible: Object.keys(root.keptForeign).length > 0
                    width: ListView.view.width
                    height: keptColumn.implicitHeight + Metrics.md
                    implicitHeight: height

                    ColumnLayout {
                        id: keptColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: Metrics.sm
                        spacing: Metrics.xxs

                        Text {
                            text: Object.keys(root.keptForeign).length
                                  + " setting(s) from this object's file are not used here. "
                                  + "They are kept and written back when the project is saved."
                            wrapMode: Text.WordWrap
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textMuted
                            Layout.fillWidth: true
                        }

                        Repeater {
                            model: Object.keys(root.keptForeign).sort()

                            delegate: RowLayout {
                                required property string modelData

                                Layout.fillWidth: true
                                spacing: Metrics.sm

                                Text {
                                    text: parent.modelData
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeCaption
                                    color: Colors.textSecondary
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: String(root.keptForeign[parent.modelData])
                                    font.family: Type.family
                                    font.pixelSize: Type.sizeCaption
                                    color: Colors.textMuted
                                }
                            }
                        }
                    }
                }

                delegate: Rectangle {
                    id: settingRow
                    required property var modelData

                    readonly property bool overridden: root.overrides[modelData.name] !== undefined

                    width: ListView.view.width
                    height: overridden ? 76 : 44
                    radius: Metrics.radiusSmall
                    color: overridden ? Colors.alpha(Colors.accent, 0.10) : Colors.surfaceContainer

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Metrics.sm
                        spacing: Metrics.xxs

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Metrics.sm

                            Text {
                                text: settingRow.modelData.label
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: settingRow.overridden
                                      ? (root.volumeMode ? "this volume" : "this object")
                                      : "from the preset"
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: settingRow.overridden ? Colors.accent : Colors.textMuted
                            }

                            Toggle {
                                checked: settingRow.overridden
                                onToggled: {
                                    if (settingRow.overridden) {
                                        root.clearOverride(settingRow.modelData.name)
                                    } else {
                                        const current = settingRow.modelData.value
                                        root.setOverride(settingRow.modelData.name,
                                                         current === undefined ? true : current)
                                    }
                                    root.refresh()
                                }
                            }
                        }

                        SettingEditor {
                            visible: settingRow.overridden
                            Layout.fillWidth: true
                            externalWrite: true
                            item: Object.assign({}, settingRow.modelData, {
                                value: root.overrideValue(settingRow.modelData.name,
                                                          settingRow.modelData.value),
                                modified: false
                            })
                            onValueEdited: (name, value) => {
                                root.setOverride(name, value)
                                root.refresh()
                            }
                        }
                    }
                }
            }

            // Layer height: a band and a height for it. The profile is written to the
            // object, so a slice honours it exactly as a project file would.
            ColumnLayout {
                visible: !root.settingsTab
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Metrics.md
                spacing: Metrics.md

                Text {
                    text: root.profile.length > 0
                          ? "This object has its own layer height profile (" + root.profile.length + " bands)"
                          : "This object uses the printer's layer height"
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    color: Colors.textPrimary
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.md

                    Text {
                        text: "Band from"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                        Layout.preferredWidth: 90
                    }

                    NumberField {
                        id: bandFrom
                        Layout.preferredWidth: 120
                        value: 0
                        from: 0
                        to: 1000
                        decimals: 1
                    }

                    Text {
                        text: "to"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                    }

                    NumberField {
                        id: bandTo
                        Layout.preferredWidth: 120
                        value: 0
                        from: 0
                        to: 1000
                        decimals: 1
                    }

                    Text {
                        text: "mm"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.md

                    Text {
                        text: "Layer height"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                        Layout.preferredWidth: 90
                    }

                    NumberField {
                        id: layerHeight
                        Layout.preferredWidth: 120
                        value: 0.1
                        from: 0.05
                        to: 0.6
                        decimals: 2
                    }

                    Text {
                        text: "mm inside the band"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.sm

                    Button {
                        text: "Apply to the band"
                        primary: true
                        onClicked: {
                            const applied = plateDocument.setLayerHeightRange(root.objectId,
                                                                              bandFrom.value,
                                                                              bandTo.value,
                                                                              layerHeight.value)
                            root.statusText = applied ? "band applied" : "the band is empty"
                        }
                    }

                    Button {
                        text: "Back to uniform"
                        enabled: root.profile.length > 0
                        onClicked: plateDocument.clearLayerHeights(root.objectId)
                    }

                    Item { Layout.fillWidth: true }
                }

                Text {
                    id: statusLabel
                    text: root.statusText
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                Divider { Layout.fillWidth: true }

                Text {
                    text: "Painting"
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.sm

                    Button {
                        text: "Support"
                        primary: root.paintKind === "supports"
                        onClicked: root.paintKind = "supports"
                    }
                    Button {
                        text: "Seam"
                        primary: root.paintKind === "seam"
                        onClicked: root.paintKind = "seam"
                    }
                    Button {
                        text: "Colour"
                        primary: root.paintKind === "tool"
                        onClicked: root.paintKind = "tool"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "Brush"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                    }

                    Slider {
                        Layout.preferredWidth: 160
                        from: 0.5
                        to: 20
                        step: 0.5
                        value: root.brushRadius
                        onMoved: (value) => root.brushRadius = value
                    }

                    Text {
                        text: root.brushRadius.toFixed(1) + " mm"
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textSecondary
                        Layout.preferredWidth: 60
                    }
                }

                // One swatch per tool of the changer: a tool changer prints each
                // painted region with the tool that was assigned to it, so the swatch
                // is the colour that will come out.
                Flow {
                    Layout.fillWidth: true
                    spacing: Metrics.xs
                    visible: root.paintKind === "tool"

                    Repeater {
                        model: plateDocument.toolColours

                        delegate: Rectangle {
                            id: swatch
                            required property var modelData
                            required property int index

                            width: Metrics.controlHeight
                            height: Metrics.controlHeight
                            radius: Metrics.radiusSmall
                            color: swatch.modelData
                            border.width: root.paintTool === index + 1 ? 2 : 1
                            border.color: root.paintTool === index + 1 ? Colors.textPrimary
                                                                       : Colors.outlineVariant

                            Text {
                                anchors.centerIn: parent
                                text: swatch.index + 1
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                font.bold: true
                                color: "#101010"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.paintTool = swatch.index + 1
                                    if (root.painting)
                                        root.paintRequested(root.objectId, "tool",
                                                            root.brushRadius, root.erasing)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.sm

                    Button {
                        text: "Start painting"
                        primary: !root.painting
                        onClicked: root.togglePainting()
                    }

                    Button {
                        text: root.erasing ? "Erase" : "Paint"
                        onClicked: root.erasing = !root.erasing
                    }

                    Button {
                        text: root.paintKind === "tool" ? "Clear colour paint"
                                                         : root.paintKind === "seam" ? "Clear seam paint"
                                                                                     : "Clear support paint"
                        onClicked: {
                            plateDocument.clearPaint(root.objectId, root.paintKind)
                            root.refresh()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.labelsForKind(root.paintKind)
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: Colors.textMuted
                    }
                }
            }

            Divider { Layout.fillWidth: true }

            RowLayout {
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
