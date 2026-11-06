pragma Singleton
import QtQuick

// Type scale. The Mono Nerd Font variant draws every icon inside a single
// letter cell, so an icon needs no vertical correction against the text it
// leads: iconInline beside caption/small text, iconLead for nav rows and card
// headers.
QtObject {
    readonly property string family: "JetBrainsMono Nerd Font Mono"
    readonly property int sizeCaption: 14
    readonly property int sizeSmall: 16
    readonly property int sizeBody: 18
    readonly property int sizeTitle: 19
    readonly property int sizeHeading: 22
    readonly property int sizeLarge: 27
    readonly property int sizeMedium: 33
    readonly property int sizeXLarge: 38

    readonly property int iconInline: 14
    readonly property int iconLead: 18
    readonly property int iconStandalone: 22
}
