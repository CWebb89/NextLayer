// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>
#include <QVector>

#include <string>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

/// One renamed setting, for the dump `--dump-foreign-settings` prints and the gate
/// that checks both ends of it.
struct ForeignSettingPair
{
    QString source;
    QString target;
};

namespace foreignSettingsDetail {

inline QString fromRust(const rust::String& value)
{
    return QString::fromStdString(static_cast<std::string>(value));
}

}

/// Translate one setting from the Bambu/Orca dialect into this engine's.
///
/// The two slicers share a lineage, so some names and values are the same, some are
/// the same meaning under a different name, and some describe behaviour this engine
/// does not have. Returns false only for the last group, which is listed as a known
/// gap; anything else is translated and then checked by the engine's own definition,
/// which refuses a value it does not list.
///
/// The table and the lookups over it are Rust's
/// (`src/app/src/ForeignSettings.rs`); this header keeps the spelling its callers use.
inline bool translateForeignSetting(const QString& key, const QString& value,
                                    QString& translatedName, QString& translatedValue)
{
    const rust::Vec<rust::String> translated =
        translate_foreign_setting(key.toStdString(), value.toStdString());
    if (translated.size() != 2)
        return false;
    translatedName = foreignSettingsDetail::fromRust(translated[0]);
    translatedValue = foreignSettingsDetail::fromRust(translated[1]);
    return true;
}

/// Every rename the table carries, in the order it is written.
inline QVector<ForeignSettingPair> foreignSettingTable()
{
    const rust::Vec<rust::String> pairs = foreign_setting_pairs();
    QVector<ForeignSettingPair> table;
    table.reserve(int(pairs.size() / 2));
    for (size_t at = 0; at + 1 < pairs.size(); at += 2) {
        table.append({foreignSettingsDetail::fromRust(pairs[at]),
                      foreignSettingsDetail::fromRust(pairs[at + 1])});
    }
    return table;
}

/// The engine name a foreign key maps to, without looking at its value: the key
/// itself when the table carries no rename for it.
inline QString foreignTargetFor(const QString& source)
{
    return foreignSettingsDetail::fromRust(foreign_target_for(source.toStdString()));
}

/// The source name this table accepts for an engine key when a file carries more
/// than one name that means it. Empty when the table carries no entry for that key.
///
/// Two slicer names can mean one setting here (`support_type` and `support_style`
/// are both `support_material_style`). A file may carry both, and which one the
/// object ends up with is a rule rather than the order a hash happened to hold them
/// in: the first entry in the table, which is also the name `foreignNameFor` writes
/// back, so importing and exporting agree.
inline QString canonicalForeignSource(const QString& engineKey)
{
    return foreignSettingsDetail::fromRust(canonical_foreign_source(engineKey.toStdString()));
}

/// The name and value one of this engine's settings goes out under when a project is
/// written for the Bambu/Orca dialect, which is the reverse of
/// `translateForeignSetting`.
///
/// Returns false when that slicer has no name for the setting -- a name it invented
/// or a value this engine allows and the other one does not -- and the caller writes
/// the engine's own name instead of guessing. Several source names can collapse onto
/// one engine name; the first in the table wins, so the dialect written is stable
/// across runs rather than dependent on hash order.
inline bool foreignNameFor(const QString& engineKey, const QString& engineValue, QString& name,
                           QString& value)
{
    const rust::Vec<rust::String> written =
        foreign_name_for(engineKey.toStdString(), engineValue.toStdString());
    if (written.size() != 2)
        return false;
    name = foreignSettingsDetail::fromRust(written[0]);
    value = foreignSettingsDetail::fromRust(written[1]);
    return true;
}

}
