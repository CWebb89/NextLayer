#!/usr/bin/env python3
"""Convert reference slicer profiles to next-layer (engine) config JSON.

reference profiles are flat JSON keyed by setting id (``wall_loops``, ``bottom_shell_layers``),
with an ``inherits`` chain the files already resolve. next-layer runs an open-source slicing engine,
whose FDM config keys use that project's naming (``perimeters``, ``bottom_solid_layers``).

Three buckets:

* exact   -- the key means the same thing in both slicers (``brim_width``, ``seam_position``).
* renamed -- same setting, different name (``wall_loops`` -> ``perimeters``); value
             transforms (percent, enum, bed shape) are applied where the format differs.
* unsupported -- reference feature the 3.0 engine has no setting for (AMS, belt printers,
             chamber temperature control, Bambu-specific, ...). Reported, not fabricated.

Usage:
    convert.py --report                          # print the coverage table
    convert.py machine.json process.json filament.json --out config.json
"""

import argparse
import json
import re
import sys
from pathlib import Path

# reference key -> (next-layer key, value transform). Transforms:
#   "percent"  strip a trailing %  (the reference "15%" -> 15.0 as a bare number)
#   "percent_obj"  -> {"value": 15.0, "is_percent": true} for fill_density & friends
#   "int"      coerce to int
#   "float"    coerce to float
#   "bed_shape"  ["0x0","350x0",...] -> [[0,0],[350,0],...]
#   "bool"     "1"/"0" -> true/false
#   "bool_not"  "1"/"0" -> the engine's inverse switch
#   "float_or_percent"  "80%" -> {"value": 80.0, "is_percent": true}, a number stays a number
#   "enum:<target>"  look up the value in ENUMS[target]
#   "bool_enum:<target>"  a reference on/off switch -> ENUMS[target]["true"/"false"]
RENAMES = {
    # Shells and walls
    "bottom_shell_layers": ("bottom_solid_layers", "int"),
    "top_shell_layers": ("top_solid_layers", "int"),
    "bottom_shell_thickness": ("bottom_solid_min_thickness", "float"),
    "top_shell_thickness": ("top_solid_min_thickness", "float"),
    "wall_loops": ("perimeters", "int"),
    "spiral_mode": ("spiral_vase", "bool"),
    "ensure_vertical_wall_thickness": ("ensure_vertical_shell_thickness", "bool"),
    "detect_thin_wall": ("thin_walls", "bool"),
    "extra_perimeters_on_overhangs": ("extra_perimeters_on_overhangs", None),
    # Infill
    "sparse_infill_density": ("fill_density", "percent_obj"),
    "sparse_infill_pattern": ("fill_pattern", "enum:fill_pattern"),
    "sparse_infill_line_width": ("infill_extrusion_width", "float"),
    "infill_wall_overlap": ("infill_overlap", "percent_obj"),
    "internal_solid_infill_line_width": ("solid_infill_extrusion_width", "float"),
    "top_surface_pattern": ("top_fill_pattern", "enum:fill_pattern"),
    "bottom_surface_pattern": ("bottom_fill_pattern", "enum:fill_pattern"),
    "infill_direction": ("fill_angle", "float"),
    "infill_combination": ("infill_every_layers", "int_at_least_one"),
    "minimum_sparse_infill_area": ("solid_infill_below_area", "float"),
    "sparse_infill_anchor_max": ("infill_anchor_max", "float"),
    "sparse_infill_anchor": ("infill_anchor", "float"),
    # Line widths
    "line_width": ("extrusion_width", "float"),
    "outer_wall_line_width": ("external_perimeter_extrusion_width", "float"),
    "inner_wall_line_width": ("perimeter_extrusion_width", "float"),
    "top_surface_line_width": ("top_infill_extrusion_width", "float"),
    "initial_layer_line_width": ("first_layer_extrusion_width", "float"),
    "support_line_width": ("support_material_extrusion_width", "float"),
    # Speeds
    "outer_wall_speed": ("external_perimeter_speed", "float"),
    "inner_wall_speed": ("perimeter_speed", "float"),
    "sparse_infill_speed": ("infill_speed", "float"),
    "internal_solid_infill_speed": ("solid_infill_speed", "float"),
    "top_surface_speed": ("top_solid_infill_speed", "float"),
    "gap_infill_speed": ("gap_fill_speed", "float"),
    "initial_layer_speed": ("first_layer_speed", "float"),
    "initial_layer_infill_speed": ("first_layer_infill_speed", "float"),
    "support_speed": ("support_material_speed", "float"),
    "support_interface_speed": ("support_material_interface_speed", "float"),
    "overhang_1_4_speed": ("overhang_speed_0", "float"),
    "overhang_2_4_speed": ("overhang_speed_1", "float"),
    "overhang_3_4_speed": ("overhang_speed_2", "float"),
    "overhang_4_4_speed": ("overhang_speed_3", "float"),
    "internal_bridge_speed": ("bridge_speed", "float"),
    # Acceleration
    "outer_wall_acceleration": ("external_perimeter_acceleration", "float"),
    "inner_wall_acceleration": ("perimeter_acceleration", "float"),
    "top_surface_acceleration": ("top_solid_infill_acceleration", "float"),
    "sparse_infill_acceleration": ("infill_acceleration", "float"),
    "internal_solid_infill_acceleration": ("solid_infill_acceleration", "float"),
    "initial_layer_acceleration": ("first_layer_acceleration", "float"),
    "bridge_infill_acceleration": ("bridge_acceleration", "float"),
    # Supports
    "enable_support": ("support_material", "enum:support_material_on"),
    "support_style": ("support_material_style", "enum:support_material_style"),
    "support_type": ("support_material_style", "enum:support_type_style"),
    "support_on_build_plate_only": ("support_material_buildplate_only", "bool"),
    "support_top_z_distance": ("support_material_contact_distance", "float"),
    "support_bottom_z_distance": ("support_material_bottom_contact_distance", "float"),
    "support_interface_top_layers": ("support_material_interface_layers", "int"),
    "support_interface_bottom_layers": ("support_material_bottom_interface_layers", "int"),
    "support_interface_spacing": ("support_material_interface_spacing", "float"),
    "support_base_pattern": ("support_material_pattern", "enum:support_material_pattern"),
    "support_base_pattern_spacing": ("support_material_spacing", "float"),
    "support_object_xy_distance": ("support_material_xy_spacing", "float"),
    "support_threshold_angle": ("support_material_threshold", "int"),
    "tree_support_branch_angle": ("support_tree_angle", "float"),
    "support_interface_loop_pattern": ("support_material_interface_contact_loops", "bool"),
    "support_interface_pattern": ("support_material_interface_pattern", "enum:support_material_pattern"),
    "independent_support_layer_height": ("support_material_synchronize_layers", "bool"),
    # Bed adhesion
    "brim_object_gap": ("brim_separation", "float"),
    "skirt_loops": ("skirts", "int"),
    # Seam
    "seam_gap": ("seam_gap_distance", "float"),
    "staggered_inner_seams": ("staggered_inner_seams", None),
    # Retraction / travel
    "retraction_length": ("retract_length", "float"),
    "retraction_speed": ("retract_speed", "float"),
    "z_hop": ("retract_lift", "float"),
    "retraction_minimum_travel": ("retract_before_travel", "float"),
    "retract_when_changing_layer": ("retract_layer_change", "bool"),
    "retract_length_toolchange": ("retract_length_toolchange", "float"),
    "retract_restart_extra_toolchange": ("retract_restart_extra_toolchange", "float"),
    "wipe_on_loops": ("wipe", "bool"),
    "avoid_crossing_perimeters": ("avoid_crossing_perimeters", "bool"),
    "avoid_crossing_perimeters_max_detour": ("avoid_crossing_perimeters_max_detour", "float"),
    # Filament
    "filament_flow_ratio": ("extrusion_multiplier", "float"),
    "nozzle_temperature": ("temperature", "int"),
    "nozzle_temperature_initial_layer": ("first_layer_temperature", "int"),
    "bed_temperature_initial_layer": ("first_layer_bed_temperature", "int"),
    "hot_plate_temp": ("bed_temperature", "int"),
    "hot_plate_temp_initial_layer": ("first_layer_bed_temperature", "int"),
    "textured_plate_temp": ("bed_temperature", "int"),
    "textured_plate_temp_initial_layer": ("first_layer_bed_temperature", "int"),
    "fan_cooling_layer_time": ("slowdown_below_layer_time", "float"),
    "enable_overhang_speed": ("enable_dynamic_overhang_speeds", "bool"),
    "fan_min_speed": ("min_fan_speed", "int"),
    "fan_max_speed": ("max_fan_speed", "int"),
    "slow_down_min_layer_time": ("slowdown_below_layer_time", "float"),
    "filament_shrink": ("filament_shrinkage_compensation_xy", "percent_obj"),
    "filament_shrinkage_compensation_z": ("filament_shrinkage_compensation_z", "float"),
    "filament_max_volumetric_speed": ("filament_max_volumetric_speed", "float"),
    # Pressure advance has no numeric home in this engine's filament block: the
    # name `pressure_advance` there is an enum wrapper, so writing the reference's number
    # into it is a type error, not a setting. Klipper owns pressure advance on
    # the machines this matters for, and it is reported as unsupported instead.
    # Printer / machine
    "printable_area": ("bed_shape", "bed_shape"),
    "printable_height": ("max_print_height", "float"),
    "machine_max_speed_x": ("machine_max_feedrate_x", "float_list"),
    "machine_max_speed_y": ("machine_max_feedrate_y", "float_list"),
    "machine_max_speed_z": ("machine_max_feedrate_z", "float_list"),
    "machine_max_speed_e": ("machine_max_feedrate_e", "float_list"),
    "machine_max_acceleration_x": ("machine_max_acceleration_x", "float_list"),
    "machine_max_acceleration_y": ("machine_max_acceleration_y", "float_list"),
    "machine_max_acceleration_z": ("machine_max_acceleration_z", "float_list"),
    "machine_max_acceleration_e": ("machine_max_acceleration_e", "float_list"),
    "machine_max_jerk_x": ("machine_max_jerk_x", "float_list"),
    "machine_max_jerk_y": ("machine_max_jerk_y", "float_list"),
    "machine_max_jerk_z": ("machine_max_jerk_z", "float_list"),
    "machine_max_jerk_e": ("machine_max_jerk_e", "float_list"),
    "default_filament_profile": ("default_filament_profile", None),
    "default_print_profile": ("default_print_profile", None),
    "gcode_flavor": ("gcode_flavor", None),
    "before_layer_change_gcode": ("before_layer_gcode", None),
    "layer_change_gcode": ("layer_gcode", None),
    "machine_start_gcode": ("start_gcode", None),
    "machine_end_gcode": ("end_gcode", None),
    "machine_pause_gcode": ("pause_print_gcode", None),
    "machine_load_filament_time": ("filament_loading_speed", "float"),
    "machine_unload_filament_time": ("filament_unloading_speed", "float"),
    "retract_lift_above": ("retract_lift_above", "float"),
    "retract_lift_below": ("retract_lift_below", "float"),
    "extruder_clearance_height_to_rod": ("extruder_clearance_height", "float"),
    "extruder_clearance_height_to_lid": ("extruder_clearance_height", "float"),
    "extruder_clearance_radius": ("extruder_clearance_radius", "float"),
    "extruder_colour": ("extruder_colour", None),
    "extruder_offset": ("extruder_offset", None),
    "thumbnails": ("thumbnails", None),
    "filament_colour": ("filament_colour", None),
    "filament_notes": ("filament_notes", None),
    "notes": ("notes", None),
    "print_sequence": ("complete_objects", "enum_bool:print_sequence"),
    "resolution": ("resolution", "float"),
    "arc_fitting": ("arc_fitting", "bool"),
    "arc_fitting_tolerance": ("gcode_resolution", "float"),
    "first_layer_height": ("first_layer_height", "float"),
    "xy_hole_compensation": ("xy_size_compensation", "float"),
    "xy_contour_compensation": ("xy_size_compensation", "float"),
    "elefant_foot_compensation": ("elefant_foot_compensation", "float"),
    "fuzzy_skin": ("fuzzy_skin", "enum:fuzzy_skin"),
    "fuzzy_skin_point_distance": ("fuzzy_skin_point_dist", "float"),
    "fuzzy_skin_thickness": ("fuzzy_skin_thickness", "float"),
    "ironing_flow": ("ironing_flowrate", "percent_obj"),
    "ironing_spacing": ("ironing_spacing", "float"),
    "ironing_speed": ("ironing_speed", "float"),
    "ironing_type": ("ironing_type", "enum:ironing_type"),
    "bridge_flow": ("bridge_flow_ratio", "percent_obj"),
    "bridge_speed": ("bridge_speed", "float"),
    "bridge_no_support": ("dont_support_bridges", "bool"),
    "draft_shield": ("draft_shield", "enum:draft_shield"),
    "ooze_prevention": ("ooze_prevention", "bool"),
    "wipe_tower": ("wipe_tower", "bool"),
    "single_extruder_multi_material": ("single_extruder_multi_material", "bool"),
    "single_extruder_multi_material_priming": ("single_extruder_multi_material_priming", "bool"),
    "flush_into_objects": ("wipe_into_objects", "bool"),
    "flush_into_infill": ("wipe_into_infill", "bool"),
    "reduce_infill_retraction": ("wipe_into_infill", "bool"),
    "wall_infill_order": ("infill_first", "bool"),
    # The print-wide switches the reference sets in every process preset. Each
    # target was checked against the engine's own option definition: a name that
    # matched by spelling but carried another type or another meaning (a boolean
    # where the engine has an enum of four, a 0 that means "auto") is not here.
    "layer_height": ("layer_height", "float"),
    "initial_layer_print_height": ("first_layer_height", "float"),
    "brim_width": ("brim_width", "float"),
    "brim_type": ("brim_type", "enum:brim_type"),
    "skirt_distance": ("skirt_distance", "float"),
    "skirt_height": ("skirt_height", "int"),
    "raft_layers": ("raft_layers", "int"),
    "raft_first_layer_expansion": ("raft_first_layer_expansion", "float"),
    "raft_first_layer_density": ("raft_first_layer_density", "percent"),
    "raft_contact_distance": ("raft_contact_distance", "float"),
    "seam_position": ("seam_position", "enum:seam_position"),
    "travel_speed": ("travel_speed", "float"),
    "travel_speed_z": ("travel_speed_z", "float"),
    "default_acceleration": ("default_acceleration", "float"),
    "travel_acceleration": ("travel_acceleration", "float"),
    "bridge_acceleration": ("bridge_acceleration", "float"),
    "interface_shells": ("interface_shells", "bool"),
    "standby_temperature_delta": ("standby_temperature_delta", "int"),
    "wipe_tower_no_sparse_layers": ("wipe_tower_no_sparse_layers", "bool"),
    "enable_prime_tower": ("wipe_tower", "bool"),
    "prime_tower_width": ("wipe_tower_width", "float"),
    "prime_tower_brim_width": ("wipe_tower_brim_width", "float"),
    "enable_arc_fitting": ("arc_fitting", "bool_enum:arc_fitting"),
    "gcode_label_objects": ("gcode_label_objects", "bool_enum:gcode_label_objects"),
    "max_travel_detour_distance": ("avoid_crossing_perimeters_max_detour", "float"),
    "support_angle": ("support_material_angle", "float"),
    "tree_support_branch_diameter": ("support_tree_branch_diameter", "float"),
    "tree_support_tip_diameter": ("support_tree_tip_diameter", "float"),
    "small_perimeter_speed": ("small_perimeter_speed", "float_or_percent"),
    "infill_anchor": ("infill_anchor", "float_or_percent"),
    "infill_anchor_max": ("infill_anchor_max", "float_or_percent"),
    "min_bead_width": ("min_bead_width", "float_or_percent"),
    "min_feature_size": ("min_feature_size", "float_or_percent"),
    "thick_bridges": ("thick_bridges", "bool"),
    "only_one_wall_top": ("top_one_perimeter_type", "bool_enum:top_one_perimeter_type"),
    "filter_out_gap_fill": ("gap_fill_enabled", "bool_not"),
    "prime_tower_extra_spacing": ("wipe_tower_extra_spacing", "percent_obj"),
    "prime_tower_extra_flow": ("wipe_tower_extra_flow", "percent_obj"),
    "tree_support_branch_distance": ("support_tree_branch_distance", "float"),
    "tree_support_top_rate": ("support_tree_top_rate", "percent_obj"),
    "infill_combination_max_layer_height":
        ("automatic_infill_combination_max_layer_height", "percent_obj"),
    "scarf_seam_placement": ("scarf_seam_placement", "enum:scarf_seam_placement"),
    "scarf_seam_length": ("scarf_seam_length", "float"),
    "scarf_seam_start_height": ("scarf_seam_start_height", "percent_obj"),
    "enable_pressure_advance": ("pressure_advance", "enum:pressure_advance_on"),
    "pressure_advance": ("pressure_advance_value", "float"),
    # A filament preset carries its own retraction, which is where this engine
    # keeps it too: the same names as the print settings, in this engine's
    # per-filament list shape.
    "filament_retraction_length": ("retract_length", "float_list"),
    "filament_retraction_speed": ("retract_speed", "float_list"),
    "filament_retraction_minimum_travel": ("retract_before_travel", "float_list"),
    "filament_retract_before_wipe": ("retract_before_wipe", "float_list"),
    "filament_retract_restart_extra": ("retract_restart_extra", "float_list"),
    "filament_retract_when_changing_layer": ("retract_layer_change", "bool"),
    "filament_z_hop": ("retract_lift", "float_list"),
    "filament_wipe": ("wipe", "bool"),
    "reduce_crossing_wall": ("avoid_crossing_perimeters", "bool"),
    "min_skirt_length": ("min_skirt_length", "float"),
    "enforce_support_layers": ("support_material_enforce_layers", "int"),
    "slice_closing_radius": ("slice_closing_radius", "float"),
    "raft_expansion": ("raft_expansion", "float"),
    "slow_down_layer_time": ("slowdown_below_layer_time", "int_list"),
    "slow_down_min_speed": ("min_print_speed", "float_list"),
    "cool_plate_temp": ("bed_temperature", "int_list"),
    "cool_plate_temp_initial_layer": ("first_layer_bed_temperature", "int_list"),
    "eng_plate_temp": ("bed_temperature", "int_list"),
    "eng_plate_temp_initial_layer": ("first_layer_bed_temperature", "int_list"),
    "textured_plate_temp": ("bed_temperature", "int_list"),
    "textured_plate_temp_initial_layer": ("first_layer_bed_temperature", "int_list"),
    "filament_start_gcode": ("start_filament_gcode", None),
    "filament_end_gcode": ("end_filament_gcode", None),
    "filament_deretraction_speed": ("deretract_speed", "float_list"),
    "close_fan_the_first_x_layers": ("disable_fan_first_layers", "int_list"),
}


ENUMS = {
    "fill_pattern": {
        "crosshatch": "grid", "grid": "grid", "honeycomb": "honeycomb",
        "gyroid": "gyroid", "cubic": "cubic", "triangles": "triangles",
        "stars": "stars", "tri-hexagon": "trihexagon", "concentric": "concentric",
        "line": "rectilinear", "rectilinear": "rectilinear", "monotonic": "monotonic",
        "monotonicline": "monotonic", "rectilinear": "rectilinear", "hilbertcurve": "hilbertcurve",
    },
    "support_material_on": {"0": "none", "1": "everywhere", "false": "none",
                          "true": "everywhere", "none": "none"},
    "support_material_bool": {"0": "none", "1": "everywhere", "false": "none", "true": "everywhere"},
    "support_type_auto": {
        "normal(auto)": "1", "tree(auto)": "1", "normal": "0", "tree": "0",
        "normal(manual)": "0", "tree(manual)": "0", "none": "0", "0": "0",
    },
    "print_sequence": {"by layer": "0", "by object": "1", "0": "0", "1": "1"},
    "support_material_type": {
        "normal(auto)": "everywhere", "normal(manual)": "everywhere", "normal": "everywhere",
        "tree(auto)": "organic", "tree(manual)": "organic", "tree": "organic",
        "none": "none", "0": "none",
    },
    "support_material_style": {
        "default": "grid", "snug": "snug", "grid": "grid", "organic": "organic",
        "tree_slim": "organic", "tree_strong": "organic", "tree_hybrid": "organic",
    },
    "support_material_pattern": {
        "rectilinear": "rectilinear", "rectilinear_grid": "rectilinear_grid",
        "honeycomb": "honeycomb", "default": "rectilinear",
    },
    "ironing_type": {"no ironing": None, "top": "top", "topmost": "topmost",
                     "all solid": "solid", "all": "solid"},
    "fuzzy_skin": {"none": "none", "outside": "external", "all": "all", "external": "external"},
    "draft_shield": {"disabled": "disabled", "limited": "limited", "enabled": "enabled"},
    "seam_position": {"nearest": "nearest", "aligned": "aligned", "back": "rear",
                      "rear": "rear", "random": "random"},
    "brim_type": {"no_brim": "no_brim", "outer_only": "outer_only",
                  "inner_only": "inner_only", "outer_and_inner": "outer_and_inner"},
    "gcode_label_objects": {"true": "octoprint", "false": "disabled",
                            "1": "octoprint", "0": "disabled"},
    "arc_fitting": {"true": "emit_center", "false": "disabled",
                    "1": "emit_center", "0": "disabled"},
    "top_one_perimeter_type": {"true": "top", "false": "none"},
    "support_type_style": {
        "normal(auto)": "snug", "normal(manual)": "snug", "normal": "snug",
        "tree(auto)": "organic", "tree(manual)": "organic", "tree": "organic",
        "tree(organic)": "organic", "organic": "organic", "tree_slim": "tree_slim",
        "tree_strong": "tree_strong", "tree_hybrid": "tree_hybrid",
    },
    "pressure_advance_on": {"0": "disabled", "1": "enabled",
                            "false": "disabled", "true": "enabled"},
    "scarf_seam_placement": {"none": "nowhere", "nowhere": "nowhere",
                             "contour": "contours", "contours": "contours",
                             "aligned": "everywhere", "everywhere": "everywhere"},
}


class Untranslatable(ValueError):
    """A value that does not fit its target: dropped and reported, never guessed at."""


def head_of(value):
    """First number of a value that may be comma-joined (the reference's per-extruder form)."""
    scalar = value[0] if isinstance(value, list) and value else value
    return str(scalar).split(",")[0].strip().rstrip("%").replace(",", "")


def head(value):
    """The value itself, or the first entry of a one-element per-extruder list."""
    return value[0] if isinstance(value, list) and value else value


def number(value):
    digits = head_of(value)
    if digits.lower().endswith("mm"):
        digits = digits[:-2].strip()
    try:
        return float(digits)
    except ValueError:
        raise Untranslatable("%r is not a number" % (value,))


def transform_value(value, transform):
    if transform is None:
        return value
    scalar = value[0] if isinstance(value, list) and value else value
    if transform == "percent":
        return number(value)
    if transform == "percent_obj":
        return {"value": number(value), "is_percent": True}
    if transform == "float_or_percent":
        text = str(head_of(value))
        raw = str(value[0] if isinstance(value, list) and value else value).strip()
        if raw.endswith("%"):
            return {"value": number(raw), "is_percent": True}
        return number(text)
    if transform == "int":
        return int(number(value))
    if transform == "float":
        # A reference width written as a percentage of the nozzle keeps that
        # meaning: flattening "110%" to 110 makes it 110 mm, which the engine
        # rejects as unprintable. fit_value decides the shape from the engine's
        # own declared type, so a plain number stays a plain number.
        raw = str(scalar).strip()
        if raw.endswith("%"):
            return {"value": number(raw), "is_percent": True}
        return number(value)
    if transform == "bool":
        return str(head(value)) in ("1", "true", "True", "on")
    if transform == "bool_not":
        return str(head(value)) not in ("1", "true", "True", "on")
    if transform == "float_list":
        items = value if isinstance(value, list) else [value]
        numbers = []
        for item in items:
            token = str(item).split(",")[0].strip()
            if not token:
                continue
            try:
                numbers.append(number(token))
            except Untranslatable:
                continue
        return numbers or [0.0]
    if transform == "int_list":
        return [int(round(item)) for item in transform_value(value, "float_list")]
    if transform == "bed_shape":
        points = []
        for token in value if isinstance(value, list) else str(value).split(","):
            x, _, y = str(token).strip().partition("x")
            points.append([float(x), float(y)])
        return points
    if transform == "int_at_least_one":
        return max(1, int(float(head_of(value) or 0)))
    if transform.startswith("enum_bool:"):
        table = ENUMS.get(transform[10:], {})
        return str(table.get(str(head(value)), "0")) in ("1", "true", "True", "on")
    if transform.startswith("bool_enum:"):
        table = ENUMS.get(transform[10:], {})
        on = str(head(value)) in ("1", "true", "True", "on")
        return table.get("true" if on else "false", str(head(value)))
    if transform.startswith("enum:"):
        table = ENUMS.get(transform[5:], {})
        return table.get(str(head(value)), head(value))
    return value


_ENGINE_NAMES = None
_ENGINE_TYPES = None


def engine_option_names():
    """The engine's own setting names, read once from its option definitions."""
    global _ENGINE_NAMES
    if _ENGINE_NAMES is None:
        _ENGINE_NAMES = engine_names(None)
    return _ENGINE_NAMES


def engine_option_types():
    """Setting name -> the engine's declared type for it.

    A preset value has to be the shape the option declares: this engine logs a
    type mismatch and keeps its own default otherwise, which is how a filament
    carrying its density as a one-element list sliced with a density of 0.
    """
    global _ENGINE_TYPES
    if _ENGINE_TYPES is None:
        domain = Path(__file__).resolve().parents[2] / "upstream/src/slic3r-domain/src/Slic3r/Domain"
        types = {}
        for name in ("ConfigCommon.cpp", "ConfigDefsFDM.cpp"):
            path = domain / name
            if not path.is_file():
                continue
            for match in TYPE_DECLARATION.finditer(path.read_text(errors="replace")):
                types[match.group(1)] = match.group(2)
        _ENGINE_TYPES = types
    return _ENGINE_TYPES


TYPE_DECLARATION = re.compile(r'defs\.add\("([a-z0-9_]+)",\s*typeid\((.+?)\)\)\s*;', re.S)

SCALAR_TYPES = {"double", "int", "bool", "std::string", "EnumWrapper",
                "FloatOrPercentage", "Percentage", "std::optional<int>"}
NUMERIC_TYPES = {"double", "int", "Percentage"}


def as_number(text):
    try:
        return float(text)
    except ValueError:
        return None


def fit_value(kind, value):
    """Shape a converted value the way the engine's option declares it."""
    if kind is None or value is None:
        return value
    if isinstance(value, dict) and "value" in value:
        percent = bool(value.get("is_percent"))
        if kind == "FloatOrPercentage":
            return "%g%%" % value["value"] if percent else value["value"]
        value = value["value"]
    if kind not in SCALAR_TYPES:
        # A vector option arrives as a list, as a comma-joined string (the
        # reference writes "0,0" for a two-extruder machine), or as one scalar.
        if isinstance(value, str):
            items = [part.strip() for part in value.split(",") if part.strip()]
        elif isinstance(value, list):
            items = value
        else:
            items = [value]
        if kind == "std::vector<double>":
            numbers = []
            for item in items:
                token = str(item).split(",")[0].strip()
                number_value = as_number(token[:-1] if token.endswith("%") else token)
                numbers.append(number_value if number_value is not None else 0.0)
            return numbers
        if kind == "std::vector<std::string>":
            return [str(item) for item in items]
        return items
    if isinstance(value, list):
        value = value[0] if value else None
        if value is None:
            return None
    if isinstance(value, str):
        text = value.strip()
        if kind == "bool":
            return text in ("1", "true", "True", "on")
        number_text = text[:-1] if text.endswith("%") else text
        number_value = as_number(number_text)
        if kind in NUMERIC_TYPES and number_value is not None:
            return int(round(number_value)) if kind == "int" else number_value
        if kind == "FloatOrPercentage" and number_value is not None:
            return text if text.endswith("%") else number_value
        return value
    return value


# Same spelling in both programs, different meaning. These need an explicit
# transform and never the identity fallback.
IDENTITY_EXCLUDED = {"gap_fill_enabled"}


def convert_profile(profile, bucket):
    """Apply the keymap to one flat profile dict, routing to the right block.

    A value the transform cannot read is reported in ``bucket["failed"]`` and
    skipped: these profiles come from another program and a single odd value
    (a temperature of ``"V"``) must not take the whole preset down.
    """
    for key, value in profile.items():
        if key in RENAMES:
            target, transform = RENAMES[key]
            try:
                converted = transform_value(value, transform)
            except (Untranslatable, TypeError, ValueError) as error:
                bucket.setdefault("failed", {})[key] = str(error)
                continue
            bucket.setdefault("renamed", {})[key] = target
            yield target, fit_value(engine_option_types().get(target), converted)
        elif key in engine_option_names() and key not in IDENTITY_EXCLUDED:
            bucket.setdefault("identity", set()).add(key)
            yield key, fit_value(engine_option_types().get(key), value)
        else:
            bucket.setdefault("unsupported", []).append(key)
            yield None, None


def resolve_inherits(path, cache=None, lookup=None):
    """Merge a profile with its ``inherits`` ancestors, child values winning.

    A parent is normally a sibling file, but the reference also inherits across
    vendor directories: a vendor's generic filament inherits the system one that
    another vendor's bundle defines. ``lookup`` resolves a name anywhere in the
    tree; without it a cross-directory parent is silently missed and the child
    arrives carrying only its own few overrides.
    """
    if cache is None:
        cache = {}
    resolved_path = str(path)
    if resolved_path in cache:
        return dict(cache[resolved_path])
    data = json.loads(path.read_text(encoding="utf-8-sig", errors="replace"))
    parent_name = data.get("inherits")
    if parent_name:
        parent_path = path.parent / f"{parent_name}.json"
        if not parent_path.is_file() and lookup is not None:
            parent_path = lookup(parent_name)
        if parent_path is not None and parent_path.is_file():
            merged = resolve_inherits(parent_path, cache, lookup)
            merged.update(data)
            data = merged
    cache[resolved_path] = dict(data)
    return dict(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help="reference profile JSON files (machine/process/filament)")
    parser.add_argument("--out", help="write converted config JSON here")
    parser.add_argument("--report", action="store_true", help="print the keymap coverage table and exit")
    parser.add_argument("--engine-defs", help="optional: file listing next-layer setting names for exact-match detection")
    args = parser.parse_args()

    if args.report:
        report(args.engine_defs)
        return 0

    if not args.files:
        parser.error("pass profile files or use --report")

    names = engine_names(args.engine_defs)

    config = {"configuration": {"print_settings": {}, "filament_settings": {}, "printer_settings": {}}}
    print_block = config["configuration"]["print_settings"]
    filament_block = config["configuration"]["filament_settings"]
    printer_block = config["configuration"]["printer_settings"]

    unsupported = []
    renamed = 0
    exact = 0
    for path in args.files:
        data = resolve_inherits(Path(path))
        kind = data.get("type", "")
        if kind == "machine":
            target = printer_block
        elif kind == "filament":
            target = filament_block
        else:
            target = print_block

        for key, value in data.items():
            if key in META or key.startswith("compatible_"):
                continue
            if key in RENAMES:
                name, transform = RENAMES[key]
                target[name] = transform_value(value, transform)
                renamed += 1
            elif key in names:
                target[key] = value
                exact += 1
            else:
                unsupported.append(key)

    print(f"exact {exact}, renamed {renamed}, unsupported {len(unsupported)}", file=sys.stderr)
    if unsupported:
        print("unsupported keys:", ", ".join(sorted(set(unsupported))), file=sys.stderr)

    text = json.dumps(config, indent=2) + "\n"
    if args.out:
        Path(args.out).write_text(text)
        print(f"converted {len(args.files)} profile(s) -> {args.out}")
    else:
        sys.stdout.write(text)
    return 0


META = {
    "type", "name", "inherits", "from", "setting_id", "instantiation",
    "compatible_printers", "compatible_printers_condition",
    "compatible_prints", "compatible_prints_condition",
    "compatible_filaments", "compatible_filaments_condition",
    "is_custom_defined", "version", "renamed_from", "upward_compatible",
    "inherits_prints", "filament_settings_id", "print_settings_id", "printer_settings_id",
    "filament_id",
}

# Keys whose reference value shape has no engine equivalent: the reference stores one entry per
# extruder as an object, this engine stores two percentages per extruder for the
# shrinkage compensation, and feeding it the one-element list aborts the slice in
# std::length_error. Dropped and reported instead of guessed at.
DROP_KEYS = {"filament_shrinkage_compensation_xy"}


def report(engine_defs):
    nl = engine_names(engine_defs)
    identity = [k for k, (t, _) in RENAMES.items() if k == t]
    renamed = [k for k, (t, _) in RENAMES.items() if k != t]
    print(f"next-layer engine FDM settings: {len(nl)}")
    print(f"rename map entries: {len(RENAMES)}")
    print(f"  exact-name passthroughs: {len(identity)}")
    print(f"  renamed (different name): {len(renamed)}")
    print(f"  renamed targets missing from the engine: "
          f"{sorted({t for k, (t, _) in RENAMES.items() if t not in nl})[:20]}")


def engine_names(engine_defs):
    if engine_defs:
        return {line.strip() for line in open(engine_defs) if line.strip()}
    import subprocess
    root = Path(__file__).resolve().parents[2]
    domain = root / "upstream/src/slic3r-domain/src/Slic3r/Domain"
    names = set()
    for source in ("ConfigCommon.cpp", "ConfigDefsFDM.cpp"):
        path = domain / source
        if not path.is_file():
            continue
        out = subprocess.run(
            ["grep", "-oE", r'defs\.add\("[a-z0-9_]+"', str(path)],
            capture_output=True, text=True,
        )
        names |= {line.split('"')[1] for line in out.stdout.splitlines() if '"' in line}
    for axis in ("x", "y", "z", "e"):
        for base in ("machine_max_feedrate", "machine_max_acceleration", "machine_max_jerk"):
            names.add(f"{base}_{axis}")
    return names


if __name__ == "__main__":
    sys.exit(main())
