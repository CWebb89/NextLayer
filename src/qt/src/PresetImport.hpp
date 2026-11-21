// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "SettingsModel.hpp"

#include "next-layer-app/src/bridge.rs.h"

#include <QPair>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVector>

#include <string>

namespace next_layer::presetimport {

/// What an import produced, in the user's own names. `refused` is not an error:
/// a preset this engine has no vocabulary for is reported rather than guessed at.
///
/// The nine lists stay `QStringList` on this side even though the reader is Rust:
/// `PresetStore::finishImport(const Report&)` is the only consumer and it inserts
/// them straight into the map QML reads, so the shape is the contract and the
/// conversion happens once, below.
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
    /// G-code templates left out because this engine refuses them: an unknown
    /// placeholder, or a G92 E in a layer-change template while E is absolute.
    /// One refused template makes the slice write no file at all, so the machine
    /// keeps the engine's own template in its place instead.
    QStringList droppedTemplates;
    /// Things the import did that are worth saying out loud but are not losses:
    /// an inherited preset that is not on this machine, or no install found to
    /// resolve against at all.
    QStringList notes;
    QString error;

    bool ok() const { return error.isEmpty(); }
    int count() const { return printers.size() + prints.size() + filaments.size(); }
};

namespace detail {

inline QStringList toList(const rust::Vec<rust::String>& values)
{
    QStringList list;
    for (const rust::String& value : values)
        list.append(QString::fromStdString(static_cast<std::string>(value)));
    return list;
}

inline Report toReport(const next_layer::ImportReportRecord& record)
{
    Report report;
    report.printers = toList(record.printers);
    report.prints = toList(record.prints);
    report.filaments = toList(record.filaments);
    report.refused = toList(record.refused);
    report.elsewhere = toList(record.elsewhere);
    report.droppedKeys = toList(record.dropped_keys);
    report.unfitted = toList(record.unfitted);
    report.droppedTemplates = toList(record.dropped_templates);
    report.notes = toList(record.notes);
    report.error = QString::fromStdString(static_cast<std::string>(record.error));
    return report;
}

} // namespace detail

/// Reads another slicer's presets out of `sourceDirectory` and writes the ones
/// this engine can express into the user's own bundle as printer, print and
/// filament presets, under the user's own names. `settings` supplies the engine's
/// vocabulary: a key is written only into the block it belongs to, so a machine
/// profile's retraction settings do not land in a printer preset.
inline Report importPresets(const QString& sourceDirectory, SettingsModel& settings, bool write)
{
    return detail::toReport(
        preset_import_presets(sourceDirectory.toStdString(), settings.core(), write));
}

/// Reads one preset file -- an Orca/Bambu single-preset `.json` export or a
/// PrusaSlicer `.ini`, whether it holds one preset or a whole bundle -- and writes
/// what this engine can express into the user's own bundle. Inheritance is
/// resolved against the slicer installs found on this machine, which is what a
/// single exported preset cannot carry.
inline Report importPresetFile(const QString& path, SettingsModel& settings, bool write)
{
    return detail::toReport(preset_import_file(path.toStdString(), settings.core(), write));
}

/// The other slicers this app knows how to read, as name and directory, in the
/// order they are worth offering. Absent directories are included; the caller
/// checks.
///
/// The location is read here and decoded in Rust: Qt 6 reports
/// `QStandardPaths::ConfigLocation` as a `file://` URL, and treating it as a path
/// returns `%20`-encoded directory names, so the import finds nothing at all.
inline QVector<QPair<QString, QString>> slicerCandidates()
{
    const QString location = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QVector<QPair<QString, QString>> sources;
    for (const next_layer::SlicerSourceRecord& source :
         preset_slicer_candidates(location.toStdString())) {
        sources.append({QString::fromStdString(static_cast<std::string>(source.name)),
                        QString::fromStdString(static_cast<std::string>(source.path))});
    }
    return sources;
}

} // namespace next_layer::presetimport
