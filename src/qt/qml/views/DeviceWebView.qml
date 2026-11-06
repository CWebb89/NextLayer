// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma ComponentBehavior: Bound

import QtQuick
import QtWebEngine
import "../theme"

// The printer's own page, in its own file so the Device tab only pays for
// Chromium when it shows it: importing QtWebEngine initialises the whole engine,
// and a headless run never needs that.
WebEngineView {
    property color surfaceColor: Colors.surfaceContainerLow

    signal loadingStarted

    backgroundColor: surfaceColor

    onLoadingChanged: (request) => {
        if (request.status === WebEngineView.LoadStartedStatus)
            loadingStarted()
    }
}
