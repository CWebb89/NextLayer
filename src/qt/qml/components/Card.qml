import QtQuick
import "../theme"

// Solid rounded container with a 2px outlineVariant frame, an optional title,
// and a padding-inset content slot. Callers size it; children placed in the
// default property land inside the padded body and anchor themselves.
Item {
    id: root

    property string title: ""
    property color fill: Colors.surfaceContainer
    property color border: Colors.outlineVariant
    default property alias content: body.data

    readonly property int bodyTop: header.visible
        ? Metrics.panelPadding + header.implicitHeight + Metrics.md
        : Metrics.panelPadding

    implicitWidth: 320
    implicitHeight: 160

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusLarge
        color: root.fill
    }

    Rectangle {
        anchors.fill: parent
        radius: Metrics.radiusLarge
        color: "transparent"
        border.width: Metrics.cardBorderWidth
        border.color: hover.hovered ? Colors.edgeStrong : root.border
        Behavior on border.color { ColorAnimation { duration: 140 } }
    }

    Text {
        id: header
        visible: root.title !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Metrics.panelPadding
        anchors.rightMargin: Metrics.panelPadding
        anchors.topMargin: Metrics.panelPadding
        text: root.title
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Type.family
        font.pixelSize: Type.sizeCaption
        font.bold: true
        color: Colors.textPrimary
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: Metrics.panelPadding
        anchors.rightMargin: Metrics.panelPadding
        anchors.topMargin: root.bodyTop
        anchors.bottomMargin: Metrics.panelPadding
        clip: true
    }

    HoverHandler {
        id: hover
    }
}
