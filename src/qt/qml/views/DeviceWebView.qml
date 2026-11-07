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
    /// Emitted when a load finishes. `reason` is empty on success. The caller has
    /// no QtWebEngine import of its own -- it never loads this unless the tab is
    /// open -- so the status enum stays on this side of the boundary.
    signal loadEnded(bool ok, string reason)

    backgroundColor: surfaceColor

    onLoadingChanged: (request) => {
        if (request.status === WebEngineView.LoadStartedStatus) {
            loadingStarted()
            return
        }
        loadEnded(request.status !== WebEngineView.LoadFailedStatus,
                  request.status === WebEngineView.LoadFailedStatus ? request.errorString : "")
    }
}
