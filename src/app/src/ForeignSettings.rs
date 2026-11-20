//! The Bambu/Orca settings dialect, translated.
//!
//! Ported from `src/qt/src/ForeignSettings.cpp`. The two slicers share a lineage,
//! so some settings are the same name and value, some are the same meaning under a
//! different word, and some describe behaviour this engine does not have. This file
//! is the table plus the three lookups over it: a foreign name in, this engine's
//! name out; and the reverse, for writing a project the other slicer opens.
//!
//! Order is part of the contract. A name can have two foreign spellings
//! (`support_type` and `support_style` are both `support_material_style`) and a value
//! can too (`1`, `true` and `enable` all mean `everywhere`), so the first entry in
//! the table is the one a project is written with; the C++ this replaces left the
//! value choice to `QHash` iteration order, which is seeded per process, and promised
//! table order in its own comment. The promise is what is implemented here.

/// The names that mean the same thing under a different word, that slicer's name
/// first because that is what a downloaded project carries. Both sides are proven by
/// `--dump-foreign-settings` and the gate that reads it, so a typo on either side
/// fails rather than quietly dropping a setting.
const RENAMED: [(&str, &str); 109] = [
    ("wall_loops", "perimeters"),
    ("top_shell_layers", "top_solid_layers"),
    ("top_shell_thickness", "top_solid_min_thickness"),
    ("top_surface_pattern", "top_fill_pattern"),
    ("top_surface_line_width", "top_infill_extrusion_width"),
    ("top_surface_speed", "top_solid_infill_speed"),
    ("bottom_shell_layers", "bottom_solid_layers"),
    ("bottom_shell_thickness", "bottom_solid_min_thickness"),
    ("bottom_surface_pattern", "bottom_fill_pattern"),
    ("sparse_infill_density", "fill_density"),
    ("sparse_infill_pattern", "fill_pattern"),
    ("infill_direction", "fill_angle"),
    ("sparse_infill_line_width", "infill_extrusion_width"),
    ("sparse_infill_speed", "infill_speed"),
    ("sparse_infill_acceleration", "infill_acceleration"),
    ("infill_wall_overlap", "infill_overlap"),
    ("internal_solid_infill_line_width", "solid_infill_extrusion_width"),
    ("internal_solid_infill_speed", "solid_infill_speed"),
    ("internal_solid_infill_acceleration", "solid_infill_acceleration"),
    ("inner_wall_line_width", "perimeter_extrusion_width"),
    ("inner_wall_speed", "perimeter_speed"),
    ("inner_wall_acceleration", "perimeter_acceleration"),
    ("outer_wall_line_width", "external_perimeter_extrusion_width"),
    ("outer_wall_speed", "external_perimeter_speed"),
    ("outer_wall_acceleration", "external_perimeter_acceleration"),
    ("initial_layer_print_height", "first_layer_height"),
    ("initial_layer_line_width", "first_layer_extrusion_width"),
    ("initial_layer_speed", "first_layer_speed"),
    ("initial_layer_infill_speed", "first_layer_infill_speed"),
    ("initial_layer_acceleration", "first_layer_acceleration"),
    ("enable_support", "support_material"),
    ("support_type", "support_material_style"),
    ("support_angle", "support_material_angle"),
    ("support_on_build_plate_only", "support_material_buildplate_only"),
    ("support_top_z_distance", "support_material_contact_distance"),
    ("enforce_support_layers", "support_material_enforce_layers"),
    ("support_interface_top_layers", "support_material_interface_layers"),
    ("support_interface_pattern", "support_material_interface_pattern"),
    ("support_interface_spacing", "support_material_interface_spacing"),
    ("support_interface_speed", "support_material_interface_speed"),
    ("support_base_pattern", "support_material_pattern"),
    ("support_base_pattern_spacing", "support_material_spacing"),
    ("support_speed", "support_material_speed"),
    ("support_threshold_angle", "support_material_threshold"),
    ("bridge_flow", "bridge_flow_ratio"),
    ("retraction_length", "retract_length"),
    ("retraction_speed", "retract_speed"),
    ("retraction_minimum_travel", "retract_before_travel"),
    ("prime_tower_width", "wipe_tower_width"),
    ("prime_tower_brim_width", "wipe_tower_brim_width"),
    ("fan_max_speed", "max_fan_speed"),
    ("fan_min_speed", "min_fan_speed"),
    ("fan_cooling_layer_time", "fan_below_layer_time"),
    ("close_fan_the_first_x_layers", "disable_fan_first_layers"),
    ("additional_fan_full_speed_layer", "full_fan_speed_layer"),
    ("enable_arc_fitting", "arc_fitting"),
    ("before_layer_change_gcode", "before_layer_gcode"),
    ("layer_change_gcode", "layer_gcode"),
    ("machine_start_gcode", "start_gcode"),
    ("machine_end_gcode", "end_gcode"),
    ("machine_pause_gcode", "pause_print_gcode"),
    ("filament_start_gcode", "start_filament_gcode"),
    ("filament_end_gcode", "end_filament_gcode"),
    ("infill_combination", "automatic_infill_combination"),
    ("infill_combination_max_layer_height", "automatic_infill_combination_max_layer_height"),
    ("internal_bridge_angle", "bridge_angle"),
    ("internal_bridge_speed", "bridge_speed"),
    ("internal_bridge_flow", "bridge_flow_ratio"),
    ("reduce_fan_stop_start_freq", "fan_always_on"),
    ("overhang_1_4_speed", "overhang_speed_1"),
    ("overhang_2_4_speed", "overhang_speed_2"),
    ("overhang_3_4_speed", "overhang_speed_3"),
    ("overhang_4_4_speed", "overhang_speed_3"),
    ("only_one_wall_first_layer", "only_one_perimeter_first_layer"),
    ("thick_internal_bridges", "thick_bridges"),
    ("gap_infill_speed", "gap_fill_speed"),
    ("seam_gap", "seam_gap_distance"),
    ("seam_slope_entire_loop", "scarf_seam_entire_loop"),
    ("seam_slope_start_height", "scarf_seam_start_height"),
    ("seam_slope_min_length", "scarf_seam_length"),
    ("spiral_mode", "spiral_vase"),
    ("support_style", "support_material_style"),
    ("support_bottom_z_distance", "support_material_bottom_contact_distance"),
    ("support_interface_bottom_layers", "support_material_bottom_interface_layers"),
    ("support_object_xy_distance", "support_material_xy_spacing"),
    ("support_line_width", "support_material_extrusion_width"),
    ("support_expansion", "support_material_first_layer_expansion"),
    ("tree_support_branch_angle", "support_tree_angle"),
    ("tree_support_branch_angle_organic", "support_tree_angle"),
    ("tree_support_angle_slow", "support_tree_angle_slow"),
    ("tree_support_branch_diameter", "support_tree_branch_diameter"),
    ("tree_support_branch_diameter_organic", "support_tree_branch_diameter"),
    ("tree_support_branch_diameter_angle", "support_tree_branch_diameter_angle"),
    ("tree_support_branch_distance", "support_tree_branch_distance"),
    ("tree_support_branch_distance_organic", "support_tree_branch_distance"),
    ("tree_support_tip_diameter", "support_tree_tip_diameter"),
    ("tree_support_top_rate", "support_tree_top_rate"),
    ("retract_when_changing_layer", "retract_layer_change"),
    ("nozzle_temperature", "temperature"),
    ("nozzle_temperature_initial_layer", "first_layer_temperature"),
    ("enable_prime_tower", "wipe_tower"),
    ("skirt_loops", "skirts"),
    ("brim_object_gap", "brim_separation"),
    ("deretraction_speed", "deretract_speed"),
    ("z_hop", "retract_lift"),
    ("filament_flow_ratio", "extrusion_multiplier"),
    ("material_density", "filament_density"),
    ("slow_down_min_speed", "min_print_speed"),
    ("top_surface_acceleration", "top_solid_infill_acceleration"),
];

/// Values for a name both slicers spell the same way but do not fill the same way.
/// The pair is that slicer's value, then this engine's; the engine's name is the
/// head of the block, because that is the definition which has to accept the result.
const SUPPORT_MATERIAL: [(&str, &str); 5] = [
    ("1", "everywhere"),
    ("true", "everywhere"),
    ("enable", "everywhere"),
    ("0", "none"),
    ("false", "none"),
];

const SUPPORT_MATERIAL_STYLE: [(&str, &str); 13] = [
    ("normal(auto)", "grid"),
    ("normal(manual)", "snug"),
    ("tree(auto)", "organic"),
    ("tree(manual)", "organic"),
    ("tree(hybrid)", "organic"),
    ("hybrid(auto)", "grid"),
    ("hybrid(manual)", "snug"),
    ("grid", "grid"),
    ("snug", "snug"),
    ("organic", "organic"),
    ("tree_slim", "organic"),
    ("tree_strong", "organic"),
    ("tree_hybrid", "organic"),
];

const BRIM_TYPE: [(&str, &str); 4] = [
    ("no_brim", "no_brim"),
    ("outer_only", "outer_only"),
    ("inner_only", "inner_only"),
    ("outer_and_inner", "outer_and_inner"),
];

const SEAM_POSITION: [(&str, &str); 5] = [
    ("nearest", "nearest"),
    ("aligned", "aligned"),
    ("back", "rear"),
    ("rear", "rear"),
    ("random", "random"),
];

const TOP_FILL_PATTERN: [(&str, &str); 1] = [
    ("monotonicline", "monotoniclines"),
];

const BOTTOM_FILL_PATTERN: [(&str, &str); 1] = [
    ("monotonicline", "monotoniclines"),
];

const SUPPORT_MATERIAL_PATTERN: [(&str, &str); 2] = [
    ("rectilinear", "rectilinear"),
    ("honeycomb", "honeycomb"),
];

const SUPPORT_MATERIAL_INTERFACE_PATTERN: [(&str, &str); 3] = [
    ("auto", "auto"),
    ("rectilinear", "rectilinear"),
    ("concentric", "concentric"),
];

const IRONING_TYPE: [(&str, &str); 3] = [
    ("top", "top"),
    ("topmost", "topmost"),
    ("solid", "solid"),
];

fn revalued(engine_name: &str) -> Option<&'static [(&'static str, &'static str)]> {
    Some(match engine_name {
        "support_material" => &SUPPORT_MATERIAL,
        "support_material_style" => &SUPPORT_MATERIAL_STYLE,
        "brim_type" => &BRIM_TYPE,
        "seam_position" => &SEAM_POSITION,
        "top_fill_pattern" => &TOP_FILL_PATTERN,
        "bottom_fill_pattern" => &BOTTOM_FILL_PATTERN,
        "support_material_pattern" => &SUPPORT_MATERIAL_PATTERN,
        "support_material_interface_pattern" => &SUPPORT_MATERIAL_INTERFACE_PATTERN,
        "ironing_type" => &IRONING_TYPE,
        _ => return None,
    })
}

/// Names this application knows it cannot carry, because the behaviour they
/// describe does not exist here. Membership is what the lookups use; the sentence
/// is the record of why, and nothing shows it to the person opening the file.
const NO_COUNTERPART: [(&str, &str); 16] = [
    ("skeleton_infill_density", "that slicer's skeleton infill has no counterpart here"),
    ("skin_infill_density", "that slicer's skin infill has no counterpart here"),
    ("support_object_first_layer_gap", "this engine states the gap to the bed with support_material_contact_distance"),
    ("filament_map_mode", "no filament mapping here"),
    ("filament_maps", "no filament mapping here"),
    ("filament_volume_maps", "no filament mapping here"),
    ("different_settings_to_system", "a preset bookkeeping key"),
    ("auto_brim", "auto brim selection is not modelled here"),
    ("brim_ears", "brim ears are a different shape"),
    ("outer_wall_infill_direction", "that slicer's wall direction ordering is not modelled here"),
    ("adaptive_infill_mode", "that slicer's combined adaptive infill is not modelled here"),
    ("height_speed_limit", "that slicer's per-height speed table is not modelled here"),
    ("overhang_fan_threshold", "this engine grades the overhang fan speed across four overlap steps, so one activation threshold has no single meaning here"),
    ("inner_wall_fan_speed", "this engine has no per-wall fan setting, and neither does the reference it was converted from"),
    ("outer_wall_fan_speed", "this engine has no per-wall fan setting, and neither does the reference it was converted from"),
    ("internal_solid_infill_pattern", "this engine names a pattern per surface, not one for all solid infill"),
];

/// Translate one setting into this engine's name and value, or `None` for a name
/// listed in [`NO_COUNTERPART`]. A name the table does not carry is its own target,
/// and a value the engine does not re-list is passed through unchanged.
pub fn translate_foreign_setting(key: &str, value: &str) -> Option<(String, String)> {
    if NO_COUNTERPART.iter().any(|(source, _reason)| *source == key) {
        return None;
    }

    let target = foreign_target_for(key);
    let Some(values) = revalued(&target) else {
        return Some((target, value.to_string()));
    };
    values
        .iter()
        .find(|(foreign, _)| *foreign == value)
        .map(|(_, engine)| (target, (*engine).to_string()))
}

/// Every rename the table carries, as `source`, `target` pairs flattened in table
/// order, which is the order `--dump-foreign-settings` prints and the gate reads.
pub fn foreign_setting_pairs() -> Vec<String> {
    RENAMED
        .iter()
        .flat_map(|(source, target)| [(*source).to_string(), (*target).to_string()])
        .collect()
}

/// The engine name a foreign key maps to, without looking at its value: the key
/// itself when the table carries no rename for it.
pub fn foreign_target_for(source: &str) -> String {
    RENAMED
        .iter()
        .find(|(foreign, _)| *foreign == source)
        .map_or_else(|| source.to_string(), |(_, target)| (*target).to_string())
}

/// The source name this table accepts for an engine key when a file carries more
/// than one that means it: the first entry in the table, which is also the name
/// [`foreign_name_for`] writes back, so importing and exporting agree.
pub fn canonical_foreign_source(engine_key: &str) -> String {
    RENAMED
        .iter()
        .find(|(_, target)| *target == engine_key)
        .map_or_else(String::new, |(source, _)| (*source).to_string())
}

/// The name and value one of this engine's settings goes out under, or `None` when
/// that slicer has no name for it -- a name it invented, or a value this engine
/// allows and the other one does not -- and the caller writes the engine's own name
/// instead of guessing.
pub fn foreign_name_for(engine_key: &str, engine_value: &str) -> Option<(String, String)> {
    for (source, target) in RENAMED.iter().filter(|(_, target)| *target == engine_key) {
        let Some(values) = revalued(target) else {
            continue;
        };
        if let Some(spelling) = spelling_of(values, engine_value) {
            return Some(((*source).to_string(), spelling.to_string()));
        }
    }

    for (source, target) in RENAMED.iter().filter(|(_, target)| *target == engine_key) {
        if source == target || revalued(target).is_some() {
            continue;
        }
        return Some(((*source).to_string(), engine_value.to_string()));
    }

    let spelling = spelling_of(revalued(engine_key)?, engine_value)?;
    Some((engine_key.to_string(), spelling.to_string()))
}

/// The foreign spelling of `engine_value`, first in table order. The identity spelling
/// is the weakest match: when the other side has a word of its own for a value, that
/// word is what goes out.
fn spelling_of(
    values: &'static [(&'static str, &'static str)],
    engine_value: &str,
) -> Option<&'static str> {
    let mut identity = None;
    for (foreign, engine) in values {
        if *engine != engine_value {
            continue;
        }
        if *foreign != engine_value {
            return Some(foreign);
        }
        identity = Some(*foreign);
    }
    identity
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_table_is_the_one_the_dump_prints() {
        let pairs = foreign_setting_pairs();
        assert_eq!(pairs.len(), 109 * 2);
        assert_eq!(pairs[0], "wall_loops");
        assert_eq!(pairs[1], "perimeters");
        assert_eq!(pairs[pairs.len() - 2], "top_surface_acceleration");
        assert_eq!(pairs[pairs.len() - 1], "top_solid_infill_acceleration");
    }

    #[test]
    fn a_renamed_name_keeps_its_value_and_an_unknown_name_is_its_own() {
        assert_eq!(
            translate_foreign_setting("wall_loops", "4"),
            Some(("perimeters".to_string(), "4".to_string()))
        );
        assert_eq!(
            translate_foreign_setting("seam_gap", "15%"),
            Some(("seam_gap_distance".to_string(), "15%".to_string()))
        );
        assert_eq!(
            translate_foreign_setting("enable_support", "1"),
            Some(("support_material".to_string(), "everywhere".to_string()))
        );
        assert_eq!(translate_foreign_setting("skeleton_infill_density", "15%"), None);
        assert_eq!(
            translate_foreign_setting("support_material_style", "tree(auto)"),
            Some(("support_material_style".to_string(), "organic".to_string()))
        );
    }

    #[test]
    fn a_value_the_engine_does_not_re_list_is_refused_not_guessed() {
        assert_eq!(translate_foreign_setting("seam_position", "sideways"), None);
        assert_eq!(translate_foreign_setting("brim_type", "auto_brim"), None);
    }

    #[test]
    fn two_sources_for_one_key_resolve_to_the_first_in_the_table() {
        assert_eq!(canonical_foreign_source("support_material_style"), "support_type");
        assert_eq!(canonical_foreign_source("bridge_flow_ratio"), "bridge_flow");
        assert_eq!(canonical_foreign_source("no_such_setting_here"), "");
    }

    #[test]
    fn the_reverse_picks_the_identity_weakest_and_the_table_first() {
        assert_eq!(
            foreign_name_for("support_material", "everywhere"),
            Some(("enable_support".to_string(), "1".to_string()))
        );
        assert_eq!(
            foreign_name_for("support_material", "none"),
            Some(("enable_support".to_string(), "0".to_string()))
        );
        assert_eq!(
            foreign_name_for("perimeters", "4"),
            Some(("wall_loops".to_string(), "4".to_string()))
        );
        assert_eq!(
            foreign_name_for("seam_position", "rear"),
            Some(("seam_position".to_string(), "back".to_string()))
        );
        assert_eq!(
            foreign_name_for("seam_position", "nearest"),
            Some(("seam_position".to_string(), "nearest".to_string()))
        );
    }

    #[test]
    fn every_mapped_value_can_be_written_back() {
        for (engine_name, entries) in [
            ("support_material", &SUPPORT_MATERIAL[..]),
            ("support_material_style", &SUPPORT_MATERIAL_STYLE[..]),
            ("brim_type", &BRIM_TYPE[..]),
            ("seam_position", &SEAM_POSITION[..]),
            ("top_fill_pattern", &TOP_FILL_PATTERN[..]),
            ("bottom_fill_pattern", &BOTTOM_FILL_PATTERN[..]),
            ("support_material_pattern", &SUPPORT_MATERIAL_PATTERN[..]),
            ("support_material_interface_pattern", &SUPPORT_MATERIAL_INTERFACE_PATTERN[..]),
            ("ironing_type", &IRONING_TYPE[..]),
        ] {
            for (_, engine_value) in entries {
                let written = foreign_name_for(engine_name, engine_value);
                assert!(written.is_some(), "{engine_name} = {engine_value} cannot be written back");
                let (name, value) = written.unwrap();
                assert_eq!(
                    translate_foreign_setting(&name, &value).map(|(_, engine)| engine),
                    Some((*engine_value).to_string()),
                    "{name} = {value} does not translate back to {engine_name} = {engine_value}"
                );
            }
        }
    }

    #[test]
    fn a_key_this_engine_invented_is_not_written_under_a_foreign_name() {
        assert_eq!(foreign_name_for("no_such_setting_here", "1"), None);
        assert_eq!(foreign_name_for("seam_position", "sideways"), None);
    }
}
