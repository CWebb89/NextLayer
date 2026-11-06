// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <QHash>

class SettingsModel;

namespace next_layer::presetimport {

/// What an import produced, in the user's own names. `refused` is not an error:
/// a preset this engine has no vocabulary for is reported rather than guessed at.
struct Report
{
    QStringList printers;
    QStringList prints;
    QStringList filaments;
    QStringList refused;
    /// Keys this engine has, but in a different kind of preset than the one being
    /// written: a machine's retraction settings are per-filament here, so they do
    /// not belong in a printer preset and are not lost settings either.
    QStringList elsewhere;
    QStringList droppedKeys;
    QStringList unfitted;
    QString error;

    bool ok() const { return error.isEmpty(); }
    int count() const { return printers.size() + prints.size() + filaments.size(); }
};

using KindIndex = QHash<QString, QHash<QString, QJsonObject>>;

/// Every preset an installation knows, by kind and then by name: its vendor
/// bundles first, its own saved presets over them. Kinds are kept apart because
/// that slicer reuses names across them — a process preset called "Voron 0.2"
/// sits beside the machine of the same name, and one flat index would let it
/// shadow the machine and lose its bed.
KindIndex presetIndex(const QString& sourceDirectory);

/// A preset merged with its `inherits` ancestors, child values winning.
QJsonObject resolvePreset(const QString& kind, const QString& name, const KindIndex& index);

/// Reads another slicer's presets out of `sourceDirectory` and writes the ones
/// this engine can express into the user's own bundle as printer, print and
/// filament presets, under the user's own names. `settings` supplies the
/// engine's vocabulary: a key is written only into the block it belongs to, so a
/// machine profile's retraction settings do not land in a printer preset.
Report importPresets(const QString& sourceDirectory, SettingsModel& settings, bool write);

} // namespace next_layer::presetimport
