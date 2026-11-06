import QtQuick
import "../theme"

// Vertical hairline separating toolbar groups, matching the dividers Orca draws
// between its icon clusters.
Rectangle {
    width: Metrics.hairline
    height: Metrics.xxl
    color: Colors.separator
}
