// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import "../theme"

// Vertical hairline separating toolbar groups, matching the dividers between
// icon clusters.
Rectangle {
    width: Metrics.hairline
    height: Metrics.xxl
    color: Colors.separator
}
