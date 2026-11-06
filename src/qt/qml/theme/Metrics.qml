pragma Singleton
import QtQuick

// Structural spacing and geometry tokens. Card, control and type metrics are
// klipshell's; the window and chrome tokens are this app's layout.
QtObject {
    readonly property int hairline: 1
    readonly property int spacing: 8
    readonly property int sectionGap: 8
    readonly property int panelPadding: 22
    readonly property int panelMargin: 20

    readonly property int radiusSmall: 8
    readonly property int radiusMedium: 13
    readonly property int radiusLarge: 17
    readonly property int borderWidth: 1
    readonly property int cardBorderWidth: 2

    readonly property int controlHeight: 38
    readonly property int rowHeight: 40
    readonly property int controlGap: 8
    readonly property int controlPaddingX: 16
    readonly property int controlPaddingY: 8

    readonly property int xxs: 2
    readonly property int xs: 4
    readonly property int sm: 6
    readonly property int md: 8
    readonly property int lg: 12
    readonly property int xl: 16
    readonly property int xxl: 20
    readonly property int huge: 24

    readonly property int gaugeTrack: 5
    readonly property int gaugeKnob: 19

    readonly property int windowWidth: 1600
    readonly property int windowHeight: 950
    readonly property int railWidth: 44
    readonly property int headerHeight: 56
    readonly property int footerHeight: 64
    readonly property int settingsWidth: 470
    readonly property int drawerWidth: 340
    readonly property int drawerHandleWidth: 18
}
