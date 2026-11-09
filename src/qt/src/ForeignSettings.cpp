// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "ForeignSettings.hpp"

#include <QHash>
#include <QStringList>

namespace next_layer {

namespace {

/// Names that mean the same thing under a different word. Both sides are proven by
/// their definitions at the other end of this: the translated name still has to
/// exist here and the translated value still has to be one this engine accepts.
const QHash<QString, QString>& renamed()
{
    static const QHash<QString, QString> table{
        {QStringLiteral("sparse_infill_density"), QStringLiteral("fill_density")},
        {QStringLiteral("sparse_infill_pattern"), QStringLiteral("fill_pattern")},
        {QStringLiteral("wall_loops"), QStringLiteral("perimeters")},
        {QStringLiteral("top_shell_layers"), QStringLiteral("top_solid_layers")},
        {QStringLiteral("bottom_shell_layers"), QStringLiteral("bottom_solid_layers")},
        {QStringLiteral("top_shell_thickness"), QStringLiteral("top_solid_min_thickness")},
        {QStringLiteral("bottom_shell_thickness"), QStringLiteral("bottom_solid_min_thickness")},
        {QStringLiteral("enable_support"), QStringLiteral("support_material")},
        {QStringLiteral("support_type"), QStringLiteral("support_material_style")},
        {QStringLiteral("tree_support_wall_count"), QStringLiteral("tree_support_wall_count")},
        {QStringLiteral("initial_layer_print_height"), QStringLiteral("first_layer_height")},
        {QStringLiteral("layer_height"), QStringLiteral("layer_height")},
        {QStringLiteral("outer_wall_line_width"), QStringLiteral("external_perimeter_extrusion_width")},
        {QStringLiteral("inner_wall_line_width"), QStringLiteral("perimeter_extrusion_width")},
        {QStringLiteral("internal_solid_infill_line_width"), QStringLiteral("solid_infill_extrusion_width")},
        {QStringLiteral("sparse_infill_line_width"), QStringLiteral("infill_extrusion_width")},
        {QStringLiteral("initial_layer_line_width"), QStringLiteral("first_layer_extrusion_width")},
        {QStringLiteral("travel_speed"), QStringLiteral("travel_speed")},
        {QStringLiteral("outer_wall_speed"), QStringLiteral("external_perimeter_speed")},
        {QStringLiteral("inner_wall_speed"), QStringLiteral("perimeter_speed")},
        {QStringLiteral("sparse_infill_speed"), QStringLiteral("infill_speed")},
        {QStringLiteral("internal_solid_infill_speed"), QStringLiteral("solid_infill_speed")},
        {QStringLiteral("initial_layer_speed"), QStringLiteral("first_layer_speed")},
    };
    return table;
}

/// Values for a name both slicers spell the same way but do not fill the same way.
const QHash<QString, QHash<QString, QString>>& revalued()
{
    static const QHash<QString, QHash<QString, QString>> table{
        // This engine's support material is an enum of three, that one's is a flag
        // plus a type. "On" is the automatic mode on both sides.
        {QStringLiteral("support_material"),
         {{QStringLiteral("1"), QStringLiteral("everywhere")},
          {QStringLiteral("true"), QStringLiteral("everywhere")},
          {QStringLiteral("0"), QStringLiteral("none")},
          {QStringLiteral("false"), QStringLiteral("none")}}},
        // normal supports behave like grid, tree supports are the organic ones here.
        // The "(manual)" variants describe painting, which is stored as facets
        // separately, so they take the same style as their automatic form.
        {QStringLiteral("support_material_style"),
         {{QStringLiteral("normal(auto)"), QStringLiteral("grid")},
          {QStringLiteral("normal(manual)"), QStringLiteral("snug")},
          {QStringLiteral("tree(auto)"), QStringLiteral("organic")},
          {QStringLiteral("tree(manual)"), QStringLiteral("organic")},
          {QStringLiteral("tree(hybrid)"), QStringLiteral("organic")},
          {QStringLiteral("hybrid(auto)"), QStringLiteral("grid")},
          {QStringLiteral("hybrid(manual)"), QStringLiteral("snug")}}},
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
    };
    return table;
}

/// Names this app knows it cannot carry, because the behaviour they describe does
/// not exist here. Listing them keeps them out of the "unknown setting" report a
/// person can do nothing about.
const QHash<QString, QString>& noCounterpart()
{
    static const QHash<QString, QString> table{
        {QStringLiteral("skeleton_infill_density"),
         QStringLiteral("that slicer's skeleton infill has no counterpart here")},
        {QStringLiteral("skin_infill_density"),
         QStringLiteral("that slicer's skin infill has no counterpart here")},
        {QStringLiteral("support_object_xy_distance"),
         QStringLiteral("this engine states support spacing as a share of the extrusion width")},
        {QStringLiteral("support_object_first_layer_gap"),
         QStringLiteral("this engine states support spacing as a share of the extrusion width")},
        {QStringLiteral("support_top_z_distance"),
         QStringLiteral("this engine states the gap as support_material_contact_distance")},
        {QStringLiteral("filament_map_mode"), QStringLiteral("no filament mapping here")},
        {QStringLiteral("filament_maps"), QStringLiteral("no filament mapping here")},
        {QStringLiteral("filament_volume_maps"), QStringLiteral("no filament mapping here")},
        {QStringLiteral("different_settings_to_system"), QStringLiteral("a preset bookkeeping key")},
        {QStringLiteral("auto_brim"), QStringLiteral("auto brim selection is not modelled here")},
    };
    return table;
}

} // namespace

ForeignSetting translateForeignSetting(const QString& key, const QString& value,
                                       QString& translatedName, QString& translatedValue)
{
    if (noCounterpart().contains(key))
        return ForeignSetting::NoCounterpart;

    translatedName = renamed().value(key, key);
    translatedValue = value;

    const auto values = revalued().constFind(translatedName);
    if (values != revalued().constEnd()) {
        const auto mapped = values->constFind(value);
        if (mapped == values->constEnd())
            return ForeignSetting::NoCounterpart;
        translatedValue = *mapped;
    }

    return ForeignSetting::Translated;
}

}
