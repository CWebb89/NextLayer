// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "SettingsModel.hpp"
#include "PresetYaml.hpp"

#include <Slic3r/Domain/ConfigDef.hpp>
#include <Slic3r/Domain/ConfigDefsFDM.hpp>
#include <Slic3r/Domain/ConfigValue.hpp>
#include <Slic3r/Domain/Percentage.hpp>
#include <Slic3r/Domain/PrinterTechnology.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace Slic3r::Domain;
namespace yaml = next_layer::yaml;

template <typename T>
struct IsStdVector : std::false_type {};

template <typename T, typename Allocator>
struct IsStdVector<std::vector<T, Allocator>> : std::true_type {};

template <typename T>
QVariant variantFrom(const T& value)
{
    using Type = std::remove_cvref_t<T>;

    if constexpr (IsStdVector<Type>::value) {
        QVariantList list;
        for (const auto& element : value)
            list.append(variantFrom(element));
        return list;
    } else if constexpr (std::is_same_v<Type, bool> || std::is_same_v<Type, int> || std::is_same_v<Type, double>) {
        return value;
    } else if constexpr (std::is_same_v<Type, std::optional<int>>) {
        return value.has_value() ? QVariant(*value) : QVariant();
    } else if constexpr (std::is_same_v<Type, std::string>) {
        return QString::fromStdString(value);
    } else if constexpr (std::is_same_v<Type, Vec2d>) {
        QVariantList list;
        list.append(value[0]);
        list.append(value[1]);
        return list;
    } else if constexpr (std::is_same_v<Type, Percentage>) {
        return value.value;
    } else if constexpr (std::is_same_v<Type, FloatOrPercentage>) {
        return value.is_percentage() ? value.percentage().value : value.float_value();
    } else if constexpr (std::is_same_v<Type, EnumWrapper>) {
        return QString::fromStdString(std::string(value.get_string()));
    } else if constexpr (std::is_same_v<Type, EnumVectorWrapper>) {
        QVariantList list;
        for (const std::string_view string : value.get_strings())
            list.append(QString::fromStdString(std::string(string)));
        return list;
    } else {
        return QVariant();
    }
}

template <typename T>
QJsonValue jsonFrom(const T& value)
{
    using Type = std::remove_cvref_t<T>;

    if constexpr (IsStdVector<Type>::value) {
        QJsonArray array;
        for (const auto& element : value)
            array.append(jsonFrom(element));
        return array;
    } else if constexpr (std::is_same_v<Type, bool>) {
        return value;
    } else if constexpr (std::is_same_v<Type, int>) {
        return value;
    } else if constexpr (std::is_same_v<Type, double>) {
        return value;
    } else if constexpr (std::is_same_v<Type, std::optional<int>>) {
        return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
    } else if constexpr (std::is_same_v<Type, std::string>) {
        return QString::fromStdString(value);
    } else if constexpr (std::is_same_v<Type, Vec2d>) {
        QJsonArray array;
        array.append(value[0]);
        array.append(value[1]);
        return array;
    } else if constexpr (std::is_same_v<Type, Percentage>) {
        QJsonObject object;
        object.insert(QStringLiteral("value"), value.value);
        object.insert(QStringLiteral("is_percent"), true);
        return object;
    } else if constexpr (std::is_same_v<Type, FloatOrPercentage>) {
        QJsonObject object;
        object.insert(
            QStringLiteral("value"),
            value.is_percentage() ? value.percentage().value : value.float_value()
        );
        object.insert(QStringLiteral("is_percent"), value.is_percentage());
        return object;
    } else if constexpr (std::is_same_v<Type, EnumWrapper>) {
        return QString::fromStdString(std::string(value.get_string()));
    } else if constexpr (std::is_same_v<Type, EnumVectorWrapper>) {
        QJsonArray array;
        for (const std::string_view string : value.get_strings())
            array.append(QString::fromStdString(std::string(string)));
        return array;
    } else {
        return QJsonValue(QJsonValue::Null);
    }
}

QString yamlScalarOf(const Slic3r::Domain::ConfigValue& value);

// One YAML value for a preset: a scalar starts with a space, a sequence starts
// with its own newline so the caller can keep the key and items in one string.
// An empty result means the item cannot be written and must be skipped.
template <typename T>
QString yamlElement(const T& value)
{
    using namespace Slic3r::Domain;
    using Type = std::remove_cvref_t<T>;

    if constexpr (IsStdVector<Type>::value) {
        if (value.empty())
            return QStringLiteral(" []");
        QString text = QStringLiteral("\n");
        bool first = true;
        for (const auto& element : value) {
            const QString item = yamlElement(element);
            if (item.isEmpty())
                continue;
            if (!first)
                text += QLatin1Char('\n');
            text += QStringLiteral("  -") + item;
            first = false;
        }
        return first ? QString() : text;
    } else if constexpr (std::is_same_v<Type, bool>) {
        return value ? QStringLiteral(" true") : QStringLiteral(" false");
    } else if constexpr (std::is_same_v<Type, int>) {
        return QLatin1Char(' ') + QString::number(value);
    } else if constexpr (std::is_same_v<Type, double>) {
        return QLatin1Char(' ') + yaml::number(value);
    } else if constexpr (std::is_same_v<Type, std::optional<int>>) {
        return value.has_value() ? QLatin1Char(' ') + QString::number(*value) : QString();
    } else if constexpr (std::is_same_v<Type, std::string>) {
        return QLatin1Char(' ') + yaml::quoted(QString::fromStdString(value));
    } else if constexpr (std::is_same_v<Type, Vec2d>) {
        return QLatin1Char(' ') + yaml::point(value.x(), value.y());
    } else if constexpr (std::is_same_v<Type, Percentage>) {
        return QLatin1Char(' ') + yaml::quoted(yaml::number(value.value) + QLatin1Char('%'));
    } else if constexpr (std::is_same_v<Type, FloatOrPercentage>) {
        return value.is_percentage()
             ? QLatin1Char(' ') + yaml::quoted(yaml::number(value.percentage().value) + QLatin1Char('%'))
             : QLatin1Char(' ') + yaml::number(value.float_value());
    } else if constexpr (std::is_same_v<Type, EnumWrapper>) {
        return QLatin1Char(' ') + yaml::quoted(QString::fromStdString(std::string(value.get_string())));
    } else if constexpr (std::is_same_v<Type, EnumVectorWrapper>) {
        const auto strings = value.get_strings();
        if (strings.empty())
            return QStringLiteral(" []");
        QString text = QStringLiteral("\n");
        bool first = true;
        for (const std::string_view string : strings) {
            if (!first)
                text += QLatin1Char('\n');
            text += QStringLiteral("  -") + QLatin1Char(' ')
                  + yaml::quoted(QString::fromStdString(std::string(string)));
            first = false;
        }
        return text;
    } else {
        return QString();
    }
}

QString yamlScalarOf(const Slic3r::Domain::ConfigValue& value)
{
    return value.visit([](const auto& element) { return yamlElement(element); });
}

QString itemType(const ConfigItemDef& def)
{
    if (*def.type == typeid(bool) || *def.type == typeid(std::vector<bool>))
        return QStringLiteral("Boolean");
    if (*def.type == typeid(int) || *def.type == typeid(std::optional<int>)
        || *def.type == typeid(std::vector<int>) || *def.type == typeid(std::vector<std::optional<int>>))
        return QStringLiteral("Int");
    if (*def.type == typeid(double) || *def.type == typeid(std::vector<double>)
        || *def.type == typeid(Vec2d) || *def.type == typeid(std::vector<Vec2d>))
        return QStringLiteral("Float");
    if (*def.type == typeid(Percentage) || *def.type == typeid(std::vector<Percentage>))
        return QStringLiteral("Percentage");
    if (*def.type == typeid(FloatOrPercentage) || *def.type == typeid(std::vector<FloatOrPercentage>))
        return QStringLiteral("FloatOrPercentage");
    if (*def.type == typeid(std::string))
        return QStringLiteral("String");
    if (*def.type == typeid(std::vector<std::string>))
        return QStringLiteral("Strings");
    if (*def.type == typeid(EnumWrapper) || *def.type == typeid(EnumVectorWrapper))
        return QStringLiteral("Enum");
    return QStringLiteral("String");
}

QString guiTypeName(ConfigItemDef::GUIType type)
{
    switch (type) {
    case ConfigItemDef::GUIType::color: return QStringLiteral("color");
    case ConfigItemDef::GUIType::file_picker: return QStringLiteral("file");
    case ConfigItemDef::GUIType::bed_shape: return QStringLiteral("bedShape");
    case ConfigItemDef::GUIType::substitutions: return QStringLiteral("substitutions");
    case ConfigItemDef::GUIType::points: return QStringLiteral("points");
    case ConfigItemDef::GUIType::extruder_selection: return QStringLiteral("extruderSelection");
    case ConfigItemDef::GUIType::unit_or_percentage: return QStringLiteral("unitOrPercentage");
    default: return QString();
    }
}

bool assignEnum(ConfigValue& current, const QVariant& input)
{
    if (!current.holds_alternative<EnumWrapper>())
        return false;

    EnumWrapper wrapper = current.get<EnumWrapper>();
    const EnumValueDefs& definitions = wrapper.def();
    const QString text = input.toString();

    for (size_t index = 0; index < definitions.size(); ++index) {
        const EnumValueDef& definition = definitions[index];
        if (QString::fromStdString(definition.str_serialized) == text
            || QString::fromStdString(definition.str_ui) == text) {
            wrapper.set_index(index);
            current = ConfigValue{wrapper};
            return true;
        }
    }

    bool numeric = false;
    const int number = input.toInt(&numeric);
    if (numeric) {
        for (size_t index = 0; index < definitions.size(); ++index) {
            if (definitions[index].enum_value == number) {
                wrapper.set_index(index);
                current = ConfigValue{wrapper};
                return true;
            }
        }
    }

    return false;
}

bool assignEnumVector(ConfigValue& current, const QVariant& input)
{
    if (!current.holds_alternative<EnumVectorWrapper>())
        return false;

    EnumVectorWrapper wrapper = current.get<EnumVectorWrapper>();
    const EnumValueDefs& definitions = wrapper.def();
    std::vector<int> values;

    const QVariantList list = input.toList();
    values.reserve(size_t(list.size()));
    for (const QVariant& element : list) {
        const QString text = element.toString();
        const EnumValueDef* match = nullptr;
        for (const EnumValueDef& definition : definitions) {
            if (QString::fromStdString(definition.str_serialized) == text
                || QString::fromStdString(definition.str_ui) == text) {
                match = &definition;
                break;
            }
        }
        if (match == nullptr)
            return false;
        values.push_back(match->enum_value);
    }

    wrapper.raw() = values;
    current = ConfigValue{wrapper};
    return true;
}

bool percentageParts(const QVariant& input, double& value, bool& isPercent)
{
    if (input.canConvert<QVariantMap>()) {
        const QVariantMap object = input.toMap();
        const QVariant raw = object.value(QStringLiteral("value"));
        if (raw.isValid()) {
            value = raw.toDouble();
            isPercent = object.value(QStringLiteral("is_percent")).toBool();
            return true;
        }
    }

    QString text = input.toString();
    isPercent = text.endsWith(QLatin1Char('%'));
    if (isPercent)
        text.chop(1);
    bool ok = false;
    value = text.toDouble(&ok);
    return ok;
}

bool assignValue(ConfigValue& current, const ConfigItemDef& def, const QVariant& incoming)
{
    // Slicers that write one value per extruder express even a single-nozzle
    // machine as a one-element list. For a setting that holds one value, the list
    // is that value; handing the list to the string case wrote an empty filament
    // type, which the engine then treated as no filament at all.
    QVariant input = incoming;
    const std::type_info& type = *def.type;
    const bool holdsList = type == typeid(std::vector<bool>) || type == typeid(std::vector<int>)
        || type == typeid(std::vector<double>) || type == typeid(std::vector<Percentage>)
        || type == typeid(std::vector<FloatOrPercentage>) || type == typeid(std::vector<Vec2d>)
        || type == typeid(std::vector<std::string>);
    if (!holdsList && input.typeId() == QMetaType::QVariantList && !input.toList().isEmpty())
        input = input.toList().first();

    if (*def.type == typeid(bool)) {
        // The other slicers write booleans as "1"/"0" as often as true/false, and
        // a raw string handed to QVariant::toBool would not always survive that.
        const QVariant::Type type = input.typeId() == QMetaType::QString
                                        ? QVariant::String : QVariant::Bool;
        if (type == QVariant::String) {
            const QString text = input.toString().trimmed().toLower();
            current = ConfigValue{!(text.isEmpty() || text == QLatin1String("0")
                                    || text == QLatin1String("false"))};
        } else {
            current = ConfigValue{input.toBool()};
        }
        return true;
    }

    if (*def.type == typeid(int)) {
        bool ok = false;
        const int value = input.toInt(&ok);
        if (!ok)
            return false;
        current = ConfigValue{value};
        return true;
    }

    if (*def.type == typeid(std::optional<int>)) {
        if (!input.isValid())
            current = ConfigValue{std::optional<int>{}};
        else {
            bool ok = false;
            const int value = input.toInt(&ok);
            if (!ok)
                return false;
            current = ConfigValue{std::optional<int>{value}};
        }
        return true;
    }

    if (*def.type == typeid(double)) {
        bool ok = false;
        const double value = input.toDouble(&ok);
        if (!ok)
            return false;
        current = ConfigValue{value};
        return true;
    }

    if (*def.type == typeid(std::string)) {
        current = ConfigValue{input.toString().toStdString()};
        return true;
    }

    if (*def.type == typeid(std::vector<std::string>)) {
        std::vector<std::string> values;
        const QVariantList list = input.toList();
        values.reserve(size_t(list.size()));
        for (const QVariant& element : list)
            values.push_back(element.toString().toStdString());
        current = ConfigValue{values};
        return true;
    }

    if (*def.type == typeid(Percentage)) {
        double value = 0.0;
        bool isPercent = true;
        if (!percentageParts(input, value, isPercent))
            return false;
        current = ConfigValue{Percentage{value}};
        return true;
    }

    if (*def.type == typeid(FloatOrPercentage)) {
        const bool heldPercentage = current.holds_alternative<FloatOrPercentage>()
            && current.get<FloatOrPercentage>().is_percentage();

        double value = 0.0;
        bool percentage = heldPercentage;
        const QVariantMap object = input.canConvert<QVariantMap>() ? input.toMap() : QVariantMap{};
        const bool explicitObject = object.value(QStringLiteral("value")).isValid();
        if (explicitObject)
            percentage = object.value(QStringLiteral("is_percent")).toBool();
        if (!percentageParts(input, value, percentage))
            return false;

        current = percentage
            ? ConfigValue{FloatOrPercentage{Percentage{value}}}
            : ConfigValue{FloatOrPercentage{value}};
        return true;
    }

    if (*def.type == typeid(std::vector<double>)) {
        std::vector<double> values;
        const QVariantList list = input.toList();
        values.reserve(size_t(list.size()));
        for (const QVariant& element : list) {
            bool ok = false;
            const double value = element.toDouble(&ok);
            if (!ok)
                return false;
            values.push_back(value);
        }
        current = ConfigValue{values};
        return true;
    }

    if (*def.type == typeid(std::vector<Vec2d>)) {
        std::vector<Vec2d> values;
        const QVariantList list = input.toList();
        values.reserve(size_t(list.size()));
        for (const QVariant& element : list) {
            const QVariantList point = element.toList();
            if (point.size() < 2)
                return false;
            Vec2d value;
            value[0] = point.at(0).toDouble();
            value[1] = point.at(1).toDouble();
            values.push_back(value);
        }
        current = ConfigValue{values};
        return true;
    }

    if (*def.type == typeid(EnumWrapper))
        return assignEnum(current, input);

    if (*def.type == typeid(EnumVectorWrapper))
        return assignEnumVector(current, input);

    return false;
}

/// The engine's own UI strings are generic about one thing this app is specific
/// about: its organic support generator is a tree, and calling it "Organic" tells
/// a user nothing about what they will get on the plate.
QString enumLabel(const QString& name, const EnumValueDef& definition)
{
    const std::string& serialized = definition.str_serialized;
    if (name == QLatin1String("support_material_style") && serialized == "organic")
        return QStringLiteral("Tree");
    return QString::fromStdString(definition.str_ui);
}

QVariantList choicesFor(const QString& name, const ConfigValue& value);

/// One tree generator, several shapes. The engine exposes a single organic
/// support style whose branch angle, branch diameter, tip diameter and top
/// density are settings; the four tree styles this app offers are parameter
/// sets over that generator, so a user picks a shape and every number it stands
/// for stays visible and editable in the same sub-tab.
struct TreeKnob
{
    const char* name;
    double value;
};

struct TreeProfile
{
    const char* key;
    const char* label;
    TreeKnob knobs[8];
};

const TreeProfile kTreeProfiles[] = {
    {"organic", "Tree", {}},
    {"tree_slim",
     "Tree slim",
     {{"support_tree_branch_diameter", 1.2},
         {"support_tree_branch_diameter_angle", 2.0},
         {"support_tree_tip_diameter", 0.5},
         {"support_tree_top_rate", 20.0},
         {"support_tree_angle", 45.0},
         {"support_tree_angle_slow", 30.0},
         {"support_tree_branch_distance", 1.5},
         {}}},
    {"tree_strong",
     "Tree strong",
     {{"support_tree_branch_diameter", 3.5},
         {"support_tree_branch_diameter_angle", 10.0},
         {"support_tree_tip_diameter", 1.4},
         {"support_tree_top_rate", 35.0},
         {"support_tree_angle", 30.0},
         {"support_tree_angle_slow", 20.0},
         {"support_tree_branch_distance", 0.8},
         {}}},
    {"tree_hybrid",
     "Tree hybrid",
     {{"support_tree_branch_diameter", 2.5},
         {"support_tree_branch_diameter_angle", 6.0},
         {"support_tree_tip_diameter", 0.6},
         {"support_tree_top_rate", 30.0},
         {"support_tree_angle", 35.0},
         {"support_tree_angle_slow", 25.0},
         {"support_tree_branch_distance", 2.5},
         {}}},
};

const TreeProfile* treeProfileFor(const QString& key)
{
    for (const TreeProfile& profile : kTreeProfiles)
        if (key == QLatin1String(profile.key))
            return &profile;
    return nullptr;
}

QVariantList choicesFor(const QString& name, const ConfigValue& value)
{
    QVariantList choices;

    if (value.holds_alternative<EnumWrapper>()) {
        for (const EnumValueDef& definition : value.get<EnumWrapper>().def()) {
            QVariantMap choice;
            choice.insert(QStringLiteral("value"), QString::fromStdString(definition.str_serialized));
            choice.insert(QStringLiteral("label"), enumLabel(name, definition));
            choices.append(choice);
        }
    } else if (value.holds_alternative<EnumVectorWrapper>()) {
        for (const EnumValueDef& definition : value.get<EnumVectorWrapper>().def()) {
            QVariantMap choice;
            choice.insert(QStringLiteral("value"), QString::fromStdString(definition.str_serialized));
            choice.insert(QStringLiteral("label"), enumLabel(name, definition));
            choices.append(choice);
        }
    }

    if (name == QLatin1String("support_material_style")) {
        for (const TreeProfile& profile : kTreeProfiles) {
            if (QLatin1String(profile.key) == QLatin1String("organic"))
                continue;
            QVariantMap choice;
            choice.insert(QStringLiteral("value"), QString::fromLatin1(profile.key));
            choice.insert(QStringLiteral("label"), QString::fromLatin1(profile.label));
            choices.append(choice);
        }
    }

    return choices;
}

QVariantList unitsFor(const ConfigItemDef& def)
{
    QVariantList units;
    for (const std::string& unit : def.units)
        units.append(QString::fromStdString(unit));
    return units;
}

struct TabSpec
{
    const char* key;
    const char* title;
    FDMConfigLocation location;
};

const TabSpec kTabs[] = {
    {"process", "Process", FDMConfigLocation::Print},
    {"filament", "Filament", FDMConfigLocation::Filament},
    {"printer", "Printer", FDMConfigLocation::Printer},
};

// next-layer drives tool changers, not MMU-style single-nozzle colour mixing:
// the options that only exist to mix colours through one nozzle are dropped from
// the panel, and the two sub-tabs that describe tool assignment and tool changes
// are titled for what they do on a tool changer.
/// Options that only exist for a single-nozzle colour MMU. They are filtered by
/// name rather than by tab, so a future engine that renames a tab cannot quietly
/// bring them back. A tool changer has no use for flushing volumes into a purge
/// block; the wipe-tower multipliers stay because a changer does share a tower.
bool isColourMixingOption(const std::string& name)
{
    static const char* const kMixing[] = {
        "mmu_segmented_region_interlocking_depth",
        "mmu_segmented_region_max_width",
        "single_extruder_multi_material_priming",
        "filament_flush_volume",
        "filament_flush_speed",
    };
    for (const char* option : kMixing)
        if (name == option)
            return true;
    return false;
}

QString subtabTitleFor(int category, const std::string& engineTitle)
{
    if (category == int(ConfigItemDef::Category::Print_MultiMaterial))
        return QStringLiteral("Tools & Wipe Tower");
    if (category == int(ConfigItemDef::Category::Filament_MultiMaterial))
        return QStringLiteral("Tool Changes");
    return QString::fromStdString(engineTitle);
}

QString tabKeyFor(FDMConfigLocation location)
{
    for (const TabSpec& tab : kTabs)
        if (tab.location == location)
            return QString::fromLatin1(tab.key);
    return QString();
}

QString tabTitleFor(const QString& key)
{
    for (const TabSpec& tab : kTabs)
        if (key == QLatin1String(tab.key))
            return QString::fromLatin1(tab.title);
    return key;
}

bool matchesFilter(const ConfigItemDef& def, const QString& filter)
{
    if (filter.isEmpty())
        return true;
    return QString::fromStdString(def.label).contains(filter, Qt::CaseInsensitive)
        || QString::fromStdString(def.name).contains(filter, Qt::CaseInsensitive);
}

}

struct SettingsModel::Impl
{
    struct Entry
    {
        const ConfigItemDef* def = nullptr;
        ConfigValue defaultValue;
        ConfigValue currentValue;
        QString name;
        QString label;
        QString tabKey;
        QString subtabKey;
        QString subtabTitle;
        QString engineSubtabTitle;
        QString engineGroupTitle;
        QString type;
        QString guiType;
        QString groupKey;
        QString groupTitle;
        int category = 0;
        int group = 0;
        int sortCategory = 0;
        int sortPosition = -1;
        bool multiline = false;
        bool fullWidth = false;
        bool modified = false;

        FDMConfigLocation location() const
        {
            return std::get<FDMConfigLocation>(def->location);
        }
    };

    struct Placement
    {
        QString pageKey;
        QString pageTitle;
        QString sectionKey;
        QString sectionTitle;
        int pageOrder = 0;
        int sectionOrder = 0;
        int position = 0;
    };

    std::vector<Entry> entries;
    QHash<QString, int> index;
    QHash<QString, Placement> placement;
    QJsonDocument m_template;

    // The pages and sections the sidebar shows, which are next-layer's own and
    // not the engine's categories: tools/settings_layout.py writes them from
    // tools/settings-layout-map.json. An entry without a placement keeps the
    // engine's category, so a setting added to the engine tomorrow still appears
    // -- in the engine's grouping -- instead of vanishing.
    void loadLayout()
    {
        QFile file(QStringLiteral(":/next-layer/settings-layout.json"));
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject tabs = root.value(QStringLiteral("tabs")).toObject();

        static const QStringList tabOrder{ QStringLiteral("process"),
                                           QStringLiteral("filament"),
                                           QStringLiteral("printer") };

        for (const QString& tabKey : tabOrder) {
            const int tabIndex = int(tabOrder.indexOf(tabKey));
            const QJsonArray pages = tabs.value(tabKey).toArray();
            for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
                const QJsonObject page = pages.at(pageIndex).toObject();
                const QJsonArray sections = page.value(QStringLiteral("sections")).toArray();
                for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
                    const QJsonObject section = sections.at(sectionIndex).toObject();
                    const QJsonArray settings = section.value(QStringLiteral("settings")).toArray();
                    for (int position = 0; position < settings.size(); ++position) {
                        Placement target;
                        target.pageKey = page.value(QStringLiteral("key")).toString();
                        target.pageTitle = page.value(QStringLiteral("title")).toString();
                        target.sectionKey = target.pageKey + QLatin1Char('/')
                                            + QString::number(sectionIndex);
                        target.sectionTitle = section.value(QStringLiteral("title")).toString();
                        target.pageOrder = tabIndex * 100 + pageIndex;
                        target.sectionOrder = sectionIndex;
                        target.position = position;
                        placement.insert(settings.at(position).toString(), target);
                    }
                }
            }
        }
    }

    Impl()
    {
        loadLayout();

        for (const ConfigItemDef& def : get_defs_fdm().defs()) {
            const auto* location = std::get_if<FDMConfigLocation>(&def.location);
            if (location == nullptr)
                continue;
            const QString tabKey = tabKeyFor(*location);
            if (tabKey.isEmpty())
                continue;
            if (def.category == ConfigItemDef::Category::Unknown)
                continue;
            if (isColourMixingOption(def.name))
                continue;

            const bool hidden = def.category == ConfigItemDef::Category::Hidden;

            ConfigValue defaultValue = def.init_fn ? def.init_fn() : def.init_fn_ex(def.location);

            Entry entry;
            entry.def = &def;
            entry.defaultValue = defaultValue;
            entry.currentValue = defaultValue;
            entry.name = QString::fromStdString(def.name);
            entry.label = QString::fromStdString(def.label);
            entry.tabKey = tabKey;
            entry.category = int(def.category);
            entry.group = int(def.option_group);
            entry.subtabKey = hidden ? QStringLiteral("advanced")
                                     : QString::number(entry.category);
            entry.subtabTitle = hidden
                ? QStringLiteral("Advanced")
                : subtabTitleFor(int(def.category),
                                 ConfigItemDef::translate_category(def.category,
                                                                   PrinterTechnology::FFF));
            entry.sortCategory = hidden ? 1000 : entry.category;
            entry.type = itemType(def);
            entry.guiType = guiTypeName(def.gui_type);
            entry.groupKey = QString::number(entry.group);
            entry.groupTitle = QString::fromStdString(ConfigItemDef::translate_option_group(def.option_group));
            entry.engineSubtabTitle = entry.subtabTitle;
            entry.engineGroupTitle = entry.groupTitle;
            if (def.option_group == ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports)
                entry.groupTitle = QStringLiteral("Tree supports");
            entry.multiline = def.multiline;
            entry.fullWidth = def.full_width;

            const auto target = placement.constFind(entry.name);
            if (target != placement.constEnd()) {
                entry.subtabKey = target->pageKey;
                entry.subtabTitle = target->pageTitle;
                entry.groupKey = target->sectionKey;
                entry.groupTitle = target->sectionTitle;
                entry.sortCategory = target->pageOrder;
                entry.group = target->sectionOrder;
                entry.sortPosition = target->position;
            }

            entries.push_back(std::move(entry));
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) {
            if (lhs.sortCategory != rhs.sortCategory)
                return lhs.sortCategory < rhs.sortCategory;
            if (lhs.group != rhs.group)
                return lhs.group < rhs.group;
            if (lhs.sortPosition >= 0 && rhs.sortPosition >= 0
                && lhs.sortPosition != rhs.sortPosition)
                return lhs.sortPosition < rhs.sortPosition;
            if (lhs.def->order != rhs.def->order)
                return lhs.def->order < rhs.def->order;
            return lhs.def->name < rhs.def->name;
        });

        for (size_t position = 0; position < entries.size(); ++position)
            index.insert(entries[position].name, int(position));

        seedFromProfile();
    }

    // The shipped base-config.json is a machine-neutral --load config: every
    // setting at a value of the right shape, no printer named. It exists for the
    // shape, not for a machine — the engine's item types (a points list, a
    // per-tool vector) cannot be inferred from an untyped default, so a config
    // built without it is rejected item by item. Nothing is drawn from it: the
    // bed and the printer model in it are nominal and a resolved preset replaces
    // every one of these values before anything slices.
    void seedFromProfile()
    {
        QFile file(QStringLiteral(":/next-layer/base-config.json"));
        if (!file.open(QIODevice::ReadOnly))
            return;
        m_template = QJsonDocument::fromJson(file.readAll());
        if (!m_template.isObject())
            return;
        seedFromConfiguration(m_template.object().value(QStringLiteral("configuration")).toObject());
    }

    // Reseeds the whole editor from a resolved preset configuration (the
    // engine's --save output). The document also becomes the template that
    // configJson() overlays, so settings the editor does not surface (hidden
    // print options, tool_print, project) keep their preset values instead of
    // the shipped base config.
    void seedFromConfigJson(const QString& json)
    {
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
        if (!document.isObject())
            return;
        m_template = document;
        seedFromConfiguration(m_template.object().value(QStringLiteral("configuration")).toObject());
    }

    // Filament options are stored per material slot, so the first slot's value
    // seeds the single-slot editor.
    void seedFromConfiguration(const QJsonObject& configuration)
    {
        struct Block
        {
            const char* key;
            FDMConfigLocation location;
        };
        const Block blocks[] = {
            {"print_settings", FDMConfigLocation::Print},
            {"filament_settings", FDMConfigLocation::Filament},
            {"printer_settings", FDMConfigLocation::Printer},
        };

        for (const Block& block : blocks) {
            const QJsonObject settings = configuration.value(QLatin1String(block.key)).toObject();
            for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
                Entry* entry = find(it.key());
                if (entry == nullptr || entry->location() != block.location)
                    continue;

                QVariant input = it.value().toVariant();
                if (block.location == FDMConfigLocation::Filament) {
                    const QJsonArray materialSlots = it.value().toArray();
                    if (materialSlots.isEmpty())
                        continue;
                    input = materialSlots.at(0).toVariant();
                }

                ConfigValue seeded = entry->defaultValue;
                if (!assignValue(seeded, *entry->def, input))
                    continue;
                entry->defaultValue = seeded;
                entry->currentValue = seeded;
                entry->modified = false;
            }
        }
    }

    Entry* find(const QString& name)
    {
        const auto position = index.constFind(name);
        return position == index.constEnd() ? nullptr : &entries[size_t(position.value())];
    }

    const Entry* find(const QString& name) const
    {
        const auto position = index.constFind(name);
        return position == index.constEnd() ? nullptr : &entries[size_t(position.value())];
    }

    QVariantMap itemMap(const Entry& entry) const
    {
        QVariantMap item;
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("label"), entry.label);
        item.insert(QStringLiteral("fullLabel"), QString::fromStdString(entry.def->full_label));
        item.insert(QStringLiteral("tooltip"), QString::fromStdString(entry.def->tooltip));
        item.insert(QStringLiteral("type"), entry.type);
        item.insert(QStringLiteral("guiType"), entry.guiType);
        item.insert(QStringLiteral("value"), entry.currentValue.visit([](const auto& value) {
            return variantFrom(value);
        }));
        item.insert(
            QStringLiteral("min"),
            entry.def->min.has_value() ? QVariant(*entry.def->min) : QVariant()
        );
        item.insert(
            QStringLiteral("max"),
            entry.def->max.has_value() ? QVariant(*entry.def->max) : QVariant()
        );
        item.insert(QStringLiteral("units"), unitsFor(*entry.def));
        item.insert(QStringLiteral("choices"), choicesFor(entry.name, entry.currentValue));
        item.insert(QStringLiteral("modified"), entry.modified);
        item.insert(
            QStringLiteral("percent"),
            entry.currentValue.holds_alternative<FloatOrPercentage>()
                && entry.currentValue.get<FloatOrPercentage>().is_percentage()
        );
        item.insert(QStringLiteral("multiline"), entry.multiline);
        item.insert(QStringLiteral("fullWidth"), entry.fullWidth);
        if (entry.name == QLatin1String("support_material_style")) {
            const QString profile = treeProfileInUse();
            if (!profile.isEmpty())
                item.insert(QStringLiteral("value"), profile);
        }
        return item;
    }

    bool assignEntry(Entry& entry, const QVariant& input, bool& changed)
    {
        const ConfigValue previous = entry.currentValue;
        if (!assignValue(entry.currentValue, *entry.def, input))
            return false;
        entry.modified = !(entry.currentValue == entry.defaultValue);
        if (!(entry.currentValue == previous))
            changed = true;
        return true;
    }

    double knobValue(const Entry& entry) const
    {
        return entry.currentValue.visit([](const auto& value) { return variantFrom(value); }).toDouble();
    }

    /// The tree style the current numbers correspond to: the engine's style has
    /// to be organic, and every knob of a profile has to match, so a profile
    /// reads as itself after a preset reloads and never as itself after the user
    /// has edited one of its numbers.
    QString treeProfileInUse() const
    {
        const Entry* style = find(QStringLiteral("support_material_style"));
        if (style == nullptr || !style->currentValue.holds_alternative<EnumWrapper>())
            return QString();
        if (style->currentValue.get<EnumWrapper>().get_string() != "organic")
            return QString();

        for (const TreeProfile& profile : kTreeProfiles) {
            if (QLatin1String(profile.key) == QLatin1String("organic"))
                continue;
            bool matches = true;
            for (const TreeKnob* knob = profile.knobs; knob->name != nullptr; ++knob) {
                const Entry* entry = find(QString::fromLatin1(knob->name));
                if (entry == nullptr || qAbs(knobValue(*entry) - knob->value) > 1e-6) {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return QString::fromLatin1(profile.key);
        }

        return QString();
    }

    bool applyTreeProfile(const QString& key, bool& changed)
    {
        const TreeProfile* profile = treeProfileFor(key);
        if (profile == nullptr)
            return false;
        Entry* style = find(QStringLiteral("support_material_style"));
        if (style == nullptr)
            return false;

        bool applied = assignEntry(*style, QStringLiteral("organic"), changed);
        for (const TreeKnob* knob = profile->knobs; knob->name != nullptr; ++knob) {
            Entry* entry = find(QString::fromLatin1(knob->name));
            applied = entry != nullptr && assignEntry(*entry, knob->value, changed) && applied;
        }
        return applied;
    }
};

SettingsModel::SettingsModel(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
}

SettingsModel::~SettingsModel() = default;

QVariantList SettingsModel::tabList() const
{
    QVariantList tabs;

    for (const TabSpec& tab : kTabs) {
        const QString key = QString::fromLatin1(tab.key);
        QVariantList subtabs;
        int tabCount = 0;
        int tabModified = 0;

        QString subtabKey;
        QString subtabTitle;
        int subtabCount = 0;
        int subtabModified = 0;
        bool open = false;

        const auto finishSubtab = [&] {
            if (!open)
                return;
            QVariantMap subtab;
            subtab.insert(QStringLiteral("key"), subtabKey);
            subtab.insert(QStringLiteral("title"), subtabTitle);
            subtab.insert(QStringLiteral("count"), subtabCount);
            subtab.insert(QStringLiteral("modified"), subtabModified);
            subtabs.append(subtab);
            open = false;
        };

        for (const Impl::Entry& entry : m_impl->entries) {
            if (entry.tabKey != key)
                continue;
            if (!open || entry.subtabKey != subtabKey) {
                finishSubtab();
                subtabKey = entry.subtabKey;
                subtabTitle = entry.subtabTitle;
                subtabCount = 0;
                subtabModified = 0;
                open = true;
            }
            ++subtabCount;
            if (entry.modified)
                ++subtabModified;
        }
        finishSubtab();

        QVariantMap entry;
        entry.insert(QStringLiteral("key"), key);
        entry.insert(QStringLiteral("title"), QString::fromLatin1(tab.title));
        entry.insert(QStringLiteral("count"), tabCount);
        entry.insert(QStringLiteral("modified"), tabModified);
        entry.insert(QStringLiteral("subtabs"), subtabs);
        tabs.append(entry);
    }

    for (QVariant& value : tabs) {
        QVariantMap tab = value.toMap();
        int count = 0;
        int modified = 0;
        const QVariantList subtabs = tab.value(QStringLiteral("subtabs")).toList();
        for (const QVariant& subtabValue : subtabs) {
            const QVariantMap subtab = subtabValue.toMap();
            count += subtab.value(QStringLiteral("count")).toInt();
            modified += subtab.value(QStringLiteral("modified")).toInt();
        }
        tab.insert(QStringLiteral("count"), count);
        tab.insert(QStringLiteral("modified"), modified);
        value = tab;
    }

    return tabs;
}

QVariantList SettingsModel::groups(const QString& tab, const QString& subtab, const QString& filter) const
{
    QVariantList result;
    QVariantMap group;
    QVariantList items;

    QString currentTab;
    QString currentSubtab;
    QString currentSubtabTitle;
    QString currentGroup;
    QString currentGroupTitle;
    bool open = false;

    const auto finishGroup = [&] {
        if (!open)
            return;
        group.insert(QStringLiteral("items"), items);
        result.append(group);
        open = false;
    };

    for (const Impl::Entry& entry : m_impl->entries) {
        if (!tab.isEmpty() && entry.tabKey != tab)
            continue;
        if (!subtab.isEmpty() && entry.subtabKey != subtab)
            continue;
        if (!matchesFilter(*entry.def, filter))
            continue;

        if (!open || entry.tabKey != currentTab || entry.subtabKey != currentSubtab
            || entry.groupKey != currentGroup) {
            finishGroup();

            currentTab = entry.tabKey;
            currentSubtab = entry.subtabKey;
            currentSubtabTitle = entry.subtabTitle;
            currentGroup = entry.groupKey;
            currentGroupTitle = entry.groupTitle;

            group.clear();
            group.insert(QStringLiteral("key"), currentTab + QLatin1Char('/')
                                                + currentSubtab + QLatin1Char('/') + currentGroup);
            group.insert(QStringLiteral("title"), currentGroupTitle);
            group.insert(QStringLiteral("tab"), currentTab);
            group.insert(QStringLiteral("tabTitle"), tabTitleFor(currentTab));
            group.insert(QStringLiteral("subtab"), currentSubtab);
            group.insert(QStringLiteral("subtabTitle"), currentSubtabTitle);
            items.clear();
            open = true;
        }

        items.append(m_impl->itemMap(entry));
    }

    finishGroup();
    return result;
}

QVariant SettingsModel::value(const QString& name) const
{
    const Impl::Entry* entry = m_impl->find(name);
    if (entry == nullptr)
        return QVariant();
    return entry->currentValue.visit([](const auto& current) {
        return variantFrom(current);
    });
}

QVariantList SettingsModel::points(const QString& name) const
{
    QVariantList flat;
    const Impl::Entry* entry = m_impl->find(name);
    if (entry == nullptr || !entry->currentValue.holds_alternative<std::vector<Vec2d>>())
        return flat;
    for (const Vec2d& point : entry->currentValue.get<std::vector<Vec2d>>()) {
        flat.append(point[0]);
        flat.append(point[1]);
    }
    return flat;
}

void SettingsModel::setValue(const QString& name, const QVariant& value)
{
    applyImported(name, value);
}

bool SettingsModel::applyImported(const QString& name, const QVariant& value)
{
    Impl::Entry* entry = m_impl->find(name);
    if (entry == nullptr)
        return false;

    if (entry->name == QLatin1String("support_material_style")) {
        const QString requested = value.toString();
        if (requested != QLatin1String("organic") && treeProfileFor(requested) != nullptr) {
            bool changed = false;
            const bool applied = m_impl->applyTreeProfile(requested, changed);
            if (changed)
                emit this->changed();
            return applied;
        }
    }

    const std::type_info& type = *entry->def->type;
    QVariant input = value;
    const bool listType = type == typeid(std::vector<bool>) || type == typeid(std::vector<int>)
        || type == typeid(std::vector<double>) || type == typeid(std::vector<Percentage>)
        || type == typeid(std::vector<FloatOrPercentage>) || type == typeid(std::vector<Vec2d>)
        || type == typeid(std::vector<std::string>);
    if (listType && input.typeId() != QMetaType::QVariantList) {
        QVariantList single;
        single.append(input);
        input = single;
    }

    const ConfigValue previous = entry->currentValue;
    if (!assignValue(entry->currentValue, *entry->def, input))
        return false;

    entry->modified = !(entry->currentValue == entry->defaultValue);
    if (!(entry->currentValue == previous))
        emit changed();
    return true;
}

void SettingsModel::reset(const QString& name)
{
    Impl::Entry* entry = m_impl->find(name);
    if (entry == nullptr)
        return;
    if (entry->currentValue == entry->defaultValue)
        return;

    entry->currentValue = entry->defaultValue;
    entry->modified = false;
    emit changed();
}

bool SettingsModel::seedFromConfigJson(const QString& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject())
        return false;
    if (!document.object().value(QStringLiteral("configuration")).isObject())
        return false;
    m_impl->seedFromConfigJson(json);
    emit changed();
    return true;
}

void SettingsModel::resetSubtab(const QString& tab, const QString& subtab)
{
    bool any = false;
    for (Impl::Entry& entry : m_impl->entries) {
        if (entry.tabKey != tab || entry.subtabKey != subtab)
            continue;
        if (!entry.modified)
            continue;
        entry.currentValue = entry.defaultValue;
        entry.modified = false;
        any = true;
    }
    if (any)
        emit changed();
}

void SettingsModel::resetTab(const QString& tab)
{
    bool any = false;
    for (Impl::Entry& entry : m_impl->entries) {
        if (entry.tabKey != tab || !entry.modified)
            continue;
        entry.currentValue = entry.defaultValue;
        entry.modified = false;
        any = true;
    }
    if (any)
        emit changed();
}

int SettingsModel::modifiedCount(const QString& tab) const
{
    int count = 0;
    for (const Impl::Entry& entry : m_impl->entries)
        if (entry.modified && (tab.isEmpty() || entry.tabKey == tab))
            ++count;
    return count;
}

QVariantList SettingsModel::objectSettingDefs(const QString& filter) const
{
    QVariantList list;
    const QString needle = filter.trimmed().toLower();

    for (const ConfigItemDef& def : Slic3r::Domain::get_defs_fdm().defs()) {
        if (def.overrides_in.count(FDMConfigLocation::Object) == 0)
            continue;

        const QString name = QString::fromStdString(def.name);
        const QString label = QString::fromStdString(def.full_label.empty() ? def.label
                                                                           : def.full_label);
        if (!needle.isEmpty() && !name.toLower().contains(needle)
            && !label.toLower().contains(needle))
            continue;

        const Impl::Entry* entry = m_impl->find(name);
        if (entry != nullptr) {
            list.append(m_impl->itemMap(*entry));
            continue;
        }

        QVariantMap item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("label"), label);
        item.insert(QStringLiteral("type"), QStringLiteral("String"));
        item.insert(QStringLiteral("guiType"), QString());
        item.insert(QStringLiteral("value"), QVariant());
        item.insert(QStringLiteral("choices"), QVariantList());
        item.insert(QStringLiteral("modified"), false);
        list.append(item);
    }
    return list;
}

QVariantList SettingsModel::modifiedValues(const QString& tab) const
{
    QVariantList list;
    for (const Impl::Entry& entry : m_impl->entries) {
        if (!entry.modified || (!tab.isEmpty() && entry.tabKey != tab))
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("label"), entry.label);
        item.insert(QStringLiteral("tab"), entry.tabKey);
        item.insert(QStringLiteral("subtab"), entry.subtabTitle);
        item.insert(QStringLiteral("subtabKey"), entry.subtabKey);
        item.insert(QStringLiteral("before"),
                    entry.defaultValue.visit([](const auto& value) { return variantFrom(value); }));
        item.insert(QStringLiteral("after"),
                    entry.currentValue.visit([](const auto& value) { return variantFrom(value); }));
        list.append(item);
    }
    return list;
}

/// The block a setting belongs to: "printer", "print" or "filament", empty for
/// a name this engine does not have. An import needs this because another
/// slicer's machine profile carries settings this one keeps in the filament
/// block (retraction, for one), and writing those into a printer preset makes
/// the engine reject it.
QString SettingsModel::blockOf(const QString& name) const
{
    for (const Impl::Entry& entry : m_impl->entries)
        if (entry.name == name)
            return entry.tabKey;
    return QString();
}

bool SettingsModel::canHoldIn(const QString& name, const QString& block) const
{
    for (const Impl::Entry& entry : m_impl->entries) {
        if (entry.name != name)
            continue;
        if (entry.tabKey == block)
            return true;
        // Retraction, for one, is a process setting this engine lets a filament
        // override; a filament preset may carry it even though the process block
        // is where it is defined.
        for (const ConfigLocation& location : entry.def->overrides_in)
            if (const auto* fdm = std::get_if<FDMConfigLocation>(&location);
                fdm != nullptr && tabKeyFor(*fdm) == block)
                return true;
        return false;
    }
    return false;
}

QString SettingsModel::nameDump() const
{
    QStringList names;
    for (const QVariant& tabValue : tabList()) {
        const QVariantMap tab = tabValue.toMap();
        const QVariantList subtabs = tab.value(QStringLiteral("subtabs")).toList();
        for (const QVariant& subValue : subtabs) {
            const QVariantMap sub = subValue.toMap();
            const QVariantList groups =
                const_cast<SettingsModel*>(this)->groups(tab.value(QStringLiteral("key")).toString(),
                                                         sub.value(QStringLiteral("key")).toString(),
                                                         QString());
            for (const QVariant& groupValue : groups) {
                const QVariantList items = groupValue.toMap().value(QStringLiteral("items")).toList();
                for (const QVariant& itemValue : items)
                    names.append(itemValue.toMap().value(QStringLiteral("name")).toString());
            }
        }
    }
    return names.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString SettingsModel::tabDump() const
{
    QStringList lines;
    int total = 0;

    for (const QVariant& tabValue : tabList()) {
        const QVariantMap tab = tabValue.toMap();
        const QVariantList subtabs = tab.value(QStringLiteral("subtabs")).toList();
        lines.append(QStringLiteral("%1 \"%2\": %3 sub-tabs, %4 settings")
                         .arg(tab.value(QStringLiteral("key")).toString(),
                              tab.value(QStringLiteral("title")).toString())
                         .arg(subtabs.size())
                         .arg(tab.value(QStringLiteral("count")).toInt()));
        total += tab.value(QStringLiteral("count")).toInt();

        for (const QVariant& subtabValue : subtabs) {
            const QVariantMap subtab = subtabValue.toMap();
            lines.append(QStringLiteral("  %1 \"%2\": %3 settings")
                             .arg(subtab.value(QStringLiteral("key")).toString(),
                                  subtab.value(QStringLiteral("title")).toString())
                             .arg(subtab.value(QStringLiteral("count")).toInt()));
        }
    }

    lines.append(QStringLiteral("total: %1 settings").arg(total));
    return lines.join(QLatin1Char('\n'));
}

// Every setting the sidebar exposes, in the order the sidebar shows it, with
// the page and section it currently lands in. tools/settings_layout.py reads
// this to build the layout table: the app is the authority on what it shows,
// so the table cannot drift from the model by parsing the engine instead.
// What the sidebar shows, in the order it shows it: the page and section the
// setting lands in, and the engine's own page and section titles beside them.
// tools/settings_layout.py matches on the engine's titles, because the layout it
// writes is what replaces them -- reading the displayed titles back would be
// circular, and a regenerated table could never be checked against the old one.
QString SettingsModel::settingsJson() const
{
    QJsonArray out;

    for (const Impl::Entry& entry : m_impl->entries) {
        QJsonObject row;
        row.insert(QStringLiteral("tab"), entry.tabKey);
        row.insert(QStringLiteral("subtab"), entry.subtabKey);
        row.insert(QStringLiteral("subtabTitle"), entry.subtabTitle);
        row.insert(QStringLiteral("group"), entry.groupKey);
        row.insert(QStringLiteral("groupTitle"), entry.groupTitle);
        row.insert(QStringLiteral("engineSubtabTitle"), entry.engineSubtabTitle);
        row.insert(QStringLiteral("engineGroupTitle"), entry.engineGroupTitle);
        row.insert(QStringLiteral("name"), entry.name);
        row.insert(QStringLiteral("label"), entry.label);
        row.insert(QStringLiteral("type"), entry.type);
        out.append(row);
    }

    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
}

QString SettingsModel::configJson() const
{
    if (!m_impl->m_template.isObject())
        return QStringLiteral("{}");

    QJsonObject root = m_impl->m_template.object();
    QJsonObject configuration = root.value(QStringLiteral("configuration")).toObject();
    QJsonObject printSettings = configuration.value(QStringLiteral("print_settings")).toObject();
    QJsonObject filamentSettings = configuration.value(QStringLiteral("filament_settings")).toObject();
    QJsonObject printerSettings = configuration.value(QStringLiteral("printer_settings")).toObject();

    for (const Impl::Entry& entry : m_impl->entries) {
        QJsonValue value = entry.currentValue.visit([](const auto& current) {
            return jsonFrom(current);
        });

        switch (entry.location()) {
        case FDMConfigLocation::Print:
            printSettings.insert(entry.name, value);
            break;
        case FDMConfigLocation::Filament: {
            // A filament option is a vector with one entry per tool. The panel
            // edits the first slot, so the rest of the vector -- one entry per
            // other extruder of a tool changer -- has to survive untouched.
            // Rebuilding it with a single element is what collapsed every
            // multicolour plate back to one filament.
            QJsonArray materialSlots = filamentSettings.value(entry.name).toArray();
            if (materialSlots.isEmpty()) {
                materialSlots.append(value);
            } else {
                materialSlots.replace(0, value);
            }
            filamentSettings.insert(entry.name, materialSlots);
            break;
        }
        case FDMConfigLocation::Printer:
            printerSettings.insert(entry.name, value);
            break;
        default:
            break;
        }
    }

    configuration.insert(QStringLiteral("print_settings"), printSettings);
    configuration.insert(QStringLiteral("filament_settings"), filamentSettings);
    configuration.insert(QStringLiteral("printer_settings"), printerSettings);
    root.insert(QStringLiteral("configuration"), configuration);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// The `values:` body of an engine preset. Preset values are typed by the config
// definitions, so every item has to be written in the engine's own preset
// spelling: percentages carry their sign, points are `x xy`, enum names and
// vectors stay as lists. Anything that cannot be represented is skipped rather
// than written as a wrong type.
QString SettingsModel::valuesYaml(const QString& tab, bool changedOnly) const
{
    QString text;
    for (const Impl::Entry& entry : m_impl->entries) {
        if (!tab.isEmpty() && entry.tabKey != tab)
            continue;
        if (changedOnly && !entry.modified)
            continue;

        const QString body = yamlScalarOf(entry.currentValue);
        if (body.isEmpty())
            continue;

        text += QStringLiteral("  ") + entry.name + QLatin1Char(':') + body + QLatin1Char('\n');
    }
    return text;
}

void SettingsModel::clearModified()
{
    bool any = false;
    for (Impl::Entry& entry : m_impl->entries) {
        if (!entry.modified)
            continue;
        entry.defaultValue = entry.currentValue;
        entry.modified = false;
        any = true;
    }
    if (any)
        emit changed();
}
