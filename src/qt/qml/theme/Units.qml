pragma Singleton
import QtQuick

// Display units. The engine, every preset on disk and every project file are
// millimetres -- this is a presentation choice applied at the editor boundary,
// the same place PrusaSlicer converts, so a value never changes meaning when
// the user switches system. Lengths are the only quantity with a unit here;
// percentages, angles, temperatures and speeds pass through untouched.
QtObject {
    property string system: "mm"

    readonly property bool imperial: system === "in" || system === "inch"
    readonly property real perMillimetre: imperial ? 1 / 25.4 : 1
    readonly property string lengthSuffix: imperial ? "in" : "mm"
    readonly property int lengthDecimals: imperial ? 4 : 2

    function fromMillimetres(millimetres) {
        return millimetres * perMillimetre
    }

    function toMillimetres(value) {
        return value / perMillimetre
    }

    function lengthText(millimetres) {
        return fromMillimetres(millimetres).toFixed(lengthDecimals) + " " + lengthSuffix
    }
}
