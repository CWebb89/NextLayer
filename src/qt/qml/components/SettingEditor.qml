pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "../theme"
import "."

// Editor for one engine setting, chosen from the definition's own type and
// gui_type the way OptionsGroup::build_field does it: gui_type first, then the
// value type. Everything the FFF definitions use is covered, so a setting never
// falls back to a raw string field unless the engine has no better shape for it.
// The editor writes through the settings model and never assigns `item`, so a
// reload of the resolved config re-renders every field.
Item {
    id: root

    property var item: ({})
    property string tabKey: ""

    // An editor can hand its value to the caller instead of the settings panel:
    // a per-object override is stored on the object, not in the preset, and it
    // must not mark the preset dirty.
    property bool externalWrite: false
    signal valueEdited(string name, var value)

    // bedShape, points, color, substitutions, textList, bedShape...
    signal requestDialog(string kind)

    readonly property string gui: root.item.guiType === undefined || root.item.guiType === null
                                  ? "" : String(root.item.guiType)
    readonly property string kind: root.item.type === undefined || root.item.type === null
                                   ? "" : String(root.item.type)
    readonly property var value: root.item.value
    readonly property bool isOpenEnum: root.gui === "i_enum_open" || root.gui === "f_enum_open"
                                       || root.gui === "s_enum_open"
    readonly property bool isSequence: root.isSequenceValue(root.value)
    readonly property bool isVec2: root.kind === "Float" && root.isSequence
                                   && root.value.length === 2 && root.gui !== "points"
    readonly property bool isNumeric: root.kind === "Float" || root.kind === "Int"
                                      || root.kind === "Percentage" || root.isFloatOrPercent
    readonly property bool isFloatOrPercent: root.kind === "FloatOrPercentage"
                                             || root.gui === "unitOrPercentage"
    readonly property bool isTextList: root.gui === "textfields" || root.kind === "Strings"
    readonly property bool isMultiline: root.item.multiline === true
    readonly property bool isPercentValue: root.item.percent === true
    readonly property bool isSpecial: root.gui === "points" || root.gui === "bedShape"
                                      || root.gui === "color" || root.gui === "file"
                                      || root.gui === "substitutions" || root.gui === "ramming_params"
                                      || root.gui === "extruder_selection"

    readonly property real lower: root.kind === "Percentage" ? 0
                                 : root.item.min !== undefined && root.item.min !== null
                                   ? Number(root.item.min) : -1000000
    readonly property real upper: root.kind === "Percentage" ? 100
                                 : root.item.max !== undefined && root.item.max !== null
                                   ? Number(root.item.max) : 1000000
    readonly property int decimals: root.kind === "Int" ? 0 : 2
    readonly property string units: root.kind === "Percentage" || root.isPercentValue
                                    ? "%"
                                    : root.item.units !== undefined && root.item.units !== null
                                      && root.item.units.length > 0 ? String(root.item.units[0]) : ""

    /// A millimetre quantity. Everything stored is millimetres; the field shows
    /// the user's system and converts back on write, so no value on disk ever
    /// depends on the preference.
    readonly property bool isLength: root.units === "mm"

    readonly property real lengthScale: root.isLength ? Units.perMillimetre : 1

    implicitHeight: Metrics.controlHeight

    // Engine values arrive as QVariantList. A flat one converts to a JS array, a
    // nested one (bed_shape, extruder_offset) converts to an array-like object,
    // so both are tested by length rather than Array.isArray.
    function isSequenceValue(v) {
        return v !== null && v !== undefined && typeof v === "object"
                && typeof v.length === "number"
    }

    function numericValue(v) {
        const raw = v !== null && typeof v === "object" ? Number(v.value) : Number(v)
        return root.isLength ? Units.fromMillimetres(raw) : raw
    }

    function storedValue(value) {
        return root.isLength ? Units.toMillimetres(value) : value
    }

    function listText(v) {
        if (!root.isSequenceValue(v))
            return v === undefined || v === null ? "" : String(v)
        return v.join(", ")
    }

    function write(value) {
        if (typeof value === "number")
            value = root.storedValue(value)
        if (root.externalWrite) {
            root.valueEdited(root.item.name, value)
            return
        }
        settingsModel.setValue(root.item.name, value)
    }

    function writeFloatOrPercent(number) {
        root.write({
            value: root.isPercentValue ? number : root.storedValue(number),
            is_percent: root.isPercentValue
        })
    }

    function writeList(text) {
        const parts = String(text).split(",")
        const values = []
        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i].trim()
            if (part.length === 0)
                continue
            values.push(root.kind === "Int" ? parseInt(part) : part)
        }
        root.write(values)
    }

    TextField {
        id: pathField
        anchors.fill: parent
        visible: root.gui === "file"
        text: root.value === undefined || root.value === null ? "" : String(root.value)
        rightPadding: browseButton.width
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: Colors.textPrimary
        selectByMouse: true
        onEditingFinished: root.write(pathField.text)

        background: Rectangle {
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: pathField.activeFocus ? Colors.primary : Colors.outlineVariant
        }
    }

    ToolButton {
        id: browseButton
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.controlHeight
        height: Metrics.controlHeight
        visible: root.gui === "file"
        icon: "folder_open"
        hint: "Choose a file"
        onClicked: fileDialog.open()
    }

    FileDialog {
        id: fileDialog
        title: root.item.label !== undefined ? String(root.item.label) : "Choose a file"
        onAccepted: root.write(fileDialog.selectedFile.toString().replace(/^file:\/\//, ""))
    }

    Rectangle {
        id: colourSwatch
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Metrics.controlHeight
        height: Metrics.controlHeight
        radius: Metrics.radiusSmall
        visible: root.gui === "color"
        color: root.value === undefined || root.value === null || String(root.value).length === 0
               ? Colors.surfaceContainerHigh : String(root.value)
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.requestDialog("color")
        }
    }

    TextField {
        id: colourField
        anchors.left: colourSwatch.visible ? colourSwatch.right : parent.left
        anchors.leftMargin: colourSwatch.visible ? Metrics.xs : 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: Metrics.controlHeight
        visible: root.gui === "color"
        text: root.value === undefined || root.value === null ? "" : String(root.value)
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: Colors.textPrimary
        selectByMouse: true
        onEditingFinished: root.write(colourField.text)

        background: Rectangle {
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: colourField.activeFocus ? Colors.primary : Colors.outlineVariant
        }
    }

    Toggle {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: root.kind === "Boolean"
        checked: Boolean(root.value)
        onToggled: (checked) => root.write(checked)
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.kind === "Enum" && !root.isOpenEnum

        ComboBox {
            width: parent.width
            elideMode: Text.ElideRight
            choices: root.item.choices === undefined ? [] : root.item.choices
            currentValue: root.value
            onSelected: (value) => root.write(value)
        }
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.isOpenEnum

        TextField {
            id: openEnumField
            width: parent.width - openEnumCombo.width - Metrics.xs
            anchors.verticalCenter: parent.verticalCenter
            height: Metrics.controlHeight
            text: root.value === undefined || root.value === null ? "" : String(root.value)
            font.family: Type.family
            font.pixelSize: Type.sizeSmall
            color: Colors.textPrimary
            selectByMouse: true
            onEditingFinished: root.write(openEnumField.text)

            background: Rectangle {
                radius: Metrics.radiusSmall
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: openEnumField.activeFocus ? Colors.primary : Colors.outlineVariant
            }
        }

        ComboBox {
            id: openEnumCombo
            width: Math.max(Metrics.controlHeight * 2, parent.width * 0.4)
            elideMode: Text.ElideRight
            choices: root.item.choices === undefined ? [] : root.item.choices
            currentValue: root.value
            onSelected: (value) => root.write(value)
        }
    }

    NumberField {
        anchors.fill: parent
        visible: root.isNumeric && !root.isVec2 && !root.isFloatOrPercent && !root.isSpecial
                 && !root.isTextList
        value: root.numericValue(root.value)
        from: root.lower
        to: root.upper
        units: root.isLength ? Units.lengthSuffix : root.units
        decimals: root.isLength ? Units.lengthDecimals : root.decimals
        onEdited: (value) => root.write(value)
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.isFloatOrPercent

        NumberField {
            width: parent.width - unitButton.width - Metrics.xs
            value: root.numericValue(root.value)
            from: root.isPercentValue ? 0 : root.lower
            to: root.isPercentValue ? 100 : root.upper
            units: root.isPercentValue ? "%" : Units.lengthSuffix
            decimals: root.isPercentValue ? root.decimals : Units.lengthDecimals
            onEdited: (value) => root.writeFloatOrPercent(value)
        }

        Button {
            id: unitButton
            width: Metrics.controlHeight * 1.6
            height: Metrics.controlHeight
            text: root.isPercentValue ? "%" : "mm"
            hint: "Switch between a percentage and an absolute value"
            onClicked: root.write({ value: root.numericValue(root.value), is_percent: !root.isPercentValue })
        }
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.isVec2

        NumberField {
            width: (parent.width - Metrics.xs) / 2
            value: Number(root.value[0])
            units: "x"
            decimals: root.decimals
            onEdited: (value) => root.write([value, Number(root.value[1])])
        }

        NumberField {
            width: (parent.width - Metrics.xs) / 2
            value: Number(root.value[1])
            units: "y"
            decimals: root.decimals
            onEdited: (value) => root.write([Number(root.value[0]), value])
        }
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.gui === "bedShape"

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - bedShapeButton.width - Metrics.xs
            text: root.isSequence ? root.value.length + " points" : "no shape"
            elide: Text.ElideRight
            font.family: Type.family
            font.pixelSize: Type.sizeSmall
            color: Colors.textSecondary
        }

        Button {
            id: bedShapeButton
            width: Metrics.controlHeight * 2.4
            height: Metrics.controlHeight
            text: "Edit"
            hint: "Edit the bed outline"
            onClicked: root.requestDialog("bedShape")
        }
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.gui === "points" || root.gui === "substitutions"
                 || root.gui === "ramming_params" || root.gui === "extruder_selection"

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - pointsButton.width - Metrics.xs
            text: {
                if (root.gui === "substitutions")
                    return Math.round((root.isSequence ? root.value.length : 0) / 4) + " substitutions"
                if (root.gui === "ramming_params")
                    return root.value === undefined || root.value === null || String(root.value).length === 0
                           ? "none" : "custom"
                if (root.gui === "extruder_selection")
                    return "extruder " + String(root.value)
                return root.isSequence ? root.value.length + " points" : "none"
            }
            elide: Text.ElideRight
            font.family: Type.family
            font.pixelSize: Type.sizeSmall
            color: Colors.textSecondary
        }

        Button {
            id: pointsButton
            width: Metrics.controlHeight * 2.4
            height: Metrics.controlHeight
            text: "Edit"
            visible: root.gui !== "extruder_selection"
            hint: root.gui === "substitutions" ? "Edit G-code substitutions"
                  : root.gui === "ramming_params" ? "Edit ramming parameters"
                  : "Edit the point list"
            onClicked: root.requestDialog(root.gui === "ramming_params" ? "ramming" : root.gui)
        }
    }

    Row {
        anchors.fill: parent
        spacing: Metrics.xs
        visible: root.isTextList

        TextField {
            id: listField
            width: parent.width - listButton.width - Metrics.xs
            anchors.verticalCenter: parent.verticalCenter
            height: Metrics.controlHeight
            text: root.listText(root.value)
            font.family: Type.family
            font.pixelSize: Type.sizeSmall
            color: Colors.textPrimary
            selectByMouse: true
            onEditingFinished: root.writeList(listField.text)

            background: Rectangle {
                radius: Metrics.radiusSmall
                color: Colors.surfaceContainer
                border.width: Metrics.borderWidth
                border.color: listField.activeFocus ? Colors.primary : Colors.outlineVariant
            }
        }

        ToolButton {
            id: listButton
            width: Metrics.controlHeight
            height: Metrics.controlHeight
            icon: "more_horiz"
            hint: "Edit one value per line"
            onClicked: root.requestDialog("textList")
        }
    }

    TextField {
        id: scalarField
        anchors.fill: parent
        visible: !root.gui && root.kind === "String" && !root.isMultiline
        text: root.value === undefined || root.value === null ? "" : String(root.value)
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: Colors.textPrimary
        selectByMouse: true
        onEditingFinished: root.write(scalarField.text)

        background: Rectangle {
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: scalarField.activeFocus ? Colors.primary : Colors.outlineVariant
        }
    }

    TextArea {
        id: multiField
        anchors.fill: parent
        visible: root.kind === "String" && root.isMultiline
        text: root.value === undefined || root.value === null ? "" : String(root.value)
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: Colors.textPrimary
        selectByMouse: true
        wrapMode: TextEdit.WrapAnywhere
        onEditingFinished: root.write(multiField.text)

        background: Rectangle {
            radius: Metrics.radiusSmall
            color: Colors.surfaceContainer
            border.width: Metrics.borderWidth
            border.color: multiField.activeFocus ? Colors.primary : Colors.outlineVariant
        }
    }
}
