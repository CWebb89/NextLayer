pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../theme"
import "../components"

// Object statistics: what the mesh actually is. Volume and surface area come
// from the mesh itself, and the mass is that volume times the current
// filament's density -- the same numbers Orca shows in its object dialog.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property int objectId: 0
    property var stats: objectId > 0 ? plateDocument.objectStatistics(objectId) : ({})

    readonly property real density: {
        const value = settingsModel.value("filament_density")
        return value === undefined || value === null ? 1.24 : Number(value)
    }

    readonly property real grams: (stats.volume_cm3 !== undefined ? stats.volume_cm3 : 0) * root.density

    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 0

    function openFor(id) {
        root.objectId = id
        root.open()
    }

    function row(label, value) {
        return { label: label, value: value }
    }

    readonly property var rows: root.objectId <= 0 ? [] : [
        row("Volume", Number(root.stats.volume_cm3).toFixed(2) + " cm³"),
        row("Surface area", Number(root.stats.area_cm2).toFixed(2) + " cm²"),
        row("Facets", String(root.stats.facets)),
        row("Size", Number(root.stats.width).toFixed(2) + " x "
                     + Number(root.stats.depth).toFixed(2) + " x "
                     + Number(root.stats.height).toFixed(2) + " mm"),
        row("Bounding box", Number(root.stats.min_x).toFixed(1) + ", "
                            + Number(root.stats.min_y).toFixed(1) + ", "
                            + Number(root.stats.min_z).toFixed(1) + "  →  "
                            + Number(root.stats.max_x).toFixed(1) + ", "
                            + Number(root.stats.max_y).toFixed(1) + ", "
                            + Number(root.stats.max_z).toFixed(1)),
        row("Volumes", String(root.stats.volumes)),
        row("Mass at " + root.density.toFixed(2) + " g/cm³", root.grams.toFixed(2) + " g")
    ]

    background: Rectangle {
        radius: Metrics.radiusLarge
        color: Colors.surface
        border.width: Metrics.borderWidth
        border.color: Colors.edgeStrong
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
                Layout.margins: Metrics.md

                Text {
                    text: root.stats.name === undefined ? "Statistics" : String(root.stats.name)
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                ToolButton {
                    Layout.preferredWidth: Metrics.controlHeight
                    Layout.preferredHeight: Metrics.controlHeight
                    icon: "close"
                    label: "Close"
                    onClicked: root.close()
                }
            }

            Column {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.md
                Layout.rightMargin: Metrics.md
                Layout.bottomMargin: Metrics.md
                spacing: Metrics.xs

                Repeater {
                    model: root.rows

                    delegate: RowLayout {
                        id: statRow

                        required property var modelData

                        width: parent.width
                        spacing: Metrics.sm

                        Text {
                            text: statRow.modelData.label
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            color: Colors.textSecondary
                            Layout.fillWidth: true
                        }

                        Text {
                            text: statRow.modelData.value
                            font.family: Type.family
                            font.pixelSize: Type.sizeSmall
                            color: Colors.textPrimary
                        }
                    }
                }
            }

            Button {
                Layout.alignment: Qt.AlignRight
                Layout.rightMargin: Metrics.md
                Layout.bottomMargin: Metrics.md
                text: "close"
                onClicked: root.close()
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
