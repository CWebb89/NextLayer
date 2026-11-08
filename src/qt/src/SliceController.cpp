// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "SliceController.hpp"

#include "GcodeBrand.hpp"
#include "PlateDocument.hpp"
#include "SettingsModel.hpp"
#include "SliceRuntime.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "Scratch.hpp"
#include <QList>
#include <QMetaObject>

#include <chrono>

namespace {

struct GcodeCounts
{
    int layers = 0;
    int moves = 0;
    double grams = 0.0;
    QString time;
};

void countGcode(const QString& path, GcodeCounts& counts)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.startsWith(";LAYER_CHANGE")) {
            ++counts.layers;
            continue;
        }
        // The engine writes its own statistics into the file; taking them from
        // there rather than recomputing keeps the footer honest.
        if (line.startsWith("; total filament used [g] = ")) {
            counts.grams = line.mid(27).trimmed().toDouble();
            continue;
        }
        if (line.startsWith("; estimated printing time (normal mode) = ")) {
            counts.time = QString::fromUtf8(line.mid(40).trimmed());
            continue;
        }
        if (!line.startsWith("G1 "))
            continue;
        const QList<QByteArray> words = line.split(' ');
        for (const QByteArray& word : words) {
            if (word.size() > 1 && word.startsWith('E')) {
                ++counts.moves;
                break;
            }
        }
    }
}

QString modelName(const QString& path)
{
    return QFileInfo(path).fileName();
}

}

SliceController::SliceController(std::string modelPath, SettingsModel* settings, QObject* parent)
    : QObject(parent)
    , m_modelPath(std::move(modelPath))
    , m_settings(settings)
    , m_configPath(m_tempDir->filePath(QStringLiteral("config.json")))
{
}

SliceController::~SliceController()
{
    if (m_worker.joinable())
        m_worker.join();
}

bool SliceController::busy() const
{
    return m_busy;
}

QString SliceController::exportProject3mf(const QString& path)
{
    if (m_document == nullptr)
        return QStringLiteral("There is nothing on the plate");

    // A file dialog hands over a URL, and a person typing a path into a shell does
    // not; both mean the same file.
    const QString target = path.startsWith(QLatin1String("file:")) ? QUrl(path).toLocalFile() : path;
    if (target.isEmpty())
        return QStringLiteral("There is no file to write");

    const QVector<next_layer::PlateSliceObject> objects = m_document->projectObjects();
    if (objects.isEmpty())
        return QStringLiteral("There is nothing on the plate");

    const QByteArray json = m_settings->configJson().toUtf8();
    QFile config(m_configPath);
    const bool written = config.open(QIODevice::WriteOnly | QIODevice::Truncate)
                      && config.write(json) == json.size();
    config.close();
    if (!written)
        return QStringLiteral("Could not write the settings file");

    QString engineError;
    if (!m_runtime.exportPlate3mf(objects, m_configPath, target, engineError,
                                 m_document->layerGcodeList()))
        return QStringLiteral("Export failed: %1").arg(engineError);
    return {};
}

QString SliceController::status() const
{
    return m_status;
}

QString SliceController::gcodePath() const
{
    return m_gcodePath;
}

double SliceController::elapsedMs() const
{
    return m_elapsedMs;
}

int SliceController::layerCount() const
{
    return m_layerCount;
}

int SliceController::moveCount() const
{
    return m_moveCount;
}

void SliceController::setModelPath(std::string modelPath)
{
    if (m_busy)
        return;
    m_modelPath = std::move(modelPath);
}

void SliceController::slice()
{
    if (m_busy)
        return;

    if (m_modelPath.empty()) {
        m_status = QStringLiteral("Slice failed: no model is open");
        emit statusChanged();
        emit failed(m_status);
        return;
    }

    if (m_worker.joinable())
        m_worker.join();

    const QByteArray json = m_settings->configJson().toUtf8();
    QFile config(m_configPath);
    const bool written = config.open(QIODevice::WriteOnly | QIODevice::Truncate)
                      && config.write(json) == json.size();
    config.close();
    if (!written) {
        m_status = QStringLiteral("Slice failed: could not write the settings file");
        emit statusChanged();
        emit failed(m_status);
        return;
    }

    const QString modelPath = QString::fromStdString(m_modelPath);
    const QString outputPath = m_tempDir->filePath(QStringLiteral("slice-%1.gcode").arg(++m_sliceIndex));
    const int plate = m_document != nullptr ? m_document->activePlate() : 0;

    m_sliceAll = false;
    m_busy = true;
    emit busyChanged();
    m_status = QStringLiteral("Slicing %1").arg(modelName(modelPath));
    emit statusChanged();

    const QVector<next_layer::PlateSliceObject> objects =
        m_document != nullptr ? m_document->sliceObjects()
                              : QVector<next_layer::PlateSliceObject>();

    m_worker = std::thread([this, modelPath, outputPath, objects, plate] {
        const auto started = std::chrono::steady_clock::now();

        QString engineError;
        const QVector<next_layer::PlateLayerGcode> layerGcodes =
            m_document != nullptr ? m_document->layerGcodeList()
                                  : QVector<next_layer::PlateLayerGcode>();
        const bool sliced = !objects.empty()
            ? m_runtime.slicePlate(objects, m_configPath, outputPath, engineError, layerGcodes)
            : m_runtime.slice(modelPath, m_configPath, outputPath, engineError);

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

        const QFileInfo gcode(outputPath);
        GcodeCounts counts;
        if (gcode.exists() && gcode.size() > 0)
            countGcode(outputPath, counts);
        const int layers = counts.layers;
        const int moves = counts.moves;

        QString message;
        bool ok = true;
        if (!sliced) {
            ok = false;
            message = QStringLiteral("Slice failed: %1").arg(engineError);
        } else if (!gcode.exists() || gcode.size() == 0) {
            ok = false;
            message = QStringLiteral("Slice failed: the engine wrote no G-code");
        } else {
            next_layer::brandGcode(outputPath);
            message = QStringLiteral("Sliced %1: %2 layers, %3 moves")
                          .arg(modelName(modelPath))
                          .arg(layers)
                          .arg(moves);
        }

        QMetaObject::invokeMethod(
            this,
            [this, ok, message, outputPath, elapsedMs, layers, moves, counts, plate] {
                if (ok) {
                    m_plateGcodes.insert(plate, outputPath);
                    ++m_gcodeVersion;
                    emit plateGcodesChanged();
                }
                publishResult(ok, message, outputPath, elapsedMs, layers, moves,
                              counts.grams, counts.time);
            },
            Qt::QueuedConnection
        );
    });
}

void SliceController::sliceAll()
{
    if (m_busy)
        return;

    if (m_document == nullptr) {
        m_status = QStringLiteral("Slice failed: no plate is open");
        emit statusChanged();
        emit failed(m_status);
        return;
    }

    if (m_worker.joinable())
        m_worker.join();

    const QByteArray json = m_settings->configJson().toUtf8();
    QFile config(m_configPath);
    const bool written = config.open(QIODevice::WriteOnly | QIODevice::Truncate)
                      && config.write(json) == json.size();
    config.close();
    if (!written) {
        m_status = QStringLiteral("Slice failed: could not write the settings file");
        emit statusChanged();
        emit failed(m_status);
        return;
    }

    const int plates = m_document->plateCount();
    const QVector<next_layer::PlateLayerGcode> layerGcodes = m_document->layerGcodeList();
    QVector<QVector<next_layer::PlateSliceObject>> perPlate;
    perPlate.reserve(plates);
    int empty = 0;
    for (int plate = 0; plate < plates; ++plate) {
        QVector<next_layer::PlateSliceObject> objects = m_document->sliceObjectsFor(plate);
        if (objects.isEmpty())
            ++empty;
        perPlate.append(std::move(objects));
    }
    if (empty == plates) {
        m_status = QStringLiteral("Slice failed: every plate is empty");
        emit statusChanged();
        emit failed(m_status);
        return;
    }

    const int run = ++m_sliceIndex;
    m_sliceAll = true;
    m_busy = true;
    emit busyChanged();
    m_status = QStringLiteral("Slicing %1 plates").arg(plates - empty);
    emit statusChanged();

    m_worker = std::thread([this, run, plates, perPlate, layerGcodes] {
        const auto started = std::chrono::steady_clock::now();
        QStringList failures;
        int sliced = 0;
        int totalLayers = 0;
        int totalMoves = 0;
        double totalGrams = 0.0;
        QString lastPath;

        for (int plate = 0; plate < plates; ++plate) {
            if (perPlate[plate].isEmpty())
                continue;

            const QString outputPath =
                m_tempDir->filePath(QStringLiteral("slice-%1-plate-%2.gcode").arg(run).arg(plate + 1));
            QString engineError;
            const bool ok = m_runtime.slicePlate(perPlate[plate], m_configPath, outputPath,
                                                 engineError, layerGcodes);
            const QFileInfo gcode(outputPath);
            GcodeCounts counts;
            if (gcode.exists() && gcode.size() > 0)
                countGcode(outputPath, counts);

            if (!ok || !gcode.exists() || gcode.size() == 0) {
                failures.append(QStringLiteral("plate %1: %2")
                                    .arg(plate + 1)
                                    .arg(ok ? QStringLiteral("the engine wrote no G-code")
                                            : engineError));
                continue;
            }

            next_layer::brandGcode(outputPath);
            ++sliced;
            totalLayers += counts.layers;
            totalMoves += counts.moves;
            totalGrams += counts.grams;
            lastPath = outputPath;

            QMetaObject::invokeMethod(
                this,
                [this, plate, outputPath, counts] {
                    m_plateGcodes.insert(plate, outputPath);
                    ++m_gcodeVersion;
                    emit plateGcodesChanged();
                    emit plateSliced(plate, outputPath, counts.layers, counts.moves, counts.grams,
                                     counts.time);
                },
                Qt::QueuedConnection
            );
        }

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const bool ok = sliced > 0;
        QString message;
        if (sliced == 0) {
            message = QStringLiteral("Slice failed: %1").arg(failures.join(QStringLiteral("; ")));
        } else if (!failures.isEmpty()) {
            message = QStringLiteral("Sliced %1 of %2 plates: %3 layers, %4 moves, %5 g")
                          .arg(sliced)
                          .arg(plates)
                          .arg(totalLayers)
                          .arg(totalMoves)
                          .arg(totalGrams, 0, 'f', 1);
        } else {
            message = QStringLiteral("Sliced all %1 plates: %2 layers, %3 moves, %4 g in %5 s")
                          .arg(sliced)
                          .arg(totalLayers)
                          .arg(totalMoves)
                          .arg(totalGrams, 0, 'f', 1)
                          .arg(elapsedMs / 1000.0, 0, 'f', 1);
        }

        QMetaObject::invokeMethod(
            this,
            [this, ok, message, lastPath, elapsedMs, totalLayers, totalMoves, totalGrams] {
                publishResult(ok, message, lastPath, elapsedMs, totalLayers, totalMoves,
                              totalGrams, QString());
            },
            Qt::QueuedConnection
        );
    });
}

void SliceController::shiftPlateGcodes(int at, int delta)
{
    if (at < 0 || (delta != 1 && delta != -1))
        return;

    QHash<int, QString> shifted;
    for (auto it = m_plateGcodes.constBegin(); it != m_plateGcodes.constEnd(); ++it) {
        int plate = it.key();
        if (delta < 0) {
            if (plate == at)
                continue;
            if (plate > at)
                --plate;
        } else if (plate >= at) {
            ++plate;
        }
        shifted.insert(plate, it.value());
    }

    m_plateGcodes = shifted;
    ++m_gcodeVersion;
    emit plateGcodesChanged();
}

void SliceController::duplicatePlateGcode(int from, int at)
{
    if (from < 0 || at < 0 || from == at)
        return;

    const QString source = m_plateGcodes.value(from);
    shiftPlateGcodes(at, 1);
    if (!source.isEmpty())
        m_plateGcodes.insert(from > at ? from + 1 : from, source);

    ++m_gcodeVersion;
    emit plateGcodesChanged();
}

void SliceController::publishResult(bool ok,
                                    const QString& message,
                                    const QString& gcodePath,
                                    double elapsedMs,
                                    int layerCount,
                                    int moveCount,
                                    double filamentGrams,
                                    const QString& estimatedTime)
{
    m_busy = false;
    emit busyChanged();

    m_status = message;
    emit statusChanged();

    m_gcodePath = ok ? gcodePath : QString();
    emit gcodePathChanged();

    m_elapsedMs = elapsedMs;
    emit elapsedMsChanged();

    m_layerCount = ok ? layerCount : 0;
    emit layerCountChanged();

    m_moveCount = ok ? moveCount : 0;
    m_filamentGrams = ok ? filamentGrams : 0.0;
    m_estimatedTime = ok ? estimatedTime : QString();
    emit moveCountChanged();

    if (ok)
        emit finished();
    else
        emit failed(message);
}
