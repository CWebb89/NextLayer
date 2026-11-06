pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// One-value-per-row editor for the string list settings (vector<string>) and the
// settings the engine keeps as a single multi-line string, such as the ramming
// parameters. Both write a value the engine parses back verbatim.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property var setting: ({})
    property var rows: []
    property string singleText: ""
    property bool singleString: false

    readonly property string settingName: root.setting.name === undefined ? ""
                                         : String(root.setting.name)

    parent: Overlay.overlay
    width: Math.min(700, parent.width - Metrics.xl * 2)
    height: Math.min(620, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(item) {
        root.setting = item
        root.singleString = item.type === "String" && !root.isSequence(item.value)
        root.rows = []
        root.singleText = ""
        if (root.singleString) {
            root.singleText = item.value === undefined || item.value === null
                              ? "" : String(item.value)
        } else if (root.isSequence(item.value)) {
            for (let i = 0; i < item.value.length; ++i)
                root.rows.push(String(item.value[i]))
        }
        root.open()
    }

    function isSequence(v) {
        return v !== null && v !== undefined && typeof v === "object"
                && typeof v.length === "number"
    }

    function apply() {
        if (root.singleString)
            settingsModel.setValue(root.settingName, root.singleText)
        else
            settingsModel.setValue(root.settingName, root.rows)
        root.close()
    }

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

            spacing: Metrics.sm

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.topMargin: Metrics.lg

                Text {
                    Layout.fillWidth: true
                    text: root.setting.label === undefined ? "Values"
                                                           : String(root.setting.label)
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                    elide: Text.ElideRight
                }

                ToolButton {
                    icon: "close"
                    hint: "Close without applying"
                    onClicked: root.close()
                }
            }

            TextArea {
                id: rawField
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.singleString
                text: root.singleText
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                color: Colors.textPrimary
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                onTextChanged: root.singleText = rawField.text

                background: Rectangle {
                    radius: Metrics.radiusSmall
                    color: Colors.surfaceContainer
                    border.width: Metrics.borderWidth
                    border.color: rawField.activeFocus ? Colors.primary : Colors.outlineVariant
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                visible: !root.singleString
                contentWidth: width
                contentHeight: entryColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: entryColumn
                    width: parent.width
                    spacing: Metrics.sm

                    Repeater {
                        model: root.rows

                        delegate: RowLayout {
                            id: entryRow

                            required property int index

                            Layout.fillWidth: true
                            spacing: Metrics.xs

                            TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(Metrics.controlHeight,
                                                                 contentHeight + Metrics.md)
                                text: entryRow.index < root.rows.length
                                      ? String(root.rows[entryRow.index]) : ""
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: Colors.textPrimary
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                onEditingFinished: {
                                    const next = root.rows.slice()
                                    next[entryRow.index] = text
                                    root.rows = next
                                }

                                background: Rectangle {
                                    radius: Metrics.radiusSmall
                                    color: Colors.surfaceContainer
                                    border.width: Metrics.borderWidth
                                    border.color: Colors.outlineVariant
                                }
                            }

                            ToolButton {
                                icon: "close"
                                hint: "Remove this value"
                                onClicked: {
                                    const next = root.rows.slice()
                                    next.splice(entryRow.index, 1)
                                    root.rows = next
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: "Add value"
                            onClicked: {
                                const next = root.rows.slice()
                                next.push("")
                                root.rows = next
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            RowLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.lg

                Text {
                    Layout.fillWidth: true
                    text: root.singleString ? "One value, free text"
                                            : root.rows.length + " value"
                                              + (root.rows.length === 1 ? "" : "s")
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textMuted
                }

                Button {
                    text: "Cancel"
                    onClicked: root.close()
                }

                Button {
                    text: "Apply"
                    primary: true
                    onClicked: root.apply()
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
