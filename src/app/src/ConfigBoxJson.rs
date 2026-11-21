//! The settings a config box overrides, and the overrides a project file states as
//! text, in the shapes this application's per-object settings map stores.
//!
//! Ported from `src/qt/src/ConfigBoxJson.cpp`. Three answers live here and nowhere
//! else: which shapes a `ConfigValue` becomes in a settings map, which shape a
//! printed value is read back as from the setting's own definition, and whether this
//! engine lets a setting be overridden per object.
//!
//! A percentage leaves as the `{value, is_percent}` object `load_box` reads back,
//! never as a bare number: `40%` printed as `40` is a different setting, not a
//! rounding. That is why the value record this module returns is not
//! `SettingValueRecord` — that one is the *panel's* vocabulary, where a percentage is
//! a plain number and the editor decides the unit. Two vocabularies, two records,
//! because collapsing them changes the file.
//!
//! Nothing here reads a name to decide a type. A value's type is its definition's,
//! and its overridability is the definition's `location` and `overrides_in` set,
//! carried across the bridge as the engine's own `FDMConfigLocation` numbers.

use crate::bridge::{
    ObjectSettingInput, ObjectSettingRecord, ObjectSettingsRead, SettingDefinitionRecord,
    SettingEnumValueRecord, SettingValueRecord,
};

/// `Slic3r::Domain::FDMConfigLocation::Object`, as a definition record carries it.
/// `0` in that field is the record's answer for a location that is not an FDM one,
/// so a setting with a non-FDM location reads as "not Object" and is decided by
/// `overrides_in` alone.
const LOCATION_OBJECT: u8 = 6;

/// `SettingValueRecord::kind`, the vocabulary `src/app/src/SettingsModel.rs` reads.
const SETTING_BOOLEAN: u8 = 1;
const SETTING_INTEGER: u8 = 2;
const SETTING_NUMBER: u8 = 3;
const SETTING_TEXT: u8 = 4;
const SETTING_LIST: u8 = 5;
const SETTING_PERCENT: u8 = 6;
const SETTING_FLOAT_OR_PERCENT: u8 = 7;
const SETTING_ENUM: u8 = 8;

/// `SettingValueRecord::items`, the list element shapes this module distinguishes.
const ITEMS_NUMBERS: u8 = 0;
const ITEMS_TEXTS: u8 = 1;
const ITEMS_INTEGERS: u8 = 3;
const ITEMS_FLAGS: u8 = 4;
const ITEMS_ENUM_SPELLINGS: u8 = 8;

/// `ObjectSettingRecord::kind`: what a `QVariant` in the settings map is.
const APP_TEXT: u8 = 0;
const APP_BOOLEAN: u8 = 1;
const APP_INTEGER: u8 = 2;
const APP_NUMBER: u8 = 3;
const APP_PERCENT: u8 = 4;
const APP_FLOAT_OR_PERCENT: u8 = 5;
const APP_TEXTS: u8 = 6;
const APP_INTEGERS: u8 = 7;
const APP_NUMBERS: u8 = 8;
const APP_FLAGS: u8 = 9;

/// One config value between the engine's shapes and the settings map's.
#[derive(Clone, Debug, PartialEq)]
enum Shape {
    Boolean(bool),
    Integer(i64),
    Number(f64),
    Text(String),
    Percent(f64),
    FloatOrPercent { value: f64, percent: bool },
    Texts(Vec<String>),
    Integers(Vec<i64>),
    Numbers(Vec<f64>),
    Flags(Vec<u8>),
}

/// The settings `box` overrides, in the order the box names them.
///
/// A box carries every setting it knows, defaults in one list and the ones that
/// were set in another. Only the second is an override: writing the whole box back
/// turns a default into a decision and pins it against the preset it came from.
///
/// Every value whose type this reader does not represent is named in `unreadable`
/// rather than dropped, so a caller has something to report.
pub fn overridden_settings(items: &[ObjectSettingInput]) -> ObjectSettingsRead {
    let mut read = empty_read();
    for item in items {
        match shape_of_setting(&item.value) {
            Some(shape) => read.settings.push(record_of(item.name.as_str(), shape)),
            None => read.unreadable.push(item.name.clone()),
        }
    }
    read
}

/// The same, for the key/value lines a project file writes.
///
/// `names` and `values` are parallel and in the order the caller iterated its
/// record, so the names that come back in `unreadable` are in that order too. A key
/// this build does not know, or a value its own definition cannot accept, is named
/// in `unreadable`; nothing is guessed and nothing is dropped.
pub fn settings_from_text(
    names: &[String],
    values: &[String],
    definitions: &[SettingDefinitionRecord],
) -> ObjectSettingsRead {
    let mut read = empty_read();
    for (name, text) in names.iter().zip(values.iter()) {
        let shape = definition_named(definitions, name.as_str())
            .and_then(|definition| shape_from_text(text.as_str(), definition));
        match shape {
            Some(shape) => read.settings.push(record_of(name.as_str(), shape)),
            None => read.unreadable.push(name.clone()),
        }
    }
    read
}

/// Whether this engine lets `name` be overridden per object.
///
/// An object's settings box holds the settings that live there and the ones it may
/// override; anything else belongs to the print or the machine, and the engine
/// refuses it on an object. So the answer is the definition's own `location` or its
/// `overrides_in` set — a bit set over locations, distinct from the single location
/// the setting lives at, and the only one of the two that makes
/// `first_layer_height` a print setting an object may not carry.
pub fn overrides_at_object(name: &str, definitions: &[SettingDefinitionRecord]) -> bool {
    let Some(definition) = definition_named(definitions, name) else {
        return false;
    };
    definition.location == LOCATION_OBJECT || definition.overrides_in.contains(&LOCATION_OBJECT)
}

fn definition_named<'a>(
    definitions: &'a [SettingDefinitionRecord],
    name: &str,
) -> Option<&'a SettingDefinitionRecord> {
    definitions
        .iter()
        .find(|definition| definition.name.as_str() == name)
}

fn empty_read() -> ObjectSettingsRead {
    ObjectSettingsRead {
        settings: Vec::new(),
        unreadable: Vec::new(),
    }
}

fn record_of(name: &str, shape: Shape) -> ObjectSettingRecord {
    let mut record = ObjectSettingRecord {
        name: name.to_owned(),
        kind: APP_TEXT,
        truth: false,
        integer: 0,
        number: 0.0,
        text: String::new(),
        integers: Vec::new(),
        numbers: Vec::new(),
        texts: Vec::new(),
        flags: Vec::new(),
    };
    match shape {
        Shape::Boolean(flag) => {
            record.kind = APP_BOOLEAN;
            record.truth = flag;
        }
        Shape::Integer(value) => {
            record.kind = APP_INTEGER;
            record.integer = value;
        }
        Shape::Number(value) => {
            record.kind = APP_NUMBER;
            record.number = value;
        }
        Shape::Text(text) => {
            record.kind = APP_TEXT;
            record.text = text;
        }
        Shape::Percent(value) => {
            record.kind = APP_PERCENT;
            record.number = value;
        }
        Shape::FloatOrPercent { value, percent } => {
            record.kind = APP_FLOAT_OR_PERCENT;
            record.number = value;
            record.truth = percent;
        }
        Shape::Texts(texts) => {
            record.kind = APP_TEXTS;
            record.texts = texts;
        }
        Shape::Integers(integers) => {
            record.kind = APP_INTEGERS;
            record.integers = integers;
        }
        Shape::Numbers(numbers) => {
            record.kind = APP_NUMBERS;
            record.numbers = numbers;
        }
        Shape::Flags(flags) => {
            record.kind = APP_FLAGS;
            record.flags = flags;
        }
    }
    record
}

/// One engine value as the settings map stores it.
///
/// What has no shape here is `None`: a point, a list of points, a list of
/// float-or-percentages, a list of percentages, a list of optional integers, and an
/// optional integer that holds nothing. A percentage and a float-or-percentage do
/// have a shape — two shapes, because their `is_percent` differs and one of them can
/// never be `false`.
fn shape_of_setting(value: &SettingValueRecord) -> Option<Shape> {
    match value.kind {
        SETTING_BOOLEAN => Some(Shape::Boolean(value.truth)),
        SETTING_INTEGER => Some(Shape::Integer(value.integer)),
        SETTING_NUMBER => Some(Shape::Number(value.number)),
        SETTING_TEXT => Some(Shape::Text(value.text.clone())),
        SETTING_PERCENT => Some(Shape::Percent(value.number)),
        SETTING_FLOAT_OR_PERCENT => Some(Shape::FloatOrPercent {
            value: value.number,
            percent: value.truth,
        }),
        SETTING_ENUM => Some(Shape::Text(value.text.clone())),
        SETTING_LIST => list_of_setting(value),
        _ => None,
    }
}

fn list_of_setting(value: &SettingValueRecord) -> Option<Shape> {
    match value.items {
        ITEMS_NUMBERS => Some(Shape::Numbers(value.numbers.iter().copied().collect())),
        ITEMS_TEXTS | ITEMS_ENUM_SPELLINGS => {
            Some(Shape::Texts(value.texts.iter().cloned().collect()))
        }
        ITEMS_INTEGERS => Some(Shape::Integers(
            value.numbers.iter().map(|number| *number as i64).collect(),
        )),
        ITEMS_FLAGS => Some(Shape::Flags(
            value.flags.iter().map(|flag| u8::from(*flag != 0)).collect(),
        )),
        _ => None,
    }
}

/// One value as the map stores it, recovered from the text a project file printed.
///
/// The setting's own definition decides the type: a box refuses a value that does
/// not match, so guessing here would only move the failure. A definition whose type
/// has no text form — a point, a list of points, an optional integer, a list of
/// float-or-percentages, a list of percentages — refuses every value it is handed.
///
/// The enum vocabulary is the definition's `enum_values`, not part of the default
/// value: a name the definition does not list is not a value.
fn shape_from_text(text: &str, definition: &SettingDefinitionRecord) -> Option<Shape> {
    match definition.default_value.kind {
        SETTING_BOOLEAN => Some(Shape::Boolean(flag_from_text(text))),
        SETTING_INTEGER => text
            .trim()
            .parse::<i32>()
            .ok()
            .map(|number| Shape::Integer(i64::from(number))),
        SETTING_NUMBER => text.trim().parse::<f64>().ok().map(Shape::Number),
        SETTING_TEXT => Some(Shape::Text(text.to_owned())),
        SETTING_ENUM => enum_from_text(text, &definition.enum_values),
        SETTING_PERCENT => percent_from_text(text).map(Shape::Percent),
        SETTING_FLOAT_OR_PERCENT => percent_from_text(text)
            .map(|value| Shape::FloatOrPercent {
                value,
                percent: text.ends_with('%'),
            }),
        SETTING_LIST => list_from_text(text, &definition.default_value),
        _ => None,
    }
}

/// The definition owns the vocabulary: a name it does not list is not a value, and
/// a box would only fail later with less to go on.
fn enum_from_text(text: &str, spellings: &[SettingEnumValueRecord]) -> Option<Shape> {
    spellings
        .iter()
        .any(|entry| entry.serialized.as_str() == text)
        .then(|| Shape::Text(text.to_owned()))
}

/// A printed percentage is the number and a marker; every `%` is taken out of it,
/// wherever it sits, because that is what a writer that states a share puts beside
/// the digits.
fn percent_from_text(text: &str) -> Option<f64> {
    let digits: String = text
        .chars()
        .filter(|character| *character != '%')
        .collect();
    digits.trim().parse::<f64>().ok()
}

fn flag_from_text(text: &str) -> bool {
    text == "1" || text.eq_ignore_ascii_case("true")
}

/// One value written for all the tools it could apply to: a setting that is
/// per-extruder here is a single number in a project from another slicer, and a
/// semicolon-separated list is honoured as written. An empty text is no value at
/// all, and one token that will not convert refuses the whole setting rather than
/// becoming a shorter list.
fn list_from_text(text: &str, definition: &SettingValueRecord) -> Option<Shape> {
    let parts: Vec<&str> = text
        .split(';')
        .filter(|part| !part.is_empty())
        .collect();
    if parts.is_empty() {
        return None;
    }

    let mut numbers: Vec<f64> = Vec::new();
    let mut integers: Vec<i64> = Vec::new();
    let mut texts: Vec<String> = Vec::new();
    let mut flags: Vec<u8> = Vec::new();
    for part in parts {
        let token = part.trim();
        match definition.items {
            ITEMS_NUMBERS => numbers.push(token.parse::<f64>().ok()?),
            ITEMS_INTEGERS => integers.push(i64::from(token.parse::<i32>().ok()?)),
            ITEMS_FLAGS => flags.push(u8::from(flag_from_text(token))),
            ITEMS_TEXTS => texts.push(token.to_owned()),
            _ => return None,
        }
    }

    Some(match definition.items {
        ITEMS_NUMBERS => Shape::Numbers(numbers),
        ITEMS_INTEGERS => Shape::Integers(integers),
        ITEMS_FLAGS => Shape::Flags(flags),
        _ => Shape::Texts(texts),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn setting(kind: u8, items: u8) -> SettingValueRecord {
        SettingValueRecord {
            kind,
            truth: false,
            integer: 0,
            number: 0.0,
            text: String::new(),
            items,
            numbers: Vec::new(),
            texts: Vec::new(),
            flags: Vec::new(),
        }
    }

    fn definition(
        name: &str,
        location: u8,
        overrides_in: &[u8],
        default_value: SettingValueRecord,
    ) -> SettingDefinitionRecord {
        SettingDefinitionRecord {
            name: name.to_owned(),
            label: String::new(),
            full_label: String::new(),
            tooltip: String::new(),
            location,
            overrides_in: overrides_in.to_vec(),
            category: 0,
            option_group: 0,
            category_title: String::new(),
            option_group_title: String::new(),
            order: 0,
            gui_type: String::new(),
            units: Vec::new(),
            has_min: false,
            min: 0.0,
            has_max: false,
            max: 0.0,
            multiline: false,
            full_width: false,
            enum_values: Vec::new(),
            default_value,
        }
    }

    fn input(name: &str, value: SettingValueRecord) -> ObjectSettingInput {
        ObjectSettingInput {
            name: name.to_owned(),
            value,
        }
    }

    fn names_of(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| (*value).to_owned()).collect()
    }

    #[test]
    fn a_percentage_leaves_as_the_pair_the_engine_reads_back() {
        let percent = definition("fill_density", 3, &[6], setting(SETTING_PERCENT, 0));
        let either = definition(
            "perimeter_speed",
            3,
            &[6],
            setting(SETTING_FLOAT_OR_PERCENT, 0),
        );
        let definitions = [percent, either];

        let read = settings_from_text(
            &names_of(&["fill_density", "perimeter_speed"]),
            &names_of(&["15%", "150"]),
            &definitions,
        );

        assert!(read.unreadable.is_empty());
        assert_eq!(read.settings[0].kind, APP_PERCENT);
        assert_eq!(read.settings[0].number, 15.0);
        assert_eq!(read.settings[1].kind, APP_FLOAT_OR_PERCENT);
        assert_eq!(read.settings[1].number, 150.0);
        assert!(!read.settings[1].truth);
    }

    #[test]
    fn a_trailing_percent_is_not_what_makes_a_share_one() {
        let either = definition(
            "perimeter_speed",
            3,
            &[6],
            setting(SETTING_FLOAT_OR_PERCENT, 0),
        );
        let read = settings_from_text(
            &names_of(&["perimeter_speed"]),
            &names_of(&["50 % "]),
            &[either],
        );
        assert_eq!(read.settings[0].number, 50.0);
        assert!(!read.settings[0].truth);
    }

    #[test]
    fn a_name_nothing_defines_and_a_value_its_type_refuses_are_both_named() {
        let number = definition("layer_height", 3, &[6], setting(SETTING_NUMBER, 0));
        let read = settings_from_text(
            &names_of(&["made_up_setting", "layer_height", "perimeters"]),
            &names_of(&["1", "0.3", "3"]),
            &[number],
        );

        assert_eq!(read.settings.len(), 1);
        assert_eq!(read.settings[0].name, "layer_height");
        assert_eq!(read.settings[0].number, 0.3);
        assert_eq!(
            read.unreadable,
            names_of(&["made_up_setting", "perimeters"])
        );
    }

    #[test]
    fn an_enum_value_is_the_name_the_definition_lists() {
        let mut enum_value = setting(SETTING_ENUM, 0);
        enum_value.text = "gyroid".to_owned();
        let mut definition = definition("fill_pattern", 3, &[6], enum_value);
        definition.enum_values.push(SettingEnumValueRecord {
            serialized: "gyroid".to_owned(),
            label: "Gyroid".to_owned(),
            number: 2,
        });

        let read = settings_from_text(
            &names_of(&["fill_pattern", "fill_pattern"]),
            &names_of(&["gyroid", "Gyroid"]),
            std::slice::from_ref(&definition),
        );

        assert_eq!(read.settings.len(), 1);
        assert_eq!(read.settings[0].text, "gyroid");
        assert_eq!(read.unreadable, names_of(&["fill_pattern"]));
    }

    #[test]
    fn a_type_with_no_shape_here_is_named_rather_than_dropped() {
        let bed = definition("bed_shape", 1, &[], setting(SETTING_LIST, 2));
        let unset = definition("seam_gap", 3, &[6], setting(0, 0));
        let read = settings_from_text(
            &names_of(&["bed_shape", "seam_gap"]),
            &names_of(&["0x0,200x0", "1"]),
            &[bed, unset],
        );

        assert!(read.settings.is_empty());
        assert_eq!(read.unreadable, names_of(&["bed_shape", "seam_gap"]));
    }

    #[test]
    fn a_box_override_keeps_the_percentage_pair_and_names_a_point() {
        let mut percent = setting(SETTING_PERCENT, 0);
        percent.number = 40.0;
        let mut either = setting(SETTING_FLOAT_OR_PERCENT, 0);
        either.number = 80.0;
        either.truth = true;
        let mut point = setting(SETTING_LIST, 2);
        point.numbers = vec![1.0, 2.0];

        let read = overridden_settings(&[
            input("fill_density", percent),
            input("perimeter_speed", either),
            input("bed_shape", point),
        ]);

        assert_eq!(read.settings[0].kind, APP_PERCENT);
        assert_eq!(read.settings[0].number, 40.0);
        assert_eq!(read.settings[1].kind, APP_FLOAT_OR_PERCENT);
        assert!(read.settings[1].truth);
        assert_eq!(read.unreadable, names_of(&["bed_shape"]));
    }

    #[test]
    fn overridability_is_the_definition_not_the_name() {
        let print_override = definition("perimeters", 3, &[6, 7, 2], setting(SETTING_INTEGER, 0));
        let print_only = definition("first_layer_height", 3, &[2], setting(SETTING_NUMBER, 0));
        let lives_on_the_object = definition("extruder", 6, &[7], setting(SETTING_INTEGER, 0));
        let definitions = [print_override, print_only, lives_on_the_object];

        assert!(overrides_at_object("perimeters", &definitions));
        assert!(!overrides_at_object("first_layer_height", &definitions));
        assert!(overrides_at_object("extruder", &definitions));
        assert!(!overrides_at_object("made_up_setting", &definitions));
    }
}
