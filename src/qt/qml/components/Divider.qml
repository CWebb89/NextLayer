// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Hairline rule. Horizontal by default, because that is how it is used: between
// sections of a panel or a dialog, where the caller anchors it left-to-right or
// asks a layout to fill the width. Toolbar clusters set `vertical: true` and get
// the short upright mark that separates one icon group from the next.
Rectangle {
    property bool vertical: false

    implicitWidth: vertical ? Metrics.hairline : Metrics.xxl * 8
    implicitHeight: vertical ? Metrics.xxl : Metrics.hairline
    width: implicitWidth
    height: implicitHeight
    color: Colors.separator
}
