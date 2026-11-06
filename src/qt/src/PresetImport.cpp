// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PresetImport.hpp"

#include "PresetFiles.hpp"
#include "PresetYaml.hpp"
#include "SettingsModel.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

#include <algorithm>

namespace {

using next_layer::presetimport::Report;

/// Keys that describe the other slicer's own bookkeeping rather than a setting.
const QSet<QString>& metaKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("inherits"),
        QStringLiteral("from"), QStringLiteral("setting_id"), QStringLiteral("instantiation"),
        QStringLiteral("compatible_printers"), QStringLiteral("compatible_printers_condition"),
        QStringLiteral("compatible_prints"), QStringLiteral("compatible_prints_condition"),
        QStringLiteral("compatible_filaments"), QStringLiteral("compatible_filaments_condition"),
        QStringLiteral("is_custom_defined"), QStringLiteral("version"),
        QStringLiteral("renamed_from"), QStringLiteral("upward_compatible"),
        QStringLiteral("inherits_prints"),
    };
    return keys;
}

enum class Transform {
    Rename,
    Percent,
    PercentObject,
    Integer,
    Real,
    RealList,
    Boolean,
    BedShape,
    IntAtLeastOne,
    Enum,
    EnumBoolean,
};

struct Mapping
{
    const char* source;
    const char* target;
    Transform transform;
    const char* table;
};

/// The other slicer's vocabulary to this engine's. Every entry is a setting the
/// engine has; nothing here is guessed, and anything absent is reported as
/// dropped rather than written with a name the engine will reject.
const Mapping* mappingTable(size_t& count)
{
    static const Mapping table[] = {
        {"bottom_shell_layers", "bottom_solid_layers", Transform::Integer, nullptr},
        {"top_shell_layers", "top_solid_layers", Transform::Integer, nullptr},
        {"bottom_shell_thickness", "bottom_solid_min_thickness", Transform::Real, nullptr},
        {"top_shell_thickness", "top_solid_min_thickness", Transform::Real, nullptr},
        {"wall_loops", "perimeters", Transform::Integer, nullptr},
        {"spiral_mode", "spiral_vase", Transform::Boolean, nullptr},
        {"ensure_vertical_wall_thickness", "ensure_vertical_shell_thickness", Transform::Boolean, nullptr},
        {"detect_thin_wall", "thin_walls", Transform::Boolean, nullptr},
        {"extra_perimeters_on_overhangs", "extra_perimeters_on_overhangs", Transform::Rename, nullptr},
        {"sparse_infill_density", "fill_density", Transform::PercentObject, nullptr},
        {"sparse_infill_pattern", "fill_pattern", Transform::Enum, "fill_pattern"},
        {"sparse_infill_line_width", "infill_extrusion_width", Transform::Real, nullptr},
        {"infill_wall_overlap", "infill_overlap", Transform::PercentObject, nullptr},
        {"internal_solid_infill_line_width", "solid_infill_extrusion_width", Transform::Real, nullptr},
        {"top_surface_pattern", "top_fill_pattern", Transform::Enum, "fill_pattern"},
        {"bottom_surface_pattern", "bottom_fill_pattern", Transform::Enum, "fill_pattern"},
        {"infill_direction", "fill_angle", Transform::Real, nullptr},
        {"infill_combination", "infill_every_layers", Transform::IntAtLeastOne, nullptr},
        {"minimum_sparse_infill_area", "solid_infill_below_area", Transform::Real, nullptr},
        {"sparse_infill_anchor_max", "infill_anchor_max", Transform::Real, nullptr},
        {"sparse_infill_anchor", "infill_anchor", Transform::Real, nullptr},
        {"line_width", "extrusion_width", Transform::Real, nullptr},
        {"outer_wall_line_width", "external_perimeter_extrusion_width", Transform::Real, nullptr},
        {"inner_wall_line_width", "perimeter_extrusion_width", Transform::Real, nullptr},
        {"top_surface_line_width", "top_infill_extrusion_width", Transform::Real, nullptr},
        {"initial_layer_line_width", "first_layer_extrusion_width", Transform::Real, nullptr},
        {"support_line_width", "support_material_extrusion_width", Transform::Real, nullptr},
        {"outer_wall_speed", "external_perimeter_speed", Transform::Real, nullptr},
        {"inner_wall_speed", "perimeter_speed", Transform::Real, nullptr},
        {"sparse_infill_speed", "infill_speed", Transform::Real, nullptr},
        {"internal_solid_infill_speed", "solid_infill_speed", Transform::Real, nullptr},
        {"top_surface_speed", "top_solid_infill_speed", Transform::Real, nullptr},
        {"gap_infill_speed", "gap_fill_speed", Transform::Real, nullptr},
        {"initial_layer_speed", "first_layer_speed", Transform::Real, nullptr},
        {"initial_layer_infill_speed", "first_layer_infill_speed", Transform::Real, nullptr},
        {"support_speed", "support_material_speed", Transform::Real, nullptr},
        {"support_interface_speed", "support_material_interface_speed", Transform::Real, nullptr},
        {"overhang_1_4_speed", "overhang_speed_0", Transform::Real, nullptr},
        {"overhang_2_4_speed", "overhang_speed_1", Transform::Real, nullptr},
        {"overhang_3_4_speed", "overhang_speed_2", Transform::Real, nullptr},
        {"overhang_4_4_speed", "overhang_speed_3", Transform::Real, nullptr},
        {"internal_bridge_speed", "bridge_speed", Transform::Real, nullptr},
        {"outer_wall_acceleration", "external_perimeter_acceleration", Transform::Real, nullptr},
        {"inner_wall_acceleration", "perimeter_acceleration", Transform::Real, nullptr},
        {"top_surface_acceleration", "top_solid_infill_acceleration", Transform::Real, nullptr},
        {"sparse_infill_acceleration", "infill_acceleration", Transform::Real, nullptr},
        {"internal_solid_infill_acceleration", "solid_infill_acceleration", Transform::Real, nullptr},
        {"initial_layer_acceleration", "first_layer_acceleration", Transform::Real, nullptr},
        {"bridge_infill_acceleration", "bridge_acceleration", Transform::Real, nullptr},
        {"enable_support", "support_material", Transform::Enum, "support_material_on"},
        {"support_type", "support_material_auto", Transform::EnumBoolean, "support_type_auto"},
        {"support_style", "support_material_style", Transform::Enum, "support_material_style"},
        {"support_on_build_plate_only", "support_material_buildplate_only", Transform::Boolean, nullptr},
        {"support_top_z_distance", "support_material_contact_distance", Transform::Real, nullptr},
        {"support_bottom_z_distance", "support_material_bottom_contact_distance", Transform::Real, nullptr},
        {"support_interface_top_layers", "support_material_interface_layers", Transform::Integer, nullptr},
        {"support_interface_bottom_layers", "support_material_bottom_interface_layers", Transform::Integer, nullptr},
        {"support_interface_spacing", "support_material_interface_spacing", Transform::Real, nullptr},
        {"support_base_pattern", "support_material_pattern", Transform::Enum, "support_material_pattern"},
        {"support_base_pattern_spacing", "support_material_spacing", Transform::Real, nullptr},
        {"support_object_xy_distance", "support_material_xy_spacing", Transform::Real, nullptr},
        {"support_threshold_angle", "support_material_threshold", Transform::Integer, nullptr},
        {"tree_support_branch_angle", "support_tree_angle", Transform::Real, nullptr},
        {"support_interface_loop_pattern", "support_material_interface_contact_loops", Transform::Boolean, nullptr},
        {"support_interface_pattern", "support_material_interface_pattern", Transform::Enum, "support_material_pattern"},
        {"independent_support_layer_height", "support_material_synchronize_layers", Transform::Boolean, nullptr},
        {"brim_object_gap", "brim_separation", Transform::Real, nullptr},
        {"skirt_loops", "skirts", Transform::Integer, nullptr},
        {"seam_gap", "seam_gap_distance", Transform::Real, nullptr},
        {"staggered_inner_seams", "staggered_inner_seams", Transform::Rename, nullptr},
        {"retraction_length", "retract_length", Transform::Real, nullptr},
        {"retraction_speed", "retract_speed", Transform::Real, nullptr},
        {"z_hop", "retract_lift", Transform::Real, nullptr},
        {"retraction_minimum_travel", "retract_before_travel", Transform::Real, nullptr},
        {"retract_when_changing_layer", "retract_layer_change", Transform::Boolean, nullptr},
        {"retract_length_toolchange", "retract_length_toolchange", Transform::Real, nullptr},
        {"retract_restart_extra_toolchange", "retract_restart_extra_toolchange", Transform::Real, nullptr},
        {"wipe_on_loops", "wipe", Transform::Boolean, nullptr},
        {"avoid_crossing_perimeters", "avoid_crossing_perimeters", Transform::Boolean, nullptr},
        {"avoid_crossing_perimeters_max_detour", "avoid_crossing_perimeters_max_detour", Transform::Real, nullptr},
        {"filament_flow_ratio", "extrusion_multiplier", Transform::Real, nullptr},
        {"nozzle_temperature", "temperature", Transform::Integer, nullptr},
        {"nozzle_temperature_initial_layer", "first_layer_temperature", Transform::Integer, nullptr},
        {"bed_temperature_initial_layer", "first_layer_bed_temperature", Transform::Integer, nullptr},
        {"hot_plate_temp", "bed_temperature", Transform::Integer, nullptr},
        {"hot_plate_temp_initial_layer", "first_layer_bed_temperature", Transform::Integer, nullptr},
        {"textured_plate_temp", "bed_temperature", Transform::Integer, nullptr},
        {"textured_plate_temp_initial_layer", "first_layer_bed_temperature", Transform::Integer, nullptr},
        {"fan_cooling_layer_time", "slowdown_below_layer_time", Transform::Real, nullptr},
        {"enable_overhang_speed", "enable_dynamic_overhang_speeds", Transform::Boolean, nullptr},
        {"fan_min_speed", "min_fan_speed", Transform::Integer, nullptr},
        {"fan_max_speed", "max_fan_speed", Transform::Integer, nullptr},
        {"slow_down_min_layer_time", "slowdown_below_layer_time", Transform::Real, nullptr},
        {"filament_shrink", "filament_shrinkage_compensation_xy", Transform::PercentObject, nullptr},
        {"filament_shrinkage_compensation_z", "filament_shrinkage_compensation_z", Transform::Real, nullptr},
        {"filament_max_volumetric_speed", "filament_max_volumetric_speed", Transform::Real, nullptr},
        {"printable_area", "bed_shape", Transform::BedShape, nullptr},
        {"printable_height", "max_print_height", Transform::Real, nullptr},
        {"machine_max_speed_x", "machine_max_feedrate_x", Transform::RealList, nullptr},
        {"machine_max_speed_y", "machine_max_feedrate_y", Transform::RealList, nullptr},
        {"machine_max_speed_z", "machine_max_feedrate_z", Transform::RealList, nullptr},
        {"machine_max_speed_e", "machine_max_feedrate_e", Transform::RealList, nullptr},
        {"machine_max_acceleration_x", "machine_max_acceleration_x", Transform::RealList, nullptr},
        {"machine_max_acceleration_y", "machine_max_acceleration_y", Transform::RealList, nullptr},
        {"machine_max_acceleration_z", "machine_max_acceleration_z", Transform::RealList, nullptr},
        {"machine_max_acceleration_e", "machine_max_acceleration_e", Transform::RealList, nullptr},
        {"machine_max_jerk_x", "machine_max_jerk_x", Transform::RealList, nullptr},
        {"machine_max_jerk_y", "machine_max_jerk_y", Transform::RealList, nullptr},
        {"machine_max_jerk_z", "machine_max_jerk_z", Transform::RealList, nullptr},
        {"machine_max_jerk_e", "machine_max_jerk_e", Transform::RealList, nullptr},
        {"default_filament_profile", "default_filament_profile", Transform::Rename, nullptr},
        {"default_print_profile", "default_print_profile", Transform::Rename, nullptr},
        {"gcode_flavor", "gcode_flavor", Transform::Rename, nullptr},
        {"before_layer_change_gcode", "before_layer_gcode", Transform::Rename, nullptr},
        {"layer_change_gcode", "layer_gcode", Transform::Rename, nullptr},
        {"machine_start_gcode", "start_gcode", Transform::Rename, nullptr},
        {"machine_end_gcode", "end_gcode", Transform::Rename, nullptr},
        {"machine_pause_gcode", "pause_print_gcode", Transform::Rename, nullptr},
        {"machine_load_filament_time", "filament_loading_speed", Transform::Real, nullptr},
        {"machine_unload_filament_time", "filament_unloading_speed", Transform::Real, nullptr},
        {"retract_lift_above", "retract_lift_above", Transform::Real, nullptr},
        {"retract_lift_below", "retract_lift_below", Transform::Real, nullptr},
        {"extruder_clearance_height_to_rod", "extruder_clearance_height", Transform::Real, nullptr},
        {"extruder_clearance_height_to_lid", "extruder_clearance_height", Transform::Real, nullptr},
        {"extruder_clearance_radius", "extruder_clearance_radius", Transform::Real, nullptr},
        {"extruder_colour", "extruder_colour", Transform::Rename, nullptr},
        {"extruder_offset", "extruder_offset", Transform::Rename, nullptr},
        {"thumbnails", "thumbnails", Transform::Rename, nullptr},
        {"filament_colour", "filament_colour", Transform::Rename, nullptr},
        {"filament_notes", "filament_notes", Transform::Rename, nullptr},
        {"notes", "notes", Transform::Rename, nullptr},
        {"print_sequence", "complete_objects", Transform::EnumBoolean, "print_sequence"},
        {"resolution", "resolution", Transform::Real, nullptr},
        {"arc_fitting", "arc_fitting", Transform::Boolean, nullptr},
        {"arc_fitting_tolerance", "gcode_resolution", Transform::Real, nullptr},
        {"first_layer_height", "first_layer_height", Transform::Real, nullptr},
        {"xy_hole_compensation", "xy_size_compensation", Transform::Real, nullptr},
        {"xy_contour_compensation", "xy_size_compensation", Transform::Real, nullptr},
        {"elefant_foot_compensation", "elefant_foot_compensation", Transform::Real, nullptr},
        {"fuzzy_skin", "fuzzy_skin", Transform::Enum, "fuzzy_skin"},
        {"fuzzy_skin_point_distance", "fuzzy_skin_point_dist", Transform::Real, nullptr},
        {"fuzzy_skin_thickness", "fuzzy_skin_thickness", Transform::Real, nullptr},
        {"ironing_flow", "ironing_flowrate", Transform::PercentObject, nullptr},
        {"ironing_spacing", "ironing_spacing", Transform::Real, nullptr},
        {"ironing_speed", "ironing_speed", Transform::Real, nullptr},
        {"ironing_type", "ironing_type", Transform::Enum, "ironing_type"},
        {"bridge_flow", "bridge_flow_ratio", Transform::PercentObject, nullptr},
        {"bridge_speed", "bridge_speed", Transform::Real, nullptr},
        {"bridge_no_support", "dont_support_bridges", Transform::Boolean, nullptr},
        {"draft_shield", "draft_shield", Transform::Enum, "draft_shield"},
        {"ooze_prevention", "ooze_prevention", Transform::Boolean, nullptr},
        {"wipe_tower", "wipe_tower", Transform::Boolean, nullptr},
        {"single_extruder_multi_material", "single_extruder_multi_material", Transform::Boolean, nullptr},
        {"single_extruder_multi_material_priming", "single_extruder_multi_material_priming", Transform::Boolean, nullptr},
        {"flush_into_objects", "wipe_into_objects", Transform::Boolean, nullptr},
        {"flush_into_infill", "wipe_into_infill", Transform::Boolean, nullptr},
        {"reduce_infill_retraction", "wipe_into_infill", Transform::Boolean, nullptr},
        {"wall_infill_order", "infill_first", Transform::Boolean, nullptr},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

const Mapping* findMapping(const QString& source)
{
    size_t count = 0;
    const Mapping* table = mappingTable(count);
    for (size_t index = 0; index < count; ++index) {
        if (source == QLatin1String(table[index].source))
            return &table[index];
    }
    return nullptr;
}

const QHash<QString, QString>& enumTable(const QString& name)
{
    static const QHash<QString, QHash<QString, QString>> tables = {
        {QStringLiteral("fill_pattern"),
         {{"crosshatch", "grid"}, {"grid", "grid"}, {"honeycomb", "honeycomb"},
          {"gyroid", "gyroid"}, {"cubic", "cubic"}, {"triangles", "triangles"},
          {"stars", "stars"}, {"tri-hexagon", "trihexagon"}, {"concentric", "concentric"},
          {"line", "rectilinear"}, {"rectilinear", "rectilinear"}, {"monotonic", "monotonic"},
          {"monotonicline", "monotonic"}, {"hilbertcurve", "hilbertcurve"}}},
        {QStringLiteral("support_material_on"),
         {{"0", "none"}, {"1", "everywhere"}, {"false", "none"}, {"true", "everywhere"},
          {"none", "none"}}},
        {QStringLiteral("support_type_auto"),
         {{"normal(auto)", "1"}, {"tree(auto)", "1"}, {"normal", "0"}, {"tree", "0"},
          {"normal(manual)", "0"}, {"tree(manual)", "0"}, {"none", "0"}, {"0", "0"}}},
        {QStringLiteral("print_sequence"),
         {{"by layer", "0"}, {"by object", "1"}, {"0", "0"}, {"1", "1"}}},
        {QStringLiteral("support_material_pattern"),
         {{"rectilinear", "rectilinear"}, {"rectilinear_grid", "rectilinear_grid"},
          {"honeycomb", "honeycomb"}, {"default", "rectilinear"}}},
        {QStringLiteral("support_material_style"),
         {{"default", "grid"}, {"snug", "snug"}, {"grid", "grid"}, {"organic", "organic"},
          {"tree_slim", "organic"}, {"tree_strong", "organic"}, {"tree_hybrid", "organic"}}},
        {QStringLiteral("ironing_type"),
         {{"no ironing", ""}, {"top", "top"}, {"topmost", "topmost"},
          {"all solid", "solid"}, {"all", "solid"}}},
        {QStringLiteral("fuzzy_skin"),
         {{"none", "none"}, {"outside", "external"}, {"all", "all"}, {"external", "external"}}},
        {QStringLiteral("draft_shield"),
         {{"disabled", "disabled"}, {"limited", "limited"}, {"enabled", "enabled"}}},
    };
    static const QHash<QString, QString> empty;
    const auto found = tables.constFind(name);
    return found == tables.constEnd() ? empty : *found;
}

/// Keys whose value shape this engine has no equivalent for: it stores two
/// percentages per extruder for the shrinkage compensation, and feeding it the
/// other slicer's single number aborts the load. Dropped and reported rather
/// than guessed at.
const QSet<QString>& unmappedKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("filament_shrinkage_compensation_xy"),
    };
    return keys;
}

/// One preset file's YAML scalar for a translated value, in the shapes the
/// engine's own bundles carry: points as `'0x0'` tokens, per-tool vectors as flow
/// lists, percentages with their sign. The shapes matter — a bed written as a
/// list of number pairs is read as no bed at all.
QString yamlOf(const QVariant& value)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        if (map.value(QStringLiteral("is_percent")).toBool())
            return QString::number(map.value(QStringLiteral("value")).toDouble(), 'g', 15)
                 + QLatin1Char('%');
    }
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList items = value.toList();
        QStringList parts;
        for (const QVariant& item : items) {
            if (item.typeId() == QMetaType::QVariantList) {
                const QVariantList pair = item.toList();
                if (pair.size() == 2)
                    parts.append(next_layer::yaml::quoted(
                        QString::number(pair.at(0).toDouble(), 'g', 15) + QLatin1Char('x')
                        + QString::number(pair.at(1).toDouble(), 'g', 15)));
                continue;
            }
            parts.append(yamlOf(item));
        }
        return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    if (value.typeId() == QMetaType::QString || value.typeId() == QMetaType::QStringList)
        return next_layer::yaml::quoted(value.toString());
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong)
        return QString::number(value.toLongLong());
    if (value.canConvert<double>())
        return QString::number(value.toDouble(), 'g', 15);
    return next_layer::yaml::quoted(value.toString());
}

QString valuesBlock(const QMap<QString, QVariant>& values)
{
    QString text;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        text += QStringLiteral("  ") + it.key() + QStringLiteral(": ") + yamlOf(it.value())
              + QLatin1Char('\n');
    return text;
}

/// The leading number of a value that may be a per-extruder list or a
/// comma-joined list, which is how that slicer writes anything tool-specific.
QString headOf(const QJsonValue& value)
{
    QJsonValue scalar = value;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (!array.isEmpty())
            scalar = array.at(0);
    }
    QString text = scalar.isString() ? scalar.toString() : QString::number(scalar.toDouble());
    if (scalar.isBool())
        text = scalar.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    text = text.split(QLatin1Char(',')).first().trimmed();
    if (text.endsWith(QLatin1Char('%')))
        text.chop(1);
    return text;
}

double headNumber(const QJsonValue& value, bool* valid)
{
    const QString text = headOf(value);
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (valid != nullptr)
        *valid = ok;
    return ok ? number : 0.0;
}

bool isTruthy(const QString& text)
{
    return text == QLatin1String("1") || text == QLatin1String("true")
        || text == QLatin1String("True") || text == QLatin1String("on");
}

QVariant percentObject(double number)
{
    QVariantMap object;
    object.insert(QStringLiteral("value"), number);
    object.insert(QStringLiteral("is_percent"), true);
    return object;
}

QVariant transformValue(const QJsonValue& value, const Mapping& mapping)
{
    switch (mapping.transform) {
    case Transform::Rename:
        return value.toVariant();
    case Transform::Percent:
        return headNumber(value, nullptr);
    case Transform::PercentObject:
        return percentObject(headNumber(value, nullptr));
    case Transform::Integer:
        return int(std::lround(headNumber(value, nullptr)));
    case Transform::Real:
        return headNumber(value, nullptr);
    case Transform::RealList: {
        QVariantList numbers;
        const QJsonArray items = value.isArray() ? value.toArray() : QJsonArray{value};
        for (const QJsonValue& item : items) {
            const QString head = headOf(item).split(QLatin1Char(',')).first().trimmed();
            if (!head.isEmpty())
                numbers.append(head.toDouble());
        }
        if (numbers.isEmpty())
            numbers.append(0.0);
        return numbers;
    }
    case Transform::Boolean: {
        const QJsonValue scalar = value.isArray() && !value.toArray().isEmpty()
                                ? value.toArray().at(0) : value;
        if (scalar.isBool())
            return scalar.toBool();
        return isTruthy(headOf(value));
    }
    case Transform::BedShape: {
        QVariantList points;
        QJsonArray tokens = value.isArray() ? value.toArray() : QJsonArray{value};
        if (tokens.size() == 1 && tokens.at(0).isString()
            && tokens.at(0).toString().contains(QLatin1Char(','))) {
            const QString joined = tokens.at(0).toString();
            tokens = QJsonArray();
            for (const QString& token : joined.split(QLatin1Char(',')))
                tokens.append(token);
        }
        for (const QJsonValue& token : tokens) {
            const QString text = token.isString() ? token.toString()
                                                  : QString::number(token.toDouble());
            const QStringList parts = text.split(QLatin1Char('x'));
            if (parts.size() != 2)
                continue;
            // A braced QVariantList appended to a QVariantList flattens into its
            // elements, which turns a bed into eight numbers and no polygon; the
            // pair has to be wrapped as a QVariant first.
            QVariantList pair;
            pair << parts.at(0).trimmed().toDouble() << parts.at(1).trimmed().toDouble();
            points.append(QVariant(pair));
        }
        return points;
    }
    case Transform::IntAtLeastOne: {
        bool valid = false;
        const double number = headNumber(value, &valid);
        return valid ? std::max(1, int(std::lround(number))) : 1;
    }
    case Transform::Enum: {
        const QString text = headOf(value);
        const QHash<QString, QString>& table = enumTable(QString::fromLatin1(mapping.table));
        return table.value(text, text);
    }
    case Transform::EnumBoolean: {
        const QString text = headOf(value);
        const QHash<QString, QString>& table = enumTable(QString::fromLatin1(mapping.table));
        return isTruthy(table.value(text, text));
    }
    }
    return value.toVariant();
}

QJsonObject readJson(const QString& path, bool* ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    *ok = document.isObject();
    return document.object();
}

/// One preset out of a file that may hold one object or a list of them. The kind
/// comes from the preset itself, falling back to the directory it was found in.
void appendPresets(const QString& path, QHash<QString, QJsonObject>& index)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const auto insert = [&index](const QJsonObject& entry) {
        const QString name = entry.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            index.insert(name, entry);
    };
    if (document.isArray()) {
        for (const QJsonValue& value : document.array())
            insert(value.toObject());
        return;
    }
    insert(document.object());
}

QStringList userPresetPaths(const QString& sourceDirectory, const QString& kind)
{
    QStringList paths;
    const QDir defaults(sourceDirectory + QStringLiteral("/user/default/") + kind);
    for (const QString& name : defaults.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        paths.append(defaults.filePath(name));
    return paths;
}

QStringList presetKinds()
{
    return {QStringLiteral("machine"), QStringLiteral("process"), QStringLiteral("filament")};
}

QString printerNameFrom(const QString& sourceName)
{
    QString name = sourceName;
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() > 1 && words.last().compare(QLatin1String("nozzle"), Qt::CaseInsensitive) == 0) {
        name = words.mid(0, words.size() - 1).join(QLatin1Char(' '));
    }
    return name.trimmed();
}

} // namespace

namespace next_layer::presetimport {

KindIndex presetIndex(const QString& sourceDirectory)
{
    KindIndex index;
    for (const QString& kind : presetKinds())
        index.insert(kind, {});

    const QDir root(sourceDirectory);
    if (!root.exists())
        return index;

    // A system file names its own kind in `type`; the directory it sits in is the
    // fallback, and it is what the vendor bundles actually encode.
    QDirIterator system(sourceDirectory + QStringLiteral("/system"),
                        {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (system.hasNext()) {
        const QString path = system.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QJsonObject first = document.isArray()
            ? (document.array().isEmpty() ? QJsonObject{} : document.array().at(0).toObject())
            : document.object();
        QString kind = first.value(QStringLiteral("type")).toString();
        if (!index.contains(kind)) {
            const QStringList parts = QFileInfo(path).path().split(QLatin1Char('/'));
            const QString directory = parts.isEmpty() ? QString() : parts.last();
            kind = index.contains(directory) ? directory : QStringLiteral("process");
        }
        appendPresets(path, index[kind]);
    }

    for (const QString& kind : presetKinds())
        for (const QString& path : userPresetPaths(sourceDirectory, kind))
            appendPresets(path, index[kind]);

    QDirIterator loose(sourceDirectory, {QStringLiteral("*.json")}, QDir::Files);
    while (loose.hasNext()) {
        const QString path = loose.next();
        for (const QString& kind : presetKinds())
            appendPresets(path, index[kind]);
    }

    return index;
}

QJsonObject resolvePreset(const QString& kind, const QString& name, const KindIndex& index)
{
    const auto found = index.constFind(kind);
    if (found == index.constEnd())
        return {};
    const QHash<QString, QJsonObject> presets = *found;
    QJsonObject merged;
    QString current = name;
    QSet<QString> seen;
    while (!current.isEmpty() && !seen.contains(current)) {
        seen.insert(current);
        const auto entryFound = presets.constFind(current);
        if (entryFound == presets.constEnd())
            break;
        const QJsonObject entry = *entryFound;
        for (auto it = entry.constBegin(); it != entry.constEnd(); ++it) {
            if (it.key() != QLatin1String("inherits"))
                merged.insert(it.key(), it.value());
        }
        current = entry.value(QStringLiteral("inherits")).toString();
    }
    return merged;
}

Report importPresets(const QString& sourceDirectory, SettingsModel& settings, bool write)
{
    Report report;
    if (!QDir(sourceDirectory).exists()) {
        report.error = QStringLiteral("%1 is not a directory").arg(sourceDirectory);
        return report;
    }

    const KindIndex index = presetIndex(sourceDirectory);
    if (index.isEmpty()) {
        report.error = QStringLiteral("no presets under %1").arg(sourceDirectory);
        return report;
    }

    const QString vendorDirectory = presets::customVendorDirectory();
    QSet<QString> dropped;

    for (const QString& kind : presetKinds()) {
        const QStringList paths = userPresetPaths(sourceDirectory, kind);
        for (const QString& path : paths) {
            bool ok = false;
            const QJsonObject own = readJson(path, &ok);
            if (!ok)
                continue;
            const QString sourceName = own.value(QStringLiteral("name")).toString();
            if (sourceName.isEmpty())
                continue;
            const QJsonObject resolved = resolvePreset(kind, sourceName, index);

            QMap<QString, QVariant> values;
            int translated = 0;
            for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it) {
                const QString key = it.key();
                if (metaKeys().contains(key) || key.startsWith(QLatin1String("compatible_")))
                    continue;
                const Mapping* mapping = findMapping(key);
                if (mapping == nullptr || unmappedKeys().contains(key)) {
                    dropped.insert(key);
                    continue;
                }
                const QString target = QString::fromLatin1(mapping->target);
                const QString block = settings.blockOf(target);
                if (block.isEmpty() || block != (kind == QLatin1String("machine")
                                                 ? QStringLiteral("printer")
                                                 : kind == QLatin1String("filament")
                                                   ? QStringLiteral("filament")
                                                   : QStringLiteral("process"))) {
                    dropped.insert(key);
                    continue;
                }
                const QVariant transformed = transformValue(it.value(), *mapping);
                if (transformed.typeId() == QMetaType::QString && transformed.toString().isEmpty())
                    continue;
                values.insert(target, transformed);
                ++translated;
            }

            if (translated == 0) {
                report.refused.append(QStringLiteral("%1: nothing this engine has").arg(sourceName));
                continue;
            }

            // A machine needs a bed and a height before it is a machine at all.
            if (kind == QLatin1String("machine")) {
                const QVariantList bed = values.value(QStringLiteral("bed_shape")).toList();
                const double height = values.value(QStringLiteral("max_print_height")).toDouble();
                if (bed.size() < 3 || height <= 0.0) {
                    report.refused.append(
                        QStringLiteral("%1: %2 translated key(s), %3 bed point(s), height %4")
                            .arg(sourceName).arg(translated).arg(bed.size()).arg(height));
                    continue;
                }
            }

            const QString presetName = kind == QLatin1String("machine")
                                       ? printerNameFrom(sourceName) : sourceName;
            const QString slug = presets::slugOf(presetName);

            // A printer preset binds to its model by name: the vendor bundle gives
            // the model id `NL_<slug>_MODEL`, and a preset without that value is
            // never attached to a machine, so the engine finds no tool for it.
            if (kind == QLatin1String("machine")) {
                values.insert(QStringLiteral("printer_model"), QStringLiteral("NL_") + slug
                                                              + QStringLiteral("_MODEL"));
                values.insert(QStringLiteral("printer_technology"), QStringLiteral("FFF"));
                values.insert(QStringLiteral("printer_vendor"), QStringLiteral("Custom"));
            }

            const QString block = valuesBlock(values);

            if (kind == QLatin1String("machine")) {
                if (write) {
                    const QString error = presets::addCustomPrinter(presetName);
                    if (!error.isEmpty()) {
                        report.error = error;
                        return report;
                    }
                    if (!presets::ensureToolPrintPreset().isEmpty()) {
                        report.error = QStringLiteral("Could not write the tool preset");
                        return report;
                    }
                    const QString target = presets::customPrinterPath(presetName);
                    const QString condition = QStringLiteral("printer.model == \"NL_")
                        + slug + QStringLiteral("_MODEL\"");
                    if (target.isEmpty()
                        || !presets::writeFile(target,
                               presets::document(QStringLiteral("printer"), presetName, QString(),
                                                 condition, block,
                                                 QStringLiteral("nl-") + slug))) {
                        report.error = QStringLiteral("Could not write the printer preset");
                        return report;
                    }
                }
                report.printers.append(presetName);
                continue;
            }

            const QString presetKind = kind == QLatin1String("filament")
                                       ? QStringLiteral("filament") : QStringLiteral("print");
            if (write) {
                const QString target = vendorDirectory + QStringLiteral("/preset-") + presetKind
                                     + QLatin1Char('-') + slug + QStringLiteral(".yaml");
                if (!presets::writeFile(target,
                        presets::document(presetKind, presetName, QString(), QString(), block))) {
                    report.error = QStringLiteral("Could not write %1").arg(presetName);
                    return report;
                }
            }
            (kind == QLatin1String("filament") ? report.filaments : report.prints)
                .append(presetName);
        }
    }

    report.droppedKeys = QStringList(dropped.constBegin(), dropped.constEnd());
    std::sort(report.droppedKeys.begin(), report.droppedKeys.end());
    return report;
}

} // namespace next_layer::presetimport
