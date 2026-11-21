// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Domain/Config.hpp>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "SettingsModel.hpp"
#include "next-layer-app/src/bridge.rs.h"

/// What this header still holds, and why it cannot leave yet: a `ConfigBox`, a
/// `ConfigValue` and a `QVariantMap` cannot cross the boundary, so the three
/// functions below are the translation in both directions between the engine's
/// types and the record the Rust module speaks. The rules themselves — which shape a
/// value takes, which shape a printed value is read back as, and whether a setting
/// may be overridden per object — are `src/app/src/ConfigBoxJson.rs`.
///
/// The one engine read here is `box.overrides.overridden_items()`; everything else
/// is a converter. It stays until `PlateDocument` stops handing these functions an
/// engine box, which is `PlateDocument`'s own port.

namespace configBoxJsonDetail {

/// One settings-map value as the `QVariant` the app and the QML already read.
///
/// `kind` 4 and 5 rebuild the `{value, is_percent}` object under exactly those two
/// keys, because `load_box` reads the pair back and `--dump-applied`, `HwfEngine`'s
/// G-code writer and the panel all read `is_percent` by name.
inline QVariant valueOf(const next_layer::ObjectSettingRecord& record)
{
    switch (record.kind) {
    case 1: return QVariant(record.truth);
    case 2: return QVariant(int(record.integer));
    case 3: return QVariant(record.number);
    case 4: {
        QVariantMap percent;
        percent.insert(QStringLiteral("value"), record.number);
        percent.insert(QStringLiteral("is_percent"), true);
        return percent;
    }
    case 5: {
        QVariantMap either;
        either.insert(QStringLiteral("value"), record.number);
        either.insert(QStringLiteral("is_percent"), record.truth);
        return either;
    }
    case 6: {
        QVariantList names;
        for (const rust::String& text : record.texts)
            names.append(settingsModelDetail::fromRust(text));
        return names;
    }
    case 7: {
        QVariantList integers;
        for (const int64_t number : record.integers)
            integers.append(QVariant(int(number)));
        return integers;
    }
    case 8: {
        QVariantList numbers;
        for (const double number : record.numbers)
            numbers.append(number);
        return numbers;
    }
    case 9: {
        QVariantList flags;
        for (const uint8_t flag : record.flags)
            flags.append(QVariant(flag != 0));
        return flags;
    }
    default: return QVariant(settingsModelDetail::fromRust(record.text));
    }
}

/// The settings a read produced, and every name it could not represent appended to
/// the caller's list. Insertion is in the order the reader produced them, so the map
/// keeps its own key order and a repeated name is the last one, as `QVariantMap`
/// itself would give.
inline QVariantMap mapOf(const next_layer::ObjectSettingsRead& read, QStringList& unreadable)
{
    QVariantMap settings;
    for (const next_layer::ObjectSettingRecord& record : read.settings)
        settings.insert(settingsModelDetail::fromRust(record.name), valueOf(record));
    for (const rust::String& name : read.unreadable)
        unreadable.append(settingsModelDetail::fromRust(name));
    return settings;
}

inline rust::Slice<const next_layer::ObjectSettingInput> sliceOf(
    const std::vector<next_layer::ObjectSettingInput>& values)
{
    return rust::Slice<const next_layer::ObjectSettingInput>(values.data(), values.size());
}

inline rust::Slice<const rust::String> sliceOf(const std::vector<rust::String>& values)
{
    return rust::Slice<const rust::String>(values.data(), values.size());
}

/// The engine's settings definitions, as the records the reader types a value from.
///
/// Built once: the table is the engine's own and does not change under a process,
/// and an imported project would otherwise rebuild 383 records to read one value.
inline const std::vector<next_layer::SettingDefinitionRecord>& definitions()
{
    static const std::vector<next_layer::SettingDefinitionRecord> table(
        settingsModelDetail::definitions());
    return table;
}

}

namespace next_layer {

/// The settings a config box overrides, as this app stores them.
///
/// A box carries every setting it knows at its own location -- defaults in one
/// list, the ones that were actually set in another. Only the second is an
/// override: writing the whole box back would turn a default into a decision and
/// pin it against the preset it came from. Values keep their types, because
/// `load_box` is typed and a 0.3 arriving as a string is rejected.
///
/// Types this reader does not represent are named in \a unreadable rather than
/// dropped quietly, and a caller that gets one has something to report.
inline QVariantMap overriddenSettings(const Slic3r::Domain::ConfigBox& box,
                                     QStringList& unreadable)
{
    std::vector<ObjectSettingInput> items;
    items.reserve(box.overrides.overridden_items().size());
    for (const std::reference_wrapper<const Slic3r::Domain::ConfigItem>& item :
         box.overrides.overridden_items()) {
        ObjectSettingInput input;
        input.name = item.get().name();
        input.value = settingsModelDetail::valueRecord(item.get().value());
        items.push_back(std::move(input));
    }
    return configBoxJsonDetail::mapOf(
        overridden_settings(configBoxJsonDetail::sliceOf(items)), unreadable);
}

/// The same, for the key/value lines a project file writes: an INI-shaped record
/// names each setting and prints its value, so the type has to be recovered from
/// the setting's own definition. A key this build does not know, or a value its
/// type cannot accept, is named in \a refused; nothing is guessed.
inline QVariantMap settingsFromText(const QHash<QString, QString>& record, QStringList& refused)
{
    std::vector<rust::String> names;
    std::vector<rust::String> values;
    names.reserve(size_t(record.size()));
    values.reserve(size_t(record.size()));
    for (auto entry = record.constBegin(); entry != record.constEnd(); ++entry) {
        names.push_back(rust::String(entry.key().toStdString()));
        values.push_back(rust::String(entry.value().toStdString()));
    }
    const std::vector<next_layer::SettingDefinitionRecord>& table =
        configBoxJsonDetail::definitions();
    return configBoxJsonDetail::mapOf(
        settings_from_text(configBoxJsonDetail::sliceOf(names),
                           configBoxJsonDetail::sliceOf(values),
                           rust::Slice<const next_layer::SettingDefinitionRecord>(
                               table.data(), table.size())),
        refused);
}

/// Whether this engine lets a setting be overridden per object. A setting that
/// belongs to the print or the machine alone is refused by the engine when an
/// object claims it, so a foreign project's per-object copy of one has to be kept
/// rather than applied -- that is the difference between a file this app can save
/// and one it cannot.
inline bool overridesAtObject(const QString& name)
{
    const std::vector<next_layer::SettingDefinitionRecord>& table =
        configBoxJsonDetail::definitions();
    return overrides_at_object(name.toStdString(),
                               rust::Slice<const next_layer::SettingDefinitionRecord>(
                                   table.data(), table.size()));
}

}
