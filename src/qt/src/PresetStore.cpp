// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PresetStore.hpp"

#include "SliceRuntime.hpp"
#include "PresetYaml.hpp"
#include "SettingsModel.hpp"

#include <Slic3r/App/CLI/CLIApp.hpp>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QDir>

#include <functional>

namespace {

constexpr const char* kCustomRepo = "next-layer-custom";
constexpr const char* kCustomVendor = "Custom";

QString presetsRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/next-layer/presets");
}

QStringList bundleRoots()
{
    return {presetsRoot() + QStringLiteral("/local"),
            QString::fromLatin1(NEXT_LAYER_RESOURCES_DIR) + QStringLiteral("/presets")};
}

QString slugOf(const QString& name)
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

QString freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// One entry per hardware configuration: `name` is what the engine resolves by
// (the hw config name), `presetName` is the printer preset it was grouped under.
QVariantList printerListFrom(const QByteArray& json)
{
    const QJsonArray models = QJsonDocument::fromJson(json)
        .object()
        .value(QStringLiteral("printer_models"))
        .toArray();

    QVariantList printers;
    for (const QJsonValue& modelValue : models) {
        const QJsonObject model = modelValue.toObject();
        const QString vendor = model.value(QStringLiteral("vendor_name")).toString();
        const QString vendorId = model.value(QStringLiteral("vendor_id")).toString();
        const QString presetName = model.value(QStringLiteral("name")).toString();
        if (model.value(QStringLiteral("technology")).toString() != QLatin1String("FFF"))
            continue;

        for (const QJsonValue& variantValue : model.value(QStringLiteral("variants")).toArray()) {
            const QJsonObject variant = variantValue.toObject();
            const QJsonArray profiles = variant.value(QStringLiteral("printer_profiles")).toArray();
            for (const QJsonValue& profileValue : profiles) {
                const QJsonObject profile = profileValue.toObject();
                const QJsonObject bed = profile.value(QStringLiteral("bed")).toObject();
                QVariantMap entry;
                entry.insert(QStringLiteral("name"), profile.value(QStringLiteral("name")).toString());
                entry.insert(QStringLiteral("vendor"), vendor);
                entry.insert(QStringLiteral("vendorId"), vendorId);
                entry.insert(QStringLiteral("presetName"), presetName);
                entry.insert(QStringLiteral("configId"), variant.value(QStringLiteral("id")).toString());
                entry.insert(QStringLiteral("extruders"), profile.value(QStringLiteral("extruders_cnt")).toInt());
                entry.insert(QStringLiteral("width"), bed.value(QStringLiteral("width")).toDouble());
                entry.insert(QStringLiteral("height"), bed.value(QStringLiteral("height")).toDouble());
                entry.insert(QStringLiteral("maxPrintHeight"), bed.value(QStringLiteral("max_print_height")).toDouble());
                printers.append(entry);
            }
        }
    }
    return printers;
}

void profileListsFrom(const QByteArray& json, QVariantList& printProfiles,
                      QVariantList& filamentProfiles)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray prints = root.value(QStringLiteral("print_profiles")).toArray();

    for (const QJsonValue& printValue : prints)
        printProfiles.append(printValue.toObject().value(QStringLiteral("name")).toString());

    // A machine lists its materials per print profile, and vendor bundles do not
    // always give every print profile the same set: the selectable materials are
    // the union of them, in the order the bundle lists them.
    QStringList materials;
    for (const QJsonValue& printValue : prints) {
        const QJsonArray filaments = printValue.toObject()
            .value(QStringLiteral("filament_profiles"))
            .toArray();
        for (const QJsonValue& filamentValue : filaments) {
            const QString name = filamentValue.toString();
            if (!materials.contains(name))
                materials.append(name);
        }
    }
    for (const QString& name : materials)
        filamentProfiles.append(name);
}

QString pickName(const QVariantList& choices, const QString& wanted)
{
    QStringList names;
    for (const QVariant& choice : choices) {
        const QVariantMap entry = choice.toMap();
        names.append(entry.isEmpty() ? choice.toString()
                                     : entry.value(QStringLiteral("name")).toString());
    }
    if (!wanted.isEmpty()) {
        if (names.contains(wanted))
            return wanted;
        for (const QString& name : names)
            if (name.startsWith(wanted))
                return name;
    }
    return names.isEmpty() ? QString() : names.first();
}

bool vendorYamlMatches(const QString& path, const QString& vendorId)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1String("id:")))
            return next_layer::yaml::unquoted(line.mid(3)) == vendorId;
    }
    return false;
}

// A preset document in the engine's own shape: kind, id, name, the parent it
// inherits, an optional hardware condition and the values the user changed.
// Written by hand rather than through the engine's YAML writer so a preset can
// be produced without a Workbench. `id` is carried over when the same preset is
// rewritten: another preset may inherit it, and a fresh id would orphan that
// child — the engine asserts on an id it cannot resolve.
QString presetDocument(const QString& kind, const QString& name, const QString& parent,
                       const QString& condition, const QString& valuesYaml,
                       const QString& id = QString())
{
    QString document = QStringLiteral("kind: ") + kind
                     + QStringLiteral("\nid: ") + (id.isEmpty() ? freshId() : id)
                     + QStringLiteral("\nname: ") + next_layer::yaml::quoted(name)
                     + QLatin1Char('\n');
    if (!condition.isEmpty())
        document += QStringLiteral("condition: ") + condition + QLatin1Char('\n');
    if (!parent.isEmpty())
        document += QStringLiteral("inherits:\n- ") + next_layer::yaml::quoted(parent)
                  + QLatin1Char('\n');
    if (!valuesYaml.trimmed().isEmpty())
        document += QStringLiteral("values:\n") + valuesYaml;
    return document;
}

bool writeFile(const QString& path, const QString& contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents.toUtf8()) >= 0;
}

QString presetIdIn(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray& line : lines) {
        if (!line.startsWith("id:"))
            continue;
        return QString::fromUtf8(line.mid(3)).trimmed().remove(QLatin1Char('\''));
    }
    return {};
}

// The id a preset inherits, as written by presetDocument(). A save has to keep
// the parent the preset already has: once a user preset is the selected one, the
// resolved config reports its own id, and inheriting your own id makes the
// engine recurse until the stack dies.
QString presetInheritsIn(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (int index = 0; index + 1 < lines.size(); ++index) {
        if (!lines[index].startsWith("inherits:"))
            continue;
        QByteArray value = lines[index + 1].trimmed();
        if (!value.startsWith('-'))
            return {};
        value = value.mid(1).trimmed();
        if (value.size() >= 2 && value.startsWith('\'') && value.endsWith('\''))
            value = value.mid(1, value.size() - 2);
        return QString::fromUtf8(value);
    }
    return {};
}

// The `values:` body of a preset this app wrote, split into one block per key so
// a second save can merge into the first instead of dropping it. Sequence items
// are two-space indented `- ` lines; every other two-space line starts a key.
QList<QPair<QString, QStringList>> valueBlocks(const QStringList& lines)
{
    QList<QPair<QString, QStringList>> blocks;
    bool inValues = false;
    for (const QString& line : lines) {
        if (line == QLatin1String("values:")) {
            inValues = true;
            continue;
        }
        if (!inValues)
            continue;
        if (!line.startsWith(QLatin1String("  ")) || line.trimmed().isEmpty()) {
            inValues = false;
            continue;
        }
        const bool sequenceItem = line.startsWith(QLatin1String("  -"));
        if (!sequenceItem) {
            const int colon = line.indexOf(QLatin1Char(':'));
            blocks.append({colon < 0 ? line.trimmed() : line.mid(2, colon - 2), {line}});
        } else if (!blocks.isEmpty()) {
            blocks.last().second.append(line);
        }
    }
    return blocks;
}

QString mergeValues(const QString& existing, const QString& updates)
{
    QList<QPair<QString, QStringList>> blocks = valueBlocks(existing.split(QLatin1Char('\n')));
    const QString updatesDocument = QStringLiteral("values:\n") + updates;
    for (const QPair<QString, QStringList>& fresh :
         valueBlocks(updatesDocument.split(QLatin1Char('\n')))) {
        bool replaced = false;
        for (QPair<QString, QStringList>& block : blocks) {
            if (block.first != fresh.first)
                continue;
            block.second = fresh.second;
            replaced = true;
            break;
        }
        if (!replaced)
            blocks.append(fresh);
    }

    QString text;
    for (const QPair<QString, QStringList>& block : blocks)
        for (const QString& line : block.second)
            text += line + QLatin1Char('\n');
    return text;
}

} // namespace

QString PresetStore::dataDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/next-layer");
}

PresetStore::PresetStore(QObject* parent)
    : QObject(parent)
{
}

PresetStore::~PresetStore()
{
    if (m_worker.joinable())
        m_worker.join();
}

void PresetStore::refreshPrinters()
{
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    m_error.clear();
    emit errorChanged();

    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread([this] {
        const QByteArray json = next_layer::SliceRuntime::instance().printerModels();
        QMetaObject::invokeMethod(this, [this, json] {
            m_busy = false;
            emit busyChanged();
            if (json.isEmpty()) {
                m_error = QStringLiteral("Could not list printer presets");
                emit errorChanged();
            } else {
                parsePrinters(json);
            }
        }, Qt::QueuedConnection);
    });
}

void PresetStore::selectPrinter(const QString& name)
{
    if (m_busy || name.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();
    m_error.clear();
    emit errorChanged();

    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread([this, name] {
        const QByteArray json = next_layer::SliceRuntime::instance().profilesFor(name);
        QMetaObject::invokeMethod(this, [this, name, json] {
            m_busy = false;
            emit busyChanged();
            if (json.isEmpty()) {
                m_error = QStringLiteral("Could not list profiles for %1").arg(name);
                emit errorChanged();
            } else {
                m_currentPrinter = name;
                noteCurrentVendor(name);
                parseProfiles(json);
                emit currentPrinterChanged();
            }
        }, Qt::QueuedConnection);
    });
}

QString PresetStore::filamentForTool(int tool) const
{
    if (m_filaments.isEmpty())
        return m_currentFilament;
    const int index = std::clamp(tool - 1, 0, int(m_filaments.size()) - 1);
    return m_filaments.at(index);
}

QStringList PresetStore::filamentsForResolve(const QString& printer, const QString& filament)
{
    // The printer decides how many extruders exist; the material list has to have
    // one entry per extruder or the engine prints the whole plate with tool 1.
    int tools = 0;
    for (const QVariant& value : m_printers) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("name")).toString() != printer)
            continue;
        tools = std::max(1, entry.value(QStringLiteral("extruders")).toInt());
        break;
    }
    // Headless runs resolve before the printer list has been read, so an unknown
    // count must not clip the list the caller asked for.
    const int count = std::max({1, tools, int(m_filaments.size())});
    // With a single material chosen and several tools, every tool gets that
    // material until the user assigns something else.
    const QString first = m_filaments.isEmpty() ? filament : m_filaments.first();
    QStringList result;
    result.reserve(count);
    for (int tool = 0; tool < count; ++tool) {
        const QString name = tool < m_filaments.size() ? m_filaments.at(tool) : QString();
        result.append(name.isEmpty() ? (first.isEmpty() ? filament : first) : name);
    }
    return result;
}

void PresetStore::adoptResolvedFilaments()
{
    // The engine answers with the materials it actually evaluated, one per
    // extruder. Reading them back is what makes the tool chips show a name
    // rather than whatever was typed.
    QStringList resolved;
    const QVariantList materials = m_resolvedPreset.value(QStringLiteral("materials")).toList();
    for (const QVariant& value : materials) {
        const QString name = value.toMap().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            resolved.append(name);
    }
    if (resolved.isEmpty())
        return;
    bool changed = resolved.size() != m_filaments.size();
    for (int index = 0; !changed && index < resolved.size(); ++index) {
        if (m_filaments.at(index) != resolved.at(index))
            changed = true;
    }
    if (!changed)
        return;
    m_filaments = resolved;
    emit filamentsChanged();
}

void PresetStore::setFilaments(const QStringList& names)
{
    QStringList next;
    for (const QString& name : names) {
        if (!name.trimmed().isEmpty())
            next.append(name.trimmed());
    }
    if (next == m_filaments)
        return;
    m_filaments = next;
    emit filamentsChanged();
}

void PresetStore::setFilamentForTool(int tool, const QString& name)
{
    const int index = std::max(0, tool - 1);
    while (m_filaments.size() <= index)
        m_filaments.append(QString());
    if (m_filaments.at(index) == name)
        return;
    m_filaments[index] = name;
    emit filamentsChanged();
    reload(m_currentPrinter, m_currentPrint, m_currentFilament);
}

void PresetStore::resolve(const QString& printer, const QString& print, const QString& filament)
{
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    m_error.clear();
    emit errorChanged();

    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread([this, printer, print, filament] {
        const QStringList requested = filamentsForResolve(printer, filament);
        const QByteArray json =
            next_layer::SliceRuntime::instance().resolvedConfig(printer, print, requested);
        QMetaObject::invokeMethod(this, [this, printer, print, filament, json] {
            m_busy = false;
            emit busyChanged();
            if (json.isEmpty()) {
                m_error = QStringLiteral("Could not resolve %1 / %2 / %3")
                              .arg(printer, print, filament);
                emit errorChanged();
            } else {
                m_currentPrinter = printer;
                m_currentPrint = print;
                m_currentFilament = filament;
                noteCurrentVendor(printer);
                parseResolved(json);
                adoptResolvedFilaments();
                emit currentPrinterChanged();
                emit currentPrintChanged();
                emit currentFilamentChanged();
                emit resolved();
            }
        }, Qt::QueuedConnection);
    });
}

bool PresetStore::refreshPrintersSync()
{
    const QByteArray json = next_layer::SliceRuntime::instance().printerModels();
    if (json.isEmpty())
        return false;
    parsePrinters(json);
    return true;
}

bool PresetStore::selectPrinterSync(const QString& name)
{
    const QByteArray json = next_layer::SliceRuntime::instance().profilesFor(name);
    if (json.isEmpty())
        return false;
    m_currentPrinter = name;
    noteCurrentVendor(name);
    parseProfiles(json);
    return true;
}

bool PresetStore::resolveSync(const QString& printer, const QString& print, const QString& filament)
{
    const QByteArray json = next_layer::SliceRuntime::instance().resolvedConfig(
        printer, print, filamentsForResolve(printer, filament));
    if (json.isEmpty())
        return false;
    m_currentPrinter = printer;
    m_currentPrint = print;
    m_currentFilament = filament;
    noteCurrentVendor(printer);
    parseResolved(json);
    adoptResolvedFilaments();
    return true;
}

void PresetStore::parsePrinters(const QByteArray& json)
{
    m_printers = printerListFrom(json);
    emit printersChanged();
}

void PresetStore::parseProfiles(const QByteArray& json)
{
    QVariantList printProfiles;
    QVariantList filamentProfiles;
    profileListsFrom(json, printProfiles, filamentProfiles);

    m_printProfiles = printProfiles;
    m_filamentProfiles = filamentProfiles;
    emit printProfilesChanged();
    emit filamentProfilesChanged();
}

void PresetStore::parseResolved(const QByteArray& json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject configuration = root.value(QStringLiteral("configuration")).toObject();
    const QJsonObject printer = configuration.value(QStringLiteral("printer_settings")).toObject();

    m_resolvedConfigJson = json;
    m_resolvedConfiguration = configuration.toVariantMap();
    m_resolvedPreset = root.value(QStringLiteral("preset")).toObject().toVariantMap();

    m_resolvedPrinterModel = printer.value(QStringLiteral("printer_model")).toString();
    m_resolvedMaxPrintHeight = printer.value(QStringLiteral("max_print_height")).toDouble();

    m_resolvedBedShape.clear();
    const QJsonArray bedShape = printer.value(QStringLiteral("bed_shape")).toArray();
    for (const QJsonValue& pointValue : bedShape) {
        const QJsonArray point = pointValue.toArray();
        if (point.size() != 2)
            continue;
        QVariantList xy;
        xy.append(point.at(0).toDouble());
        xy.append(point.at(1).toDouble());
        m_resolvedBedShape.append(QVariant::fromValue(xy));
    }
}

PresetStore::BundleLocation PresetStore::locateBundle(const QString& vendorId)
{
    if (vendorId.isEmpty())
        return {};

    for (const QString& root : bundleRoots()) {
        const QDir dir(root);
        const QStringList repos = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& repo : repos) {
            const QDir repoDir(dir.filePath(repo));
            const QStringList vendors = repoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& vendor : vendors) {
                const QString yaml = repoDir.filePath(vendor + QStringLiteral("/vendor.yaml"));
                if (QFileInfo::exists(yaml) && vendorYamlMatches(yaml, vendorId))
                    return {repo, vendor};
            }
        }
    }
    return {};
}

QString PresetStore::presetPrefix(const QString& kind)
{
    return kind + QLatin1Char('-');
}

QString PresetStore::presetPathFor(const BundleLocation& bundle, const QString& kind,
                                   const QString& name)
{
    return presetsRoot() + QStringLiteral("/user/") + bundle.repo + QLatin1Char('/')
         + bundle.vendor + QLatin1Char('/') + presetPrefix(kind) + name + QStringLiteral(".yaml");
}

bool PresetStore::validPresetName(const QString& name)
{
    if (name.isEmpty() || name.size() > 80 || name != name.trimmed())
        return false;
    for (const QChar character : name) {
        if (character == QLatin1Char('/') || character == QLatin1Char('\\')
            || character == QLatin1Char('\n') || character == QLatin1Char('\r'))
            return false;
    }
    return true;
}

void PresetStore::noteCurrentVendor(const QString& printerName)
{
    for (const QVariant& value : m_printers) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("name")).toString() != printerName)
            continue;
        m_currentVendorId = entry.value(QStringLiteral("vendorId")).toString();
        return;
    }
}

void PresetStore::reload(const QString& printer, const QString& print, const QString& filament)
{
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    m_error.clear();
    emit errorChanged();

    if (m_worker.joinable())
        m_worker.join();
    const QStringList filaments = filamentsForResolve(printer, filament);
    m_worker = std::thread([this, printer, print, filament, filaments] {
        const ReloadResult result = computeReload(printer, print, filament, filaments);
        QMetaObject::invokeMethod(this, [this, result] {
            m_busy = false;
            emit busyChanged();
            applyReload(result);
        }, Qt::QueuedConnection);
    });
}

bool PresetStore::reloadSync(const QString& printer, const QString& print, const QString& filament)
{
    const ReloadResult result =
        computeReload(printer, print, filament, filamentsForResolve(printer, filament));
    return applyReload(result);
}

PresetStore::ReloadResult PresetStore::computeReload(const QString& printer, const QString& print,
                                                     const QString& filament,
                                                     const QStringList& filaments)
{
    ReloadResult result;
    const QByteArray printerJson = next_layer::SliceRuntime::instance().printerModels();
    if (printerJson.isEmpty()) {
        result.failure = QStringLiteral("the engine could not list the printer profiles");
        return result;
    }
    result.printers = printerListFrom(printerJson);
    if (qEnvironmentVariableIsSet("NEXT_LAYER_DEBUG_PRESETS")) {
        QStringList listed;
        for (const QVariant& value : result.printers)
            listed.append(value.toMap().value(QStringLiteral("name")).toString());
        std::fprintf(stderr, "reload: %lld bytes, %lld printers: %s\n",
                     static_cast<long long>(printerJson.size()),
                     static_cast<long long>(result.printers.size()),
                     listed.join(QLatin1String(" | ")).toUtf8().constData());
    }
    result.printer = pickName(result.printers, printer);
    if (result.printer.isEmpty()) {
        result.failure = QStringLiteral("no printer matched %1").arg(printer);
        return result;
    }

    const QString target = result.printer;
    const QByteArray profileJson = next_layer::SliceRuntime::instance().profilesFor(target);
    if (profileJson.isEmpty()) {
        result.failure = QStringLiteral("the engine could not list the profiles of %1").arg(target);
        return result;
    }
    profileListsFrom(profileJson, result.printProfiles, result.filamentProfiles);

    result.print = pickName(result.printProfiles, print);
    result.filament = pickName(result.filamentProfiles, filament);
    if (result.print.isEmpty() || result.filament.isEmpty()) {
        result.failure = QStringLiteral("%1 has no process or filament profiles").arg(target);
        return result;
    }

    result.resolvedConfig = next_layer::SliceRuntime::instance().resolvedConfig(
        result.printer, result.print, filaments);
    if (result.resolvedConfig.isEmpty())
        result.failure = QStringLiteral("the engine could not resolve %1 / %2 / %3")
                             .arg(result.printer, result.print, result.filament);
    return result;
}

bool PresetStore::applyReload(const ReloadResult& result)
{
    if (!result.printers.isEmpty()) {
        m_printers = result.printers;
        emit printersChanged();
    }
    m_printProfiles = result.printProfiles;
    m_filamentProfiles = result.filamentProfiles;
    emit printProfilesChanged();
    emit filamentProfilesChanged();

    if (result.resolvedConfig.isEmpty()) {
        m_error = result.failure.isEmpty()
            ? QStringLiteral("Could not resolve %1 / %2 / %3").arg(result.printer, result.print, result.filament)
            : result.failure;
        emit errorChanged();
        return false;
    }

    m_currentPrinter = result.printer;
    m_currentPrint = result.print;
    m_currentFilament = result.filament;
    noteCurrentVendor(result.printer);
    parseResolved(result.resolvedConfig);
    adoptResolvedFilaments();
    // The engine reports which presets it actually evaluated; a requested name it
    // cannot honour (a preset that is not compatible with the machine, say) is
    // replaced silently, so the labels follow the answer, not the request.
    const QVariantMap selectedPrint = m_resolvedPreset.value(QStringLiteral("print")).toMap();
    if (!selectedPrint.value(QStringLiteral("name")).toString().isEmpty())
        m_currentPrint = selectedPrint.value(QStringLiteral("name")).toString();
    const QVariantList materials = m_resolvedPreset.value(QStringLiteral("materials")).toList();
    if (!materials.isEmpty()) {
        const QString material = materials.first().toMap().value(QStringLiteral("name")).toString();
        if (!material.isEmpty())
            m_currentFilament = material;
    }
    emit currentPrinterChanged();
    emit currentPrintChanged();
    emit currentFilamentChanged();
    emit resolved();
    return true;
}

QString PresetStore::saveUserPreset(const QString& kind, const QString& name, const QString& tab)
{
    if (!validPresetName(name))
        return QStringLiteral("A preset name cannot be empty or contain a slash");
    if (m_settings == nullptr)
        return QStringLiteral("The settings model is not available");

    const BundleLocation bundle = locateBundle(m_currentVendorId);
    if (!bundle.valid())
        return QStringLiteral("No preset bundle is loaded for the current printer");

    // Preset inheritance in this engine is by id, not by name, and the ids of
    // the vendor's own presets are opaque. The resolved config reports the root
    // id of every preset that produced it, which is the id a child inherits.
    const QVariantMap preset = m_resolvedPreset;
    QVariantMap parentNode = preset
        .value(kind == QLatin1String("filament") ? QStringLiteral("materials")
                                                 : QStringLiteral("print"))
        .toMap();
    if (parentNode.isEmpty()) {
        const QVariantList parents = preset
            .value(kind == QLatin1String("filament") ? QStringLiteral("materials")
                                                     : QStringLiteral("print"))
            .toList();
        if (!parents.isEmpty())
            parentNode = parents.first().toMap();
    }
    const QString path = presetPathFor(bundle, kind, name);
    // An existing preset keeps its parent: a save changes values, not what the
    // preset is derived from.
    const QString existingParent = presetInheritsIn(path);
    const QString parent = existingParent.isEmpty()
                         ? parentNode.value(QStringLiteral("root_id")).toString()
                         : existingParent;
    if (parent.isEmpty())
        return QStringLiteral("The current preset has no id to inherit from");

    const QString values = m_settings->valuesYaml(tab, true);
    if (values.trimmed().isEmpty())
        return QStringLiteral("Nothing has been changed yet, so there is nothing to save");

    QString existing;
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            existing = QString::fromUtf8(file.readAll());
    }
    const QString merged = mergeValues(existing, values);
    if (!writeFile(path, presetDocument(kind, name, parent, QString(), merged, presetIdIn(path))))
        return QStringLiteral("Could not write %1").arg(path);

    // The saved values become the new baseline, so the panel stops reporting
    // them as changed while the engine is reloading the preset.
    m_settings->clearModified();
    return {};
}

QString PresetStore::renameUserPreset(const QString& kind, const QString& oldName,
                                      const QString& newName)
{
    if (!validPresetName(newName))
        return QStringLiteral("A preset name cannot be empty or contain a slash");

    const BundleLocation bundle = locateBundle(m_currentVendorId);
    if (!bundle.valid())
        return QStringLiteral("No preset bundle is loaded for the current printer");

    const QString oldPath = presetPathFor(bundle, kind, oldName);
    const QString newPath = presetPathFor(bundle, kind, newName);
    if (!QFileInfo::exists(oldPath))
        return QStringLiteral("%1 is not a user preset").arg(oldName);
    if (QFileInfo::exists(newPath))
        return QStringLiteral("A preset named %1 already exists").arg(newName);

    QFile input(oldPath);
    if (!input.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not read %1").arg(oldPath);
    QStringList lines = QString::fromUtf8(input.readAll()).split(QLatin1Char('\n'));
    input.close();

    bool renamed = false;
    for (QString& line : lines) {
        if (line.startsWith(QLatin1String("name:")) && !line.startsWith(QLatin1Char(' '))) {
            line = QStringLiteral("name: ") + next_layer::yaml::quoted(newName);
            renamed = true;
        } else if (line.startsWith(QLatin1String("id:")) && !line.startsWith(QLatin1Char(' '))) {
            line = QStringLiteral("id: ") + freshId();
        }
    }
    if (!renamed)
        return QStringLiteral("%1 has no preset name to change").arg(oldName);

    if (!writeFile(newPath, lines.join(QLatin1Char('\n'))))
        return QStringLiteral("Could not write %1").arg(newPath);
    QFile::remove(oldPath);
    return {};
}

QString PresetStore::deleteUserPreset(const QString& kind, const QString& name)
{
    const BundleLocation bundle = locateBundle(m_currentVendorId);
    if (!bundle.valid())
        return QStringLiteral("No preset bundle is loaded for the current printer");

    const QString path = presetPathFor(bundle, kind, name);
    if (!QFileInfo::exists(path))
        return QStringLiteral("%1 is not a user preset").arg(name);
    if (!QFile::remove(path))
        return QStringLiteral("Could not remove %1").arg(path);
    return {};
}

bool PresetStore::presetIsUser(const QString& kind, const QString& name) const
{
    const BundleLocation bundle = locateBundle(m_currentVendorId);
    if (!bundle.valid() || !validPresetName(name))
        return false;
    return QFileInfo::exists(presetPathFor(bundle, kind, name));
}

QString PresetStore::exportPreset(const QString& kind, const QString& name,
                                  const QString& path) const
{
    if (path.isEmpty())
        return QStringLiteral("No file to write");

    const BundleLocation bundle = locateBundle(m_currentVendorId);
    if (!bundle.valid())
        return QStringLiteral("No preset bundle is loaded for the current printer");

    const QString source = presetPathFor(bundle, kind, name);
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly))
        return QStringLiteral("%1 is not a user preset").arg(name);
    const QByteArray body = input.readAll();
    input.close();

    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Could not write %1").arg(path);
    if (output.write(body) != body.size()) {
        output.close();
        return QStringLiteral("Could not write all of %1").arg(path);
    }
    return {};
}

QString PresetStore::importPreset(const QString& kind, const QString& path)
{
    const BundleLocation bundle = locateBundle(m_currentVendorId);
    if (!bundle.valid())
        return QStringLiteral("No preset bundle is loaded for the current printer");

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not read %1").arg(path);
    const QString body = QString::fromUtf8(input.readAll());
    input.close();

    // The name and the id are the only two fields an import has to touch: the
    // id must be fresh or it would collide with the copy already on this
    // machine, and the name must not overwrite a preset the user owns.
    QString candidate;
    for (const QString& line : body.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("name:")) && !line.startsWith(QLatin1Char(' '))) {
            QString value = line.mid(5).trimmed();
            if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                value = value.mid(1, value.size() - 2);
            candidate = value;
            break;
        }
    }
    if (!validPresetName(candidate))
        return QStringLiteral("%1 carries no preset name").arg(path);

    QString name = candidate;
    for (int suffix = 1; QFileInfo::exists(presetPathFor(bundle, kind, name)); ++suffix)
        name = suffix == 1 ? QStringLiteral("%1 (imported)").arg(candidate)
                           : QStringLiteral("%1 (imported %2)").arg(candidate).arg(suffix);

    QStringList lines;
    for (const QString& line : body.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("name:")) && !line.startsWith(QLatin1Char(' ')))
            lines.append(QStringLiteral("name: ") + next_layer::yaml::quoted(name));
        else if (line.startsWith(QLatin1String("id:")) && !line.startsWith(QLatin1Char(' ')))
            lines.append(QStringLiteral("id: ") + freshId());
        else
            lines.append(line);
    }

    const QString target = presetPathFor(bundle, kind, name);
    if (!writeFile(target, lines.join(QLatin1Char('\n'))))
        return QStringLiteral("Could not write %1").arg(target);

    m_importedPresetName = name;
    return {};
}

QString PresetStore::exportConfiguration(const QString& path) const
{
    if (m_settings == nullptr)
        return QStringLiteral("The settings model is not available");
    if (path.isEmpty())
        return QStringLiteral("No file to write");

    const QByteArray body = m_settings->configJson().toUtf8();
    if (body.isEmpty())
        return QStringLiteral("There is no configuration to export");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Could not write %1").arg(path);
    if (file.write(body) != body.size()) {
        file.close();
        return QStringLiteral("Could not write all of %1").arg(path);
    }
    file.close();
    return {};
}

QString PresetStore::importConfiguration(const QString& path)
{
    if (m_settings == nullptr)
        return QStringLiteral("The settings model is not available");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not read %1").arg(path);
    const QString body = QString::fromUtf8(file.readAll());
    file.close();

    if (!m_settings->seedFromConfigJson(body))
        return QStringLiteral("%1 is not a configuration this app can read").arg(path);
    return {};
}

QString PresetStore::createPrinter(const QString& name, double bedWidth, double bedHeight,
                                   double bedDepth)
{
    if (!validPresetName(name))
        return QStringLiteral("A printer name cannot be empty or contain a slash");
    if (m_settings == nullptr)
        return QStringLiteral("The settings model is not available");
    if (bedWidth <= 0.0 || bedHeight <= 0.0 || bedDepth <= 0.0)
        return QStringLiteral("The bed size and height must be positive");

    const QString dir = presetsRoot() + QStringLiteral("/local/") + QLatin1String(kCustomRepo)
                      + QLatin1Char('/') + QLatin1String(kCustomVendor);
    const QString manifestPath = dir + QStringLiteral("/printers.json");

    QJsonArray manifest;
    if (QFileInfo::exists(manifestPath)) {
        QFile file(manifestPath);
        if (file.open(QIODevice::ReadOnly))
            manifest = QJsonDocument::fromJson(file.readAll()).object()
                .value(QStringLiteral("printers")).toArray();
    }

    QStringList slugs;
    bool known = false;
    QString slug;
    for (const QJsonValue& value : manifest) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("name")).toString() == name) {
            known = true;
            slug = entry.value(QStringLiteral("slug")).toString();
        }
        slugs.append(entry.value(QStringLiteral("slug")).toString());
    }

    if (!known) {
        slug = slugOf(name);
        if (slug.isEmpty())
            return QStringLiteral("The name needs at least one letter or digit");
        const QString baseSlug = slug;
        int suffix = 2;
        while (slugs.contains(slug))
            slug = baseSlug + QLatin1Char('-') + QString::number(suffix++);
    }

    const QVariantList bed = {
        QVariantList{0.0, 0.0}, QVariantList{bedWidth, 0.0},
        QVariantList{bedWidth, bedHeight}, QVariantList{0.0, bedHeight}
    };
    m_settings->setValue(QStringLiteral("bed_shape"), bed);
    m_settings->setValue(QStringLiteral("max_print_height"), bedDepth);

    const auto writePreset = [&dir](const QString& fileName, const QString& kind,
                                    const QString& presetName, const QString& condition,
                                    const QString& values) {
        const QString path = dir + QLatin1Char('/') + fileName;
        return writeFile(path, presetDocument(kind, presetName, QString(), condition, values,
                                             presetIdIn(path)));
    };

    const QString condition = QStringLiteral("printer.model == \"NL_") + slug + QStringLiteral("_MODEL\"");
    QStringList printerValues = m_settings->valuesYaml(QStringLiteral("printer"), false)
                                    .split(QLatin1Char('\n'));
    // The engine parses a numeric-looking YAML scalar as a double, so the
    // string item printer_variant cannot round-trip through a preset and only
    // produces a type-mismatch error per evaluation.
    printerValues.removeIf([](const QString& line) {
        return line.startsWith(QLatin1String("  printer_variant:"));
    });
    if (!writePreset(QStringLiteral("preset-printer-") + slug + QStringLiteral(".yaml"),
                     QStringLiteral("printer"), name, condition,
                     printerValues.join(QLatin1Char('\n'))))
        return QStringLiteral("Could not write the printer preset");

    // The process, tool and filament presets carry the settings this bundle was
    // made with, so a machine that is added again refreshes them along with its
    // own printer preset instead of keeping whatever the first attempt wrote.
    if (!writePreset(QStringLiteral("preset-print-standard.yaml"), QStringLiteral("print"),
                     QStringLiteral("Standard"), QString(),
                     m_settings->valuesYaml(QStringLiteral("process"), false)))
        return QStringLiteral("Could not write the process preset");

    if (!writePreset(QStringLiteral("preset-tool-standard.yaml"), QStringLiteral("tool_print"),
                     QStringLiteral("Standard"), QString(), QString()))
        return QStringLiteral("Could not write the tool preset");

    if (!writePreset(QStringLiteral("preset-filament-generic.yaml"), QStringLiteral("filament"),
                     QStringLiteral("Generic"), QString(),
                     m_settings->valuesYaml(QStringLiteral("filament"), false)))
        return QStringLiteral("Could not write the filament preset");

    QJsonObject entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("slug"), slug);
    if (!known)
        manifest.append(entry);

    QJsonObject root;
    root.insert(QStringLiteral("printers"), manifest);
    if (!writeFile(manifestPath, QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented))))
        return QStringLiteral("Could not write the printer manifest");

    QString vendorYaml = QStringLiteral(
        "kind: vendor\nid: Custom\nname: Custom\nversion: 1.0.0\n"
        "features:\n  printer: {}\n  tool: {}\n  feeder: {}\n  sheet: {}\nprinter_families:\n");
    for (const QJsonValue& value : manifest) {
        const QJsonObject printer = value.toObject();
        vendorYaml += QStringLiteral("- label: ") + next_layer::yaml::quoted(printer.value(QStringLiteral("name")).toString())
                   + QStringLiteral("\n  base_model: NL_") + printer.value(QStringLiteral("slug")).toString()
                   + QLatin1Char('\n');
    }
    vendorYaml += QStringLiteral(
        "---\nkind: tool\ntechnology: FFF\nid: '0.4'\nname: '0.4'\n"
        "features:\n  nozzle_diameter:\n    default: 0.4\n"
        "---\nkind: sheet\nid: pei_smooth\nname: PEI Smooth\ntype: pei_smooth\n");

    for (const QJsonValue& value : manifest) {
        const QJsonObject printer = value.toObject();
        const QString printerName = next_layer::yaml::quoted(printer.value(QStringLiteral("name")).toString());
        const QString printerSlug = printer.value(QStringLiteral("slug")).toString();
        vendorYaml += QStringLiteral("---\nkind: printer\ntechnology: FFF\nname: ") + printerName
                   + QStringLiteral("\nid: NL_") + printerSlug
                   + QStringLiteral("\nmodel:\n  base_model: NL_") + printerSlug
                   + QStringLiteral("\n  model: NL_") + printerSlug
                   + QStringLiteral("_MODEL\ntool_count: 1\nfeatures: {}\n")
                   + QStringLiteral("---\nkind: printer_config\nid: nl-") + printerSlug
                   + QStringLiteral("\nname: ") + printerName
                   + QStringLiteral("\nprinter: NL_") + printerSlug
                   + QStringLiteral("\nlegacy_printer_model:\n- ") + printerName
                   + QStringLiteral("\ntool_count: 1\ntools:\n- tool: '0.4'\nsheet: pei_smooth\n");
    }

    if (!writeFile(dir + QStringLiteral("/vendor.yaml"), vendorYaml))
        return QStringLiteral("Could not write the vendor bundle");

    return {};
}
