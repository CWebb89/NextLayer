// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "HfwEngine.hpp"

#include <Slic3r/Biz/Format/STL.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

namespace next_layer {
namespace {

QString developmentBinary()
{
    return QStringLiteral("/home/cwebb/dev/HFW/HFW-final/target/release/hfw-cli");
}

QString tail(const QString& text, int lines)
{
    const QStringList all = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const int first = std::max(0, int(all.size()) - lines);
    return all.mid(first).join(QLatin1Char('\n')).trimmed();
}

/// One setting as the text a preset writes it: `50%` for a percentage, `1`/`0`
/// for a flag, a comma-joined list for a vector.
QString settingText(const QJsonValue& value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 10);
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (!object.contains(QLatin1String("value")))
            return {};
        const QString number = settingText(object.value(QLatin1String("value")));
        return object.value(QLatin1String("is_percent")).toBool() ? number + QLatin1Char('%')
                                                                 : number;
    }
    if (value.isArray()) {
        QStringList parts;
        const QJsonArray items = value.toArray();
        for (const QJsonValue& item : items)
            parts.append(settingText(item));
        return parts.join(QLatin1Char(','));
    }
    return {};
}

/// Writes the per-object settings the plate holds, in the shape
/// `hfw-cli --object-settings` reads: one `<input> <setting> = <value>` line each,
/// the input being the 1-based order the models are handed over in. A plate built
/// from separate models carries these values here and nowhere else, so without
/// this file every object is sliced with the plate's config and the per-object
/// panel does nothing. Empty when no object overrides anything.
QString writeObjectSettings(const QTemporaryDir& work, const QVector<PlateSliceObject>& objects)
{
    QString text;
    for (int index = 0; index < objects.size(); ++index) {
        const PlateSliceObject& object = objects.at(index);
        QHash<QString, QString> overrides;
        const QJsonObject settings = QJsonDocument::fromJson(object.settingsJson).object();
        for (auto entry = settings.constBegin(); entry != settings.constEnd(); ++entry) {
            const QString value = settingText(entry.value());
            if (!value.isEmpty())
                overrides.insert(entry.key(), value);
        }
        // Settings an object was imported with are kept in the dialect its file
        // used, because they are that file's words. They are the object's overrides
        // all the same, and the engine reads both dialects, so they go out too; a
        // name it does not know is reported there rather than dropped here, where
        // nothing knows which names it has.
        for (auto entry = object.foreignSettings.constBegin();
             entry != object.foreignSettings.constEnd(); ++entry)
            overrides.insert(entry.key(), entry.value());

        QStringList names = overrides.keys();
        names.sort();
        for (const QString& name : names)
            text += QStringLiteral("%1 %2 = %3\n").arg(index + 1).arg(name, overrides.value(name));
    }
    if (text.isEmpty())
        return {};

    const QString path = work.filePath(QStringLiteral("object-settings.txt"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write(text.toUtf8());
    file.close();
    return path;
}

} // namespace

bool hfwRequested()
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    return environment.value(QStringLiteral("NEXT_LAYER_ENGINE")).compare(
               QStringLiteral("hfw"), Qt::CaseInsensitive)
        == 0;
}

QString hfwBinary()
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString configured = environment.value(QStringLiteral("NEXT_LAYER_HFW"));
    if (!configured.isEmpty())
        return QFileInfo::exists(configured) ? configured : QString();

    const QString found = QStandardPaths::findExecutable(QStringLiteral("hfw-cli"));
    if (!found.isEmpty())
        return found;

    return QFileInfo::exists(developmentBinary()) ? developmentBinary() : QString();
}

bool sliceWithHfw(const QVector<PlateSliceObject>& objects, const QString& configPath,
                  const QString& gcodePath, QString& error)
{
    if (objects.isEmpty()) {
        error = QStringLiteral("no object on the plate");
        return false;
    }
    const QString binary = hfwBinary();
    if (binary.isEmpty()) {
        error = QStringLiteral("no hfw-cli found: set NEXT_LAYER_HFW to its path");
        return false;
    }

    QTemporaryDir work;
    if (!work.isValid()) {
        error = QStringLiteral("could not create a scratch directory");
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("--preset") << configPath;
    for (int index = 0; index < objects.size(); ++index) {
        // The engine names an object after the file it came from, and that name is
        // what its report on an override calls it. The object's own name is the one
        // the user knows it by; anything a file name cannot hold is folded to `_`.
        QString name = objects.at(index).name;
        for (QChar& character : name) {
            if (!character.isLetterOrNumber() && character != QLatin1Char('-'))
                character = QLatin1Char('_');
        }
        if (name.isEmpty())
            name = QStringLiteral("object-%1").arg(index);
        const QString stl = work.filePath(QStringLiteral("%1.stl").arg(name));
        if (!Slic3r::Biz::store_stl(stl.toStdString(), objects[index].mesh, true, "next-layer")) {
            error = QStringLiteral("could not write object %1 to STL").arg(index + 1);
            return false;
        }
        arguments << stl;
    }
    arguments << QStringLiteral("--no-arrange")
              << QStringLiteral("--gcode") << gcodePath;

    const QString settings = writeObjectSettings(work, objects);
    if (!settings.isEmpty())
        arguments << QStringLiteral("--object-settings") << settings;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(binary, arguments);
    if (!process.waitForStarted(15000)) {
        error = QStringLiteral("could not start %1").arg(binary);
        return false;
    }
    if (!process.waitForFinished(-1)) {
        process.kill();
        error = QStringLiteral("%1 did not finish").arg(binary);
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    // What the engine did with the per-object settings, which is otherwise the one
    // thing about a slice this app cannot show: the plate's own panel does not know
    // which of its overrides the engine took.
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QLatin1String("override")))
            std::fprintf(stderr, "%s\n", line.trimmed().toUtf8().constData());
    }
    const bool wrote = QFileInfo(gcodePath).size() > 0;
    const bool verified = output.contains(QStringLiteral("verify RESULT PASS"));
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && wrote && verified)
        return true;

    if (!wrote) {
        error = QStringLiteral("hfw wrote no G-code: %1").arg(tail(output, 6));
        return false;
    }
    if (!verified) {
        error = QStringLiteral("the file failed HFW's own verifier: %1")
                    .arg(tail(output, 6));
        return false;
    }
    error = tail(output, 6);
    return false;
}

} // namespace next_layer
