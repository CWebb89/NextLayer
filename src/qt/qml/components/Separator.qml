import QtQuick
import "../theme"

// 1px hairline at textPrimary @ 12%.
Rectangle {
    id: root

    implicitHeight: Metrics.hairline
    height: Metrics.hairline
    width: parent ? parent.width : 0
    color: Colors.separator
}
