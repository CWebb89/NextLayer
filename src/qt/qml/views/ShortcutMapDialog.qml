pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// The shortcut map: one list, read by the window's key handler and by this
// dialog, so a binding cannot drift from the help. Rows are matched on
// `key` + `modifiers`; anything with `mouse` in its hint is listed and handled
// where that tool lives.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property var shortcuts: []
    property string filter: ""

    readonly property var visibleShortcuts: {
        const needle = root.filter.trim().toLowerCase()
        if (needle.length === 0)
            return root.shortcuts
        const result = []
        for (let i = 0; i < root.shortcuts.length; ++i) {
            const entry = root.shortcuts[i]
            if (String(entry.keys).toLowerCase().indexOf(needle) >= 0
                    || String(entry.action).toLowerCase().indexOf(needle) >= 0
                    || String(entry.group).toLowerCase().indexOf(needle) >= 0)
                result.push(entry)
        }
        return result
    }

    function openWithCurrent() {
        root.filter = ""
        root.open()
    }

    parent: Overlay.overlay
    width: Math.min(760, parent.width - Metrics.xl * 2)
    height: Math.min(680, parent.height - Metrics.xl * 2)
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
                Layout.fillWidth: true
                Layout.margins: Metrics.md
                spacing: Metrics.sm

                Text {
                    text: "Keyboard shortcuts"
                    font.family: Type.family
                    font.pixelSize: Type.sizeTitle
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: root.visibleShortcuts.length + " of " + root.shortcuts.length
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                    Layout.fillWidth: true
                }

                ToolButton {
                    icon: "close"
                    label: "Close"
                    hint: "Close the shortcut map (Escape, or ?)"
                    onClicked: root.close()
                }
            }

            TextField {
                id: filterField
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                Layout.bottomMargin: Metrics.md
                Layout.preferredHeight: Metrics.controlHeight
                leftPadding: Metrics.md
                rightPadding: Metrics.md
                placeholderText: "Filter by key, action or group"
                placeholderTextColor: Colors.textMuted
                color: Colors.textPrimary
                font.family: Type.family
                font.pixelSize: Type.sizeBody
                selectByMouse: true
                onTextEdited: root.filter = text

                // The stock control brings its own light background and its own padding,
                // which both fights the theme and packed the placeholder against the
                // border. Same surface as every other field in the app.
                background: Rectangle {
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer
                    border.width: Metrics.borderWidth
                    border.color: filterField.activeFocus ? Colors.primary : Colors.outlineVariant
                }
            }

            Divider { Layout.fillWidth: true }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Metrics.md
                clip: true
                spacing: Metrics.xs
                model: root.visibleShortcuts

                delegate: Rectangle {
                    id: row
                    required property var modelData

                    width: ListView.view.width
                    height: 40
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Metrics.sm
                        anchors.rightMargin: Metrics.sm
                        spacing: Metrics.sm

                        Rectangle {
                            Layout.preferredWidth: 190
                            Layout.preferredHeight: 26
                            radius: Metrics.radiusSmall
                            color: Colors.alpha(Colors.surfaceContainerHigh, 0.9)
                            border.width: Metrics.borderWidth
                            border.color: Colors.edge

                            Text {
                                anchors.centerIn: parent
                                text: row.modelData.keys
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                font.bold: true
                                color: Colors.textPrimary
                            }
                        }

                        Text {
                            text: row.modelData.action
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text {
                            text: row.modelData.group
                            font.family: Type.family
                            font.pixelSize: Type.sizeCaption
                            color: Colors.textMuted
                        }
                    }
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
