//! The settings the panel shows and the preset writer saves.
//!
//! Ported from `src/qt/src/SettingsModel.cpp`. What is here: the entry table built
//! from the engine's settings definitions, the page and section placement
//! `src/qt/assets/settings-layout.json` gives every entry, the shapes a setting's
//! value takes in this application, the assignment rules that turn a value arriving
//! from QML or from another slicer into one of them, the YAML a preset is written
//! with and the JSON the slice is handed, and the four tree styles this application
//! expresses as engine numbers.
//!
//! What is not here, and cannot be: the `QObject`. `SettingsModel` is a QML
//! singleton and Qt's meta-object system is a C++ ABI, so `SettingsModel.hpp` keeps
//! the class, its one signal and one thin adapter per invokable; the model below is
//! that shell's whole state.
//!
//! Three things cross this boundary as data rather than as engine types, because a
//! `QVariant` and a `Slic3r::Domain::ConfigValue` cannot cross it. A definition
//! arrives as a `SettingDefinitionRecord`, a value the panel reads as a
//! `SettingValueRecord`, and a value *entering* as a `SettingInputRecord`, which
//! carries the answers `QVariant`'s own converters gave — so no conversion of a
//! `QVariant` is reimplemented here, and none of the engine's rounding, trimming or
//! `toString` rules are guessed at.
//!
//! The layout and the base config arrive as the bytes of two Qt resources. A Rust
//! module has no handle on a qrc, and the shell reading them keeps resource
//! registration where Qt puts it; `layout_loaded` then reports the one case the
//! C++ swallowed, because a missing layout document does not fail — it silently
//! regroups every setting under the engine's own categories.

use std::collections::{BTreeSet, HashMap};

use serde_json::{Map, Value as Json};

use crate::bridge::{
    SettingChangeRecord, SettingChoiceRecord, SettingDefinitionRecord, SettingElementRecord,
    SettingGroupRecord, SettingInputRecord, SettingItemRecord, SettingModifiedRecord,
    SettingScalarRecord, SettingSubtabRecord, SettingTabRecord, SettingValueRecord,
};

/// The three tabs, in the order the sidebar shows them, with the engine location
/// each one is.
const TABS: [(&str, &str, u8); 3] = [
    ("process", "Process", LOCATION_PRINT),
    ("filament", "Filament", LOCATION_FILAMENT),
    ("printer", "Printer", LOCATION_PRINTER),
];

/// `Slic3r::Domain::FDMConfigLocation`, as a definition record carries it.
const LOCATION_PRINTER: u8 = 1;
const LOCATION_PRINT: u8 = 3;
const LOCATION_FILAMENT: u8 = 4;
const LOCATION_OBJECT: u8 = 6;

/// `ConfigItemDef::Category`: the two the sidebar treats specially, and the two
/// whose sub-tabs are titled for a tool changer rather than for a single-nozzle
/// colour MMU.
const CATEGORY_UNKNOWN: i32 = 0;
const CATEGORY_HIDDEN: i32 = 1;
const CATEGORY_PRINT_MULTI_MATERIAL: i32 = 108;
const CATEGORY_FILAMENT_MULTI_MATERIAL: i32 = 203;

/// `ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports`.
const GROUP_ORGANIC_SUPPORTS: i32 = 504;

/// The engine's serialized spelling of the one support style this application
/// builds four shapes on.
const ORGANIC_STYLE: &str = "organic";

/// The tolerance `ConfigValue::operator==` compares doubles with, from
/// `Domain::fuzzy_compare`: `|a - b| <= max(DBL_EPSILON, 100 * DBL_EPSILON * max(|a|, |b|))`.
const FUZZY_RELATIVE: f64 = f64::EPSILON * 100.0;
const FUZZY_ABSOLUTE: f64 = f64::EPSILON;

/// The tolerance a tree shape's numbers are matched at, which is a different and
/// much looser one than the engine's value equality.
const SHAPE_TOLERANCE: f64 = 1e-6;

/// One setting's value, in the shapes the engine's config definitions produce.
#[derive(Clone, Debug)]
enum Value {
    Boolean(bool),
    Booleans(Vec<bool>),
    Integer(i64),
    Integers(Vec<i64>),
    OptionalIntegers(Vec<Option<i64>>),
    Number(f64),
    Numbers(Vec<f64>),
    Text(String),
    Texts(Vec<String>),
    Percent(f64),
    Percents(Vec<f64>),
    FloatOrPercent {
        value: f64,
        percent: bool,
    },
    FloatOrPercents(Vec<(f64, bool)>),
    Points(Vec<(f64, f64)>),
    Enum(String),
    Enums(Vec<String>),
    /// A `ConfigValue` holding a type outside the eighteen `ConfigValue` declares.
    /// Unreachable from the engine's own definitions, and the answer the C++ gives
    /// everywhere it meets one: no type name beyond `String`, no `QVariant`, a JSON
    /// null, no YAML, and no assignment.
    Opaque,
}

/// One value of an engine enum, for the choices list and for matching a spelling or
/// a number back to the serialized spelling a preset file carries.
#[derive(Clone, Debug)]
struct EnumValue {
    serialized: String,
    label: String,
    number: i64,
}

/// A setting's definition: everything the panel needs that is not the value.
///
/// The two titles are the engine's own translations, carried rather than
/// reproduced: `tools/settings_layout.py` matches the layout map on them, so a
/// table of them here would be a second copy of the engine's English and would rot
/// against the map the moment one was renamed.
#[derive(Clone, Debug)]
struct Definition {
    name: String,
    label: String,
    full_label: String,
    tooltip: String,
    location: u8,
    overrides_in: Vec<u8>,
    category: i32,
    option_group: i32,
    category_title: String,
    option_group_title: String,
    order: i32,
    gui_type: String,
    units: Vec<String>,
    has_min: bool,
    min: f64,
    has_max: bool,
    max: f64,
    multiline: bool,
    full_width: bool,
    enum_values: Vec<EnumValue>,
    default: Value,
}

/// Where the layout map puts one setting.
#[derive(Clone, Debug)]
struct Placement {
    page_key: String,
    page_title: String,
    section_key: String,
    section_title: String,
    page_order: i32,
    section_order: i32,
    position: i32,
}

/// One row of the panel's table.
struct Entry {
    definition: Definition,
    default_value: Value,
    current_value: Value,
    name: String,
    label: String,
    tab_key: String,
    subtab_key: String,
    subtab_title: String,
    engine_subtab_title: String,
    engine_group_title: String,
    type_name: String,
    gui_type: String,
    group_key: String,
    group_title: String,
    group: i32,
    sort_category: i32,
    sort_position: i32,
    modified: bool,
}

impl Entry {
    fn location(&self) -> u8 {
        self.definition.location
    }

    /// Whether the setting holds a list, which decides whether one value arriving
    /// from another slicer becomes a one-element list.
    fn holds_list(&self) -> bool {
        holds_list(&self.current_value)
    }
}

/// One shape of the organic support generator: the engine's own style plus the
/// numbers this application calls a shape. `organic` is the generator untouched.
struct TreeProfile {
    key: &'static str,
    label: &'static str,
    knobs: &'static [(&'static str, f64)],
}

const TREE_PROFILES: [TreeProfile; 4] = [
    TreeProfile {
        key: "organic",
        label: "Tree",
        knobs: &[],
    },
    TreeProfile {
        key: "tree_slim",
        label: "Tree slim",
        knobs: &[
            ("support_tree_branch_diameter", 1.2),
            ("support_tree_branch_diameter_angle", 2.0),
            ("support_tree_tip_diameter", 0.5),
            ("support_tree_top_rate", 20.0),
            ("support_tree_angle", 45.0),
            ("support_tree_angle_slow", 30.0),
            ("support_tree_branch_distance", 1.5),
        ],
    },
    TreeProfile {
        key: "tree_strong",
        label: "Tree strong",
        knobs: &[
            ("support_tree_branch_diameter", 3.5),
            ("support_tree_branch_diameter_angle", 10.0),
            ("support_tree_tip_diameter", 1.4),
            ("support_tree_top_rate", 35.0),
            ("support_tree_angle", 30.0),
            ("support_tree_angle_slow", 20.0),
            ("support_tree_branch_distance", 0.8),
        ],
    },
    TreeProfile {
        key: "tree_hybrid",
        label: "Tree hybrid",
        knobs: &[
            ("support_tree_branch_diameter", 2.5),
            ("support_tree_branch_diameter_angle", 6.0),
            ("support_tree_tip_diameter", 0.6),
            ("support_tree_top_rate", 30.0),
            ("support_tree_angle", 35.0),
            ("support_tree_angle_slow", 25.0),
            ("support_tree_branch_distance", 2.5),
        ],
    },
];

/// Options that only exist for a single-nozzle colour MMU. They are filtered by
/// name rather than by tab, so a future engine that renames a tab cannot quietly
/// bring them back. A tool changer has no use for flushing volumes into a purge
/// block; the wipe-tower multipliers stay, because a changer does share a tower.
const COLOUR_MIXING_OPTIONS: [&str; 5] = [
    "mmu_segmented_region_interlocking_depth",
    "mmu_segmented_region_max_width",
    "single_extruder_multi_material_priming",
    "filament_flush_volume",
    "filament_flush_speed",
];

/// The editor's whole state.
pub struct Model {
    /// Every definition the model was handed, including the ones the panel does not
    /// show: an object may override a print setting the sidebar filters out.
    definitions: Vec<Definition>,
    entries: Vec<Entry>,
    index: HashMap<String, usize>,
    template: Option<Json>,
    seeded_from_resolved: bool,
    unsupported: BTreeSet<String>,
    layout_loaded: bool,
}

/// Builds the editor: one entry per definition the panel shows, placed by the
/// layout document, at the value the shipped base config gives it.
///
/// `layout_json` is `src/qt/assets/settings-layout.json` and `base_config_json` is
/// `src/qt/assets/base-config.json`, both read from the Qt resource by the shell.
pub fn new(
    definitions: &[SettingDefinitionRecord],
    layout_json: &str,
    base_config_json: &str,
) -> Box<Model> {
    let mut placements: HashMap<String, Placement> = HashMap::new();
    let layout_loaded = load_layout(&mut placements, layout_json);

    let mut model = Model {
        definitions: definitions.iter().map(definition_of).collect(),
        entries: Vec::new(),
        index: HashMap::new(),
        template: None,
        seeded_from_resolved: false,
        unsupported: BTreeSet::new(),
        layout_loaded,
    };

    let mut entries: Vec<Entry> = Vec::with_capacity(model.definitions.len());
    for definition in &model.definitions {
        if tab_key_for(definition.location).is_empty() {
            continue;
        }
        if definition.category == CATEGORY_UNKNOWN {
            continue;
        }
        if is_colour_mixing_option(&definition.name) {
            continue;
        }
        entries.push(entry_of(
            definition.clone(),
            placements.get(&definition.name),
        ));
    }
    model.entries = entries;

    model.entries.sort_unstable_by(|lhs, rhs| {
        lhs.sort_category
            .cmp(&rhs.sort_category)
            .then(lhs.group.cmp(&rhs.group))
            .then_with(|| match (lhs.sort_position, rhs.sort_position) {
                (left, right) if left >= 0 && right >= 0 && left != right => left.cmp(&right),
                _ => std::cmp::Ordering::Equal,
            })
            .then(lhs.definition.order.cmp(&rhs.definition.order))
            .then_with(|| lhs.definition.name.cmp(&rhs.definition.name))
    });

    for (position, entry) in model.entries.iter().enumerate() {
        model.index.insert(entry.name.clone(), position);
    }

    model.seed_from_profile(base_config_json);
    Box::new(model)
}

/// The tab tree: three tabs, each with its sub-tabs in the order the sidebar shows
/// them, each carrying how many settings it holds and how many of those are off
/// their preset value.
pub fn tab_list(model: &Model) -> Vec<SettingTabRecord> {
    let mut tabs = Vec::with_capacity(TABS.len());
    for (key, title, _) in TABS {
        let mut subtabs: Vec<SettingSubtabRecord> = Vec::new();
        for entry in &model.entries {
            if entry.tab_key != key {
                continue;
            }
            if subtabs.last().map(|last| last.key.as_str()) != Some(entry.subtab_key.as_str()) {
                subtabs.push(SettingSubtabRecord {
                    key: entry.subtab_key.clone(),
                    title: entry.subtab_title.clone(),
                    count: 0,
                    modified: 0,
                });
            }
            if let Some(last) = subtabs.last_mut() {
                last.count += 1;
                if entry.modified {
                    last.modified += 1;
                }
            }
        }
        let count = subtabs.iter().map(|subtab| subtab.count).sum();
        let modified = subtabs.iter().map(|subtab| subtab.modified).sum();
        tabs.push(SettingTabRecord {
            key: key.to_owned(),
            title: title.to_owned(),
            count,
            modified,
            subtabs,
        });
    }
    tabs
}

/// The settings one page of one tab shows, grouped by section. An empty `tab` or
/// `subtab` matches every one, which is how the filter box searches all of them.
pub fn groups(model: &Model, tab: &str, subtab: &str, filter: &str) -> Vec<SettingGroupRecord> {
    let mut result: Vec<SettingGroupRecord> = Vec::new();
    for entry in &model.entries {
        if !tab.is_empty() && entry.tab_key != tab {
            continue;
        }
        if !subtab.is_empty() && entry.subtab_key != subtab {
            continue;
        }
        if model.unsupported.contains(&entry.name) {
            continue;
        }
        if !matches_filter(&entry.definition, filter) {
            continue;
        }
        let key = format!("{}/{}/{}", entry.tab_key, entry.subtab_key, entry.group_key);
        if result.last().map(|group| group.key != key) != Some(false) {
            result.push(SettingGroupRecord {
                key: key.clone(),
                title: entry.group_title.clone(),
                tab: entry.tab_key.clone(),
                tab_title: tab_title_for(&entry.tab_key),
                subtab: entry.subtab_key.clone(),
                subtab_title: entry.subtab_title.clone(),
                items: Vec::new(),
            });
        }
        if let Some(group) = result.last_mut() {
            group.items.push(item_of(model, entry));
        }
    }
    result
}

/// One setting's value, in the shape the panel's editors index.
pub fn value(model: &Model, name: &str) -> SettingValueRecord {
    match model.find(name) {
        Some(entry) => record_of(&entry.current_value),
        None => invalid_record(),
    }
}

/// The block a setting belongs to: `printer`, `print` or `filament`; empty for a
/// name this engine does not have.
pub fn block_of(model: &Model, name: &str) -> String {
    match model.find(name) {
        Some(entry) => entry.tab_key.clone(),
        None => String::new(),
    }
}

/// True when the setting lives in that block, or the engine lets that block
/// override it.
pub fn can_hold_in(model: &Model, name: &str, block: &str) -> bool {
    let Some(entry) = model.find(name) else {
        return false;
    };
    if entry.tab_key == block {
        return true;
    }
    entry
        .definition
        .overrides_in
        .iter()
        .any(|location| tab_key_for(*location) == block)
}

/// A point-list setting flattened to `[x0, y0, x1, y1, ...]`, empty for one that is
/// not a list of points. A nested list reaches QML as an array-like object whose
/// elements are not plain arrays, so the flat form is what a point editor can index
/// safely; `value()` keeps the nested form the bed loader reads.
pub fn points(model: &Model, name: &str) -> Vec<f64> {
    let Some(entry) = model.find(name) else {
        return Vec::new();
    };
    match &entry.current_value {
        Value::Points(points) => {
            let mut flat = Vec::with_capacity(points.len() * 2);
            for (x, y) in points {
                flat.push(*x);
                flat.push(*y);
            }
            flat
        }
        _ => Vec::new(),
    }
}

/// Writes one setting. A tree shape that is not the engine's own applies the whole
/// shape; the answer says whether the value was accepted and whether the panel has
/// anything new to draw.
pub fn set_value(model: &mut Model, name: &str, input: &SettingInputRecord) -> SettingChangeRecord {
    let Some(position) = model.index.get(name).copied() else {
        return SettingChangeRecord {
            applied: false,
            changed: false,
        };
    };

    if name == "support_material_style" {
        let requested = input.whole.text.clone();
        if requested != ORGANIC_STYLE && tree_profile_for(&requested).is_some() {
            let mut changed = false;
            let applied = model.apply_tree_profile(&requested, &mut changed);
            return SettingChangeRecord { applied, changed };
        }
    }

    let entry = &mut model.entries[position];
    let wrapped;
    let incoming = if entry.holds_list() && !input.is_list {
        wrapped = SettingInputRecord {
            is_list: true,
            whole: scalar_copy(&input.whole),
            first: scalar_copy(&input.whole),
            elements: vec![element_of(&input.whole)],
        };
        &wrapped
    } else {
        input
    };

    let previous = entry.current_value.clone();
    if !assign(&mut entry.current_value, &entry.definition, incoming) {
        return SettingChangeRecord {
            applied: false,
            changed: false,
        };
    }
    entry.modified = !same(&entry.current_value, &entry.default_value);
    let changed = !same(&entry.current_value, &previous);
    SettingChangeRecord {
        applied: true,
        changed,
    }
}

/// Puts one setting back at the value its preset gives it.
pub fn reset(model: &mut Model, name: &str) -> bool {
    let Some(position) = model.index.get(name).copied() else {
        return false;
    };
    let entry = &mut model.entries[position];
    if same(&entry.current_value, &entry.default_value) {
        return false;
    }
    entry.current_value = entry.default_value.clone();
    entry.modified = false;
    true
}

/// Reseeds the whole editor from a resolved preset configuration, the engine's own
/// `--save` output. The document also becomes the template `config_json` overlays,
/// so settings the editor does not surface keep their preset values instead of the
/// shipped base config's.
pub fn seed_from_config_json(model: &mut Model, json: &str) -> bool {
    let Ok(document) = serde_json::from_str::<Json>(json) else {
        return false;
    };
    let Json::Object(root) = &document else {
        return false;
    };
    let Some(Json::Object(configuration)) = root.get("configuration") else {
        return false;
    };
    let configuration = configuration.clone();
    model.template = Some(document);
    model.seeded_from_resolved = true;
    model.seed_from_configuration(&configuration);
    true
}

/// Puts every changed setting of one page back.
pub fn reset_subtab(model: &mut Model, tab: &str, subtab: &str) -> bool {
    let mut any = false;
    for entry in &mut model.entries {
        if entry.tab_key != tab || entry.subtab_key != subtab || !entry.modified {
            continue;
        }
        entry.current_value = entry.default_value.clone();
        entry.modified = false;
        any = true;
    }
    any
}

/// Puts every changed setting of one tab back.
pub fn reset_tab(model: &mut Model, tab: &str) -> bool {
    let mut any = false;
    for entry in &mut model.entries {
        if entry.tab_key != tab || !entry.modified {
            continue;
        }
        entry.current_value = entry.default_value.clone();
        entry.modified = false;
        any = true;
    }
    any
}

/// How many settings of one tab, or of all of them, differ from their preset.
pub fn modified_count(model: &Model, tab: &str) -> i32 {
    let mut count = 0;
    for entry in &model.entries {
        if entry.modified && (tab.is_empty() || entry.tab_key == tab) {
            count += 1;
        }
    }
    count
}

/// Every setting that differs from the preset it came from, with the value it has
/// now and the one it started at.
pub fn modified_values(model: &Model, tab: &str) -> Vec<SettingModifiedRecord> {
    let mut list = Vec::new();
    for entry in &model.entries {
        if !entry.modified || (!tab.is_empty() && entry.tab_key != tab) {
            continue;
        }
        list.push(SettingModifiedRecord {
            name: entry.name.clone(),
            label: entry.label.clone(),
            tab: entry.tab_key.clone(),
            subtab: entry.subtab_title.clone(),
            subtab_key: entry.subtab_key.clone(),
            before: record_of(&entry.default_value),
            after: record_of(&entry.current_value),
        });
    }
    list
}

/// Every print setting a single object may override, in the same shape the panel
/// uses for its editors. A setting the panel does not surface still appears, with
/// no value, because the engine lets an object hold it.
pub fn object_setting_defs(model: &Model, filter: &str) -> Vec<SettingItemRecord> {
    let needle = filter.trim().to_lowercase();
    let mut list = Vec::new();
    for definition in &model.definitions {
        if !definition.overrides_in.contains(&LOCATION_OBJECT) {
            continue;
        }
        let label = if definition.full_label.is_empty() {
            definition.label.clone()
        } else {
            definition.full_label.clone()
        };
        if !needle.is_empty()
            && !definition.name.to_lowercase().contains(&needle)
            && !label.to_lowercase().contains(&needle)
        {
            continue;
        }
        match model.find(&definition.name) {
            Some(entry) => list.push(item_of(model, entry)),
            None => list.push(SettingItemRecord {
                name: definition.name.clone(),
                label,
                full_label: String::new(),
                tooltip: String::new(),
                type_name: "String".to_owned(),
                gui_type: String::new(),
                value: invalid_record(),
                has_min: false,
                min: 0.0,
                has_max: false,
                max: 0.0,
                units: Vec::new(),
                choices: Vec::new(),
                modified: false,
                percent: false,
                multiline: false,
                full_width: false,
                complete: false,
            }),
        }
    }
    list
}

/// The tab tree as text, in the shape `tools/check` reads its counts out of.
pub fn tab_dump(model: &Model) -> String {
    let mut lines = Vec::new();
    let mut total = 0;
    for tab in tab_list(model) {
        lines.push(format!(
            "{} \"{}\": {} sub-tabs, {} settings",
            tab.key,
            tab.title,
            tab.subtabs.len(),
            tab.count
        ));
        total += tab.count;
        for subtab in &tab.subtabs {
            lines.push(format!(
                "  {} \"{}\": {} settings",
                subtab.key, subtab.title, subtab.count
            ));
        }
    }
    lines.push(format!("total: {total} settings"));
    lines.join("\n")
}

/// Declares settings the active engine cannot apply: they are removed from every
/// surface, so no control is shown whose value never reaches the engine.
pub fn set_unsupported(model: &mut Model, names: &[String]) {
    model.unsupported = names.iter().cloned().collect();
}

/// Every setting currently hidden because the engine cannot apply it.
pub fn unsupported(model: &Model) -> Vec<String> {
    model.unsupported.iter().cloned().collect()
}

/// The whole table as JSON: every setting the sidebar can show, in the order it
/// shows it, with the page and section it lands in and the engine's own page and
/// section titles beside them. `tools/settings_layout.py` builds the layout from
/// this, matching on the engine's titles because the layout it writes is what
/// replaces them — reading the displayed titles back would be circular.
pub fn settings_json(model: &Model) -> String {
    let mut rows = Vec::with_capacity(model.entries.len());
    for entry in &model.entries {
        let mut row = Map::new();
        row.insert("tab".to_owned(), Json::from(entry.tab_key.as_str()));
        row.insert("subtab".to_owned(), Json::from(entry.subtab_key.as_str()));
        row.insert(
            "subtabTitle".to_owned(),
            Json::from(entry.subtab_title.as_str()),
        );
        row.insert("group".to_owned(), Json::from(entry.group_key.as_str()));
        row.insert(
            "groupTitle".to_owned(),
            Json::from(entry.group_title.as_str()),
        );
        row.insert(
            "engineSubtabTitle".to_owned(),
            Json::from(entry.engine_subtab_title.as_str()),
        );
        row.insert(
            "engineGroupTitle".to_owned(),
            Json::from(entry.engine_group_title.as_str()),
        );
        row.insert("name".to_owned(), Json::from(entry.name.as_str()));
        row.insert("label".to_owned(), Json::from(entry.label.as_str()));
        row.insert("type".to_owned(), Json::from(entry.type_name.as_str()));
        rows.push(Json::Object(row));
    }
    let mut text = serde_json::to_string_pretty(&Json::Array(rows)).unwrap_or_default();
    text.push('\n');
    text
}

/// Every setting name the sidebar exposes, one per line, in the order it exposes
/// them: the tab tree measured against the engine's own config instead of a
/// hand-written list.
pub fn name_dump(model: &Model) -> String {
    let mut names: Vec<String> = Vec::new();
    for tab in tab_list(model) {
        for subtab in &tab.subtabs {
            for group in groups(model, &tab.key, &subtab.key, "") {
                for item in &group.items {
                    names.push(item.name.clone());
                }
            }
        }
    }
    let mut text = names.join("\n");
    text.push('\n');
    text
}

/// True once a resolved preset has seeded this model. A model seeded only from the
/// shipped base config carries a nominal machine and another machine's start
/// G-code, so a slice from it is refused rather than written.
pub fn has_resolved_config(model: &Model) -> bool {
    model.seeded_from_resolved
}

/// False when the layout document could not be read at all, which is the one state
/// that collapses the sidebar onto the engine's own categories without an error.
pub fn layout_loaded(model: &Model) -> bool {
    model.layout_loaded
}

/// One sub-object of a configuration document, as a map to edit and put back.
fn object_at(object: &Map<String, Json>, key: &str) -> Map<String, Json> {
    match object.get(key) {
        Some(Json::Object(entries)) => entries.clone(),
        _ => Map::new(),
    }
}

/// The settings document the engine is handed, as one compact JSON object.
pub fn config_json(model: &Model) -> String {
    let Some(Json::Object(template)) = model.template.as_ref() else {
        return "{}".to_owned();
    };
    let mut root = template.clone();
    let mut configuration = match root.get("configuration") {
        Some(Json::Object(configuration)) => configuration.clone(),
        _ => Map::new(),
    };
    let mut print_settings = object_at(&configuration, "print_settings");
    let mut filament_settings = object_at(&configuration, "filament_settings");
    let mut printer_settings = object_at(&configuration, "printer_settings");

    for entry in &model.entries {
        let value = json_of(&entry.current_value);
        match entry.location() {
            LOCATION_PRINT => {
                print_settings.insert(entry.name.clone(), value);
            }
            LOCATION_FILAMENT => {
                // A filament option is a vector with one entry per tool. The panel
                // edits the first slot, so the rest of the vector — one entry per
                // other extruder of a tool changer — has to survive untouched.
                let mut slots = match filament_settings.get(&entry.name) {
                    Some(Json::Array(slots)) => slots.clone(),
                    _ => Vec::new(),
                };
                if slots.is_empty() {
                    slots.push(value);
                } else {
                    slots[0] = value;
                }
                filament_settings.insert(entry.name.clone(), Json::Array(slots));
            }
            LOCATION_PRINTER => {
                printer_settings.insert(entry.name.clone(), value);
            }
            _ => {}
        }
    }

    configuration.insert("print_settings".to_owned(), Json::Object(print_settings));
    configuration.insert(
        "filament_settings".to_owned(),
        Json::Object(filament_settings),
    );
    // The engine's object cancellation, which no print profile carries: Klipper
    // cancels a part from the plate by name, so a file written for it defines every
    // part. Nothing in the settings tree describes this, because in the reference
    // slicers it is not a setting — `EXCLUDE_OBJECT_DEFINE` is simply what those
    // files contain.
    if printer_settings.get("gcode_flavor").and_then(Json::as_str) == Some("klipper") {
        printer_settings.insert("exclude_object".to_owned(), Json::Bool(true));
    }
    configuration.insert(
        "printer_settings".to_owned(),
        Json::Object(printer_settings),
    );
    root.insert("configuration".to_owned(), Json::Object(configuration));
    serde_json::to_string(&Json::Object(root)).unwrap_or_else(|_| "{}".to_owned())
}

/// The `values:` body of an engine preset: every setting in the engine's own preset
/// spelling, a percentage with its sign, a point as `x xy`, an enum and a list
/// quoted the way the engine's reader accepts them. A value that cannot be written
/// is skipped rather than written as the wrong type.
pub fn values_yaml(model: &Model, tab: &str, changed_only: bool) -> String {
    let mut text = String::new();
    for entry in &model.entries {
        if !tab.is_empty() && entry.tab_key != tab {
            continue;
        }
        if changed_only && !entry.modified {
            continue;
        }
        if model.unsupported.contains(&entry.name) {
            continue;
        }
        let body = yaml_of(&entry.current_value);
        if body.is_empty() {
            continue;
        }
        text.push_str("  ");
        text.push_str(&entry.name);
        text.push(':');
        text.push_str(&body);
        text.push('\n');
    }
    text
}

/// Makes the value each setting holds now the one it is measured against, which is
/// what a save does once the engine has the preset.
pub fn clear_modified(model: &mut Model) -> bool {
    let mut any = false;
    for entry in &mut model.entries {
        if !entry.modified {
            continue;
        }
        entry.default_value = entry.current_value.clone();
        entry.modified = false;
        any = true;
    }
    any
}

impl Model {
    fn find(&self, name: &str) -> Option<&Entry> {
        self.index
            .get(name)
            .map(|position| &self.entries[*position])
    }

    /// The shipped base config is a machine-neutral document: every setting at a
    /// value of the right shape, no printer named. It exists for the shape, not for
    /// a machine — nothing is drawn from it, and a resolved preset replaces every
    /// one of these values before anything slices.
    fn seed_from_profile(&mut self, base_config_json: &str) {
        let Ok(document) = serde_json::from_str::<Json>(base_config_json) else {
            return;
        };
        let Json::Object(root) = &document else {
            return;
        };
        let Some(Json::Object(configuration)) = root.get("configuration") else {
            return;
        };
        let configuration = configuration.clone();
        self.template = Some(document);
        self.seed_from_configuration(&configuration);
    }

    /// Filament options are stored per material slot, so the first slot's value
    /// seeds the single-slot editor.
    fn seed_from_configuration(&mut self, configuration: &Map<String, Json>) {
        const BLOCKS: [(&str, u8); 3] = [
            ("print_settings", LOCATION_PRINT),
            ("filament_settings", LOCATION_FILAMENT),
            ("printer_settings", LOCATION_PRINTER),
        ];
        for (key, location) in BLOCKS {
            let Some(Json::Object(settings)) = configuration.get(key) else {
                continue;
            };
            for (name, value) in settings {
                let Some(position) = self.index.get(name).copied() else {
                    continue;
                };
                if self.entries[position].location() != location {
                    continue;
                }
                let value = if location == LOCATION_FILAMENT {
                    match value {
                        Json::Array(slots) if !slots.is_empty() => &slots[0],
                        _ => continue,
                    }
                } else {
                    value
                };
                let mut seeded = self.entries[position].default_value.clone();
                if !assign(
                    &mut seeded,
                    &self.entries[position].definition,
                    &input_of_json(value),
                ) {
                    continue;
                }
                self.entries[position].default_value = seeded.clone();
                self.entries[position].current_value = seeded;
                self.entries[position].modified = false;
            }
        }
    }

    /// The tree shape the current numbers correspond to: the engine's style has to
    /// be organic and every number of a shape has to match, so a shape reads as
    /// itself after a preset reloads and never as itself after the user has edited
    /// one of its numbers.
    fn tree_profile_in_use(&self) -> String {
        let Some(style) = self.find("support_material_style") else {
            return String::new();
        };
        let Value::Enum(serialized) = &style.current_value else {
            return String::new();
        };
        if serialized != ORGANIC_STYLE {
            return String::new();
        }
        for profile in &TREE_PROFILES {
            if profile.key == ORGANIC_STYLE {
                continue;
            }
            let matches = profile.knobs.iter().all(|(name, value)| {
                self.find(*name).is_some_and(|entry| {
                    (number_of(&entry.current_value) - value).abs() <= SHAPE_TOLERANCE
                })
            });
            if matches {
                return profile.key.to_owned();
            }
        }
        String::new()
    }

    /// One shape, which is the engine's style plus every number it stands for. A
    /// number the engine does not have makes the whole shape fail, which is what
    /// `applied` reports, but the ones before it stay written.
    fn apply_tree_profile(&mut self, key: &str, changed: &mut bool) -> bool {
        let Some(profile) = tree_profile_for(key) else {
            return false;
        };
        let Some(position) = self.index.get("support_material_style").copied() else {
            return false;
        };
        let mut applied =
            self.assign_entry(position, &Value::Enum(ORGANIC_STYLE.to_owned()), changed);
        for (name, value) in profile.knobs {
            let Some(position) = self.index.get(*name).copied() else {
                applied = false;
                continue;
            };
            applied = self.assign_entry(position, &Value::Number(*value), changed) && applied;
        }
        applied
    }

    fn assign_entry(&mut self, position: usize, value: &Value, changed: &mut bool) -> bool {
        let entry = &mut self.entries[position];
        let input = SettingInputRecord {
            is_list: false,
            whole: scalar_of(value),
            first: scalar_of(value),
            elements: Vec::new(),
        };
        let previous = entry.current_value.clone();
        if !assign(&mut entry.current_value, &entry.definition, &input) {
            return false;
        }
        entry.modified = !same(&entry.current_value, &entry.default_value);
        if !same(&entry.current_value, &previous) {
            *changed = true;
        }
        true
    }
}

fn holds_list(value: &Value) -> bool {
    matches!(
        value,
        Value::Booleans(_)
            | Value::Integers(_)
            | Value::OptionalIntegers(_)
            | Value::Numbers(_)
            | Value::Percents(_)
            | Value::FloatOrPercents(_)
            | Value::Points(_)
            | Value::Texts(_)
            | Value::Enums(_)
    )
}

fn definition_of(record: &SettingDefinitionRecord) -> Definition {
    Definition {
        name: record.name.clone(),
        label: record.label.clone(),
        full_label: record.full_label.clone(),
        tooltip: record.tooltip.clone(),
        location: record.location,
        overrides_in: record.overrides_in.clone(),
        category: record.category,
        option_group: record.option_group,
        category_title: record.category_title.clone(),
        option_group_title: record.option_group_title.clone(),
        order: record.order,
        gui_type: record.gui_type.clone(),
        units: record.units.clone(),
        has_min: record.has_min,
        min: record.min,
        has_max: record.has_max,
        max: record.max,
        multiline: record.multiline,
        full_width: record.full_width,
        enum_values: record
            .enum_values
            .iter()
            .map(|value| EnumValue {
                serialized: value.serialized.clone(),
                label: value.label.clone(),
                number: value.number,
            })
            .collect(),
        default: value_of(&record.default_value),
    }
}

fn entry_of(definition: Definition, placement: Option<&Placement>) -> Entry {
    let hidden = definition.category == CATEGORY_HIDDEN;
    let category = definition.category;
    let engine_subtab_title = if hidden {
        "Advanced".to_owned()
    } else {
        subtab_title_for(category, &definition.category_title)
    };
    let engine_group_title = definition.option_group_title.clone();
    let type_name = type_name(&definition.default).to_owned();
    let gui_type = gui_type_name(&definition.gui_type).to_owned();

    let mut entry = Entry {
        default_value: definition.default.clone(),
        current_value: definition.default.clone(),
        name: definition.name.clone(),
        label: definition.label.clone(),
        tab_key: tab_key_for(definition.location).to_owned(),
        subtab_key: if hidden {
            "advanced".to_owned()
        } else {
            category.to_string()
        },
        subtab_title: engine_subtab_title.clone(),
        engine_subtab_title,
        engine_group_title: engine_group_title.clone(),
        type_name,
        gui_type,
        group_key: definition.option_group.to_string(),
        group_title: if definition.option_group == GROUP_ORGANIC_SUPPORTS {
            "Tree supports".to_owned()
        } else {
            engine_group_title
        },
        group: definition.option_group,
        sort_category: if hidden { 1000 } else { category },
        sort_position: -1,
        modified: false,
        definition,
    };

    if let Some(placement) = placement {
        entry.subtab_key = placement.page_key.clone();
        entry.subtab_title = placement.page_title.clone();
        entry.group_key = placement.section_key.clone();
        entry.group_title = placement.section_title.clone();
        entry.sort_category = placement.page_order;
        entry.group = placement.section_order;
        entry.sort_position = placement.position;
    }
    entry
}

/// The pages and sections the sidebar shows, which are this application's own and
/// not the engine's categories: `tools/settings_layout.py` writes them from
/// `tools/settings-layout-map.json`. An entry without a placement keeps the
/// engine's category, so a setting added to the engine tomorrow still appears — in
/// the engine's grouping — instead of vanishing.
fn load_layout(placements: &mut HashMap<String, Placement>, layout_json: &str) -> bool {
    let Ok(document) = serde_json::from_str::<Json>(layout_json) else {
        return false;
    };
    let Json::Object(root) = &document else {
        return false;
    };
    let Some(Json::Object(tabs)) = root.get("tabs") else {
        return false;
    };

    for (tab_index, (tab_key, _, _)) in TABS.iter().enumerate() {
        let Some(Json::Array(pages)) = tabs.get(*tab_key) else {
            continue;
        };
        for (page_index, page) in pages.iter().enumerate() {
            let Json::Object(page) = page else {
                continue;
            };
            let page_key = page.get("key").and_then(Json::as_str).unwrap_or_default();
            let page_title = page.get("title").and_then(Json::as_str).unwrap_or_default();
            let Some(Json::Array(sections)) = page.get("sections") else {
                continue;
            };
            for (section_index, section) in sections.iter().enumerate() {
                let Json::Object(section) = section else {
                    continue;
                };
                let section_title = section
                    .get("title")
                    .and_then(Json::as_str)
                    .unwrap_or_default();
                let Some(Json::Array(settings)) = section.get("settings") else {
                    continue;
                };
                for (position, name) in settings.iter().enumerate() {
                    let Some(name) = name.as_str() else {
                        continue;
                    };
                    placements.insert(
                        name.to_owned(),
                        Placement {
                            page_key: page_key.to_owned(),
                            page_title: page_title.to_owned(),
                            section_key: format!("{page_key}/{section_index}"),
                            section_title: section_title.to_owned(),
                            page_order: tab_index as i32 * 100 + page_index as i32,
                            section_order: section_index as i32,
                            position: position as i32,
                        },
                    );
                }
            }
        }
    }
    true
}

fn tab_key_for(location: u8) -> &'static str {
    for (key, _, tab_location) in TABS {
        if tab_location == location {
            return key;
        }
    }
    ""
}

fn tab_title_for(key: &str) -> String {
    for (tab_key, title, _) in TABS {
        if tab_key == key {
            return title.to_owned();
        }
    }
    key.to_owned()
}

/// The engine's own page title, retitled where this application's tool changer
/// behaves differently from the single-nozzle machine the engine's word describes.
fn subtab_title_for(category: i32, engine_title: &str) -> String {
    if category == CATEGORY_PRINT_MULTI_MATERIAL {
        return "Tools & Wipe Tower".to_owned();
    }
    if category == CATEGORY_FILAMENT_MULTI_MATERIAL {
        return "Tool Changes".to_owned();
    }
    engine_title.to_owned()
}

fn is_colour_mixing_option(name: &str) -> bool {
    COLOUR_MIXING_OPTIONS.contains(&name)
}

fn matches_filter(definition: &Definition, filter: &str) -> bool {
    if filter.is_empty() {
        return true;
    }
    let needle = filter.to_lowercase();
    definition.label.to_lowercase().contains(&needle)
        || definition.name.to_lowercase().contains(&needle)
}

fn tree_profile_for(key: &str) -> Option<&'static TreeProfile> {
    TREE_PROFILES.iter().find(|profile| profile.key == key)
}

/// The engine's own UI strings are generic about one thing this application is
/// specific about: its organic support generator is a tree, and calling it
/// "Organic" tells a user nothing about what they will get on the plate.
fn enum_label<'a>(name: &str, value: &'a EnumValue) -> &'a str {
    if name == "support_material_style" && value.serialized == ORGANIC_STYLE {
        return "Tree";
    }
    &value.label
}

fn item_of(model: &Model, entry: &Entry) -> SettingItemRecord {
    let mut value = record_of(&entry.current_value);
    if entry.name == "support_material_style" {
        let profile = model.tree_profile_in_use();
        if !profile.is_empty() {
            value = SettingValueRecord {
                kind: 4,
                truth: false,
                integer: 0,
                number: 0.0,
                text: profile,
                items: 0,
                numbers: Vec::new(),
                texts: Vec::new(),
                flags: Vec::new(),
            };
        }
    }
    SettingItemRecord {
        name: entry.name.clone(),
        label: entry.label.clone(),
        full_label: entry.definition.full_label.clone(),
        tooltip: entry.definition.tooltip.clone(),
        type_name: entry.type_name.clone(),
        gui_type: entry.gui_type.clone(),
        value,
        has_min: entry.definition.has_min,
        min: entry.definition.min,
        has_max: entry.definition.has_max,
        max: entry.definition.max,
        units: entry.definition.units.clone(),
        choices: choices_of(entry),
        modified: entry.modified,
        percent: matches!(
            entry.current_value,
            Value::FloatOrPercent { percent: true, .. }
        ),
        multiline: entry.definition.multiline,
        full_width: entry.definition.full_width,
        complete: true,
    }
}

fn choices_of(entry: &Entry) -> Vec<SettingChoiceRecord> {
    let mut choices = Vec::new();
    if matches!(entry.current_value, Value::Enum(_) | Value::Enums(_)) {
        for value in &entry.definition.enum_values {
            choices.push(SettingChoiceRecord {
                value: value.serialized.clone(),
                label: enum_label(&entry.name, value).to_owned(),
            });
        }
    }
    if entry.name == "support_material_style" {
        for profile in &TREE_PROFILES {
            if profile.key == ORGANIC_STYLE {
                continue;
            }
            choices.push(SettingChoiceRecord {
                value: profile.key.to_owned(),
                label: profile.label.to_owned(),
            });
        }
    }
    choices
}

/// The panel's word for a setting's value type, which is the engine's
/// `ConfigItemDef` type reduced to the eight names the QML branches on.
fn type_name(value: &Value) -> &'static str {
    match value {
        Value::Boolean(_) | Value::Booleans(_) => "Boolean",
        Value::Integer(_)
        | Value::Integers(_)
        | Value::OptionalIntegers(_) => "Int",
        Value::Number(_) | Value::Numbers(_) | Value::Points(_) => "Float",
        Value::Percent(_) | Value::Percents(_) => "Percentage",
        Value::FloatOrPercent { .. } | Value::FloatOrPercents(_) => "FloatOrPercentage",
        Value::Texts(_) => "Strings",
        Value::Enum(_) | Value::Enums(_) => "Enum",
        Value::Text(_) | Value::Opaque => "String",
    }
}

/// `ConfigItemDef::GUIType`, as the panel spells it. Every other type has no widget
/// of its own: the QML picks the editor from the value type instead, and naming
/// those types here would switch on branches it deliberately leaves off.
fn gui_type_name(engine_name: &str) -> &'static str {
    match engine_name {
        "color" => "color",
        "file_picker" => "file",
        "bed_shape" => "bedShape",
        "substitutions" => "substitutions",
        "points" => "points",
        "extruder_selection" => "extruderSelection",
        "unit_or_percentage" => "unitOrPercentage",
        _ => "",
    }
}

/// `ConfigValue::operator==`: the same alternative, doubles compared with the
/// engine's own tolerance, an enum by its serialized spelling.
fn same(lhs: &Value, rhs: &Value) -> bool {
    match (lhs, rhs) {
        (Value::Boolean(a), Value::Boolean(b)) => a == b,
        (Value::Booleans(a), Value::Booleans(b)) => a == b,
        (Value::Integer(a), Value::Integer(b)) => a == b,
        (Value::Integers(a), Value::Integers(b)) => a == b,
        (Value::OptionalIntegers(a), Value::OptionalIntegers(b)) => a == b,
        (Value::Number(a), Value::Number(b)) => fuzzy_compare(*a, *b),
        (Value::Numbers(a), Value::Numbers(b)) => {
            a.len() == b.len() && a.iter().zip(b).all(|(x, y)| fuzzy_compare(*x, *y))
        }
        (Value::Text(a), Value::Text(b)) => a == b,
        (Value::Texts(a), Value::Texts(b)) => a == b,
        (Value::Percent(a), Value::Percent(b)) => fuzzy_compare(*a, *b),
        (Value::Percents(a), Value::Percents(b)) => {
            a.len() == b.len() && a.iter().zip(b).all(|(x, y)| fuzzy_compare(*x, *y))
        }
        (
            Value::FloatOrPercent {
                value: a,
                percent: a_is_percent,
            },
            Value::FloatOrPercent {
                value: b,
                percent: b_is_percent,
            },
        ) => a_is_percent == b_is_percent && fuzzy_compare(*a, *b),
        (Value::FloatOrPercents(a), Value::FloatOrPercents(b)) => {
            a.len() == b.len()
                && a.iter().zip(b).all(|((x, x_percent), (y, y_percent))| {
                    x_percent == y_percent && fuzzy_compare(*x, *y)
                })
        }
        (Value::Points(a), Value::Points(b)) => {
            a.len() == b.len()
                && a.iter().zip(b).all(|((a_x, a_y), (b_x, b_y))| {
                    fuzzy_compare(*a_x, *b_x) && fuzzy_compare(*a_y, *b_y)
                })
        }
        (Value::Enum(a), Value::Enum(b)) => a == b,
        (Value::Enums(a), Value::Enums(b)) => a == b,
        (Value::Opaque, Value::Opaque) => true,
        _ => false,
    }
}

/// `Domain::fuzzy_compare`, with the defaults the engine's equality uses.
fn fuzzy_compare(a: f64, b: f64) -> bool {
    (a - b).abs() <= FUZZY_ABSOLUTE.max(FUZZY_RELATIVE * a.abs().max(b.abs()))
}

/// The number a value reads as, for the shape matching above.
fn number_of(value: &Value) -> f64 {
    match value {
        Value::Number(number) => *number,
        Value::Integer(integer) => *integer as f64,
        Value::Percent(percent) => *percent,
        Value::FloatOrPercent { value, .. } => *value,
        _ => f64::NAN,
    }
}

/// The engine's own assignment: turns one incoming value into the shape this
/// setting holds, and answers false when the setting cannot hold it.
///
/// The branches that are missing here are missing there too. The engine's
/// assignment has no arm for a vector of anything but a double, a string or a
/// point, and none for a bare point, so a value arriving for one of those settings
/// is refused rather than converted — which is why the seed of such a setting keeps
/// the default its definition gave it.
fn assign(current: &mut Value, definition: &Definition, input: &SettingInputRecord) -> bool {
    let scalar: &SettingScalarRecord =
        if !holds_list(current) && input.is_list && !input.elements.is_empty() {
            &input.first
        } else {
            &input.whole
        };

    let mut materialised = Vec::new();
    let elements: &[SettingElementRecord] = if holds_list(current) && !input.is_list {
        materialised.push(repeated_element(&input.whole));
        &materialised
    } else {
        &input.elements
    };

    match current {
        Value::Boolean(flag) => {
            let value = if scalar.kind == 4 {
                let text = scalar.text.trim().to_lowercase();
                !(text.is_empty() || text == "0" || text == "false")
            } else {
                scalar.truth
            };
            *flag = value;
            true
        }
        Value::Integer(integer) => {
            if !scalar.integer_ok {
                return false;
            }
            *integer = scalar.integer;
            true
        }
        Value::Number(number) => {
            if !scalar.number_ok {
                return false;
            }
            *number = scalar.number;
            true
        }
        Value::Text(text) => {
            *text = scalar.text.clone();
            true
        }
        Value::Texts(texts) => {
            *texts = elements
                .iter()
                .map(|item| item.text.clone())
                .collect();
            true
        }
        Value::OptionalIntegers(integers) => {
            let mut values = Vec::with_capacity(elements.len());
            for item in elements {
                if !item.number_ok {
                    return false;
                }
                values.push(Some(item.number as i64));
            }
            *integers = values;
            true
        }
        Value::Numbers(numbers) => {
            let mut values = Vec::with_capacity(elements.len());
            for item in elements {
                if !item.number_ok {
                    return false;
                }
                values.push(item.number);
            }
            *numbers = values;
            true
        }
        Value::Percent(percent) => {
            let Some((value, _)) = percentage_parts(scalar) else {
                return false;
            };
            *percent = value;
            true
        }
        Value::FloatOrPercent { value, percent } => {
            let Some((incoming, is_percentage)) = percentage_parts(scalar) else {
                return false;
            };
            *value = incoming;
            *percent = is_percentage;
            true
        }
        Value::Points(points) => {
            let mut values = Vec::with_capacity(elements.len());
            for item in elements {
                if item.kind != 2 {
                    return false;
                }
                values.push((item.pair_first, item.pair_second));
            }
            *points = values;
            true
        }
        Value::Enum(serialized) => {
            let Some(value) = match_enum(definition, scalar) else {
                return false;
            };
            *serialized = value;
            true
        }
        Value::Enums(serials) => {
            let mut values = Vec::with_capacity(elements.len());
            for item in elements {
                let Some(value) = match_enum_text(definition, &item.text) else {
                    return false;
                };
                values.push(value);
            }
            *serials = values;
            true
        }
        _ => false,
    }
}

/// One scalar as the single element the engine repeats into every slot of a list.
///
/// A per-tool option is a vector, and a document that writes one number for it means
/// that number in every slot — `idle_temperature: 170` for a single-extruder
/// profile. Without this the value is refused and the setting keeps the default the
/// base config gave it, which is how a filament's idle temperature came back null.
fn repeated_element(scalar: &SettingScalarRecord) -> SettingElementRecord {
    SettingElementRecord {
        kind: if scalar.kind == 4 { 1 } else { 0 },
        text: scalar.text.clone(),
        number: if scalar.integer_ok {
            scalar.integer as f64
        } else {
            scalar.number
        },
        number_ok: scalar.number_ok || scalar.integer_ok,
        pair_first: 0.0,
        pair_second: 0.0,
    }
}

/// A spelling, or an engine number, back to the serialized spelling the preset
/// files and the slice use.
fn match_enum(definition: &Definition, scalar: &SettingScalarRecord) -> Option<String> {
    if let Some(value) = match_enum_text(definition, &scalar.text) {
        return Some(value);
    }
    if !scalar.integer_ok {
        return None;
    }
    definition
        .enum_values
        .iter()
        .find(|value| value.number == scalar.integer)
        .map(|value| value.serialized.clone())
}

fn match_enum_text(definition: &Definition, text: &str) -> Option<String> {
    definition
        .enum_values
        .iter()
        .find(|value| value.serialized == text || value.label == text)
        .map(|value| value.serialized.clone())
}

/// A percentage or a length as one number and whether it was written as a
/// percentage: the editor's `{value, is_percent}` object, otherwise the scalar's own
/// text with a trailing `%`.
fn percentage_parts(scalar: &SettingScalarRecord) -> Option<(f64, bool)> {
    if scalar.kind == 6 {
        return Some((scalar.map_value, scalar.map_percent));
    }
    let percentage = scalar.text.ends_with('%');
    let text = scalar.text.trim_end_matches('%').trim();
    let number = text.parse::<f64>().ok()?;
    Some((number, percentage))
}

/// `variantFrom`: the value as the record the QML editors index. `kind` 6 and 7
/// reach QML as a bare number and `kind` 8 as a string, which is the shape the
/// editors already read.
fn record_of(value: &Value) -> SettingValueRecord {
    let mut record = invalid_record();
    match value {
        Value::Boolean(flag) => {
            record.kind = 1;
            record.truth = *flag;
        }
        Value::Integer(integer) => {
            record.kind = 2;
            record.integer = *integer;
        }
        Value::Number(number) => {
            record.kind = 3;
            record.number = *number;
        }
        Value::Percent(percent) => {
            record.kind = 6;
            record.number = *percent;
        }
        Value::FloatOrPercent { value, percent } => {
            record.kind = 7;
            record.number = *value;
            record.truth = *percent;
        }
        Value::Text(text) => {
            record.kind = 4;
            record.text = text.clone();
        }
        Value::Enum(serialized) => {
            record.kind = 8;
            record.text = serialized.clone();
        }
        Value::Booleans(flags) => {
            record.kind = 5;
            record.items = 4;
            record.flags = flags.iter().map(|flag| u8::from(*flag)).collect();
        }
        Value::Integers(integers) => {
            record.kind = 5;
            record.items = 3;
            record.numbers = integers.iter().map(|value| *value as f64).collect();
        }
        Value::OptionalIntegers(integers) => {
            record.kind = 5;
            record.items = 6;
            for value in integers {
                record.flags.push(u8::from(value.is_none()));
                record.numbers.push(value.map_or(0.0, |value| value as f64));
            }
        }
        Value::Numbers(numbers) => {
            record.kind = 5;
            record.numbers = numbers.clone();
        }
        Value::Percents(percents) => {
            record.kind = 5;
            record.items = 7;
            record.numbers = percents.clone();
        }
        Value::FloatOrPercents(values) => {
            record.kind = 5;
            record.items = 5;
            for (value, percent) in values {
                record.numbers.push(*value);
                record.flags.push(u8::from(*percent));
            }
        }
        Value::Texts(texts) => {
            record.kind = 5;
            record.items = 1;
            record.texts = texts.clone();
        }
        Value::Enums(serials) => {
            record.kind = 5;
            record.items = 8;
            record.texts = serials.clone();
        }
        Value::Points(points) => {
            record.kind = 5;
            record.items = 2;
            for (x, y) in points {
                record.numbers.push(*x);
                record.numbers.push(*y);
            }
        }
        Value::Opaque => {}
    }
    record
}

fn invalid_record() -> SettingValueRecord {
    SettingValueRecord {
        kind: 0,
        truth: false,
        integer: 0,
        number: 0.0,
        text: String::new(),
        items: 0,
        numbers: Vec::new(),
        texts: Vec::new(),
        flags: Vec::new(),
    }
}

fn invalid_scalar() -> SettingScalarRecord {
    SettingScalarRecord {
        kind: 0,
        truth: false,
        integer: 0,
        integer_ok: false,
        number: 0.0,
        number_ok: false,
        text: String::new(),
        map_value: 0.0,
        map_percent: false,
    }
}

fn scalar_copy(scalar: &SettingScalarRecord) -> SettingScalarRecord {
    SettingScalarRecord {
        kind: scalar.kind,
        truth: scalar.truth,
        integer: scalar.integer,
        integer_ok: scalar.integer_ok,
        number: scalar.number,
        number_ok: scalar.number_ok,
        text: scalar.text.clone(),
        map_value: scalar.map_value,
        map_percent: scalar.map_percent,
    }
}

/// A record back to a value, for the definitions' defaults.
fn value_of(record: &SettingValueRecord) -> Value {
    match record.kind {
        1 => Value::Boolean(record.truth),
        2 => Value::Integer(record.integer),
        3 => Value::Number(record.number),
        4 => Value::Text(record.text.clone()),
        5 => match record.items {
            1 => Value::Texts(record.texts.clone()),
            2 => Value::Points(
                record
                    .numbers
                    .chunks_exact(2)
                    .map(|pair| (pair[0], pair[1]))
                    .collect(),
            ),
            3 => Value::Integers(record.numbers.iter().map(|value| *value as i64).collect()),
            4 => Value::Booleans(record.flags.iter().map(|flag| *flag != 0).collect()),
            5 => Value::FloatOrPercents(
                record
                    .numbers
                    .iter()
                    .copied()
                    .zip(record.flags.iter().map(|flag| *flag != 0))
                    .collect(),
            ),
            6 => Value::OptionalIntegers(
                record
                    .numbers
                    .iter()
                    .zip(record.flags.iter())
                    .map(|(number, absent)| {
                        if *absent != 0 {
                            None
                        } else {
                            Some(*number as i64)
                        }
                    })
                    .collect(),
            ),
            7 => Value::Percents(record.numbers.clone()),
            8 => Value::Enums(record.texts.clone()),
            _ => Value::Numbers(record.numbers.clone()),
        },
        6 => Value::Percent(record.number),
        7 => Value::FloatOrPercent {
            value: record.number,
            percent: record.truth,
        },
        8 => Value::Enum(record.text.clone()),
        _ => Value::Opaque,
    }
}

/// One value as the input record a mutation is stated in, for the tree shapes and
/// for the config seeding — the two paths that begin in this module instead of in a
/// `QVariant`.
fn scalar_of(value: &Value) -> SettingScalarRecord {
    let mut scalar = invalid_scalar();
    match value {
        Value::Boolean(flag) => {
            scalar.kind = 1;
            scalar.truth = *flag;
        }
        Value::Integer(integer) => {
            scalar.kind = 2;
            scalar.integer = *integer;
            scalar.integer_ok = true;
            scalar.number = *integer as f64;
            scalar.number_ok = true;
            scalar.text = integer.to_string();
        }
        Value::Number(number) => {
            scalar.kind = 3;
            scalar.number = *number;
            scalar.number_ok = true;
            scalar.text = number.to_string();
        }
        Value::Text(text) => {
            scalar.kind = 4;
            scalar.text = text.clone();
        }
        Value::Enum(serialized) => {
            scalar.kind = 4;
            scalar.text = serialized.clone();
        }
        _ => {}
    }
    scalar
}

fn element_of(scalar: &SettingScalarRecord) -> SettingElementRecord {
    SettingElementRecord {
        kind: if scalar.kind == 4 { 1 } else { 0 },
        text: scalar.text.clone(),
        number: scalar.number,
        number_ok: scalar.number_ok,
        pair_first: 0.0,
        pair_second: 0.0,
    }
}

/// One JSON value as the input record a mutation is stated in. A JSON number is a
/// double, as it is to `QJsonValue::toVariant`, so only a whole number can seed an
/// integer setting and a fractional one is refused rather than rounded.
fn input_of_json(value: &Json) -> SettingInputRecord {
    let mut input = SettingInputRecord {
        is_list: false,
        whole: invalid_scalar(),
        first: invalid_scalar(),
        elements: Vec::new(),
    };
    match value {
        Json::Bool(flag) => {
            input.whole.kind = 1;
            input.whole.truth = *flag;
            input.whole.text = flag.to_string();
        }
        Json::Number(number) => {
            let number = number.as_f64().unwrap_or_default();
            input.whole.kind = 3;
            input.whole.number = number;
            input.whole.number_ok = true;
            input.whole.integer_ok =
                number.fract() == 0.0 && number.abs() <= 9_007_199_254_740_992.0;
            input.whole.integer = number as i64;
            input.whole.text = number_to_text(number);
        }
        Json::String(text) => {
            input.whole.kind = 4;
            input.whole.text = text.clone();
        }
        Json::Array(items) => {
            input.is_list = true;
            input.whole.kind = 5;
            for item in items {
                input.elements.push(element_from_json(item));
            }
            if let Some(first) = input.elements.first() {
                input.first = scalar_from_element(first);
            }
        }
        Json::Object(object) => {
            // The percentage object the JSON a slice is handed stores: `{value,
            // is_percent}`, which is what the engine's own documents carry.
            if let Some(number) = object.get("value").and_then(Json::as_f64) {
                input.whole.kind = 6;
                input.whole.map_value = number;
                input.whole.map_percent = object
                    .get("is_percent")
                    .and_then(Json::as_bool)
                    .unwrap_or(false);
                input.whole.number = number;
                input.whole.number_ok = true;
                input.whole.text = number_to_text(number);
            }
        }
        Json::Null => {}
    }
    input
}

fn element_from_json(value: &Json) -> SettingElementRecord {
    let mut element = SettingElementRecord {
        kind: 0,
        text: String::new(),
        number: 0.0,
        number_ok: false,
        pair_first: 0.0,
        pair_second: 0.0,
    };
    match value {
        Json::Number(number) => {
            element.number = number.as_f64().unwrap_or_default();
            element.number_ok = true;
            element.text = number_to_text(element.number);
        }
        Json::String(text) => {
            element.kind = 1;
            element.text = text.clone();
        }
        Json::Bool(flag) => {
            element.number = if *flag { 1.0 } else { 0.0 };
            element.number_ok = true;
            element.text = flag.to_string();
        }
        Json::Array(pair) if pair.len() >= 2 => {
            element.kind = 2;
            element.pair_first = pair[0].as_f64().unwrap_or_default();
            element.pair_second = pair[1].as_f64().unwrap_or_default();
        }
        _ => {}
    }
    element
}

/// The first element of a list as a scalar: a string keeps its text, a number its
/// answer, and a point has none — a `QVariant` holding a list converts to no number
/// and to no text, which is what the engine's own assignment sees.
fn scalar_from_element(element: &SettingElementRecord) -> SettingScalarRecord {
    let mut scalar = invalid_scalar();
    match element.kind {
        1 => {
            scalar.kind = 4;
            scalar.text = element.text.clone();
        }
        2 => {
            scalar.kind = 5;
        }
        _ => {
            scalar.kind = 3;
            scalar.number = element.number;
            scalar.number_ok = element.number_ok;
            scalar.text = element.text.clone();
        }
    }
    scalar
}

/// `jsonFrom`: the value as the slice's own configuration JSON.
fn json_of(value: &Value) -> Json {
    match value {
        Value::Boolean(flag) => Json::Bool(*flag),
        Value::Booleans(flags) => Json::Array(flags.iter().map(|flag| Json::Bool(*flag)).collect()),
        Value::Integer(integer) => Json::from(*integer),
        Value::Integers(integers) => {
            Json::Array(integers.iter().map(|value| Json::from(*value)).collect())
        }
        Value::OptionalIntegers(integers) => Json::Array(
            integers
                .iter()
                .map(|value| match value {
                    Some(integer) => Json::from(*integer),
                    None => Json::Null,
                })
                .collect(),
        ),
        Value::Number(number) => number_json(*number),
        Value::Numbers(numbers) => {
            Json::Array(numbers.iter().map(|value| number_json(*value)).collect())
        }
        Value::Text(text) => Json::from(text.as_str()),
        Value::Texts(texts) | Value::Enums(texts) => {
            Json::Array(texts.iter().map(|text| Json::from(text.as_str())).collect())
        }
        Value::Percent(percent) => percentage_json(*percent, true),
        Value::Percents(percents) => Json::Array(
            percents
                .iter()
                .map(|percent| percentage_json(*percent, true))
                .collect(),
        ),
        Value::FloatOrPercent { value, percent } => percentage_json(*value, *percent),
        Value::FloatOrPercents(values) => Json::Array(
            values
                .iter()
                .map(|(value, percent)| percentage_json(*value, *percent))
                .collect(),
        ),
        Value::Points(points) => Json::Array(
            points
                .iter()
                .map(|(x, y)| Json::Array(vec![number_json(*x), number_json(*y)]))
                .collect(),
        ),
        Value::Enum(serialized) => Json::from(serialized.as_str()),
        Value::Opaque => Json::Null,
    }
}

fn number_json(number: f64) -> Json {
    serde_json::Number::from_f64(number).map_or(Json::Null, Json::Number)
}

fn percentage_json(value: f64, is_percent: bool) -> Json {
    let mut object = Map::new();
    object.insert("value".to_owned(), number_json(value));
    object.insert("is_percent".to_owned(), Json::Bool(is_percent));
    Json::Object(object)
}

/// `yamlElement`: one preset scalar, a leading space for a scalar and a leading
/// newline for a sequence so a caller can keep the key and its items in one string.
/// Empty means the value cannot be written and its key is skipped.
fn yaml_of(value: &Value) -> String {
    match value {
        Value::Boolean(flag) => format!(" {}", if *flag { "true" } else { "false" }),
        Value::Booleans(flags) => yaml_sequence(
            flags
                .iter()
                .map(|flag| format!(" {}", if *flag { "true" } else { "false" }))
                .collect(),
            flags.is_empty(),
        ),
        Value::Integer(integer) => format!(" {integer}"),
        Value::Integers(integers) => yaml_sequence(
            integers.iter().map(|value| format!(" {value}")).collect(),
            integers.is_empty(),
        ),
        Value::OptionalIntegers(integers) => yaml_sequence(
            integers
                .iter()
                .map(|value| value.map(|value| format!(" {value}")).unwrap_or_default())
                .collect(),
            integers.is_empty(),
        ),
        Value::Number(number) => format!(" {}", crate::PresetYaml::number(*number)),
        Value::Numbers(numbers) => yaml_sequence(
            numbers
                .iter()
                .map(|value| format!(" {}", crate::PresetYaml::number(*value)))
                .collect(),
            numbers.is_empty(),
        ),
        Value::Text(text) => format!(" {}", crate::PresetYaml::quoted(text)),
        Value::Texts(texts) | Value::Enums(texts) => yaml_sequence(
            texts
                .iter()
                .map(|text| format!(" {}", crate::PresetYaml::quoted(text)))
                .collect(),
            texts.is_empty(),
        ),
        Value::Percent(percent) => format!(" {}", percent_scalar(*percent)),
        Value::Percents(percents) => yaml_sequence(
            percents
                .iter()
                .map(|percent| format!(" {}", percent_scalar(*percent)))
                .collect(),
            percents.is_empty(),
        ),
        Value::FloatOrPercent { value, percent } => {
            if *percent {
                format!(" {}", percent_scalar(*value))
            } else {
                format!(" {}", crate::PresetYaml::number(*value))
            }
        }
        Value::FloatOrPercents(values) => yaml_sequence(
            values
                .iter()
                .map(|(value, percent)| {
                    if *percent {
                        format!(" {}", percent_scalar(*value))
                    } else {
                        format!(" {}", crate::PresetYaml::number(*value))
                    }
                })
                .collect(),
            values.is_empty(),
        ),
        Value::Points(points) => yaml_sequence(
            points
                .iter()
                .map(|(x, y)| format!(" {}", crate::PresetYaml::point(*x, *y)))
                .collect(),
            points.is_empty(),
        ),
        Value::Enum(serialized) => format!(" {}", crate::PresetYaml::quoted(serialized)),
        Value::Opaque => String::new(),
    }
}

fn percent_scalar(percent: f64) -> String {
    crate::PresetYaml::quoted(&format!("{}%", crate::PresetYaml::number(percent)))
}

/// A preset sequence: ` []` for an empty one, otherwise its items two spaces in and
/// prefixed by `-`, with every item that cannot be written left out — and nothing at
/// all when that leaves none, which makes the caller skip the key.
fn yaml_sequence(items: Vec<String>, empty: bool) -> String {
    if empty {
        return " []".to_owned();
    }
    let mut text = String::from("\n");
    let mut first = true;
    for item in items {
        if item.is_empty() {
            continue;
        }
        if !first {
            text.push('\n');
        }
        text.push_str("  -");
        text.push_str(&item);
        first = false;
    }
    if first {
        String::new()
    } else {
        text
    }
}

/// A number as the text a `QString` would carry. `QVariant::toString` on a double is
/// `QString::number(d)`, six significant digits, so the shortest round-trip spelling
/// here differs from it for a value with more digits than that — reachable only from
/// a document that carries a number where a setting holds text.
fn number_to_text(number: f64) -> String {
    if number.fract() == 0.0 && number.abs() < 1e15 {
        format!("{}", number as i64)
    } else {
        format!("{number}")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn definition(name: &str, location: u8, category: i32, group: i32) -> SettingDefinitionRecord {
        SettingDefinitionRecord {
            name: name.to_owned(),
            label: name.to_owned(),
            full_label: String::new(),
            tooltip: String::new(),
            location,
            overrides_in: Vec::new(),
            category,
            option_group: group,
            category_title: "Supports".to_owned(),
            option_group_title: "Generation".to_owned(),
            order: 0,
            gui_type: "undefined".to_owned(),
            units: Vec::new(),
            has_min: false,
            min: 0.0,
            has_max: false,
            max: 0.0,
            multiline: false,
            full_width: false,
            enum_values: Vec::new(),
            default_value: SettingValueRecord {
                kind: 4,
                text: String::new(),
                ..invalid_record()
            },
        }
    }

    fn new_model(definitions: Vec<SettingDefinitionRecord>) -> Box<Model> {
        new(&definitions, "{}", "{}")
    }

    #[test]
    fn a_layout_document_that_cannot_be_read_is_reported() {
        let model = new_model(vec![definition("layer_height", LOCATION_PRINT, 100, 100)]);
        assert!(!layout_loaded(&model));
        let model = new(&[], "{\"tabs\":{}}", "{}");
        assert!(layout_loaded(&model));
    }

    #[test]
    fn the_colour_mixing_filter_is_five_names_and_a_renamed_neighbour_stays() {
        let model = new_model(vec![
            definition("filament_flush_volume", LOCATION_FILAMENT, 200, 5000),
            definition("filament_flush_band", LOCATION_FILAMENT, 200, 5000),
            definition("mmu_segmented_region_max_width", LOCATION_PRINT, 108, 900),
            definition(
                "single_extruder_multi_material",
                LOCATION_PRINTER,
                300,
                10001,
            ),
        ]);
        let names: Vec<&str> = model
            .entries
            .iter()
            .map(|entry| entry.name.as_str())
            .collect();
        assert_eq!(
            names,
            vec!["filament_flush_band", "single_extruder_multi_material"]
        );
    }

    #[test]
    fn a_tree_shape_is_the_engine_style_plus_its_numbers_and_reads_back() {
        let mut style = definition("support_material_style", LOCATION_PRINT, 104, 500);
        style.enum_values = vec![
            crate::bridge::SettingEnumValueRecord {
                serialized: "grid".to_owned(),
                label: "Grid".to_owned(),
                number: 0,
            },
            crate::bridge::SettingEnumValueRecord {
                serialized: "organic".to_owned(),
                label: "Organic".to_owned(),
                number: 1,
            },
        ];
        style.default_value = SettingValueRecord {
            kind: 8,
            text: "grid".to_owned(),
            ..invalid_record()
        };
        let mut definitions: Vec<SettingDefinitionRecord> = TREE_PROFILES[3]
            .knobs
            .iter()
            .map(|(name, _)| {
                let mut knob = definition(*name, LOCATION_PRINT, 104, 504);
                knob.default_value = SettingValueRecord {
                    kind: 3,
                    number: 1.0,
                    ..invalid_record()
                };
                knob
            })
            .collect();
        definitions.push(style);
        let mut model = new_model(definitions);

        let input = input_of_json(&Json::from("tree_hybrid"));
        let outcome = set_value(&mut model, "support_material_style", &input);
        assert!(outcome.applied, "the shape was refused");
        assert!(outcome.changed, "applying a shape moved nothing");

        for (name, value) in TREE_PROFILES[3].knobs {
            let entry = model.find(name).expect("a knob");
            assert!(same(&entry.current_value, &Value::Number(*value)), "{name}");
        }
        assert_eq!(block_of(&model, "support_material_style"), "process");
        let item = item_of(
            &model,
            model.find("support_material_style").expect("the style"),
        );
        assert_eq!(item.value.text, "tree_hybrid");
        assert_eq!(item.choices.len(), 5);
    }

    #[test]
    fn the_yaml_of_a_value_is_the_preset_spelling() {
        assert_eq!(yaml_of(&Value::Number(0.25)), " 0.25");
        assert_eq!(yaml_of(&Value::Boolean(true)), " true");
        assert_eq!(yaml_of(&Value::Percent(15.0)), " '15%'");
        assert_eq!(
            yaml_of(&Value::FloatOrPercent {
                value: 20.0,
                percent: false
            }),
            " 20"
        );
        assert_eq!(yaml_of(&Value::Texts(Vec::new())), " []");
        assert_eq!(
            yaml_of(&Value::Points(vec![(0.0, 0.0), (350.0, 0.0)])),
            "\n  - '0x0'\n  - '350x0'"
        );
    }

    #[test]
    fn an_optional_integer_vector_takes_the_number_every_slot_means() {
        let mut idle = definition("idle_temperature", LOCATION_FILAMENT, 200, 5000);
        idle.default_value = SettingValueRecord {
            kind: 5,
            items: 6,
            ..invalid_record()
        };
        let mut model = new(
            &[idle],
            "{}",
            "{\"configuration\":{\"filament_settings\":{\"idle_temperature\":[170]}}}",
        );
        assert_eq!(value(&model, "idle_temperature").numbers, vec![170.0]);

        assert!(seed_from_config_json(
            &mut model,
            "{\"configuration\":{\"filament_settings\":{\"idle_temperature\":[null]}}}"
        ));
        assert_eq!(value(&model, "idle_temperature").numbers, vec![170.0]);
    }

    #[test]
    fn a_bed_shape_seeded_from_a_config_comes_back_as_points() {
        let mut bed = definition("bed_shape", LOCATION_PRINTER, 300, 0);
        bed.default_value = SettingValueRecord {
            kind: 5,
            items: 2,
            ..invalid_record()
        };
        let mut model = new(&[bed], "{}", "{}");
        assert!(seed_from_config_json(
            &mut model,
            "{\"configuration\":{\"printer_settings\":{\"bed_shape\":\
             [[0,0],[350,0],[350,350],[0,350]]}}}"
        ));
        let record = value(&model, "bed_shape");
        assert_eq!(record.kind, 5);
        assert_eq!(record.items, 2);
        assert_eq!(record.numbers.len(), 8);
    }

    #[test]
    fn the_first_filament_slot_is_the_one_the_panel_edits_and_the_rest_survive() {
        let mut nozzle = definition("nozzle_diameter", LOCATION_FILAMENT, 200, 5000);
        nozzle.default_value = SettingValueRecord {
            kind: 3,
            number: 0.4,
            ..invalid_record()
        };
        let mut model = new(
            &[nozzle],
            "{}",
            "{\"configuration\":{\"filament_settings\":{\"nozzle_diameter\":[0.4,0.6]}}}",
        );
        let input = input_of_json(&Json::from(0.5));
        assert!(set_value(&mut model, "nozzle_diameter", &input).applied);
        let text = config_json(&model);
        assert!(text.contains("\"nozzle_diameter\":[0.5,0.6]"), "{text}");
    }

    #[test]
    fn two_values_that_differ_in_the_last_bit_are_the_same_value() {
        assert!(same(&Value::Number(0.1 + 0.2), &Value::Number(0.3)));
        assert!(!same(&Value::Number(1.0), &Value::Number(1.0 + 1e-12)));
        assert!(!same(&Value::Integer(1), &Value::Number(1.0)));
        assert!(same(
            &Value::FloatOrPercent {
                value: 15.0,
                percent: true
            },
            &Value::FloatOrPercent {
                value: 15.0,
                percent: true
            }
        ));
        assert!(!same(
            &Value::FloatOrPercent {
                value: 15.0,
                percent: true
            },
            &Value::FloatOrPercent {
                value: 15.0,
                percent: false
            }
        ));
    }
}
