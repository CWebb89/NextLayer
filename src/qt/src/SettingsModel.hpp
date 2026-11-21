// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>
#include <Slic3r/Domain/ConfigDefsFDM.hpp>
#include <Slic3r/Domain/ConfigValue.hpp>
#include <Slic3r/Domain/Percentage.hpp>
#include <Slic3r/Domain/PrinterTechnology.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <QFile>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "next-layer-app/src/bridge.rs.h"

namespace settingsModelDetail {

/// A `rust::String` as the `QString` the QML and the C++ that stays both expect.
inline QString fromRust(const rust::String& value)
{
    return QString::fromStdString(static_cast<std::string>(value));
}

inline QVariantList stringList(const rust::Vec<rust::String>& values)
{
    QVariantList list;
    for (const rust::String& value : values)
        list.append(fromRust(value));
    return list;
}

/// One `QVariant`'s own accessors, as `QVariant`'s converters give them. The shell
/// computes every one of them here rather than letting the model reinterpret a
/// `QVariant`, so `toInt`'s rounding, `toBool`'s treatment of `"0"` and
/// `toString`'s spelling are Qt's answers and not a second implementation's.
inline next_layer::SettingScalarRecord scalarRecord(const QVariant& value)
{
    next_layer::SettingScalarRecord record;
    switch (value.typeId()) {
    case QMetaType::Bool: record.kind = 1; break;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: record.kind = 2; break;
    case QMetaType::Double:
    case QMetaType::Float: record.kind = 3; break;
    case QMetaType::QString: record.kind = 4; break;
    case QMetaType::QVariantList:
    case QMetaType::QStringList: record.kind = 5; break;
    case QMetaType::QVariantMap: record.kind = 6; break;
    default: record.kind = 0; break;
    }

    bool integerOk = false;
    bool numberOk = false;
    record.truth = value.toBool();
    record.integer = value.toInt(&integerOk);
    record.integer_ok = integerOk;
    record.number = value.toDouble(&numberOk);
    record.number_ok = numberOk;
    record.text = value.toString().toStdString();

    if (record.kind == 6) {
        const QVariantMap object = value.toMap();
        const QVariant raw = object.value(QStringLiteral("value"));
        record.map_value = raw.toDouble();
        record.map_percent = object.value(QStringLiteral("is_percent")).toBool();
        if (!raw.isValid())
            record.kind = 0;
    }
    return record;
}

/// One element of a list-valued setting: a number, a string, or a point — the three
/// shapes the panel and another slicer's preset put inside a list.
inline next_layer::SettingElementRecord elementRecord(const QVariant& value)
{
    next_layer::SettingElementRecord record;
    bool numberOk = false;
    record.number = value.toDouble(&numberOk);
    record.number_ok = numberOk;
    record.text = value.toString().toStdString();

    if (value.typeId() == QMetaType::QString) {
        record.kind = 1;
        return record;
    }
    if (value.canConvert<QVariantList>()) {
        const QVariantList pair = value.toList();
        if (pair.size() >= 2) {
            record.kind = 2;
            record.pair_first = pair.at(0).toDouble();
            record.pair_second = pair.at(1).toDouble();
        }
    }
    return record;
}

inline next_layer::SettingInputRecord inputRecord(const QVariant& value)
{
    next_layer::SettingInputRecord record;
    record.whole = scalarRecord(value);
    record.first = record.whole;
    record.is_list = value.typeId() == QMetaType::QVariantList
                  || value.typeId() == QMetaType::QStringList;
    if (record.is_list) {
        const QVariantList list = value.toList();
        for (const QVariant& element : list)
            record.elements.push_back(elementRecord(element));
        if (!list.isEmpty())
            record.first = scalarRecord(list.first());
    }
    return record;
}

/// `QVariant::toString` and `QVariant::toDouble` are total, but an invalid
/// `QVariant` is not a value: the engine writes `false` for it and the panel draws
/// an unset control.
inline QVariant variantOf(const next_layer::SettingValueRecord& record)
{
    QVariantList list;
    switch (record.kind) {
    case 1: return QVariant(record.truth);
    case 2: return QVariant(int(record.integer));
    case 3: return QVariant(record.number);
    case 4: return QVariant(fromRust(record.text));
    case 5:
        switch (record.items) {
        case 1:
        case 8:
            for (const rust::String& text : record.texts)
                list.append(fromRust(text));
            return list;
        case 2:
            for (size_t index = 0; index + 1 < record.numbers.size(); index += 2) {
                QVariantList point;
                point.append(record.numbers[index]);
                point.append(record.numbers[index + 1]);
                list.append(QVariant(point));
            }
            return list;
        case 3:
            for (const double number : record.numbers)
                list.append(QVariant(int(number)));
            return list;
        case 4:
            for (const uint8_t flag : record.flags)
                list.append(QVariant(flag != 0));
            return list;
        case 6:
            for (size_t index = 0; index < record.numbers.size(); ++index) {
                const bool absent = index < record.flags.size() && record.flags[index] != 0;
                list.append(absent ? QVariant() : QVariant(int(record.numbers[index])));
            }
            return list;
        default:
            for (const double number : record.numbers)
                list.append(QVariant(number));
            return list;
        }
    case 6:
    case 7: return QVariant(record.number);
    case 8: return QVariant(fromRust(record.text));
    default: return QVariant();
    }
}

inline QVariantList choiceList(const rust::Vec<next_layer::SettingChoiceRecord>& choices)
{
    QVariantList list;
    for (const next_layer::SettingChoiceRecord& choice : choices) {
        QVariantMap entry;
        entry.insert(QStringLiteral("value"), fromRust(choice.value));
        entry.insert(QStringLiteral("label"), fromRust(choice.label));
        list.append(entry);
    }
    return list;
}

/// One setting's map, with the keys the QML reads. A setting the model does not
/// hold — an object override on a setting the sidebar filters out — carries the
/// seven keys the panel needs to draw an unset row and nothing else.
inline QVariantMap itemMap(const next_layer::SettingItemRecord& record)
{
    QVariantMap item;
    item.insert(QStringLiteral("name"), fromRust(record.name));
    item.insert(QStringLiteral("label"), fromRust(record.label));
    if (record.complete) {
        item.insert(QStringLiteral("fullLabel"), fromRust(record.full_label));
        item.insert(QStringLiteral("tooltip"), fromRust(record.tooltip));
    }
    item.insert(QStringLiteral("type"), fromRust(record.type_name));
    item.insert(QStringLiteral("guiType"), fromRust(record.gui_type));
    item.insert(QStringLiteral("value"), variantOf(record.value));
    if (record.complete) {
        item.insert(QStringLiteral("min"), record.has_min ? QVariant(record.min) : QVariant());
        item.insert(QStringLiteral("max"), record.has_max ? QVariant(record.max) : QVariant());
        item.insert(QStringLiteral("units"), stringList(record.units));
    }
    item.insert(QStringLiteral("choices"), choiceList(record.choices));
    item.insert(QStringLiteral("modified"), record.modified);
    if (record.complete) {
        item.insert(QStringLiteral("percent"), record.percent);
        item.insert(QStringLiteral("multiline"), record.multiline);
        item.insert(QStringLiteral("fullWidth"), record.full_width);
    }
    return item;
}

/// A `Slic3r::Domain::ConfigLocation` as the definition record carries it, which
/// is the `FDMConfigLocation` number and `0` for every location that is not one.
inline uint8_t fdmLocation(const Slic3r::Domain::ConfigLocation& location)
{
    const auto* fdm = std::get_if<Slic3r::Domain::FDMConfigLocation>(&location);
    return fdm == nullptr ? uint8_t(0) : uint8_t(*fdm);
}

/// `ConfigItemDef::GUIType` by the enumerator's own name, which is the vocabulary
/// the model reduces to the spelling the QML branches on. The engine's list, not the
/// panel's: everything the model has no widget for arrives here and comes out empty
/// there.
inline const char* guiTypeName(Slic3r::Domain::ConfigItemDef::GUIType type)
{
    using GUIType = Slic3r::Domain::ConfigItemDef::GUIType;
    switch (type) {
    case GUIType::color: return "color";
    case GUIType::file_picker: return "file_picker";
    case GUIType::bed_shape: return "bed_shape";
    case GUIType::substitutions: return "substitutions";
    case GUIType::points: return "points";
    case GUIType::extruder_selection: return "extruder_selection";
    case GUIType::unit_or_percentage: return "unit_or_percentage";
    case GUIType::i_enum_open: return "i_enum_open";
    case GUIType::f_enum_open: return "f_enum_open";
    case GUIType::s_enum_open: return "s_enum_open";
    case GUIType::one_string: return "one_string";
    case GUIType::select_close: return "select_close";
    case GUIType::password: return "password";
    case GUIType::textfield: return "textfield";
    case GUIType::textfields: return "textfields";
    case GUIType::checkbox: return "checkbox";
    case GUIType::checkboxes: return "checkboxes";
    case GUIType::spinbox: return "spinbox";
    case GUIType::spinboxes: return "spinboxes";
    case GUIType::combobox: return "combobox";
    case GUIType::comboboxes: return "comboboxes";
    case GUIType::ramming_params: return "ramming_params";
    case GUIType::language_selection: return "language_selection";
    default: return "";
    }
}

/// `variantFrom`: one `ConfigValue` as the record the model stores. The `kind` and
/// `items` numbers are the model's own vocabulary and have to agree with
/// `src/app/src/SettingsModel.rs`'s `value_of`.
inline next_layer::SettingValueRecord valueRecord(const Slic3r::Domain::ConfigValue& value)
{
    return value.visit([](const auto& element) {
        using Type = std::remove_cvref_t<decltype(element)>;
        next_layer::SettingValueRecord record;

        if constexpr (std::is_same_v<Type, bool>) {
            record.kind = 1;
            record.truth = element;
        } else if constexpr (std::is_same_v<Type, std::vector<bool>>) {
            record.kind = 5;
            record.items = 4;
            for (const bool flag : element)
                record.flags.push_back(uint8_t(flag));
        } else if constexpr (std::is_same_v<Type, int>) {
            record.kind = 2;
            record.integer = element;
        } else if constexpr (std::is_same_v<Type, std::vector<int>>) {
            record.kind = 5;
            record.items = 3;
            for (const int number : element)
                record.numbers.push_back(double(number));
        } else if constexpr (std::is_same_v<Type, std::optional<int>>) {
            if (element.has_value()) {
                record.kind = 2;
                record.integer = *element;
            }
        } else if constexpr (std::is_same_v<Type, std::vector<std::optional<int>>>) {
            record.kind = 5;
            record.items = 6;
            for (const std::optional<int>& number : element) {
                record.flags.push_back(uint8_t(!number.has_value()));
                record.numbers.push_back(number.has_value() ? double(*number) : 0.0);
            }
        } else if constexpr (std::is_same_v<Type, double>) {
            record.kind = 3;
            record.number = element;
        } else if constexpr (std::is_same_v<Type, std::vector<double>>) {
            record.kind = 5;
            for (const double number : element)
                record.numbers.push_back(number);
        } else if constexpr (std::is_same_v<Type, std::string>) {
            record.kind = 4;
            record.text = element;
        } else if constexpr (std::is_same_v<Type, std::vector<std::string>>) {
            record.kind = 5;
            record.items = 1;
            for (const std::string& text : element)
                record.texts.push_back(text);
        } else if constexpr (std::is_same_v<Type, Slic3r::Domain::Percentage>) {
            record.kind = 6;
            record.number = element.value;
        } else if constexpr (std::is_same_v<Type, std::vector<Slic3r::Domain::Percentage>>) {
            record.kind = 5;
            record.items = 7;
            for (const Slic3r::Domain::Percentage& percent : element)
                record.numbers.push_back(percent.value);
        } else if constexpr (std::is_same_v<Type, Slic3r::Domain::FloatOrPercentage>) {
            record.kind = 7;
            record.number = element.is_percentage() ? element.percentage().value
                                                    : element.float_value();
            record.truth = element.is_percentage();
        } else if constexpr (std::is_same_v<Type, std::vector<Slic3r::Domain::FloatOrPercentage>>) {
            record.kind = 5;
            record.items = 5;
            for (const Slic3r::Domain::FloatOrPercentage& value : element) {
                record.numbers.push_back(value.is_percentage() ? value.percentage().value
                                                               : value.float_value());
                record.flags.push_back(uint8_t(value.is_percentage()));
            }
        } else if constexpr (std::is_same_v<Type, Slic3r::Domain::Vec2d>) {
            record.kind = 5;
            record.items = 2;
            record.numbers.push_back(element[0]);
            record.numbers.push_back(element[1]);
        } else if constexpr (std::is_same_v<Type, std::vector<Slic3r::Domain::Vec2d>>) {
            record.kind = 5;
            record.items = 2;
            for (const Slic3r::Domain::Vec2d& point : element) {
                record.numbers.push_back(point[0]);
                record.numbers.push_back(point[1]);
            }
        } else if constexpr (std::is_same_v<Type, Slic3r::Domain::EnumWrapper>) {
            record.kind = 8;
            record.text = std::string(element.get_string());
        } else if constexpr (std::is_same_v<Type, Slic3r::Domain::EnumVectorWrapper>) {
            record.kind = 5;
            record.items = 8;
            for (const std::string_view text : element.get_strings())
                record.texts.push_back(std::string(text));
        }
        return record;
    });
}

inline void appendEnumValues(next_layer::SettingDefinitionRecord& record,
                             const Slic3r::Domain::ConfigValue& value)
{
    if (value.holds_alternative<Slic3r::Domain::EnumWrapper>()) {
        for (const Slic3r::Domain::EnumValueDef& definition :
             value.get<Slic3r::Domain::EnumWrapper>().def()) {
            next_layer::SettingEnumValueRecord enumValue;
            enumValue.serialized = definition.str_serialized;
            enumValue.label = definition.str_ui;
            enumValue.number = definition.enum_value;
            record.enum_values.push_back(std::move(enumValue));
        }
        return;
    }
    if (value.holds_alternative<Slic3r::Domain::EnumVectorWrapper>()) {
        for (const Slic3r::Domain::EnumValueDef& definition :
             value.get<Slic3r::Domain::EnumVectorWrapper>().def()) {
            next_layer::SettingEnumValueRecord enumValue;
            enumValue.serialized = definition.str_serialized;
            enumValue.label = definition.str_ui;
            enumValue.number = definition.enum_value;
            record.enum_values.push_back(std::move(enumValue));
        }
    }
}

/// The definition's declared type decides the shape of its value. A default is the
/// value a setting starts at, and it can be empty or carry a different
/// `ConfigValue` alternative than the one the setting is declared as — an
/// `idle_temperature` whose default has no alternative at all reads back as nothing,
/// and the engine then writes `null` where the profile said 170. The C++ decided
/// assignment by the value's own alternative, which is right whenever the default is
/// populated and wrong whenever it is not; the declared type is what a definition
/// actually promises.
inline void applyDeclaredType(next_layer::SettingValueRecord& record,
                              const std::type_index& type)
{
    if (type == typeid(bool)) {
        record.kind = 1;
        record.items = 0;
    } else if (type == typeid(std::vector<bool>)) {
        record.kind = 5;
        record.items = 4;
    } else if (type == typeid(int) || type == typeid(std::optional<int>)) {
        record.kind = 2;
        record.items = 0;
    } else if (type == typeid(std::vector<int>)) {
        record.kind = 5;
        record.items = 3;
    } else if (type == typeid(std::vector<std::optional<int>>)) {
        record.kind = 5;
        record.items = 6;
    } else if (type == typeid(double)) {
        record.kind = 3;
        record.items = 0;
    } else if (type == typeid(std::vector<double>)) {
        record.kind = 5;
        record.items = 0;
    } else if (type == typeid(Slic3r::Domain::Vec2d)
               || type == typeid(std::vector<Slic3r::Domain::Vec2d>)) {
        record.kind = 5;
        record.items = 2;
    } else if (type == typeid(Slic3r::Domain::Percentage)) {
        record.kind = 6;
        record.items = 0;
    } else if (type == typeid(std::vector<Slic3r::Domain::Percentage>)) {
        record.kind = 5;
        record.items = 7;
    } else if (type == typeid(Slic3r::Domain::FloatOrPercentage)) {
        record.kind = 7;
        record.items = 0;
    } else if (type == typeid(std::vector<Slic3r::Domain::FloatOrPercentage>)) {
        record.kind = 5;
        record.items = 5;
    } else if (type == typeid(std::string)) {
        record.kind = 4;
        record.items = 0;
    } else if (type == typeid(std::vector<std::string>)) {
        record.kind = 5;
        record.items = 1;
    } else if (type == typeid(Slic3r::Domain::EnumWrapper)) {
        record.kind = 8;
        record.items = 0;
    } else if (type == typeid(Slic3r::Domain::EnumVectorWrapper)) {
        record.kind = 5;
        record.items = 8;
    }
}

/// Every definition the engine has, transcribed field for field.
///
/// This is the module's last engine read and the only thing in this header that is
/// not an adapter: the model owns the table, the ordering, the placement, the
/// typing and the writer, and what is left here is a copy of what
/// `Slic3r::Domain::ConfigDef` says, with no decision in it. It goes when HFW's
/// settings surface publishes the fields the model needs — the list is written down
/// in the port notes, section "What HFW settings JSON must expose".
inline std::vector<next_layer::SettingDefinitionRecord> definitions()
{
    std::vector<next_layer::SettingDefinitionRecord> records;
    for (const Slic3r::Domain::ConfigItemDef& def : Slic3r::Domain::get_defs_fdm().defs()) {
        next_layer::SettingDefinitionRecord record;
        record.name = def.name;
        record.label = def.label;
        record.full_label = def.full_label;
        record.tooltip = def.tooltip;
        record.location = fdmLocation(def.location);
        for (const Slic3r::Domain::ConfigLocation& location : def.overrides_in)
            record.overrides_in.push_back(fdmLocation(location));
        record.category = int(def.category);
        record.option_group = int(def.option_group);
        record.category_title = Slic3r::Domain::ConfigItemDef::translate_category(
            def.category, Slic3r::Domain::PrinterTechnology::FFF);
        record.option_group_title =
            Slic3r::Domain::ConfigItemDef::translate_option_group(def.option_group);
        record.order = def.order;
        record.gui_type = guiTypeName(def.gui_type);
        for (const std::string& unit : def.units)
            record.units.push_back(unit);
        record.has_min = def.min.has_value();
        record.min = def.min.has_value() ? *def.min : 0.0;
        record.has_max = def.max.has_value();
        record.max = def.max.has_value() ? *def.max : 0.0;
        record.multiline = def.multiline;
        record.full_width = def.full_width;

        const Slic3r::Domain::ConfigValue fallback =
            def.init_fn ? def.init_fn() : def.init_fn_ex(def.location);
        record.default_value = valueRecord(fallback);
        applyDeclaredType(record.default_value, *def.type);
        appendEnumValues(record, fallback);
        records.push_back(std::move(record));
    }
    return records;
}

/// The bytes of one Qt resource, empty when it is not registered. The model reports
/// the empty case rather than falling back quietly, because a missing layout
/// document is the one failure that changes what the sidebar looks like without
/// saying anything.
inline QString resourceText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

/// The model, built from what the engine defines and the two documents the build
/// ships. A missing layout document does not fail on its own — every setting keeps
/// the engine's category and the sidebar silently collapses — so `layout_loaded` is
/// what the entry point checks before it shows a window.
inline rust::Box<next_layer::SettingsModelCore> core()
{
    const std::vector<next_layer::SettingDefinitionRecord> records = definitions();
    return next_layer::settings_model_new(
        rust::Slice<const next_layer::SettingDefinitionRecord>(records.data(), records.size()),
        resourceText(QStringLiteral(":/next-layer/settings-layout.json")).toStdString(),
        resourceText(QStringLiteral(":/next-layer/base-config.json")).toStdString());
}

}

/// The settings the panel shows and the preset writer saves: one tree per tab, one
/// entry per setting, and the exact YAML a preset file contains.
///
/// The model is Rust's (`src/app/src/SettingsModel.rs`); this header is the QObject
/// QML has always bound to, the one `changed` signal, and one adapter per
/// invokable. It exists because Qt's meta-object system is a C++ ABI: a signal is a
/// static meta-object entry and a `Q_INVOKABLE` is a vtable slot, so the type that
/// carries them has to be C++. Everything below the adapters is Rust, including the
/// YAML writer, the JSON the slice is handed, the page map, the value typing and the
/// four tree shapes.
///
/// `changed()` is emitted after exactly the calls that moved something, which is the
/// rule the C++ has: a write that leaves the value where it was is silent, and so is
/// a reset of a setting that was never edited. The Rust calls answer that question
/// instead of taking a callback, so there is no C++ closure inside the model and no
/// signal delivered while Rust holds a mutable borrow of it.
class SettingsModel : public QObject
{
    Q_OBJECT

public:
    explicit SettingsModel(QObject* parent = nullptr)
        : QObject(parent)
        , m_core(settingsModelDetail::core())
    {
    }

    ~SettingsModel() override = default;

    Q_INVOKABLE QVariantList tabList() const
    {
        QVariantList tabs;
        for (const next_layer::SettingTabRecord& tab : next_layer::settings_tab_list(*m_core)) {
            QVariantList subtabs;
            for (const next_layer::SettingSubtabRecord& subtab : tab.subtabs) {
                QVariantMap entry;
                entry.insert(QStringLiteral("key"), settingsModelDetail::fromRust(subtab.key));
                entry.insert(QStringLiteral("title"), settingsModelDetail::fromRust(subtab.title));
                entry.insert(QStringLiteral("count"), subtab.count);
                entry.insert(QStringLiteral("modified"), subtab.modified);
                subtabs.append(entry);
            }
            QVariantMap entry;
            entry.insert(QStringLiteral("key"), settingsModelDetail::fromRust(tab.key));
            entry.insert(QStringLiteral("title"), settingsModelDetail::fromRust(tab.title));
            entry.insert(QStringLiteral("count"), tab.count);
            entry.insert(QStringLiteral("modified"), tab.modified);
            entry.insert(QStringLiteral("subtabs"), subtabs);
            tabs.append(entry);
        }
        return tabs;
    }

    Q_INVOKABLE QVariantList groups(const QString& tab, const QString& subtab,
                                    const QString& filter) const
    {
        QVariantList result;
        for (const next_layer::SettingGroupRecord& group : next_layer::settings_groups(
                 *m_core, tab.toStdString(), subtab.toStdString(), filter.toStdString())) {
            QVariantList items;
            for (const next_layer::SettingItemRecord& item : group.items)
                items.append(settingsModelDetail::itemMap(item));
            QVariantMap entry;
            entry.insert(QStringLiteral("key"), settingsModelDetail::fromRust(group.key));
            entry.insert(QStringLiteral("title"), settingsModelDetail::fromRust(group.title));
            entry.insert(QStringLiteral("tab"), settingsModelDetail::fromRust(group.tab));
            entry.insert(QStringLiteral("tabTitle"), settingsModelDetail::fromRust(group.tab_title));
            entry.insert(QStringLiteral("subtab"), settingsModelDetail::fromRust(group.subtab));
            entry.insert(QStringLiteral("subtabTitle"),
                         settingsModelDetail::fromRust(group.subtab_title));
            entry.insert(QStringLiteral("items"), items);
            result.append(entry);
        }
        return result;
    }

    Q_INVOKABLE QVariant value(const QString& name) const
    {
        return settingsModelDetail::variantOf(
            next_layer::settings_value(*m_core, name.toStdString()));
    }

    /// The block a setting belongs to: "printer", "print" or "filament"; empty when
    /// this engine has no such setting.
    Q_INVOKABLE QString blockOf(const QString& name) const
    {
        return settingsModelDetail::fromRust(
            next_layer::settings_block_of(*m_core, name.toStdString()));
    }

    /// True when the setting can be written into that block: it lives there, or the
    /// engine lets that block override it.
    Q_INVOKABLE bool canHoldIn(const QString& name, const QString& block) const
    {
        return next_layer::settings_can_hold_in(*m_core, name.toStdString(), block.toStdString());
    }

    /// The model behind the panel, for the importer: it resets and fills the same
    /// values the panel shows, and the order is what the caller's `clearModified`
    /// depends on.
    next_layer::SettingsModelCore& core() { return *m_core; }

    /// A point-list setting flattened to [x0, y0, x1, y1, ...]. A nested
    /// QVariantList reaches QML as an array-like object whose elements are not plain
    /// arrays, so the flat form is what a QML editor can index safely. `value()`
    /// keeps the nested form the bed loader reads.
    Q_INVOKABLE QVariantList points(const QString& name) const
    {
        QVariantList flat;
        for (const double number : next_layer::settings_points(*m_core, name.toStdString()))
            flat.append(number);
        return flat;
    }

    Q_INVOKABLE void setValue(const QString& name, const QVariant& value)
    {
        const next_layer::SettingChangeRecord outcome =
            next_layer::settings_set_value(*m_core, name.toStdString(),
                                           settingsModelDetail::inputRecord(value));
        if (outcome.changed)
            emit changed();
    }

    /// Applies a value translated from another slicer's preset: a scalar for a
    /// per-tool setting becomes a one-element list, and a point list arrives as
    /// [x, y] pairs. False when this setting cannot hold the value.
    bool applyImported(const QString& name, const QVariant& value)
    {
        const next_layer::SettingChangeRecord outcome =
            next_layer::settings_set_value(*m_core, name.toStdString(),
                                           settingsModelDetail::inputRecord(value));
        if (outcome.changed)
            emit changed();
        return outcome.applied;
    }

    Q_INVOKABLE void reset(const QString& name)
    {
        if (next_layer::settings_reset(*m_core, name.toStdString()))
            emit changed();
    }

    Q_INVOKABLE bool seedFromConfigJson(const QString& json)
    {
        const bool seeded =
            next_layer::settings_seed_from_config_json(*m_core, json.toStdString());
        if (seeded)
            emit changed();
        return seeded;
    }

    Q_INVOKABLE void resetSubtab(const QString& tab, const QString& subtab)
    {
        if (next_layer::settings_reset_subtab(*m_core, tab.toStdString(), subtab.toStdString()))
            emit changed();
    }

    Q_INVOKABLE void resetTab(const QString& tab)
    {
        if (next_layer::settings_reset_tab(*m_core, tab.toStdString()))
            emit changed();
    }

    Q_INVOKABLE int modifiedCount(const QString& tab) const
    {
        return next_layer::settings_modified_count(*m_core, tab.toStdString());
    }

    /// Every setting whose value differs from the preset it came from, one entry per
    /// setting: the name, its label and sub-tab, and the two values. This is what a
    /// preset diff and a "save changes" confirmation both read.
    Q_INVOKABLE QVariantList modifiedValues(const QString& tab) const
    {
        QVariantList list;
        for (const next_layer::SettingModifiedRecord& record :
             next_layer::settings_modified_values(*m_core, tab.toStdString())) {
            QVariantMap item;
            item.insert(QStringLiteral("name"), settingsModelDetail::fromRust(record.name));
            item.insert(QStringLiteral("label"), settingsModelDetail::fromRust(record.label));
            item.insert(QStringLiteral("tab"), settingsModelDetail::fromRust(record.tab));
            item.insert(QStringLiteral("subtab"), settingsModelDetail::fromRust(record.subtab));
            item.insert(QStringLiteral("subtabKey"),
                        settingsModelDetail::fromRust(record.subtab_key));
            item.insert(QStringLiteral("before"), settingsModelDetail::variantOf(record.before));
            item.insert(QStringLiteral("after"), settingsModelDetail::variantOf(record.after));
            list.append(item);
        }
        return list;
    }

    /// Every print setting a single object may override, in the same shape the
    /// settings panel uses for its editors (label, type, gui type, choices, units,
    /// and the value the current preset resolves to). One entry per setting, so a
    /// per-object override starts from the value that is actually in force.
    Q_INVOKABLE QVariantList objectSettingDefs(const QString& filter) const
    {
        QVariantList list;
        for (const next_layer::SettingItemRecord& record :
             next_layer::settings_object_defs(*m_core, filter.toStdString()))
            list.append(settingsModelDetail::itemMap(record));
        return list;
    }

    Q_INVOKABLE QString tabDump() const
    {
        return settingsModelDetail::fromRust(next_layer::settings_tab_dump(*m_core));
    }

    /// Declares settings the active engine cannot apply: they are removed from every
    /// surface, so no control can be shown whose value never reaches the engine.
    /// Empty for the engine this application ships with.
    void setUnsupported(const QStringList& names)
    {
        rust::Vec<rust::String> list;
        for (const QString& name : names)
            list.push_back(name.toStdString());
        next_layer::settings_set_unsupported(*m_core, std::move(list));
        emit changed();
    }

    /// Every setting currently hidden because the engine cannot apply it.
    Q_INVOKABLE QStringList unsupported() const
    {
        QStringList names;
        for (const rust::String& name : next_layer::settings_unsupported(*m_core))
            names.append(settingsModelDetail::fromRust(name));
        return names;
    }

    Q_INVOKABLE QString settingsJson() const
    {
        return settingsModelDetail::fromRust(next_layer::settings_json_dump(*m_core));
    }

    /// Every setting name the sidebar exposes, one per line: the tab tree measured
    /// against the engine's own config instead of a hand-written list.
    Q_INVOKABLE QString nameDump() const
    {
        return settingsModelDetail::fromRust(next_layer::settings_name_dump(*m_core));
    }

    /// True once a resolved preset has seeded this model. A model seeded only from
    /// the shipped base config carries a nominal machine and another machine's start
    /// G-code, so a slice from it is refused rather than written.
    bool hasResolvedConfig() const
    {
        return next_layer::settings_has_resolved_config(*m_core);
    }

    /// False when the compiled-in layout document could not be read at all, which is
    /// the one state that regroups every setting under the engine's own categories
    /// without an error. The interface refuses to start on it, as it does on a
    /// missing capability list: a sidebar of engine internals is not this
    /// application's product.
    bool layoutLoaded() const
    {
        return next_layer::settings_layout_loaded(*m_core);
    }

    Q_INVOKABLE QString configJson() const
    {
        return settingsModelDetail::fromRust(next_layer::settings_config_json(*m_core));
    }

    /// The `values:` body of a preset: the file a saved preset contains.
    Q_INVOKABLE QString valuesYaml(const QString& tab, bool changedOnly) const
    {
        return settingsModelDetail::fromRust(
            next_layer::settings_values_yaml(*m_core, tab.toStdString(), changedOnly));
    }

    Q_INVOKABLE void clearModified()
    {
        if (next_layer::settings_clear_modified(*m_core))
            emit changed();
    }

signals:
    void changed();

private:
    rust::Box<next_layer::SettingsModelCore> m_core;
};
