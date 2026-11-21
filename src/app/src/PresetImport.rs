// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

//! Reading another slicer's presets into this engine's user bundle.
//!
//! This module has no engine call in it, which is why it can be Rust while
//! `PresetStore` cannot: it reads files, merges `inherits` chains, renames keys
//! against the settings model's own vocabulary, and writes preset YAML through
//! `crate::PresetFiles`. The vocabulary comes from `crate::SettingsModel`: a key is
//! written into the block it belongs to, so a machine profile's retraction settings
//! do not land in a printer preset.
//!
//! Three things are deliberately not idiomatic:
//!
//! - the settings model is mutated **in place and synchronously**, in the order
//!   reset, apply, settle, write — `PresetStore::finishImport` calls
//!   `clearModified()` on what is left behind, and any other order leaves the
//!   saved-diff state wrong without an error;
//! - the config location arrives as Qt reports it, a `file://` URL, so it is
//!   percent-decoded here rather than treated as a path;
//! - the report is nine separate lists, not a summary, because the caller shows
//!   refused names and dropped keys side by side.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Path, PathBuf};

use serde_json::{Map, Value};

use crate::bridge::{
    ImportReportRecord, SettingElementRecord, SettingInputRecord, SettingScalarRecord,
    SettingsModelCore,
};
use crate::PresetFiles;
use crate::SettingsModel;

/// What an import produced, in the user's own names. `refused` is not an error: a
/// preset this engine has no vocabulary for is reported rather than guessed at.
#[derive(Clone, Debug, Default)]
pub struct ImportReport {
    pub printers: Vec<String>,
    pub prints: Vec<String>,
    pub filaments: Vec<String>,
    pub refused: Vec<String>,
    pub elsewhere: Vec<String>,
    pub dropped_keys: Vec<String>,
    pub unfitted: Vec<String>,
    pub dropped_templates: Vec<String>,
    pub notes: Vec<String>,
    pub error: String,
}

impl ImportReport {
    pub fn ok(&self) -> bool {
        self.error.is_empty()
    }

    pub fn count(&self) -> i32 {
        (self.printers.len() + self.prints.len() + self.filaments.len()) as i32
    }

    /// The same nine lists as the record the C++ caller turns back into its own
    /// `Report`, which is the shape `PresetStore::finishImport` takes.
    pub fn into_record(self) -> ImportReportRecord {
        ImportReportRecord {
            printers: self.printers,
            prints: self.prints,
            filaments: self.filaments,
            refused: self.refused,
            elsewhere: self.elsewhere,
            dropped_keys: self.dropped_keys,
            unfitted: self.unfitted,
            dropped_templates: self.dropped_templates,
            notes: self.notes,
            error: self.error,
        }
    }
}

/// One other slicer's configuration directory.
#[derive(Clone, Debug)]
pub struct SlicerSource {
    pub name: String,
    pub path: String,
}

/// A preset as the other slicer stores it: kind, then name, then the object.
pub type KindIndex = HashMap<String, HashMap<String, Map<String, Value>>>;

const KINDS: [&str; 3] = ["machine", "process", "filament"];

/// What one row of the mapping table says about a value.
#[derive(Clone, Copy, PartialEq)]
enum Transform {
    Rename,
    PercentObject,
    Integer,
    Real,
    RealList,
    Boolean,
    BedShape,
    IntAtLeastOne,
    Enum,
    EnumBoolean,
}

struct Mapping {
    source: &'static str,
    target: &'static str,
    transform: Transform,
    table: &'static str,
}

/// The five slicers this app knows how to read, in the order they are worth
/// offering. Absent directories are included; the caller checks them.
pub fn slicer_candidates(config_location: &str) -> Vec<SlicerSource> {
    let home = config_home(config_location);
    [
        "OrcaSlicer",
        "PrusaSlicer",
        "SuperSlicer",
        "BambuStudio",
        "Slic3r",
    ]
    .iter()
    .map(|name| SlicerSource {
        name: (*name).to_owned(),
        path: format!("{home}/{name}"),
    })
    .collect()
}

/// Qt 6 hands `QStandardPaths::ConfigLocation` back as a `file://` URL, and preset
/// names carry spaces: a plain path lookup of the URL finds a `%20`-encoded
/// directory that does not exist, so the import silently finds nothing at all.
pub fn config_home(config_location: &str) -> String {
    let Some(rest) = config_location.strip_prefix("file://") else {
        return config_location.to_owned();
    };
    let authority_end = rest.find('/').unwrap_or(rest.len());
    percent_decode(&rest[authority_end..])
}

fn percent_decode(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out: Vec<u8> = Vec::with_capacity(bytes.len());
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] == b'%' && index + 2 < bytes.len() {
            let high = (bytes[index + 1] as char).to_digit(16);
            let low = (bytes[index + 2] as char).to_digit(16);
            if let (Some(high), Some(low)) = (high, low) {
                out.push((high * 16 + low) as u8);
                index += 3;
                continue;
            }
        }
        out.push(bytes[index]);
        index += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// The kind names this engine's own preset writer takes.
fn empty_index() -> KindIndex {
    let mut index: KindIndex = HashMap::new();
    for kind in KINDS {
        index.insert(kind.to_owned(), HashMap::new());
    }
    index
}

/// Every preset an installation knows, by kind and then by name: its vendor
/// bundles first, its own saved presets over them. Kinds are kept apart because
/// that slicer reuses names across them: a process preset called "Voron 0.2" sits
/// beside the machine of the same name, and one flat index would let it shadow the
/// machine and lose its bed.
pub fn preset_index(source: &Path) -> KindIndex {
    let mut index = empty_index();
    if !source.is_dir() {
        return index;
    }

    let system = source.join("system");
    let mut system_files: Vec<PathBuf> = Vec::new();
    collect_json_files(&system, &mut system_files);
    system_files.sort();
    for path in system_files {
        let Some(object) = read_json_object(&path) else {
            continue;
        };
        let kind = match object.get("type").and_then(Value::as_str) {
            Some(kind) if KINDS.contains(&kind) => kind.to_owned(),
            _ => {
                let directory = path
                    .parent()
                    .and_then(Path::file_name)
                    .and_then(|name| name.to_str());
                match directory {
                    Some(directory) if KINDS.contains(&directory) => directory.to_owned(),
                    _ => "process".to_owned(),
                }
            }
        };
        append_presets(&path, index.entry(kind).or_default());
    }

    for kind in KINDS {
        for path in user_preset_paths(source, kind) {
            append_presets(&path, index.entry(kind.to_owned()).or_default());
        }
    }

    if let Ok(entries) = std::fs::read_dir(source) {
        for entry in entries.flatten() {
            let path = entry.path();
            if !path.is_file() || path.extension().and_then(|value| value.to_str()) != Some("json")
            {
                continue;
            }
            for kind in KINDS {
                append_presets(&path, index.entry(kind.to_owned()).or_default());
            }
        }
    }

    index
}

/// Every `.json` under `directory`, recursively, as `QDirIterator::Subdirectories`
/// would list it.
fn collect_json_files(directory: &Path, found: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(directory) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect_json_files(&path, found);
            continue;
        }
        if path.extension().and_then(|value| value.to_str()) == Some("json") {
            found.push(path);
        }
    }
}

/// One file that may hold one preset object or an array of them.
fn append_presets(path: &Path, index: &mut HashMap<String, Map<String, Value>>) {
    let Ok(text) = std::fs::read_to_string(path) else {
        return;
    };
    let Ok(value) = serde_json::from_str::<Value>(&text) else {
        return;
    };
    let insert = |entry: &Map<String, Value>, index: &mut HashMap<String, Map<String, Value>>| {
        if let Some(name) = entry.get("name").and_then(Value::as_str) {
            index.insert(name.to_owned(), entry.clone());
        }
    };
    match value {
        Value::Array(items) => {
            for item in items {
                if let Value::Object(entry) = item {
                    insert(&entry, index);
                }
            }
        }
        Value::Object(entry) => insert(&entry, index),
        _ => {}
    }
}

fn read_json_object(path: &Path) -> Option<Map<String, Value>> {
    let text = std::fs::read_to_string(path).ok()?;
    match serde_json::from_str::<Value>(&text).ok()? {
        Value::Object(object) => Some(object),
        Value::Array(items) => match items.into_iter().next() {
            Some(Value::Object(object)) => Some(object),
            _ => Some(Map::new()),
        },
        _ => Some(Map::new()),
    }
}

fn user_preset_paths(source: &Path, kind: &str) -> Vec<PathBuf> {
    let directory = source.join("user").join("default").join(kind);
    let Ok(entries) = std::fs::read_dir(&directory) else {
        return Vec::new();
    };
    let mut paths: Vec<PathBuf> = entries
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.is_file() && path.extension().and_then(|value| value.to_str()) == Some("json"))
        .collect();
    paths.sort();
    paths
}

/// A preset merged with its `inherits` ancestors, child values winning.
pub fn resolve_preset(kind: &str, name: &str, index: &KindIndex) -> Map<String, Value> {
    let mut merged = Map::new();
    let Some(slot) = index.get(kind) else {
        return merged;
    };
    let mut chain: Vec<&Map<String, Value>> = Vec::new();
    let mut seen: HashSet<&str> = HashSet::new();
    let mut current = name;
    while !current.is_empty() && !seen.contains(current) {
        seen.insert(current);
        let Some(entry) = slot.get(current) else {
            break;
        };
        chain.push(entry);
        current = entry.get("inherits").and_then(Value::as_str).unwrap_or("");
    }
    for entry in chain.iter().rev() {
        for (key, value) in entry.iter() {
            if key != "inherits" {
                merged.insert(key.clone(), value.clone());
            }
        }
    }
    merged
}

/// Keys that describe the other slicer's own bookkeeping rather than a setting.
fn meta_keys() -> &'static [&'static str] {
    &[
        "type",
        "name",
        "inherits",
        "from",
        "setting_id",
        "instantiation",
        "compatible_printers",
        "compatible_printers_condition",
        "compatible_prints",
        "compatible_prints_condition",
        "compatible_filaments",
        "compatible_filaments_condition",
        "is_custom_defined",
        "version",
        "renamed_from",
        "upward_compatible",
        "inherits_prints",
        "default_filament_profile",
        "default_print_profile",
        "filament_settings_id",
        "print_settings_id",
        "printer_settings_id",
        "filament_id",
        "filament_extruder_variant",
        "print_extruder_variant",
        "printer_extruder_variant",
        "printer_extruder_id",
        "print_extruder_id",
        "host_type",
        "print_host",
        "printer_agent",
        "filename_format",
        "extruder_colour",
        "filament_vendor",
        "nozzle_type",
    ]
}

/// Keys whose value shape or meaning this engine has no equivalent for, dropped
/// and reported rather than guessed at. The X/Y shrinkage compensation is inverted
/// between the two: there 100% means "no compensation", here 0% does, so importing
/// it scales the part away and the slice comes back empty.
fn unmapped_keys() -> &'static [&'static str] {
    &["filament_shrinkage_compensation_xy"]
}

/// The one key this engine has but must never receive from an import: binary
/// G-code cannot be sliced.
fn dangerous_defaults() -> &'static [&'static str] {
    &["binary_gcode"]
}

/// The templates that decide whether a slice writes a file at all.
fn gcode_template_keys() -> &'static [&'static str] {
    &[
        "start_gcode",
        "end_gcode",
        "before_layer_gcode",
        "layer_gcode",
        "toolchange_gcode",
        "between_objects_gcode",
        "color_change_gcode",
        "pause_print_gcode",
        "filament_start_gcode",
        "filament_end_gcode",
    ]
}

/// The other slicer's vocabulary to this engine's. Every entry is a setting the
/// engine has; nothing here is guessed, and anything absent is reported as dropped
/// rather than written with a name the engine will reject. A source that appears
/// more than once is read through its first row, so the order below is the table.
fn mapping_table() -> &'static [Mapping] {
    &[
        Mapping { source: "bottom_shell_layers", target: "bottom_solid_layers", transform: Transform::Integer, table: "" },
        Mapping { source: "top_shell_layers", target: "top_solid_layers", transform: Transform::Integer, table: "" },
        Mapping { source: "bottom_shell_thickness", target: "bottom_solid_min_thickness", transform: Transform::Real, table: "" },
        Mapping { source: "top_shell_thickness", target: "top_solid_min_thickness", transform: Transform::Real, table: "" },
        Mapping { source: "wall_loops", target: "perimeters", transform: Transform::Integer, table: "" },
        Mapping { source: "spiral_mode", target: "spiral_vase", transform: Transform::Boolean, table: "" },
        Mapping { source: "ensure_vertical_wall_thickness", target: "ensure_vertical_shell_thickness", transform: Transform::Boolean, table: "" },
        Mapping { source: "detect_thin_wall", target: "thin_walls", transform: Transform::Boolean, table: "" },
        Mapping { source: "extra_perimeters_on_overhangs", target: "extra_perimeters_on_overhangs", transform: Transform::Rename, table: "" },
        Mapping { source: "sparse_infill_density", target: "fill_density", transform: Transform::PercentObject, table: "" },
        Mapping { source: "sparse_infill_pattern", target: "fill_pattern", transform: Transform::Enum, table: "fill_pattern" },
        Mapping { source: "sparse_infill_line_width", target: "infill_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "infill_wall_overlap", target: "infill_overlap", transform: Transform::PercentObject, table: "" },
        Mapping { source: "internal_solid_infill_line_width", target: "solid_infill_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "top_surface_pattern", target: "top_fill_pattern", transform: Transform::Enum, table: "fill_pattern" },
        Mapping { source: "bottom_surface_pattern", target: "bottom_fill_pattern", transform: Transform::Enum, table: "fill_pattern" },
        Mapping { source: "infill_direction", target: "fill_angle", transform: Transform::Real, table: "" },
        Mapping { source: "infill_combination", target: "infill_every_layers", transform: Transform::IntAtLeastOne, table: "" },
        Mapping { source: "minimum_sparse_infill_area", target: "solid_infill_below_area", transform: Transform::Real, table: "" },
        Mapping { source: "sparse_infill_anchor_max", target: "infill_anchor_max", transform: Transform::Real, table: "" },
        Mapping { source: "sparse_infill_anchor", target: "infill_anchor", transform: Transform::Real, table: "" },
        Mapping { source: "line_width", target: "extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "outer_wall_line_width", target: "external_perimeter_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "inner_wall_line_width", target: "perimeter_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "top_surface_line_width", target: "top_infill_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "initial_layer_line_width", target: "first_layer_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "support_line_width", target: "support_material_extrusion_width", transform: Transform::Real, table: "" },
        Mapping { source: "outer_wall_speed", target: "external_perimeter_speed", transform: Transform::Real, table: "" },
        Mapping { source: "inner_wall_speed", target: "perimeter_speed", transform: Transform::Real, table: "" },
        Mapping { source: "sparse_infill_speed", target: "infill_speed", transform: Transform::Real, table: "" },
        Mapping { source: "internal_solid_infill_speed", target: "solid_infill_speed", transform: Transform::Real, table: "" },
        Mapping { source: "top_surface_speed", target: "top_solid_infill_speed", transform: Transform::Real, table: "" },
        Mapping { source: "gap_infill_speed", target: "gap_fill_speed", transform: Transform::Real, table: "" },
        Mapping { source: "initial_layer_speed", target: "first_layer_speed", transform: Transform::Real, table: "" },
        Mapping { source: "initial_layer_infill_speed", target: "first_layer_infill_speed", transform: Transform::Real, table: "" },
        Mapping { source: "support_speed", target: "support_material_speed", transform: Transform::Real, table: "" },
        Mapping { source: "support_interface_speed", target: "support_material_interface_speed", transform: Transform::Real, table: "" },
        Mapping { source: "overhang_1_4_speed", target: "overhang_speed_0", transform: Transform::Real, table: "" },
        Mapping { source: "overhang_2_4_speed", target: "overhang_speed_1", transform: Transform::Real, table: "" },
        Mapping { source: "overhang_3_4_speed", target: "overhang_speed_2", transform: Transform::Real, table: "" },
        Mapping { source: "overhang_4_4_speed", target: "overhang_speed_3", transform: Transform::Real, table: "" },
        Mapping { source: "outer_wall_acceleration", target: "external_perimeter_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "inner_wall_acceleration", target: "perimeter_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "top_surface_acceleration", target: "top_solid_infill_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "sparse_infill_acceleration", target: "infill_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "internal_solid_infill_acceleration", target: "solid_infill_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "initial_layer_acceleration", target: "first_layer_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "bridge_infill_acceleration", target: "bridge_acceleration", transform: Transform::Real, table: "" },
        Mapping { source: "enable_support", target: "support_material", transform: Transform::Enum, table: "support_material_on" },
        Mapping { source: "support_material", target: "support_material", transform: Transform::Enum, table: "support_material_on" },
        Mapping { source: "support_style", target: "support_material_style", transform: Transform::Enum, table: "support_material_style" },
        Mapping { source: "support_on_build_plate_only", target: "support_material_buildplate_only", transform: Transform::Boolean, table: "" },
        Mapping { source: "support_top_z_distance", target: "support_material_contact_distance", transform: Transform::Real, table: "" },
        Mapping { source: "support_bottom_z_distance", target: "support_material_bottom_contact_distance", transform: Transform::Real, table: "" },
        Mapping { source: "support_interface_top_layers", target: "support_material_interface_layers", transform: Transform::Integer, table: "" },
        Mapping { source: "support_interface_bottom_layers", target: "support_material_bottom_interface_layers", transform: Transform::Integer, table: "" },
        Mapping { source: "support_interface_spacing", target: "support_material_interface_spacing", transform: Transform::Real, table: "" },
        Mapping { source: "support_base_pattern", target: "support_material_pattern", transform: Transform::Enum, table: "support_material_pattern" },
        Mapping { source: "support_base_pattern_spacing", target: "support_material_spacing", transform: Transform::Real, table: "" },
        Mapping { source: "support_object_xy_distance", target: "support_material_xy_spacing", transform: Transform::Real, table: "" },
        Mapping { source: "support_threshold_angle", target: "support_material_threshold", transform: Transform::Integer, table: "" },
        Mapping { source: "tree_support_branch_angle", target: "support_tree_angle", transform: Transform::Real, table: "" },
        Mapping { source: "support_interface_loop_pattern", target: "support_material_interface_contact_loops", transform: Transform::Boolean, table: "" },
        Mapping { source: "support_interface_pattern", target: "support_material_interface_pattern", transform: Transform::Enum, table: "support_material_pattern" },
        Mapping { source: "independent_support_layer_height", target: "support_material_synchronize_layers", transform: Transform::Boolean, table: "" },
        Mapping { source: "brim_object_gap", target: "brim_separation", transform: Transform::Real, table: "" },
        Mapping { source: "skirt_loops", target: "skirts", transform: Transform::Integer, table: "" },
        Mapping { source: "seam_gap", target: "seam_gap_distance", transform: Transform::Real, table: "" },
        Mapping { source: "staggered_inner_seams", target: "staggered_inner_seams", transform: Transform::Rename, table: "" },
        Mapping { source: "retraction_length", target: "retract_length", transform: Transform::Real, table: "" },
        Mapping { source: "retraction_speed", target: "retract_speed", transform: Transform::Real, table: "" },
        Mapping { source: "z_hop", target: "retract_lift", transform: Transform::Real, table: "" },
        Mapping { source: "retraction_minimum_travel", target: "retract_before_travel", transform: Transform::Real, table: "" },
        Mapping { source: "retract_when_changing_layer", target: "retract_layer_change", transform: Transform::Boolean, table: "" },
        Mapping { source: "retract_length_toolchange", target: "retract_length_toolchange", transform: Transform::Real, table: "" },
        Mapping { source: "retract_restart_extra_toolchange", target: "retract_restart_extra_toolchange", transform: Transform::Real, table: "" },
        Mapping { source: "wipe_on_loops", target: "wipe", transform: Transform::Boolean, table: "" },
        Mapping { source: "avoid_crossing_perimeters", target: "avoid_crossing_perimeters", transform: Transform::Boolean, table: "" },
        Mapping { source: "avoid_crossing_perimeters_max_detour", target: "avoid_crossing_perimeters_max_detour", transform: Transform::Real, table: "" },
        Mapping { source: "filament_flow_ratio", target: "extrusion_multiplier", transform: Transform::Real, table: "" },
        Mapping { source: "nozzle_temperature", target: "temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "nozzle_temperature_initial_layer", target: "first_layer_temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "bed_temperature_initial_layer", target: "first_layer_bed_temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "hot_plate_temp", target: "bed_temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "hot_plate_temp_initial_layer", target: "first_layer_bed_temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "textured_plate_temp", target: "bed_temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "textured_plate_temp_initial_layer", target: "first_layer_bed_temperature", transform: Transform::Integer, table: "" },
        Mapping { source: "fan_cooling_layer_time", target: "fan_below_layer_time", transform: Transform::Real, table: "" },
        Mapping { source: "enable_overhang_speed", target: "enable_dynamic_overhang_speeds", transform: Transform::Boolean, table: "" },
        Mapping { source: "fan_min_speed", target: "min_fan_speed", transform: Transform::Integer, table: "" },
        Mapping { source: "fan_max_speed", target: "max_fan_speed", transform: Transform::Integer, table: "" },
        Mapping { source: "reduce_fan_stop_start_freq", target: "fan_always_on", transform: Transform::Boolean, table: "" },
        Mapping { source: "slow_down_layer_time", target: "slowdown_below_layer_time", transform: Transform::Real, table: "" },
        Mapping { source: "slow_down_min_speed", target: "min_print_speed", transform: Transform::Real, table: "" },
        Mapping { source: "filament_shrinkage_compensation_z", target: "filament_shrinkage_compensation_z", transform: Transform::Real, table: "" },
        Mapping { source: "filament_max_volumetric_speed", target: "filament_max_volumetric_speed", transform: Transform::Real, table: "" },
        Mapping { source: "filament_retraction_length", target: "retract_length", transform: Transform::Real, table: "" },
        Mapping { source: "filament_retraction_speed", target: "retract_speed", transform: Transform::Real, table: "" },
        Mapping { source: "filament_retraction_minimum_travel", target: "retract_before_travel", transform: Transform::Real, table: "" },
        Mapping { source: "filament_retract_before_wipe", target: "retract_before_wipe", transform: Transform::Real, table: "" },
        Mapping { source: "filament_retract_restart_extra", target: "retract_restart_extra", transform: Transform::Real, table: "" },
        Mapping { source: "filament_retract_when_changing_layer", target: "retract_layer_change", transform: Transform::Boolean, table: "" },
        Mapping { source: "filament_z_hop", target: "retract_lift", transform: Transform::Real, table: "" },
        Mapping { source: "filament_wipe", target: "wipe", transform: Transform::Boolean, table: "" },
        Mapping { source: "initial_layer_print_height", target: "first_layer_height", transform: Transform::Real, table: "" },
        Mapping { source: "printable_area", target: "bed_shape", transform: Transform::BedShape, table: "" },
        Mapping { source: "bed_shape", target: "bed_shape", transform: Transform::BedShape, table: "" },
        Mapping { source: "printable_height", target: "max_print_height", transform: Transform::Real, table: "" },
        Mapping { source: "machine_max_speed_x", target: "machine_max_feedrate_x", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_speed_y", target: "machine_max_feedrate_y", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_speed_z", target: "machine_max_feedrate_z", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_speed_e", target: "machine_max_feedrate_e", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_acceleration_x", target: "machine_max_acceleration_x", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_acceleration_y", target: "machine_max_acceleration_y", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_acceleration_z", target: "machine_max_acceleration_z", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_acceleration_e", target: "machine_max_acceleration_e", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_jerk_x", target: "machine_max_jerk_x", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_jerk_y", target: "machine_max_jerk_y", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_jerk_z", target: "machine_max_jerk_z", transform: Transform::RealList, table: "" },
        Mapping { source: "machine_max_jerk_e", target: "machine_max_jerk_e", transform: Transform::RealList, table: "" },
        Mapping { source: "gcode_flavor", target: "gcode_flavor", transform: Transform::Rename, table: "" },
        Mapping { source: "before_layer_change_gcode", target: "before_layer_gcode", transform: Transform::Rename, table: "" },
        Mapping { source: "layer_change_gcode", target: "layer_gcode", transform: Transform::Rename, table: "" },
        Mapping { source: "machine_start_gcode", target: "start_gcode", transform: Transform::Rename, table: "" },
        Mapping { source: "machine_end_gcode", target: "end_gcode", transform: Transform::Rename, table: "" },
        Mapping { source: "machine_pause_gcode", target: "pause_print_gcode", transform: Transform::Rename, table: "" },
        Mapping { source: "machine_load_filament_time", target: "filament_loading_speed", transform: Transform::Real, table: "" },
        Mapping { source: "machine_unload_filament_time", target: "filament_unloading_speed", transform: Transform::Real, table: "" },
        Mapping { source: "retract_lift_above", target: "retract_lift_above", transform: Transform::Real, table: "" },
        Mapping { source: "retract_lift_below", target: "retract_lift_below", transform: Transform::Real, table: "" },
        Mapping { source: "extruder_clearance_height_to_rod", target: "extruder_clearance_height", transform: Transform::Real, table: "" },
        Mapping { source: "extruder_clearance_height_to_lid", target: "extruder_clearance_height", transform: Transform::Real, table: "" },
        Mapping { source: "extruder_clearance_radius", target: "extruder_clearance_radius", transform: Transform::Real, table: "" },
        Mapping { source: "extruder_offset", target: "extruder_offset", transform: Transform::Rename, table: "" },
        Mapping { source: "thumbnails", target: "thumbnails", transform: Transform::Rename, table: "" },
        Mapping { source: "filament_colour", target: "filament_colour", transform: Transform::Rename, table: "" },
        Mapping { source: "filament_notes", target: "filament_notes", transform: Transform::Rename, table: "" },
        Mapping { source: "notes", target: "notes", transform: Transform::Rename, table: "" },
        Mapping { source: "print_sequence", target: "complete_objects", transform: Transform::EnumBoolean, table: "print_sequence" },
        Mapping { source: "resolution", target: "resolution", transform: Transform::Real, table: "" },
        Mapping { source: "arc_fitting", target: "arc_fitting", transform: Transform::Enum, table: "arc_fitting" },
        Mapping { source: "enable_arc_fitting", target: "arc_fitting", transform: Transform::Enum, table: "arc_fitting" },
        Mapping { source: "enable_prime_tower", target: "wipe_tower", transform: Transform::Boolean, table: "" },
        Mapping { source: "prime_tower_width", target: "wipe_tower_width", transform: Transform::Real, table: "" },
        Mapping { source: "prime_tower_brim_width", target: "wipe_tower_brim_width", transform: Transform::Real, table: "" },
        Mapping { source: "prime_tower_extra_spacing", target: "wipe_tower_extra_spacing", transform: Transform::PercentObject, table: "" },
        Mapping { source: "prime_tower_extra_flow", target: "wipe_tower_extra_flow", transform: Transform::PercentObject, table: "" },
        Mapping { source: "support_type", target: "support_material_style", transform: Transform::Enum, table: "support_type_style" },
        Mapping { source: "support_threshold_angle", target: "support_material_threshold", transform: Transform::Real, table: "" },
        Mapping { source: "tree_support_branch_angle", target: "support_tree_angle", transform: Transform::Real, table: "" },
        Mapping { source: "tree_support_branch_distance", target: "support_tree_branch_distance", transform: Transform::Real, table: "" },
        Mapping { source: "tree_support_tip_diameter", target: "support_tree_tip_diameter", transform: Transform::Real, table: "" },
        Mapping { source: "tree_support_top_rate", target: "support_tree_top_rate", transform: Transform::PercentObject, table: "" },
        Mapping { source: "infill_combination_max_layer_height", target: "automatic_infill_combination_max_layer_height", transform: Transform::PercentObject, table: "" },
        Mapping { source: "scarf_seam_placement", target: "scarf_seam_placement", transform: Transform::Enum, table: "scarf_seam_placement" },
        Mapping { source: "scarf_seam_length", target: "scarf_seam_length", transform: Transform::Real, table: "" },
        Mapping { source: "scarf_seam_start_height", target: "scarf_seam_start_height", transform: Transform::PercentObject, table: "" },
        Mapping { source: "enable_pressure_advance", target: "pressure_advance", transform: Transform::Enum, table: "pressure_advance_on" },
        Mapping { source: "pressure_advance", target: "pressure_advance_value", transform: Transform::Real, table: "" },
        Mapping { source: "hot_plate_temp", target: "bed_temperature", transform: Transform::RealList, table: "" },
        Mapping { source: "hot_plate_temp_initial_layer", target: "first_layer_bed_temperature", transform: Transform::RealList, table: "" },
        Mapping { source: "first_layer_height", target: "first_layer_height", transform: Transform::Real, table: "" },
        Mapping { source: "xy_hole_compensation", target: "xy_size_compensation", transform: Transform::Real, table: "" },
        Mapping { source: "xy_contour_compensation", target: "xy_size_compensation", transform: Transform::Real, table: "" },
        Mapping { source: "elefant_foot_compensation", target: "elefant_foot_compensation", transform: Transform::Real, table: "" },
        Mapping { source: "fuzzy_skin", target: "fuzzy_skin", transform: Transform::Enum, table: "fuzzy_skin" },
        Mapping { source: "fuzzy_skin_point_distance", target: "fuzzy_skin_point_dist", transform: Transform::Real, table: "" },
        Mapping { source: "fuzzy_skin_thickness", target: "fuzzy_skin_thickness", transform: Transform::Real, table: "" },
        Mapping { source: "ironing_flow", target: "ironing_flowrate", transform: Transform::PercentObject, table: "" },
        Mapping { source: "ironing_spacing", target: "ironing_spacing", transform: Transform::Real, table: "" },
        Mapping { source: "ironing_speed", target: "ironing_speed", transform: Transform::Real, table: "" },
        Mapping { source: "ironing_type", target: "ironing_type", transform: Transform::Enum, table: "ironing_type" },
        Mapping { source: "bridge_flow", target: "bridge_flow_ratio", transform: Transform::Real, table: "" },
        Mapping { source: "bridge_speed", target: "bridge_speed", transform: Transform::Real, table: "" },
        Mapping { source: "bridge_no_support", target: "dont_support_bridges", transform: Transform::Boolean, table: "" },
        Mapping { source: "draft_shield", target: "draft_shield", transform: Transform::Enum, table: "draft_shield" },
        Mapping { source: "ooze_prevention", target: "ooze_prevention", transform: Transform::Boolean, table: "" },
        Mapping { source: "wipe_tower", target: "wipe_tower", transform: Transform::Boolean, table: "" },
        Mapping { source: "single_extruder_multi_material", target: "single_extruder_multi_material", transform: Transform::Boolean, table: "" },
        Mapping { source: "single_extruder_multi_material_priming", target: "single_extruder_multi_material_priming", transform: Transform::Boolean, table: "" },
        Mapping { source: "flush_into_objects", target: "wipe_into_objects", transform: Transform::Boolean, table: "" },
        Mapping { source: "wall_infill_order", target: "infill_first", transform: Transform::Boolean, table: "" },
    ]
}

fn find_mapping(source: &str) -> Option<&'static Mapping> {
    mapping_table().iter().find(|entry| entry.source == source)
}

/// The value spellings a setting accepts, as the reference slicer writes them.
fn enum_table(name: &str) -> &'static [(&'static str, &'static str)] {
    match name {
        "fill_pattern" => &[
            ("crosshatch", "grid"), ("grid", "grid"), ("honeycomb", "honeycomb"),
            ("gyroid", "gyroid"), ("cubic", "cubic"), ("triangles", "triangles"),
            ("stars", "stars"), ("tri-hexagon", "trihexagon"), ("concentric", "concentric"),
            ("line", "rectilinear"), ("rectilinear", "rectilinear"), ("monotonic", "monotonic"),
            ("monotonicline", "monotonic"), ("hilbertcurve", "hilbertcurve"),
        ],
        "support_material_on" => &[
            ("0", "none"), ("1", "everywhere"), ("false", "none"), ("true", "everywhere"),
            ("none", "none"),
        ],
        "support_type_auto" => &[
            ("normal(auto)", "1"), ("tree(auto)", "1"), ("normal", "0"), ("tree", "0"),
            ("normal(manual)", "0"), ("tree(manual)", "0"), ("none", "0"), ("0", "0"),
        ],
        "print_sequence" => &[("by layer", "0"), ("by object", "1"), ("0", "0"), ("1", "1")],
        "support_material_pattern" => &[
            ("rectilinear", "rectilinear"), ("rectilinear_grid", "rectilinear_grid"),
            ("honeycomb", "honeycomb"), ("default", "rectilinear"),
        ],
        "support_material_style" => &[
            ("default", "grid"), ("snug", "snug"), ("grid", "grid"), ("organic", "organic"),
            ("tree", "organic"), ("tree_slim", "tree_slim"), ("tree_strong", "tree_strong"),
            ("tree_hybrid", "tree_hybrid"),
        ],
        "support_type_style" => &[
            ("normal(auto)", "snug"), ("normal(manual)", "snug"), ("normal", "snug"),
            ("tree(auto)", "organic"), ("tree(manual)", "organic"), ("tree", "organic"),
            ("tree(organic)", "organic"), ("organic", "organic"), ("tree_slim", "tree_slim"),
            ("tree_strong", "tree_strong"), ("tree_hybrid", "tree_hybrid"),
        ],
        "arc_fitting" => &[
            ("0", "disabled"), ("1", "emit_center"), ("false", "disabled"),
            ("true", "emit_center"),
        ],
        "pressure_advance_on" => &[
            ("0", "disabled"), ("1", "enabled"), ("false", "disabled"), ("true", "enabled"),
        ],
        "scarf_seam_placement" => &[
            ("none", "nowhere"), ("nowhere", "nowhere"), ("contour", "contours"),
            ("contours", "contours"), ("aligned", "everywhere"), ("everywhere", "everywhere"),
        ],
        "ironing_type" => &[
            ("no ironing", ""), ("top", "top"), ("topmost", "topmost"),
            ("all solid", "solid"), ("all", "solid"),
        ],
        "fuzzy_skin" => &[
            ("none", "none"), ("outside", "external"), ("all", "all"), ("external", "external"),
        ],
        "draft_shield" => &[
            ("disabled", "disabled"), ("limited", "limited"), ("enabled", "enabled"),
        ],
        _ => &[],
    }
}

fn mapped(table: &str, text: &str) -> String {
    enum_table(table)
        .iter()
        .find(|(source, _)| *source == text)
        .map(|(_, target)| (*target).to_owned())
        .unwrap_or_else(|| text.to_owned())
}

/// The leading number of a value that may be a per-extruder list or a comma-joined
/// list, which is how that slicer writes anything tool-specific.
fn head_of(value: &Value) -> String {
    let scalar = match value {
        Value::Array(items) => items.first().unwrap_or(value),
        other => other,
    };
    let text = match scalar {
        Value::String(text) => text.clone(),
        Value::Bool(flag) => (if *flag { "1" } else { "0" }).to_owned(),
        Value::Number(number) => number.as_f64().unwrap_or_default().to_string(),
        _ => String::new(),
    };
    let first = text.split(',').next().unwrap_or("").trim();
    first.strip_suffix('%').unwrap_or(first).to_owned()
}

fn head_number(value: &Value) -> f64 {
    head_of(value).parse::<f64>().unwrap_or_default()
}

/// A width or a speed is written as a bare number or as a percentage of the nozzle,
/// and the two mean different things to this engine: 100 is a hundred millimetres
/// wide, 100% is a hundred percent of the nozzle. The sign has to survive, or a
/// sliced model comes out with no toolpaths at all.
fn number_or_percent(value: &Value) -> Value {
    let scalar = match value {
        Value::Array(items) => items.first().unwrap_or(value),
        other => other,
    };
    let text = match scalar {
        Value::String(text) => text.clone(),
        Value::Number(number) => number.as_f64().unwrap_or_default().to_string(),
        _ => String::new(),
    };
    let first = text.split(',').next().unwrap_or("").trim();
    match first.strip_suffix('%') {
        Some(number) => percent_object(number.parse::<f64>().unwrap_or_default()),
        None => Value::from(first.parse::<f64>().unwrap_or_default()),
    }
}

fn is_truthy(text: &str) -> bool {
    matches!(text, "1" | "true" | "True" | "on")
}

fn percent_object(number: f64) -> Value {
    let mut object = Map::new();
    object.insert("value".to_owned(), Value::from(number));
    object.insert("is_percent".to_owned(), Value::Bool(true));
    Value::Object(object)
}

/// `QVariant::toDouble`, which parses a string and accepts a flag as 0 or 1: an INI
/// value is text, and the setting it lands on may be a number.
fn qt_double(value: &Value) -> Option<f64> {
    match value {
        Value::Number(number) => number.as_f64(),
        Value::Bool(flag) => Some(if *flag { 1.0 } else { 0.0 }),
        Value::String(text) => text.trim().parse::<f64>().ok(),
        _ => None,
    }
}

/// `QVariant::toInt`: a double is rounded, and a string only parses as a whole
/// number.
fn qt_integer(value: &Value) -> Option<i64> {
    match value {
        Value::Bool(flag) => Some(if *flag { 1 } else { 0 }),
        Value::Number(number) => number.as_f64().map(|value| value.round() as i64),
        Value::String(text) => text.trim().parse::<i64>().ok(),
        _ => None,
    }
}

/// `QVariant::toBool`: an empty string, `"false"` and `"0"` are false and any other
/// non-empty string is true, which is what an INI value is.
fn qt_bool(value: &Value) -> bool {
    match value {
        Value::Bool(flag) => *flag,
        Value::Number(number) => number.as_f64().unwrap_or_default() != 0.0,
        Value::String(text) => {
            !(text.is_empty() || text.eq_ignore_ascii_case("false") || text == "0")
        }
        _ => false,
    }
}

/// `QVariant::toString` for the values this module builds.
fn qt_text(value: &Value) -> String {
    match value {
        Value::Bool(flag) => (if *flag { "true" } else { "false" }).to_owned(),
        Value::Number(number) => number.as_f64().unwrap_or_default().to_string(),
        Value::String(text) => text.clone(),
        _ => String::new(),
    }
}

/// One value as the `QVariant`-shaped record the settings model applies: the answers
/// `QVariant`'s own converters give are computed here, exactly as the shell's
/// `inputRecord` computes them, because that is what the C++ importer handed over.
fn import_scalar(value: &Value) -> SettingScalarRecord {
    let kind = match value {
        Value::Bool(_) => 1,
        Value::Number(_) => 3,
        Value::String(_) => 4,
        Value::Array(_) => 5,
        Value::Object(_) => 6,
        Value::Null => 0,
    };
    let mut record = SettingScalarRecord {
        kind,
        truth: qt_bool(value),
        integer: 0,
        integer_ok: false,
        number: 0.0,
        number_ok: false,
        text: qt_text(value),
        map_value: 0.0,
        map_percent: false,
    };
    if kind == 6 {
        if let Value::Object(object) = value {
            match object.get("value") {
                Some(raw) => {
                    record.map_value = qt_double(raw).unwrap_or_default();
                    record.map_percent = object
                        .get("is_percent")
                        .map(qt_bool)
                        .unwrap_or(false);
                }
                None => record.kind = 0,
            }
        }
        return record;
    }
    if let Some(number) = qt_double(value) {
        record.number = number;
        record.number_ok = true;
    }
    if let Some(integer) = qt_integer(value) {
        record.integer = integer;
        record.integer_ok = true;
    }
    record
}

fn import_element(value: &Value) -> SettingElementRecord {
    let mut element = SettingElementRecord {
        kind: 0,
        text: qt_text(value),
        number: 0.0,
        number_ok: false,
        pair_first: 0.0,
        pair_second: 0.0,
    };
    if let Some(number) = qt_double(value) {
        element.number = number;
        element.number_ok = true;
    }
    match value {
        Value::String(_) => element.kind = 1,
        Value::Array(pair) if pair.len() >= 2 => {
            element.kind = 2;
            element.pair_first = qt_double(&pair[0]).unwrap_or_default();
            element.pair_second = qt_double(&pair[1]).unwrap_or_default();
        }
        _ => {}
    }
    element
}

fn import_input(value: &Value) -> SettingInputRecord {
    let whole = import_scalar(value);
    let mut input = SettingInputRecord {
        is_list: false,
        first: import_scalar(value),
        whole,
        elements: Vec::new(),
    };
    if let Value::Array(items) = value {
        input.is_list = true;
        for item in items {
            input.elements.push(import_element(item));
        }
        if let Some(first) = items.first() {
            input.first = import_scalar(first);
        }
    }
    input
}

/// The other slicer's value handed over unchanged, for a key this engine has under
/// the same name: the engine's setting holds the type and does the coercing.
fn transform_auto(value: &Value) -> Value {
    value.clone()
}

fn transform_value(value: &Value, mapping: &Mapping) -> Value {
    match mapping.transform {
        Transform::Rename => transform_auto(value),
        Transform::PercentObject => percent_object(head_number(value)),
        Transform::Integer => Value::from(head_number(value).round() as i64),
        Transform::Real => number_or_percent(value),
        Transform::RealList => {
            let empty = vec![Value::from(0.0)];
            let items: &[Value] = match value {
                Value::Array(items) => items,
                other => std::slice::from_ref(other),
            };
            let mut numbers: Vec<Value> = Vec::new();
            for item in items {
                let head = head_of(item);
                if !head.is_empty() {
                    numbers.push(Value::from(head.parse::<f64>().unwrap_or_default()));
                }
            }
            Value::Array(if numbers.is_empty() { empty } else { numbers })
        }
        Transform::Boolean => {
            let scalar = match value {
                Value::Array(items) => items.first().unwrap_or(value),
                other => other,
            };
            match scalar {
                Value::Bool(flag) => Value::Bool(*flag),
                _ => Value::Bool(is_truthy(&head_of(value))),
            }
        }
        Transform::BedShape => {
            let mut tokens: Vec<Value> = match value {
                Value::Array(items) => items.clone(),
                other => vec![other.clone()],
            };
            if tokens.len() == 1 {
                if let Value::String(joined) = &tokens[0] {
                    if joined.contains(',') {
                        tokens = joined
                            .split(',')
                            .map(|token| Value::String(token.trim().to_owned()))
                            .collect();
                    }
                }
            }
            let mut points = Vec::new();
            for token in &tokens {
                let text = match token {
                    Value::String(text) => text.clone(),
                    Value::Number(number) => number.as_f64().unwrap_or_default().to_string(),
                    _ => String::new(),
                };
                let parts: Vec<&str> = text.split('x').collect();
                if parts.len() != 2 {
                    continue;
                }
                points.push(Value::Array(vec![
                    Value::from(parts[0].trim().parse::<f64>().unwrap_or_default()),
                    Value::from(parts[1].trim().parse::<f64>().unwrap_or_default()),
                ]));
            }
            Value::Array(points)
        }
        Transform::IntAtLeastOne => {
            let number = head_number(value);
            Value::from(if number.round() < 1.0 {
                1i64
            } else {
                number.round() as i64
            })
        }
        Transform::Enum => Value::String(mapped(mapping.table, &head_of(value))),
        Transform::EnumBoolean => Value::Bool(is_truthy(&mapped(mapping.table, &head_of(value)))),
    }
}

/// `QVariant::toString` for a JSON value, as the machine-limits default reads it.
fn text_of(value: Option<&Value>) -> String {
    match value {
        Some(Value::String(text)) => text.clone(),
        Some(Value::Number(number)) => number.as_f64().unwrap_or_default().to_string(),
        Some(Value::Bool(flag)) => (if *flag { "true" } else { "false" }).to_owned(),
        _ => String::new(),
    }
}

fn placeholder_renames() -> &'static [(&'static str, &'static str)] {
    &[
        ("bed_temperature_initial_layer_single", "first_layer_bed_temperature"),
        ("bed_temperature_initial_layer", "first_layer_bed_temperature"),
        ("nozzle_temperature_initial_layer", "first_layer_temperature"),
        ("nozzle_temperature", "temperature"),
        ("initial_layer_print_height", "first_layer_height"),
        ("retraction_length", "retract_length"),
        ("retraction_speed", "retract_speed"),
        ("z_hop", "retract_lift"),
    ]
}

/// Placeholders the engine resolves without them being settings: `[layer_z]` is a
/// position, not an option, and a rule that only accepted setting names dropped the
/// layer-change template of every machine imported.
fn is_engine_placeholder(name: &str) -> bool {
    matches!(
        name,
        "layer_z"
            | "layer_num"
            | "max_layer_z"
            | "total_layer_count"
            | "current_extruder"
            | "initial_extruder"
            | "initial_tool"
            | "next_extruder"
            | "previous_extruder"
            | "num_extruders"
            | "input_filename_base"
            | "extruded_weight_total"
            | "timestamp"
            | "version"
            | "first_layer_print_min"
            | "first_layer_print_max"
            | "printer_preset"
            | "print_preset"
            | "filament_preset"
    )
}

/// Every `[name]` in a template, in order of appearance.
fn placeholders(body: &str) -> Vec<&str> {
    let bytes = body.as_bytes();
    let mut found = Vec::new();
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] != b'[' {
            index += 1;
            continue;
        }
        let start = index + 1;
        let mut end = start;
        while end < bytes.len()
            && (bytes[end].is_ascii_lowercase()
                || bytes[end].is_ascii_digit()
                || bytes[end] == b'_')
        {
            end += 1;
        }
        if end > start && end < bytes.len() && bytes[end] == b']' {
            found.push(&body[start..end]);
            index = end + 1;
            continue;
        }
        index += 1;
    }
    found
}

/// A template this engine refuses is worse than no template: one unresolvable name
/// makes the slice write no file at all. Returns the rewritten template, or nothing
/// when it has to go, with the reason appended to `report`.
fn settle_template(
    key: &str,
    body: &str,
    core: &SettingsModelCore,
    absolute_e: bool,
    scope: &str,
    report: &mut Vec<String>,
) -> Option<String> {
    let mut unknown: Vec<&str> = Vec::new();
    let mut renames: Vec<&str> = Vec::new();
    for name in placeholders(body) {
        if !SettingsModel::block_of(core, name).is_empty() || is_engine_placeholder(name) {
            continue;
        }
        let renamed = placeholder_renames()
            .iter()
            .find(|(source, _)| *source == name)
            .map(|(_, target)| *target);
        if let Some(target) = renamed {
            if !SettingsModel::block_of(core, target).is_empty() || is_engine_placeholder(target) {
                renames.push(name);
                continue;
            }
        }
        if !unknown.contains(&name) {
            unknown.push(name);
        }
    }
    if !unknown.is_empty() {
        report.push(format!(
            "{scope}: {key} dropped, placeholders this engine has no setting for: {}",
            unknown.join(", ")
        ));
        return None;
    }

    let mut settled = body.to_owned();
    for name in renames {
        let target = placeholder_renames()
            .iter()
            .find(|(source, _)| *source == name)
            .map(|(_, target)| *target)
            .unwrap_or(name);
        settled = settled.replace(&format!("[{name}]"), &format!("[{target}]"));
    }

    if absolute_e && (key == "before_layer_gcode" || key == "layer_gcode") && resets_extruder(&settled)
    {
        report.push(format!("{scope}: {key} dropped, a G92 E while E is absolute"));
        return None;
    }
    Some(settled)
}

/// `(^|\n)\s*G92\s+E[0-9.]*`, line by line.
fn resets_extruder(body: &str) -> bool {
    body.split('\n').any(|line| {
        let line = line.trim_start();
        let Some(rest) = line.strip_prefix("G92") else {
            return false;
        };
        let Some(rest) = rest.strip_prefix([' ', '\t']) else {
            return false;
        };
        let rest = rest.trim_start();
        let Some(rest) = rest.strip_prefix('E') else {
            return false;
        };
        rest.chars().all(|character| character.is_ascii_digit() || character == '.')
    })
}

/// Settings a preset must not carry even though the model holds a value for them:
/// `printer_variant` is a string that reads back as a number, which produces a type
/// mismatch on every evaluation.
fn items_not_written(block: &str) -> &'static [&'static str] {
    if block == "printer" {
        return &["printer_variant"];
    }
    &[]
}

/// A tab's YAML with the named items removed, list bodies included.
fn without_items(yaml: &str, block: &str) -> String {
    without_keys(yaml, items_not_written(block))
}

/// Drops whole keys from emitted YAML, keeping every other line, including the
/// continuation lines of a block scalar.
fn without_keys(yaml: &str, names: &[&str]) -> String {
    let mut kept: Vec<&str> = Vec::new();
    let mut skipping = false;
    for line in yaml.split('\n') {
        if line.starts_with("  ") && !line.starts_with("  -") {
            let key = line.trim().split(':').next().unwrap_or("");
            skipping = names.contains(&key);
        }
        if !skipping {
            kept.push(line);
        }
    }
    kept.join("\n")
}

/// `Voron 0.4 nozzle` names a machine `Voron 0.4`, which is what this engine's
/// bundle calls it.
pub fn printer_name_from(source: &str) -> String {
    let words: Vec<&str> = source.split_whitespace().collect();
    if words.len() > 1 && words.last().map(|word| word.eq_ignore_ascii_case("nozzle")) == Some(true)
    {
        return words[..words.len() - 1].join(" ");
    }
    source.trim().to_owned()
}

/// `QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)`: the XDG
/// variable when it is an absolute path, else `$HOME/.config`. The import of a
/// single file resolves its inheritance against the installs found here, and the
/// adapter that lists the same directories for the UI passes the location it read.
fn config_location() -> String {
    match std::env::var("XDG_CONFIG_HOME") {
        Ok(value) if value.starts_with('/') => value,
        _ => format!("{}/.config", std::env::var("HOME").unwrap_or_else(|_| "/".to_owned())),
    }
}

fn kind_of_type(kind: &str) -> &'static str {    match kind {
        "printer" | "machine" => "machine",
        "filament" => "filament",
        _ => "process",
    }
}

fn block_of_kind(kind: &str) -> &'static str {
    match kind {
        "machine" => "printer",
        "filament" => "filament",
        _ => "process",
    }
}

fn document_kind(kind: &str) -> &'static str {
    match kind {
        "machine" => "printer",
        "filament" => "filament",
        _ => "print",
    }
}

/// Reads another slicer's presets out of `source` and writes the ones this engine
/// can express into the user's own bundle. `core` supplies the vocabulary: a key is
/// written only into the block it belongs to.
///
/// Order matters and is not an implementation detail: the settings model is reset
/// and filled *before* the YAML is read out of it, and the caller clears the
/// modified set afterwards. Reordering those three leaves `saveUserPreset` writing
/// a diff against the previous preset.
pub fn import_presets(source: &Path, core: &mut SettingsModelCore, write: bool) -> ImportReport {
    let mut report = ImportReport::default();
    if !source.is_dir() {
        report.error = format!("{} is not a directory", source.display());
        return report;
    }

    let index = preset_index(source);
    let mut sink = Sink {
        core,
        write,
        dropped: HashSet::new(),
        elsewhere: Vec::new(),
        vendor_directory: PresetFiles::custom_vendor_directory(),
    };

    for kind in KINDS {
        for path in user_preset_paths(source, kind) {
            let Some(own) = read_json_object(&path) else {
                continue;
            };
            if !import_one(kind, &own, &index, &mut sink, &mut report) {
                return report;
            }
        }
    }

    finish_report(&mut report, &sink);
    report
}

/// One exported file: an Orca/Bambu single-preset `.json` or a PrusaSlicer `.ini`,
/// holding one preset or a whole bundle. Inheritance is resolved against the slicer
/// installations found on this machine, because a single exported preset cannot
/// carry its ancestors.
pub fn import_preset_file(path: &Path, core: &mut SettingsModelCore, write: bool) -> ImportReport {
    let mut report = ImportReport::default();
    if !path.is_file() {
        report.error = format!("{} is not a file", path.display());
        return report;
    }

    let presets = if path
        .extension()
        .and_then(|value| value.to_str())
        .map(|value| value.eq_ignore_ascii_case("ini"))
        == Some(true)
    {
        ini_presets(path, &mut report)
    } else {
        json_presets(path, &mut report)
    };
    if !report.error.is_empty() {
        return report;
    }
    if presets.is_empty() {
        report.error = format!("{} holds no preset this app can read", path.display());
        return report;
    }

    let mut index = empty_index();
    let mut searched: Vec<String> = Vec::new();
    for candidate in slicer_candidates(&config_location()) {
        let directory = Path::new(&candidate.path);
        if !directory.is_dir() {
            continue;
        }
        let installed = preset_index(directory);
        for kind in KINDS {
            if let Some(slot) = index.get_mut(kind) {
                if let Some(source) = installed.get(kind) {
                    for (name, entry) in source {
                        slot.insert(name.clone(), entry.clone());
                    }
                }
            }
        }
        searched.push(candidate.path);
    }
    if searched.is_empty() {
        report.notes.push(
            "no slicer install found, so nothing inherited could be resolved".to_owned(),
        );
    }

    for (kind, entry) in &presets {
        if let Some(slot) = index.get_mut(kind.as_str()) {
            if let Some(name) = entry.get("name").and_then(Value::as_str) {
                slot.insert(name.to_owned(), entry.clone());
            }
        }
    }

    let mut sink = Sink {
        core,
        write,
        dropped: HashSet::new(),
        elsewhere: Vec::new(),
        vendor_directory: PresetFiles::custom_vendor_directory(),
    };
    for (kind, entry) in &presets {
        let name = entry
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_owned();
        let inherits = entry
            .get("inherits")
            .and_then(Value::as_str)
            .unwrap_or_default();
        if !inherits.is_empty()
            && !index
                .get(kind.as_str())
                .is_some_and(|slot| slot.contains_key(inherits))
        {
            report.notes.push(format!(
                "{name} inherits {inherits}, which is not on this machine"
            ));
        }
        if !import_one(kind, entry, &index, &mut sink, &mut report) {
            return report;
        }
    }

    finish_report(&mut report, &sink);
    report
}

struct Sink<'a> {
    core: &'a mut SettingsModelCore,
    write: bool,
    dropped: HashSet<String>,
    elsewhere: Vec<String>,
    vendor_directory: String,
}

fn finish_report(report: &mut ImportReport, sink: &Sink<'_>) {
    let mut elsewhere = sink.elsewhere.clone();
    elsewhere.sort();
    elsewhere.dedup();
    report.elsewhere = elsewhere;
    let mut dropped: Vec<String> = sink.dropped.iter().cloned().collect();
    dropped.sort();
    report.dropped_keys = dropped;
}

/// Translates one preset and writes it unless the sink says otherwise. `false`
/// means a write failed, which ends the whole import; a preset that cannot be
/// translated is reported and skipped instead.
fn import_one(
    kind: &str,
    own: &Map<String, Value>,
    index: &KindIndex,
    sink: &mut Sink<'_>,
    report: &mut ImportReport,
) -> bool {
    let source_name = own.get("name").and_then(Value::as_str).unwrap_or_default();
    if source_name.is_empty() {
        return true;
    }
    let resolved = resolve_preset(kind, source_name, index);

    let mut values: BTreeMap<String, Value> = BTreeMap::new();
    let mut translated = 0;
    for (key, value) in resolved.iter() {
        if meta_keys().contains(&key.as_str()) || key.starts_with("compatible_") {
            continue;
        }
        let mapping = find_mapping(key);
        if unmapped_keys().contains(&key.as_str()) {
            sink.dropped.insert(key.clone());
            continue;
        }

        // A key with no table entry is not automatically lost: most of the other
        // slicer's names are this engine's names too, and the engine already knows
        // what type each one holds. Only a key it does not have at all is dropped,
        // and that is reported.
        let target = mapping.map(|mapping| mapping.target).unwrap_or(key.as_str());
        if SettingsModel::block_of(sink.core, target).is_empty() {
            sink.dropped.insert(key.clone());
            continue;
        }
        if !SettingsModel::can_hold_in(sink.core, target, block_of_kind(kind)) {
            sink.elsewhere.push(key.clone());
            continue;
        }
        let transformed = match mapping {
            Some(mapping) => transform_value(value, mapping),
            None => transform_auto(value),
        };
        if transformed.as_str() == Some("") {
            continue;
        }
        values.insert(target.to_owned(), transformed);
        translated += 1;
    }

    if translated == 0 {
        report
            .refused
            .push(format!("{source_name}: nothing this engine has"));
        return true;
    }

    if kind == "machine" {
        let bed_points = values
            .get("bed_shape")
            .and_then(Value::as_array)
            .map(|points| points.len())
            .unwrap_or(0);
        let height = values
            .get("max_print_height")
            .and_then(qt_double)
            .unwrap_or_default();
        if bed_points < 3 || height <= 0.0 {
            report.refused.push(format!(
                "{source_name}: {translated} translated key(s), {bed_points} bed point(s), \
                 height {height}"
            ));
            return true;
        }
    }

    // Klipper cannot take machine limits in the G-code, and this engine refuses the
    // combination outright instead of writing a file. That slicer resolves the same
    // conflict by leaving the limits out of the G-code, so a Klipper machine
    // imported without an answer of its own gets the same one.
    if text_of(values.get("gcode_flavor")).starts_with("klipper")
        && text_of(values.get("machine_limits_usage")).is_empty()
        && SettingsModel::block_of(sink.core, "machine_limits_usage") == "printer"
    {
        values.insert(
            "machine_limits_usage".to_owned(),
            Value::String("time_estimate_only".to_owned()),
        );
    }

    let preset_name = if kind == "machine" {
        printer_name_from(source_name)
    } else {
        source_name.to_owned()
    };
    let slug = PresetFiles::slug_of(&preset_name);

    // A printer preset binds to its model by name: the vendor bundle gives the model
    // id `NL_<slug>_MODEL`, and a preset without that value is never attached to a
    // machine, so the engine finds no tool for it.
    if kind == "machine" {
        values.insert(
            "printer_model".to_owned(),
            Value::String(format!("NL_{slug}_MODEL")),
        );
        values.insert(
            "printer_technology".to_owned(),
            Value::String("FFF".to_owned()),
        );
        values.insert(
            "printer_vendor".to_owned(),
            Value::String("Custom".to_owned()),
        );
    }

    let block = block_of_kind(kind);
    SettingsModel::reset_tab(sink.core, block);
    let mut applied = 0;
    for (key, value) in &values {
        if SettingsModel::set_value(sink.core, key, &import_input(value)).applied {
            applied += 1;
        } else {
            report.unfitted.push(format!("{source_name}: {key}"));
        }
    }

    // Templates are settled against the machine's own E mode before the preset is
    // written: one the engine refuses blanks the whole slice. The settled text goes
    // back through the model, because the YAML below is read out of it.
    let absolute_e = !values
        .get("use_relative_e_distances")
        .map(qt_bool)
        .unwrap_or(false);
    for key in gcode_template_keys() {
        let Some(found) = values.get(*key).cloned() else {
            continue;
        };
        let Some(settled) = settle_template(
            key,
            &text_of(Some(&found)),
            sink.core,
            absolute_e,
            source_name,
            &mut report.dropped_templates,
        ) else {
            values.remove(*key);
            continue;
        };
        if settled == text_of(Some(&found)) {
            continue;
        }
        values.insert((*key).to_owned(), Value::String(settled.clone()));
        if SettingsModel::block_of(sink.core, key).is_empty() {
            continue;
        }
        SettingsModel::set_value(
            sink.core,
            key,
            &import_input(&Value::String(settled)),
        );
    }

    let stated: HashSet<&String> = values.keys().collect();
    let unwanted: Vec<&str> = dangerous_defaults()
        .iter()
        .copied()
        .filter(|key| !stated.contains(&(*key).to_owned()))
        .collect();
    let yaml = without_keys(
        &without_items(
            &SettingsModel::values_yaml(sink.core, block, false),
            block,
        ),
        &unwanted,
    );
    if applied == 0 || yaml.is_empty() {
        report
            .refused
            .push(format!("{source_name}: nothing this engine can hold"));
        return true;
    }

    if kind == "machine" {
        if sink.write {
            let error = PresetFiles::add_custom_printer(&preset_name, 0);
            if !error.is_empty() {
                report.error = error;
                return false;
            }
            if !PresetFiles::ensure_tool_print_preset().is_empty() {
                report.error = "Could not write the tool preset".to_owned();
                return false;
            }
            let target = PresetFiles::custom_printer_path(&preset_name);
            let condition = format!("printer.model == \"NL_{slug}_MODEL\"");
            if target.is_empty()
                || !PresetFiles::write_file(
                    &target,
                    &PresetFiles::document(
                        "printer",
                        &preset_name,
                        "",
                        &condition,
                        &yaml,
                        &format!("nl-{slug}"),
                    ),
                )
            {
                report.error = "Could not write the printer preset".to_owned();
                return false;
            }
        }
        report.printers.push(preset_name);
        return true;
    }

    let preset_kind = document_kind(kind);
    if sink.write {
        let target = format!(
            "{}/preset-{preset_kind}-{slug}.yaml",
            sink.vendor_directory
        );
        if !PresetFiles::write_file(
            &target,
            &PresetFiles::document(preset_kind, &preset_name, "", "", &yaml, ""),
        ) {
            report.error = format!("Could not write {preset_name}");
            return false;
        }
    }
    match kind {
        "filament" => report.filaments.push(preset_name),
        _ => report.prints.push(preset_name),
    }
    true
}

/// The presets in a single-file JSON export: Orca and Bambu write one preset per
/// file on "Export preset", with the settings it overrides and the name it inherits
/// from.
fn json_presets(path: &Path, report: &mut ImportReport) -> Vec<(String, Map<String, Value>)> {
    let Ok(text) = std::fs::read_to_string(path) else {
        report.error = format!("cannot read {}", path.display());
        return Vec::new();
    };
    let Ok(value) = serde_json::from_str::<Value>(&text) else {
        report.error = format!("{} is not JSON", path.display());
        return Vec::new();
    };
    let mut presets = Vec::new();
    let add = |entry: Map<String, Value>, presets: &mut Vec<(String, Map<String, Value>)>| {
        if entry.is_empty() {
            return;
        }
        let kind = kind_of_type(entry.get("type").and_then(Value::as_str).unwrap_or_default());
        presets.push((kind.to_owned(), entry));
    };
    match value {
        Value::Array(items) => {
            for item in items {
                if let Value::Object(entry) = item {
                    add(entry, &mut presets);
                }
            }
        }
        Value::Object(entry) => add(entry, &mut presets),
        _ => {
            report.error = format!("{} is not JSON", path.display());
            return Vec::new();
        }
    }
    presets
}

/// The presets in a PrusaSlicer INI: `[print:Name]`, `[filament:Name]` and
/// `[printer:Name]` sections of `key = value`. A value stays the string the other
/// slicer wrote, because the translator already reads numbers, booleans and
/// comma-joined lists out of text.
fn ini_presets(path: &Path, report: &mut ImportReport) -> Vec<(String, Map<String, Value>)> {
    let Ok(text) = std::fs::read_to_string(path) else {
        report.error = format!("cannot read {}", path.display());
        return Vec::new();
    };
    let mut presets = Vec::new();
    let mut current = Map::new();
    let mut kind = String::new();
    let mut flush = |kind: &mut String, current: &mut Map<String, Value>| {
        if !kind.is_empty() && !current.is_empty() {
            let written = if *kind == "machine" { "printer" } else { kind.as_str() };
            current.insert("type".to_owned(), Value::String(written.to_owned()));
            presets.push((kind.clone(), std::mem::take(current)));
        }
        *current = Map::new();
    };
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            flush(&mut kind, &mut current);
            kind.clear();
            let header = &line[1..line.len() - 1];
            let (section, name) = match header.split_once(':') {
                Some((section, name)) => (section.trim(), name.trim()),
                None => ("", header.trim()),
            };
            if !section.is_empty()
                && matches!(section, "print" | "filament" | "printer" | "presets")
            {
                kind = kind_of_type(section).to_owned();
                current.insert("name".to_owned(), Value::String(name.to_owned()));
            }
            continue;
        }
        if kind.is_empty() {
            continue;
        }
        if let Some((key, value)) = line.split_once('=') {
            current.insert(key.trim().to_owned(), Value::String(value.trim().to_owned()));
        }
    }
    flush(&mut kind, &mut current);
    presets
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_config_location_is_a_url_and_is_decoded() {
        assert_eq!(config_home("file:///home/a%20b/.config"), "/home/a b/.config");
        assert_eq!(config_home("/home/a b/.config"), "/home/a b/.config");
        assert_eq!(
            config_home("file://localhost/home/x/.config"),
            "/home/x/.config"
        );
    }

    #[test]
    fn a_space_encoded_config_home_would_find_no_slicer() {
        let sources = slicer_candidates("file:///home/a%20b/.config");
        assert_eq!(sources[0].path, "/home/a b/.config/OrcaSlicer");
        assert_eq!(sources.len(), 5);
    }

    #[test]
    fn a_nozzle_suffix_is_dropped_from_a_machine_name() {
        assert_eq!(printer_name_from("Voron 2.4 350 nozzle"), "Voron 2.4 350");
        assert_eq!(printer_name_from("Voron 2.4"), "Voron 2.4");
    }

    #[test]
    fn a_key_with_a_continuation_block_is_removed_whole() {
        let yaml = "  binary_gcode: '1'\n  layer_height: '0.2'\n  start_gcode: |\n    G28\n";
        let out = without_keys(yaml, dangerous_defaults());
        assert!(!out.contains("binary_gcode"));
        assert!(out.contains("layer_height"));
        assert!(out.contains("G28"));
    }

    #[test]
    fn an_inherits_chain_merges_child_first() {
        let mut index = empty_index();
        let mut process = HashMap::new();
        let mut child = Map::new();
        child.insert("inherits".to_owned(), Value::String("Base".to_owned()));
        child.insert("layer_height".to_owned(), Value::from(0.1));
        let mut base = Map::new();
        base.insert("layer_height".to_owned(), Value::from(0.3));
        base.insert("perimeters".to_owned(), Value::from(4));
        process.insert("Child".to_owned(), child);
        process.insert("Base".to_owned(), base);
        index.insert("process".to_owned(), process);

        let merged = resolve_preset("process", "Child", &index);
        assert_eq!(merged.get("layer_height"), Some(&Value::from(0.1)));
        assert_eq!(merged.get("perimeters"), Some(&Value::from(4)));
        assert!(!merged.contains_key("inherits"));
    }

    #[test]
    fn the_report_counts_only_the_presets_that_were_written() {
        let report = ImportReport {
            printers: vec!["a".to_owned()],
            prints: vec!["b".to_owned(), "c".to_owned()],
            refused: vec!["d".to_owned()],
            ..ImportReport::default()
        };
        assert_eq!(report.count(), 3);
        assert!(report.ok());
    }

    #[test]
    fn a_value_keeps_the_shape_the_engine_setting_expects() {
        let percent = percent_object(15.0);
        assert_eq!(percent, serde_json::json!({"value": 15.0, "is_percent": true}));
        assert_eq!(head_of(&serde_json::json!(["0.42,0.45", "0.5"])), "0.42");
        assert_eq!(
            transform_value(&serde_json::json!("60%"), &Mapping {
                source: "x",
                target: "y",
                transform: Transform::Real,
                table: "",
            }),
            serde_json::json!({"value": 60.0, "is_percent": true})
        );
        assert_eq!(
            transform_value(&serde_json::json!("0x0,10x0,10x10"), &Mapping {
                source: "x",
                target: "bed_shape",
                transform: Transform::BedShape,
                table: "",
            }),
            serde_json::json!([[0.0, 0.0], [10.0, 0.0], [10.0, 10.0]])
        );
    }

    #[test]
    fn a_mapped_enum_falls_back_to_the_text_it_was_given() {
        assert_eq!(mapped("support_material_on", "1"), "everywhere");
        assert_eq!(mapped("fill_pattern", "gyroid"), "gyroid");
        assert_eq!(mapped("fill_pattern", "not-a-pattern"), "not-a-pattern");
    }

    #[test]
    fn an_extruder_reset_is_found_only_at_a_line_start() {
        assert!(resets_extruder("G28\nG92 E0\n"));
        assert!(resets_extruder("  G92 E0.0\n"));
        assert!(!resets_extruder("M117 G92 E0\n"));
        assert!(!resets_extruder("G92 X0 Y0\n"));
    }
}
