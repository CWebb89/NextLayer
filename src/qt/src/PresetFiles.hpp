// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QUuid>
#include <QVector>

#include <algorithm>

#include "PresetYaml.hpp"

// Where presets live on disk and how one preset file is written. Shared by the
// store that resolves them and the importer that creates them, so a preset
// written by an import is byte-for-byte the shape the store already reads.
namespace next_layer::presets {

inline QString root()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/next-layer/presets");
}

/// The bundle a user's own printers live in: created by hand on first set-up or
/// by an import, and never resolved before a vendor bundle that carries a real
/// machine, which is why its name sorts after them.
inline QString customVendorDirectory()
{
    return root() + QStringLiteral("/local/next-layer-custom/Custom");
}

/// A machine carries the filaments its bundle lists, and a bundle written here
/// holds only the one a machine was created with. The library this app ships is
/// copied in beside it, so a new machine has the range of materials a slicer is
/// expected to offer instead of a single "Generic".
inline int installFilamentLibrary(const QString& directory)
{
    const QDir library(QStringLiteral(":/next-layer/filaments"));
    if (!library.exists())
        return 0;
    int written = 0;
    for (const QString& name : library.entryList({QStringLiteral("*.yaml")}, QDir::Files)) {
        const QString target = directory + QLatin1Char('/') + name;
        if (QFileInfo::exists(target))
            continue;
        if (QFile::copy(library.filePath(name), target))
            ++written;
    }
    return written;
}

inline QString slugOf(const QString& name)
{
    QString slug;
    for (const QChar character : name.toLower()) {
        if (character.isLetterOrNumber())
            slug += character;
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug += QLatin1Char('-');
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug;
}

inline QString freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline bool writeFile(const QString& path, const QString& contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray body = contents.toUtf8();
    return file.write(body) == body.size();
}

inline QString document(const QString& kind, const QString& name, const QString& parent,
                        const QString& condition, const QString& valuesYaml,
                        const QString& id = QString())
{
    QString text = QStringLiteral("kind: ") + kind
                 + QStringLiteral("\nid: ") + (id.isEmpty() ? freshId() : id)
                 + QStringLiteral("\nname: ") + yaml::quoted(name)
                 + QLatin1Char('\n');
    if (!condition.isEmpty())
        text += QStringLiteral("condition: ") + condition + QLatin1Char('\n');
    if (!parent.isEmpty())
        text += QStringLiteral("inherits:\n- ") + yaml::quoted(parent) + QLatin1Char('\n');
    if (!valuesYaml.trimmed().isEmpty())
        text += QStringLiteral("values:\n") + valuesYaml;
    return text;
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

inline QString manifestPath()
{
    return customVendorDirectory() + QStringLiteral("/printers.json");
}

inline QVector<CustomPrinter> customPrinters()
{
    QVector<CustomPrinter> printers;
    QFile file(manifestPath());
    if (!file.open(QIODevice::ReadOnly))
        return printers;
    const QJsonArray entries = QJsonDocument::fromJson(file.readAll())
        .object().value(QStringLiteral("printers")).toArray();
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        printers.append({entry.value(QStringLiteral("name")).toString(),
                         entry.value(QStringLiteral("slug")).toString(),
                         std::max(1, entry.value(QStringLiteral("tools")).toInt(1))});
    }
    return printers;
}

inline QString vendorYaml(const QVector<CustomPrinter>& printers)
{
    QString text = QStringLiteral(
        "kind: vendor\nid: Custom\nname: Custom\nversion: 1.0.0\n"
        "features:\n  printer: {}\n  tool: {}\n  feeder: {}\n  sheet: {}\nprinter_families:\n");
    for (const CustomPrinter& printer : printers)
        text += QStringLiteral("- label: ") + yaml::quoted(printer.name)
              + QStringLiteral("\n  base_model: NL_") + printer.slug + QLatin1Char('\n');
    text += QStringLiteral(
        "---\nkind: tool\ntechnology: FFF\nid: '0.4'\nname: '0.4'\n"
        "features:\n  nozzle_diameter:\n    default: 0.4\n"
        "---\nkind: sheet\nid: pei_smooth\nname: PEI Smooth\ntype: pei_smooth\n");
    for (const CustomPrinter& printer : printers) {
        const QString name = yaml::quoted(printer.name);
        const QString tools = QString::number(std::max(1, printer.tools));
        text += QStringLiteral("---\nkind: printer\ntechnology: FFF\nname: ") + name
              + QStringLiteral("\nid: NL_") + printer.slug
              + QStringLiteral("\nmodel:\n  base_model: NL_") + printer.slug
              + QStringLiteral("\n  model: NL_") + printer.slug
              + QStringLiteral("_MODEL\ntool_count: ") + tools + QStringLiteral("\nfeatures: {}\n")
              + QStringLiteral("---\nkind: printer_config\nid: nl-") + printer.slug
              + QStringLiteral("\nname: ") + name
              + QStringLiteral("\nprinter: NL_") + printer.slug
              + QStringLiteral("\nlegacy_printer_model:\n- ") + name
              + QStringLiteral("\ntool_count: ") + tools + QStringLiteral("\ntools:\n- tool: '0.4'\nsheet: pei_smooth\n");
    }
    return text;
}

/// Registers a printer under the user's own bundle and rewrites the manifest and
/// the vendor description. An existing name keeps its slug, so a printer can be
/// rewritten without becoming a second machine, and keeps the tool count it was
/// given unless this call says otherwise.
inline QString addCustomPrinter(const QString& name, int tools = 0)
{
    QVector<CustomPrinter> printers = customPrinters();
    QString slug;
    int existingTools = 0;
    for (const CustomPrinter& printer : printers) {
        if (printer.name != name)
            continue;
        slug = printer.slug;
        existingTools = printer.tools;
    }
    if (slug.isEmpty()) {
        slug = slugOf(name);
        if (slug.isEmpty())
            return QStringLiteral("The name needs at least one letter or digit");
        const QString base = slug;
        int suffix = 2;
        const auto taken = [&printers](const QString& candidate) {
            for (const CustomPrinter& printer : printers)
                if (printer.slug == candidate)
                    return true;
            return false;
        };
        while (taken(slug))
            slug = base + QLatin1Char('-') + QString::number(suffix++);
        printers.append({name, slug, 1});
    }

    const int count = std::max(1, tools > 0 ? tools : (existingTools > 0 ? existingTools : 1));
    for (CustomPrinter& printer : printers) {
        if (printer.name == name)
            printer.tools = count;
    }

    QJsonArray manifest;
    for (const CustomPrinter& printer : printers) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), printer.name);
        entry.insert(QStringLiteral("slug"), printer.slug);
        entry.insert(QStringLiteral("tools"), printer.tools);
        manifest.append(entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("printers"), manifest);
    if (!writeFile(manifestPath(),
                   QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented))))
        return QStringLiteral("Could not write the printer manifest");
    if (!writeFile(customVendorDirectory() + QStringLiteral("/vendor.yaml"),
                   vendorYaml(printers)))
        return QStringLiteral("Could not write the vendor bundle");
    return QString();
}

inline QString customPrinterPath(const QString& name)
{
    for (const CustomPrinter& printer : customPrinters())
        if (printer.name == name)
            return customVendorDirectory() + QStringLiteral("/preset-printer-")
                 + printer.slug + QStringLiteral(".yaml");
    return QString();
}

/// A bundle's printers cannot be evaluated without at least one tool-print
/// preset: the engine looks one up per hardware configuration and asserts when
/// the collection is empty. One empty "Standard" is what a bundle without a
/// tool of its own needs.
inline QString ensureToolPrintPreset()
{
    const QString path = customVendorDirectory() + QStringLiteral("/preset-tool-standard.yaml");
    if (QFileInfo::exists(path))
        return QString();
    return writeFile(path, document(QStringLiteral("tool_print"), QStringLiteral("Standard"),
                                    QString(), QString(), QString()))
        ? QString() : QStringLiteral("Could not write the tool preset");
}

} // namespace next_layer::presets
