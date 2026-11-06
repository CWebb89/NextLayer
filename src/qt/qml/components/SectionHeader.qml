import QtQuick
import "../theme"

// Section label: bold caption on the textTertiary rung, so it introduces a
// group without competing with the values below it. Never accent, never caps.
Row {
    id: root

    property string text: ""

    spacing: Metrics.sm
    height: label.implicitHeight

    Text {
        id: label
        text: root.text
        textFormat: Text.PlainText
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        font.bold: true
        color: Colors.textSecondary
    }
}
