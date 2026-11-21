// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>

#include <string>

#include "next-layer-app/src/bridge.rs.h"

// These are the Qt-typed adapters onto the Rust implementation in
// src/app/src/PresetFiles.rs. Every path, every byte of a preset document and the
// id inside it are Rust's; this header exists so the store that resolves presets,
// the importer that creates them and the entry point keep their spelling.
namespace next_layer::presets {

/// Where presets live on disk: `$XDG_DATA_HOME/next-layer/presets`, or
/// `$HOME/.local/share/next-layer/presets` when that is unset or relative.
inline QString root()
{
    return QString::fromStdString(static_cast<std::string>(presets_root()));
}

/// The bundle a user's own printers live in: created by hand on first set-up or
/// by an import, and never resolved before a vendor bundle that carries a real
/// machine, which is why its name sorts after them.
inline QString customVendorDirectory()
{
    return QString::fromStdString(static_cast<std::string>(presets_custom_vendor_directory()));
}

/// A machine carries the filaments its bundle lists, and a bundle written here
/// holds only the one a machine was created with. The library this app ships is
/// copied in beside it, so a new machine has the range of materials a slicer is
/// expected to offer instead of a single "Generic".
///
/// Which of them are missing is Rust's decision, read off the target directory; the
/// copy itself stays here because `:/next-layer/filaments` is a Qt resource and a
/// `QDir` is the only thing that can enumerate one.
inline int installFilamentLibrary(const QString& directory)
{
    const QDir library(QStringLiteral(":/next-layer/filaments"));
    if (!library.exists())
        return 0;
    const QStringList available = library.entryList({QStringLiteral("*.yaml")}, QDir::Files);
    int written = 0;
    for (const auto& name : presets_filament_library_plan(
             directory.toStdString(), available.join(QLatin1Char('\n')).toStdString())) {
        const QString file = QString::fromStdString(static_cast<std::string>(name));
        if (QFile::copy(library.filePath(file), directory + QLatin1Char('/') + file))
            ++written;
    }
    return written;
}

/// The file-name and id fragment a printer name turns into: lowercased, every run
/// of characters that are neither a Unicode letter nor a Unicode digit collapsed
/// into one `-`, and no trailing `-`.
inline QString slugOf(const QString& name)
{
    return QString::fromStdString(static_cast<std::string>(presets_slug_of(name.toStdString())));
}

inline QString freshId()
{
    return QString::fromStdString(static_cast<std::string>(presets_fresh_id()));
}

inline bool writeFile(const QString& path, const QString& contents)
{
    return presets_write_file(path.toStdString(), contents.toStdString());
}

inline QString document(const QString& kind, const QString& name, const QString& parent,
                        const QString& condition, const QString& valuesYaml,
                        const QString& id = QString())
{
    return QString::fromStdString(static_cast<std::string>(
        presets_document(kind.toStdString(), name.toStdString(), parent.toStdString(),
                         condition.toStdString(), valuesYaml.toStdString(), id.toStdString())));
}

/// One line per printer in the user's own bundle. The bundle's manifest and its
/// vendor description are written together so a printer added by the set-up flow
/// and one added by an import land in the same place, in the same shape.
struct CustomPrinter
{
    QString name;
    QString slug;
    int tools = 1;
};

inline QVector<CustomPrinter> customPrinters()
{
    QVector<CustomPrinter> printers;
    for (const auto& record : presets_custom_printers()) {
        printers.append({QString::fromStdString(static_cast<std::string>(record.name)),
                         QString::fromStdString(static_cast<std::string>(record.slug)),
                         record.tools});
    }
    return printers;
}

/// Registers a printer under the user's own bundle and rewrites the manifest and
/// the vendor description. An existing name keeps its slug, so a printer can be
/// rewritten without becoming a second machine, and keeps the tool count it was
/// given unless this call says otherwise.
inline QString addCustomPrinter(const QString& name, int tools = 0)
{
    return QString::fromStdString(
        static_cast<std::string>(presets_add_custom_printer(name.toStdString(), tools)));
}

inline QString customPrinterPath(const QString& name)
{
    return QString::fromStdString(
        static_cast<std::string>(presets_custom_printer_path(name.toStdString())));
}

/// A bundle's printers cannot be evaluated without at least one tool-print
/// preset: the engine looks one up per hardware configuration and asserts when
/// the collection is empty. One empty "Standard" is what a bundle without a
/// tool of its own needs.
inline QString ensureToolPrintPreset()
{
    return QString::fromStdString(static_cast<std::string>(presets_ensure_tool_print_preset()));
}

} // namespace next_layer::presets
