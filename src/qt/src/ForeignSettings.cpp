// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "ForeignSettings.hpp"

#include <QHash>
#include <QStringList>

namespace next_layer {

namespace {

/// Names that mean the same thing under a different word. Both sides are proven:
/// the target still has to exist here, and the source is one of the option names
/// the other slicer defines. The two are checked against each other by
/// `--dump-foreign-settings` and the gate that reads it, so a typo on either side
/// fails rather than quietly dropping a setting.
///
/// The direction is that slicer's name first, because that is what a downloaded
/// project carries.
const QVector<ForeignSettingPair>& renamed()
{
    static const QVector<ForeignSettingPair> table{
        {QStringLiteral("wall_loops"), QStringLiteral("perimeters")},
        {QStringLiteral("top_shell_layers"), QStringLiteral("top_solid_layers")},
        {QStringLiteral("top_shell_thickness"), QStringLiteral("top_solid_min_thickness")},
        {QStringLiteral("top_surface_pattern"), QStringLiteral("top_fill_pattern")},
        {QStringLiteral("top_surface_line_width"), QStringLiteral("top_infill_extrusion_width")},
        {QStringLiteral("top_surface_speed"), QStringLiteral("top_solid_infill_speed")},
        {QStringLiteral("bottom_shell_layers"), QStringLiteral("bottom_solid_layers")},
        {QStringLiteral("bottom_shell_thickness"), QStringLiteral("bottom_solid_min_thickness")},
        {QStringLiteral("bottom_surface_pattern"), QStringLiteral("bottom_fill_pattern")},
        {QStringLiteral("sparse_infill_density"), QStringLiteral("fill_density")},
        {QStringLiteral("sparse_infill_pattern"), QStringLiteral("fill_pattern")},
        {QStringLiteral("infill_direction"), QStringLiteral("fill_angle")},
        {QStringLiteral("sparse_infill_line_width"), QStringLiteral("infill_extrusion_width")},
        {QStringLiteral("sparse_infill_speed"), QStringLiteral("infill_speed")},
        {QStringLiteral("sparse_infill_acceleration"), QStringLiteral("infill_acceleration")},
        {QStringLiteral("infill_wall_overlap"), QStringLiteral("infill_overlap")},
        {QStringLiteral("internal_solid_infill_line_width"), QStringLiteral("solid_infill_extrusion_width")},
        {QStringLiteral("internal_solid_infill_speed"), QStringLiteral("solid_infill_speed")},
        {QStringLiteral("internal_solid_infill_acceleration"), QStringLiteral("solid_infill_acceleration")},
        {QStringLiteral("inner_wall_line_width"), QStringLiteral("perimeter_extrusion_width")},
        {QStringLiteral("inner_wall_speed"), QStringLiteral("perimeter_speed")},
        {QStringLiteral("inner_wall_acceleration"), QStringLiteral("perimeter_acceleration")},
        {QStringLiteral("outer_wall_line_width"), QStringLiteral("external_perimeter_extrusion_width")},
        {QStringLiteral("outer_wall_speed"), QStringLiteral("external_perimeter_speed")},
        {QStringLiteral("outer_wall_acceleration"), QStringLiteral("external_perimeter_acceleration")},
        {QStringLiteral("initial_layer_print_height"), QStringLiteral("first_layer_height")},
        {QStringLiteral("initial_layer_line_width"), QStringLiteral("first_layer_extrusion_width")},
        {QStringLiteral("initial_layer_speed"), QStringLiteral("first_layer_speed")},
        {QStringLiteral("initial_layer_infill_speed"), QStringLiteral("first_layer_infill_speed")},
        {QStringLiteral("initial_layer_acceleration"), QStringLiteral("first_layer_acceleration")},
        {QStringLiteral("enable_support"), QStringLiteral("support_material")},
        {QStringLiteral("support_type"), QStringLiteral("support_material_style")},
        {QStringLiteral("support_angle"), QStringLiteral("support_material_angle")},
        {QStringLiteral("support_on_build_plate_only"), QStringLiteral("support_material_buildplate_only")},
        {QStringLiteral("support_top_z_distance"), QStringLiteral("support_material_contact_distance")},
        {QStringLiteral("enforce_support_layers"), QStringLiteral("support_material_enforce_layers")},
        {QStringLiteral("support_interface_top_layers"), QStringLiteral("support_material_interface_layers")},
        {QStringLiteral("support_interface_pattern"), QStringLiteral("support_material_interface_pattern")},
        {QStringLiteral("support_interface_spacing"), QStringLiteral("support_material_interface_spacing")},
        {QStringLiteral("support_interface_speed"), QStringLiteral("support_material_interface_speed")},
        {QStringLiteral("support_base_pattern"), QStringLiteral("support_material_pattern")},
        {QStringLiteral("support_base_pattern_spacing"), QStringLiteral("support_material_spacing")},
        {QStringLiteral("support_speed"), QStringLiteral("support_material_speed")},
        {QStringLiteral("support_threshold_angle"), QStringLiteral("support_material_threshold")},
        {QStringLiteral("bridge_flow"), QStringLiteral("bridge_flow_ratio")},
        {QStringLiteral("retraction_length"), QStringLiteral("retract_length")},
        {QStringLiteral("retraction_speed"), QStringLiteral("retract_speed")},
        {QStringLiteral("retraction_minimum_travel"), QStringLiteral("retract_before_travel")},
        {QStringLiteral("prime_tower_width"), QStringLiteral("wipe_tower_width")},
        {QStringLiteral("prime_tower_brim_width"), QStringLiteral("wipe_tower_brim_width")},
        {QStringLiteral("fan_max_speed"), QStringLiteral("max_fan_speed")},
        {QStringLiteral("fan_min_speed"), QStringLiteral("min_fan_speed")},
        {QStringLiteral("fan_cooling_layer_time"), QStringLiteral("fan_below_layer_time")},
        {QStringLiteral("close_fan_the_first_x_layers"), QStringLiteral("disable_fan_first_layers")},
        {QStringLiteral("additional_fan_full_speed_layer"), QStringLiteral("full_fan_speed_layer")},
        {QStringLiteral("enable_arc_fitting"), QStringLiteral("arc_fitting")},
        {QStringLiteral("before_layer_change_gcode"), QStringLiteral("before_layer_gcode")},
        {QStringLiteral("layer_change_gcode"), QStringLiteral("layer_gcode")},
        {QStringLiteral("machine_start_gcode"), QStringLiteral("start_gcode")},
        {QStringLiteral("machine_end_gcode"), QStringLiteral("end_gcode")},
        {QStringLiteral("machine_pause_gcode"), QStringLiteral("pause_print_gcode")},
        {QStringLiteral("filament_start_gcode"), QStringLiteral("start_filament_gcode")},
        {QStringLiteral("filament_end_gcode"), QStringLiteral("end_filament_gcode")},
        {QStringLiteral("infill_combination"), QStringLiteral("automatic_infill_combination")},
        {QStringLiteral("infill_combination_max_layer_height"),
         QStringLiteral("automatic_infill_combination_max_layer_height")},
        {QStringLiteral("internal_bridge_angle"), QStringLiteral("bridge_angle")},
        {QStringLiteral("internal_bridge_speed"), QStringLiteral("bridge_speed")},
        {QStringLiteral("internal_bridge_flow"), QStringLiteral("bridge_flow_ratio")},
        {QStringLiteral("internal_bridge_fan_speed"), QStringLiteral("bridge_fan_speed")},
        {QStringLiteral("overhang_1_4_speed"), QStringLiteral("overhang_speed_1")},
        {QStringLiteral("overhang_2_4_speed"), QStringLiteral("overhang_speed_2")},
        {QStringLiteral("overhang_3_4_speed"), QStringLiteral("overhang_speed_3")},
        {QStringLiteral("overhang_4_4_speed"), QStringLiteral("overhang_speed_3")},
        {QStringLiteral("only_one_wall_first_layer"), QStringLiteral("only_one_perimeter_first_layer")},
        {QStringLiteral("thick_internal_bridges"), QStringLiteral("thick_bridges")},
        {QStringLiteral("gap_infill_speed"), QStringLiteral("gap_fill_speed")},
        {QStringLiteral("seam_gap"), QStringLiteral("seam_gap_distance")},
        {QStringLiteral("seam_slope_entire_loop"), QStringLiteral("scarf_seam_entire_loop")},
        {QStringLiteral("seam_slope_start_height"), QStringLiteral("scarf_seam_start_height")},
        {QStringLiteral("seam_slope_min_length"), QStringLiteral("scarf_seam_length")},
        {QStringLiteral("spiral_mode"), QStringLiteral("spiral_vase")},
        {QStringLiteral("support_style"), QStringLiteral("support_material_style")},
        {QStringLiteral("support_bottom_z_distance"),
         QStringLiteral("support_material_bottom_contact_distance")},
        {QStringLiteral("support_interface_bottom_layers"),
         QStringLiteral("support_material_bottom_interface_layers")},
        {QStringLiteral("support_object_xy_distance"),
         QStringLiteral("support_material_xy_spacing")},
        {QStringLiteral("support_line_width"), QStringLiteral("support_material_extrusion_width")},
        {QStringLiteral("support_expansion"), QStringLiteral("support_material_first_layer_expansion")},
        {QStringLiteral("tree_support_branch_angle"), QStringLiteral("support_tree_angle")},
        {QStringLiteral("tree_support_branch_angle_organic"), QStringLiteral("support_tree_angle")},
        {QStringLiteral("tree_support_angle_slow"), QStringLiteral("support_tree_angle_slow")},
        {QStringLiteral("tree_support_branch_diameter"), QStringLiteral("support_tree_branch_diameter")},
        {QStringLiteral("tree_support_branch_diameter_organic"),
         QStringLiteral("support_tree_branch_diameter")},
        {QStringLiteral("tree_support_branch_diameter_angle"),
         QStringLiteral("support_tree_branch_diameter_angle")},
        {QStringLiteral("tree_support_branch_distance"), QStringLiteral("support_tree_branch_distance")},
        {QStringLiteral("tree_support_branch_distance_organic"),
         QStringLiteral("support_tree_branch_distance")},
        {QStringLiteral("tree_support_tip_diameter"), QStringLiteral("support_tree_tip_diameter")},
        {QStringLiteral("tree_support_top_rate"), QStringLiteral("support_tree_top_rate")},
        {QStringLiteral("retract_when_changing_layer"), QStringLiteral("retract_layer_change")},
        {QStringLiteral("nozzle_temperature"), QStringLiteral("temperature")},
        {QStringLiteral("nozzle_temperature_initial_layer"), QStringLiteral("first_layer_temperature")},
        {QStringLiteral("enable_prime_tower"), QStringLiteral("wipe_tower")},
        {QStringLiteral("skirt_loops"), QStringLiteral("skirts")},
        {QStringLiteral("brim_object_gap"), QStringLiteral("brim_separation")},
        {QStringLiteral("deretraction_speed"), QStringLiteral("deretract_speed")},
        {QStringLiteral("z_hop"), QStringLiteral("retract_lift")},
        {QStringLiteral("filament_flow_ratio"), QStringLiteral("extrusion_multiplier")},
        {QStringLiteral("material_density"), QStringLiteral("filament_density")},
        {QStringLiteral("slow_down_min_speed"), QStringLiteral("min_print_speed")},
        {QStringLiteral("top_surface_acceleration"), QStringLiteral("top_solid_infill_acceleration")},
    };
    return table;
}

/// Values for a name both slicers spell the same way but do not fill the same way.
/// Keyed by the name this engine uses, because that is the definition that has to
/// accept the result.
const QHash<QString, QHash<QString, QString>>& revalued()
{
    static const QHash<QString, QHash<QString, QString>> table{
        // This engine's support material is an enum of three, that one's is a flag
        // plus a style. "On" is the automatic mode on both sides.
        {QStringLiteral("support_material"),
         {{QStringLiteral("1"), QStringLiteral("everywhere")},
          {QStringLiteral("true"), QStringLiteral("everywhere")},
          {QStringLiteral("enable"), QStringLiteral("everywhere")},
          {QStringLiteral("0"), QStringLiteral("none")},
          {QStringLiteral("false"), QStringLiteral("none")}}},
        // normal supports behave like grid, tree supports are the organic ones here.
        // The "(manual)" variants describe painting, which is stored as facets
        // separately, so they take the same style as their automatic form. The
        // second block is the newer spelling of the same setting.
        {QStringLiteral("support_material_style"),
         {{QStringLiteral("normal(auto)"), QStringLiteral("grid")},
          {QStringLiteral("normal(manual)"), QStringLiteral("snug")},
          {QStringLiteral("tree(auto)"), QStringLiteral("organic")},
          {QStringLiteral("tree(manual)"), QStringLiteral("organic")},
          {QStringLiteral("tree(hybrid)"), QStringLiteral("organic")},
          {QStringLiteral("hybrid(auto)"), QStringLiteral("grid")},
          {QStringLiteral("hybrid(manual)"), QStringLiteral("snug")},
          {QStringLiteral("grid"), QStringLiteral("grid")},
          {QStringLiteral("snug"), QStringLiteral("snug")},
          {QStringLiteral("organic"), QStringLiteral("organic")},
          {QStringLiteral("tree_slim"), QStringLiteral("organic")},
          {QStringLiteral("tree_strong"), QStringLiteral("organic")},
          {QStringLiteral("tree_hybrid"), QStringLiteral("organic")}}},
        // That slicer's auto brim is its own decision, and its ears are a different
        // shape altogether; the four shapes this engine has are named the same.
        {QStringLiteral("brim_type"),
         {{QStringLiteral("no_brim"), QStringLiteral("no_brim")},
          {QStringLiteral("outer_only"), QStringLiteral("outer_only")},
          {QStringLiteral("inner_only"), QStringLiteral("inner_only")},
          {QStringLiteral("outer_and_inner"), QStringLiteral("outer_and_inner")}}},
        {QStringLiteral("seam_position"),
         {{QStringLiteral("nearest"), QStringLiteral("nearest")},
          {QStringLiteral("aligned"), QStringLiteral("aligned")},
          {QStringLiteral("back"), QStringLiteral("rear")},
          {QStringLiteral("rear"), QStringLiteral("rear")},
          {QStringLiteral("random"), QStringLiteral("random")}}},
        // The surface patterns differ by one letter in one name: that slicer spells
        // the whole-toolpath variant in the singular.
        {QStringLiteral("top_fill_pattern"),
         {{QStringLiteral("monotonicline"), QStringLiteral("monotoniclines")}}},
        {QStringLiteral("bottom_fill_pattern"),
         {{QStringLiteral("monotonicline"), QStringLiteral("monotoniclines")}}},
        {QStringLiteral("support_material_pattern"),
         {{QStringLiteral("rectilinear"), QStringLiteral("rectilinear")},
          {QStringLiteral("honeycomb"), QStringLiteral("honeycomb")}}},
        {QStringLiteral("support_material_interface_pattern"),
         {{QStringLiteral("auto"), QStringLiteral("auto")},
          {QStringLiteral("rectilinear"), QStringLiteral("rectilinear")},
          {QStringLiteral("concentric"), QStringLiteral("concentric")}}},
        {QStringLiteral("ironing_type"),
         {{QStringLiteral("top"), QStringLiteral("top")},
          {QStringLiteral("topmost"), QStringLiteral("topmost")},
          {QStringLiteral("solid"), QStringLiteral("solid")}}},
    };
    return table;
}

/// Names this app knows it cannot carry, because the behaviour they describe does
/// not exist here. Kept as documentation of the gap and to keep such a name out of
/// the applied list; nothing is reported to the person opening the file, because
/// there is nothing they could do about it.
const QHash<QString, QString>& noCounterpart()
{
    static const QHash<QString, QString> table{
        {QStringLiteral("skeleton_infill_density"),
         QStringLiteral("that slicer's skeleton infill has no counterpart here")},
        {QStringLiteral("skin_infill_density"),
         QStringLiteral("that slicer's skin infill has no counterpart here")},
        {QStringLiteral("support_object_first_layer_gap"),
         QStringLiteral("this engine states the gap to the bed with support_material_contact_distance")},
        {QStringLiteral("filament_map_mode"), QStringLiteral("no filament mapping here")},
        {QStringLiteral("filament_maps"), QStringLiteral("no filament mapping here")},
        {QStringLiteral("filament_volume_maps"), QStringLiteral("no filament mapping here")},
        {QStringLiteral("different_settings_to_system"), QStringLiteral("a preset bookkeeping key")},
        {QStringLiteral("auto_brim"), QStringLiteral("auto brim selection is not modelled here")},
        {QStringLiteral("brim_ears"), QStringLiteral("brim ears are a different shape")},
        {QStringLiteral("outer_wall_infill_direction"),
         QStringLiteral("that slicer's wall direction ordering is not modelled here")},
        {QStringLiteral("adaptive_infill_mode"),
         QStringLiteral("that slicer's combined adaptive infill is not modelled here")},
        {QStringLiteral("height_speed_limit"),
         QStringLiteral("that slicer's per-height speed table is not modelled here")},
        {QStringLiteral("inner_wall_fan_speed"),
         QStringLiteral("this engine's fan is overhang-driven, not per perimeter")},
        {QStringLiteral("outer_wall_fan_speed"),
         QStringLiteral("this engine's fan is overhang-driven, not per perimeter")},
        {QStringLiteral("internal_solid_infill_pattern"),
         QStringLiteral("this engine names a pattern per surface, not one for all solid infill")},
    };
    return table;
}

} // namespace

bool translateForeignSetting(const QString& key, const QString& value,
                             QString& translatedName, QString& translatedValue)
{
    if (noCounterpart().contains(key))
        return false;

    QString target = key;
    for (const ForeignSettingPair& pair : renamed()) {
        if (pair.source == key) {
            target = pair.target;
            break;
        }
    }

    translatedName = target;
    translatedValue = value;

    const auto values = revalued().constFind(target);
    if (values != revalued().constEnd()) {
        const auto mapped = values->constFind(value);
        if (mapped == values->constEnd())
            return false;
        translatedValue = *mapped;
    }

    return true;
}

QVector<ForeignSettingPair> foreignSettingTable()
{
    return renamed();
}

}
