// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

pragma Singleton
import QtQuick
import NextLayer 1.0

// matugen MD3 palette. The 36 role values are compiled-in defaults taken from
// klipshell's Colors.qml; every derived token is computed from them, so a
// palette handed over by Host (via Palette) recolours the whole app. Hardcoded
// palette hex belongs in this file only.
QtObject {
    id: root

    property color primary: "#ffb597"
    property color primaryText: "#552107"
    property color primaryContainer: "#71361b"
    property color primaryContainerText: "#ffdbcd"
    property color secondary: "#e7beae"
    property color secondaryText: "#442a1f"
    property color secondaryContainer: "#5d4034"
    property color secondaryContainerText: "#ffdbcd"
    property color tertiary: "#d3c78f"
    property color tertiaryText: "#373106"
    property color tertiaryContainer: "#4f471b"
    property color tertiaryContainerText: "#efe3a8"
    property color error: "#ffb4ab"
    property color errorText: "#690005"
    property color errorContainer: "#93000a"
    property color errorContainerText: "#ffdad6"
    property color background: "#1a110e"
    property color backgroundText: "#f1dfd9"
    property color surface: "#1a110e"
    property color surfaceText: "#f1dfd9"
    property color surfaceVariant: "#53433e"
    property color surfaceVariantText: "#d8c2ba"
    property color surfaceDim: "#1a110e"
    property color surfaceBright: "#423733"
    property color surfaceContainerLowest: "#140c09"
    property color surfaceContainerLow: "#231a16"
    property color surfaceContainer: "#271e1a"
    property color surfaceContainerHigh: "#322824"
    property color surfaceContainerHighest: "#3d322e"
    property color outline: "#a08d86"
    property color outlineVariant: "#53433e"
    property color inverseSurface: "#f1dfd9"
    property color inverseOnSurface: "#382e2a"
    property color inversePrimary: "#8e4d30"
    property color shadow: "#000000"
    property color scrim: "#000000"

    readonly property color textPrimary: surfaceText
    readonly property color accent: primary

    // Hierarchy is an explicit lightness ladder, not a hue hint: matugen
    // schemes land primary/secondary/tertiary/onSurface within 1% luminance.
    // textMuted is for disabled and placeholder text only.
    readonly property color textSecondary: surfaceVariantText
    readonly property color textTertiary: Qt.rgba(surfaceVariantText.r,
                                                  surfaceVariantText.g,
                                                  surfaceVariantText.b, 0.70)
    readonly property color textMuted: Qt.rgba(surfaceVariantText.r,
                                               surfaceVariantText.g,
                                               surfaceVariantText.b, 0.42)

    readonly property color edge: outlineVariant
    readonly property color edgeStrong: outline

    readonly property color success: "#4ade80"

    readonly property color selectedFill: Qt.rgba(surfaceText.r, surfaceText.g,
                                                  surfaceText.b, 0.08)
    readonly property color hoverFill: Qt.rgba(surfaceText.r, surfaceText.g,
                                               surfaceText.b, 0.08)
    readonly property color pressedFill: Qt.rgba(surfaceText.r, surfaceText.g,
                                                 surfaceText.b, 0.22)
    readonly property color separator: Qt.rgba(surfaceText.r, surfaceText.g,
                                               surfaceText.b, 0.12)
    readonly property color accentDim: Qt.rgba(primary.r, primary.g, primary.b, 0.16)
    readonly property color errorDim: Qt.rgba(error.r, error.g, error.b, 0.16)
    readonly property color urgent: error

    // Viewport. The 3D scene is three flat tones apart by a fixed ratio, not
    // by palette role: background 0.329, plate 0.2666, plate side 0.133 in its
    // dark theme, with grid lines a tenth brighter than the plate. Reproducing
    // the ratios on the theme's own hue is what makes the viewport read as
    // that while still following matugen.
    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t, 1)
    }

    readonly property color viewportBackground: mix(surfaceVariant, outline, 0.25)
    // The Voron bed is a dark PEI sheet, not a mid grey. A dark theme keeps
    // the plate a fixed step below its background; this steps the theme's own
    // background toward black by the same proportion, so the plate stays the
    // darkest large surface in the scene on any palette.
    readonly property color viewportPlate: mix(viewportBackground, "#000000", 0.38)
    readonly property color viewportPlateSide: mix(viewportBackground, "#000000", 0.62)
    readonly property color viewportGrid: mix(viewportPlate, surfaceVariantText, 0.42)

    readonly property color tempCool: primary
    readonly property color tempWarm: "#f59e0b"
    readonly property color tempHot: "#dc2626"

    signal themeChanged()

    readonly property var roleNames: [
        "primary", "primaryText", "primaryContainer", "primaryContainerText",
        "secondary", "secondaryText", "secondaryContainer", "secondaryContainerText",
        "tertiary", "tertiaryText", "tertiaryContainer", "tertiaryContainerText",
        "error", "errorText", "errorContainer", "errorContainerText",
        "background", "backgroundText", "surface", "surfaceText",
        "surfaceVariant", "surfaceVariantText", "surfaceDim", "surfaceBright",
        "surfaceContainerLowest", "surfaceContainerLow", "surfaceContainer",
        "surfaceContainerHigh", "surfaceContainerHighest",
        "outline", "outlineVariant", "inverseSurface", "inverseOnSurface",
        "inversePrimary", "shadow", "scrim"
    ]

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    function applyRoles(map) {
        if (!map)
            return
        for (var i = 0; i < root.roleNames.length; ++i) {
            var key = root.roleNames[i]
            var value = map[key]
            if (value === undefined || value === null || value === "")
                continue
            root[key] = value
        }
        root.themeChanged()
    }

    function loadFromHost() {
        root.applyRoles(Host.palette)
    }

    Component.onCompleted: root.loadFromHost()
}
