// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// The device tab: the printer's own web interface, embedded. Mainsail
// and Fluid are both served from the Moonraker host, so the page is whatever that
// host serves. The address is editable because a shell, a camera page or a
// dashboard belongs here too, and a bare IP is accepted: "10.0.0.22" gets its
// scheme and its Moonraker port filled in.
Item {
    id: root

    property string url: ""
    property string stateLine: "no printer is configured"
    property bool loading: false

    readonly property DeviceWebView page: web.item as DeviceWebView

    /// True while the address field owns the keyboard, so the window's bare-key
    /// shortcuts (Return slices, Space previews) stay out of the way.
    readonly property bool typingAddress: addressField.activeFocus

    signal status(string message)

    function normalized(text) {
        let address = text.trim()
        if (address.length === 0)
            return ""
        address = address.replace(/^[a-zA-Z]+:\/\//, "")
        address = address.replace(/\/+$/, "")
        // No port means the printer's own front end on port 80 -- Mainsail and
        // Fluid are served there, while Moonraker's API listens on 7125 and has no
        // front end of its own. A port typed in is used as typed.
        return "http://" + address + "/"
    }

    function addressForPrinter() {
        if (!printerLink.configured)
            return ""
        const host = printerLink.host
        if (host.length === 0)
            return ""
        return normalized(host)
    }

    function connect(text) {
        const address = normalized(text)
        if (address.length === 0) {
            root.status("Type the printer's address first")
            return
        }
        const bare = address.replace("http://", "").replace(/\/$/, "")
        const colon = bare.lastIndexOf(":")
        const host = colon > 0 ? bare.slice(0, colon) : bare
        const port = colon > 0 ? bare.slice(colon + 1) : ""
        printerLink.host = host
        // The page address and the API port are separate: a host typed without a
        // port is the front end on 80, and the upload still goes to 7125.
        if (port.length > 0)
            printerLink.port = port
        printerLink.save()
        printerLink.deviceUrl = address
        root.url = address
        addressField.text = address
        if (root.page !== null)
            root.page.url = address
        root.stateLine = printerLink.printerState()
    }

    function reload() {
        root.url = root.addressForPrinter()
    }

    Timer {
        interval: 5000
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: root.stateLine = printerLink.printerState()
    }

    onVisibleChanged: {
        if (!visible)
            return
        if (root.url.length === 0)
            root.url = root.addressForPrinter()
        if (addressField.text.length === 0)
            addressField.placeholderText = root.url.length > 0
                    ? root.url.replace("http://", "").replace(/\/$/, "")
                    : "10.0.0.22  ·  printer hostname, Mainsail, Fluid, or any page"
        root.stateLine = printerLink.printerState()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Metrics.sm
        anchors.leftMargin: Metrics.md
        anchors.rightMargin: Metrics.md
        anchors.bottomMargin: Metrics.sm
        spacing: Metrics.sm

        RowLayout {
            Layout.fillWidth: true
            spacing: Metrics.sm

            TextField {
                id: addressField
                Layout.fillWidth: true
                placeholderText: "10.0.0.22  ·  printer hostname, Mainsail, Fluid, or any page"
                selectByMouse: false
                onPressed: selectAll()
                onActiveFocusChanged: if (activeFocus) selectAll()
                font.family: Type.family
                font.pixelSize: Type.sizeSmall
                onAccepted: root.connect(text)
            }

            Button {
                text: "Connect"
                onClicked: root.connect(addressField.text)
            }

            Button {
                text: "Reload"
                enabled: root.url.length > 0
                onClicked: if (root.page !== null) root.page.reload()
            }

            Button {
                text: "Printer's page"
                enabled: printerLink.configured
                onClicked: {
                    printerLink.deviceUrl = ""
                    root.reload()
                    addressField.text = root.url
                    root.connect(root.url)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Metrics.sm

            Rectangle {
                Layout.preferredWidth: Metrics.xs
                Layout.preferredHeight: Metrics.xs
                radius: Metrics.xs / 2
                color: root.stateLine.indexOf("did not answer") >= 0
                       || root.stateLine.indexOf("unreachable") >= 0
                       || root.stateLine.indexOf("no printer") >= 0
                       ? Colors.urgent : Colors.success
            }

            Text {
                Layout.fillWidth: true
                text: root.stateLine
                elide: Text.ElideRight
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }

            Text {
                visible: root.loading
                text: "loading"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textTertiary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Metrics.radiusMedium
            color: Colors.surfaceContainerLow
            border.width: Metrics.borderWidth
            border.color: Colors.outlineVariant
            clip: true

            Loader {
                id: web
                anchors.fill: parent
                anchors.margins: Metrics.borderWidth
                active: root.visible
                source: active ? "DeviceWebView.qml" : ""

                onLoaded: {
                    if (root.url.length > 0)
                        root.page.url = root.url
                    root.page.surfaceColor = Colors.surfaceContainerLow
                    root.page.urlChanged.connect(() => {
                        const opened = root.page.url.toString()
                        if (opened.length > 0) {
                            root.url = opened
                            printerLink.deviceUrl = opened
                        }
                    })
                    root.page.loadingStarted.connect(() => root.loading = true)
                    root.page.renderProcessTerminated.connect((status, code) =>
                        root.status("the device page stopped (status " + status + ", code " + code + ")"))
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width * 0.6
                visible: root.url.length === 0
                spacing: Metrics.md

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: printerLink.configured
                          ? "Type the printer's address above and press Connect. Mainsail, Fluid and anything else the host serves loads here."
                          : "No printer is connected yet. Type its address above, or use File ▸ Send to printer… and this tab then opens its page."
                    font.family: Type.family
                    font.pixelSize: Type.sizeSmall
                    color: Colors.textTertiary
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Metrics.sm

                    Button {
                        text: "Mainsail"
                        enabled: printerLink.configured
                        onClicked: root.connect(root.normalized(printerLink.host))
                    }

                    Button {
                        text: "Fluid"
                        enabled: printerLink.configured
                        onClicked: root.connect(root.normalized(printerLink.host + "/?_fl=fluid"))
                    }
                }
            }
        }
    }
}
