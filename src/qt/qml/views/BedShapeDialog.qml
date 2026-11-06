pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// Bed outline editor for `bed_shape`: a rectangle helper that rewrites the four
// corners, or an explicit polygon with as many points as the machine needs.
// Writes the point list back through the settings model, which is what the
// engine reads as the printable area.
Popup {
    id: root

    // Escape and the tab chain need the popup to hold focus; without this the
    // close policy is set but nothing can trigger it.
    focus: true

    property var setting: ({})
    property var points: []
    property bool rectangleMode: true
    property string message: ""

    readonly property string settingName: root.setting.name === undefined ? ""
                                         : String(root.setting.name)
    readonly property bool bedShape: root.settingName === "bed_shape"
    readonly property int minimumPoints: root.bedShape ? 3 : 1

    parent: Overlay.overlay
    width: Math.min(620, parent.width - Metrics.xl * 2)
    height: Math.min(620, parent.height - Metrics.xl * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(item) {
        root.setting = item
        root.message = ""
        root.points = []
        const name = item.name === undefined || item.name === null ? "" : String(item.name)
        const flat = settingsModel.points(name)
        const next = []
        for (let i = 0; i + 1 < flat.length; i += 2)
            next.push([Number(flat[i]), Number(flat[i + 1])])
        root.points = next
        root.rectangleMode = root.bedShape && root.isRectangle(root.points)
        root.open()
    }

    function isRectangle(points) {
        if (points.length !== 4)
            return false
        const x0 = Math.min(points[0][0], points[2][0])
        const x1 = Math.max(points[0][0], points[2][0])
        const y0 = Math.min(points[0][1], points[2][1])
        const y1 = Math.max(points[0][1], points[2][1])
        for (let i = 0; i < 4; ++i)
            if (points[i][0] !== (i % 3 === 0 ? x0 : x1) || points[i][1] !== (i < 2 ? y0 : y1))
                return false
        return true
    }

    function rectangleWidth() {
        return root.points.length === 4 ? Math.abs(root.points[2][0] - root.points[0][0]) : 250
    }

    function rectangleDepth() {
        return root.points.length === 4 ? Math.abs(root.points[2][1] - root.points[0][1]) : 210
    }

    function applyRectangle(width, depth) {
        root.points = [[0, 0], [width, 0], [width, depth], [0, depth]]
    }

    function apply() {
        if (root.points.length < root.minimumPoints) {
            root.message = root.bedShape ? "A bed needs at least three points"
                                         : "Add at least one point"
            return
        }
        settingsModel.setValue(root.settingName, root.points)
        root.close()
    }

    onRectangleModeChanged: {
        if (root.rectangleMode)
            root.applyRectangle(root.rectangleWidth(), root.rectangleDepth())
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
            anchors.margins: Metrics.lg

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.topMargin: Metrics.lg

                Text {
                    Layout.fillWidth: true
                    text: root.bedShape ? "Bed shape" : "Points"
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }

                ToolButton {
                    icon: "close"
                    hint: "Close without applying"
                    onClicked: root.close()
                }
            }

            RowLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                visible: root.bedShape

                Toggle {
                    checked: root.rectangleMode
                    onToggled: (checked) => root.rectangleMode = checked
                }

                Text {
                    Layout.fillWidth: true
                    text: "Rectangular bed"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textPrimary
                }
            }

            RowLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                visible: root.bedShape && root.rectangleMode
                spacing: Metrics.sm

                Text {
                    text: "X"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textSecondary
                }

                NumberField {
                    id: widthField
                    Layout.fillWidth: true
                    value: root.rectangleWidth()
                    from: 1
                    to: 2000
                    units: "mm"
                    isLength: true
                    onEdited: (value) => root.applyRectangle(value, root.rectangleDepth())
                }

                Text {
                    text: "Y"
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textSecondary
                }

                NumberField {
                    id: depthField
                    Layout.fillWidth: true
                    value: root.rectangleDepth()
                    from: 1
                    to: 2000
                    units: "mm"
                    isLength: true
                    onEdited: (value) => root.applyRectangle(root.rectangleWidth(), value)
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                contentWidth: width
                contentHeight: pointColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                visible: !root.rectangleMode || !root.bedShape

                ColumnLayout {
                    id: pointColumn
                    width: parent.width
                    spacing: Metrics.xs

                    Repeater {
                        model: root.points

                        delegate: RowLayout {
                            id: pointRow

                            required property int index
                            required property var modelData

                            Layout.fillWidth: true
                            spacing: Metrics.sm

                            Text {
                                Layout.preferredWidth: Metrics.xl
                                text: (pointRow.index + 1) + "."
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: Colors.textTertiary
                            }

                            NumberField {
                                Layout.fillWidth: true
                                value: Number(pointRow.modelData[0])
                                from: -1000
                                to: 2000
                                units: "x"
                                isLength: true
                                onEdited: (value) => {
                                    const next = root.points.slice()
                                    next[pointRow.index] = [value, Number(pointRow.modelData[1])]
                                    root.points = next
                                }
                            }

                            NumberField {
                                Layout.fillWidth: true
                                value: Number(pointRow.modelData[1])
                                from: -1000
                                to: 2000
                                units: "y"
                                isLength: true
                                onEdited: (value) => {
                                    const next = root.points.slice()
                                    next[pointRow.index] = [Number(pointRow.modelData[0]), value]
                                    root.points = next
                                }
                            }

                            ToolButton {
                                icon: "close"
                                hint: "Remove this point"
                                onClicked: {
                                    const next = root.points.slice()
                                    next.splice(pointRow.index, 1)
                                    root.points = next
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: "Add point"
                            onClicked: {
                                const next = root.points.slice()
                                next.push([0, 0])
                                root.points = next
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Text {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.fillWidth: true
                visible: root.message.length > 0
                text: root.message
                wrapMode: Text.WordWrap
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.error
            }

            RowLayout {
                Layout.leftMargin: Metrics.lg
                Layout.rightMargin: Metrics.lg
                Layout.bottomMargin: Metrics.lg

                Text {
                    Layout.fillWidth: true
                    text: root.points.length + " points"
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
