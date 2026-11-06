// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

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
