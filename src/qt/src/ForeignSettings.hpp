// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>
#include <QVector>

namespace next_layer {

/// One renamed setting, for the dump `--dump-foreign-settings` prints and the gate
/// that checks both ends of it.
struct ForeignSettingPair
{
    QString source;
    QString target;
};

/// Translate one setting from the Bambu/Orca dialect into this engine's.
///
/// The two slicers share a lineage, so some names and values are the same, some are
/// the same meaning under a different name, and some describe behaviour this engine
/// does not have. Returns false only for the last group, which is listed here as a
/// known gap; anything else is translated and then checked by the engine's own
/// definition, which refuses a value it does not list.
bool translateForeignSetting(const QString& key, const QString& value,
                             QString& translatedName, QString& translatedValue);

/// Every rename the table carries, in the order it is written.
QVector<ForeignSettingPair> foreignSettingTable();

/// The engine name a foreign key maps to, without looking at its value: the key
/// itself when the table carries no rename for it.
QString foreignTargetFor(const QString& source);

/// The source name this table accepts for an engine key when a file carries more
/// than one name that means it. Empty when the table carries no entry for that
/// key.
///
/// Two slicer names can mean one setting here (`support_type` and `support_style`
/// are both `support_material_style`). A file may carry both, and which one the
/// object ends up with has to be a rule rather than the order a hash happened to
/// hold them in: this returns the first entry in the table, which is also the name
/// `foreignNameFor` writes back, so importing and exporting agree.
QString canonicalForeignSource(const QString& engineKey);

/// The name and value one of this engine's settings goes out under when a project
/// is written for the Bambu/Orca dialect, which is the reverse of
/// `translateForeignSetting`.
///
/// Returns false when that slicer has no name for the setting -- a name it invented
/// or a value this engine allows and the other one does not -- and the caller writes
/// the engine's own name instead of guessing. Several source names can collapse onto
/// one engine name; the first in the table wins, so the dialect written is stable
/// across runs rather than dependent on hash order.
bool foreignNameFor(const QString& engineKey, const QString& engineValue, QString& name,
                    QString& value);

}
