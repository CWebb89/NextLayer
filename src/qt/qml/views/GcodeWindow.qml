import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components"

// The G-code around the move the preview is stopped at, as OrcaSlicer's g-code
// window shows it: numbered monospace lines, the line that produced the current
// move boxed, and the neighbouring lines for context. It is how a toolpath that
// looks wrong is traced back to the line that asked for it.
Card {
    id: root

    property bool active: true
    property bool collapsed: false
    property real maxHeight: Number.POSITIVE_INFINITY

    readonly property var lines: toolpath.gcodeWindow !== undefined && toolpath.gcodeWindow !== null
                                 ? toolpath.gcodeWindow : []
    readonly property int rowHeight: Math.round(Type.sizeCaption * 1.45)

    // As many lines as fit, always keeping the current line in the middle: Orca's
    // window shrinks the surrounding context rather than clipping the card.
    readonly property int fitRows: Math.max(3, Math.floor((root.maxHeight - Metrics.panelPadding * 2
                                                           - root.rowHeight * 2) / root.rowHeight))
    readonly property var shown: {
        if (root.collapsed || root.lines.length <= root.fitRows)
            return root.collapsed ? [] : root.lines
        let centre = 0
        for (let i = 0; i < root.lines.length; ++i)
            if (root.lines[i].current)
                centre = i
        const start = Math.max(0, Math.min(root.lines.length - root.fitRows, centre - Math.floor(root.fitRows / 2)))
        return root.lines.slice(start, start + root.fitRows)
    }

    width: 330
    implicitHeight: Math.min(stack.implicitHeight + Metrics.panelPadding * 2, root.maxHeight)
    visible: root.active && toolpath.loaded && root.lines.length > 0

    Column {
        id: stack
        width: parent.width
        spacing: Metrics.xxs

        Item {
            width: parent.width
            height: root.rowHeight

            RowLayout {
                anchors.fill: parent
                spacing: Metrics.sm

                Text {
                    Layout.fillWidth: true
                    text: "G-code"
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    font.bold: true
                    color: Colors.textSecondary
                }

                Text {
                    text: root.collapsed ? "\u25b8" : "\u25be"
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
                onClicked: root.collapsed = !root.collapsed
            }
        }

        Repeater {
            model: root.shown


            delegate: Item {
                id: lineRow

                required property var modelData

                width: parent.width
                height: root.rowHeight

                Rectangle {
                    anchors.fill: parent
                    visible: lineRow.modelData.current
                    radius: Metrics.xxs
                    color: Colors.alpha(Colors.accent, 0.14)
                    border.width: Metrics.borderWidth
                    border.color: Colors.accent
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Metrics.xs
                    anchors.rightMargin: Metrics.xs
                    spacing: Metrics.sm

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        text: String(lineRow.modelData.number)
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: lineRow.modelData.current ? Colors.accent : Colors.textTertiary
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: true
                        text: String(lineRow.modelData.text)
                        elide: Text.ElideRight
                        font.family: Type.family
                        font.pixelSize: Type.sizeCaption
                        color: lineRow.modelData.current ? Colors.textPrimary : Colors.textSecondary
                    }
                }
            }
        }
    }
}
