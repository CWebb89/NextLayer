#include "SliceController.hpp"

#include "GcodeBrand.hpp"
#include "PlateDocument.hpp"
#include "SettingsModel.hpp"
#include "SliceRuntime.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
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
    , m_configPath(m_tempDir.filePath(QStringLiteral("config.json")))
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
    const QString outputPath = m_tempDir.filePath(QStringLiteral("slice-%1.gcode").arg(++m_sliceIndex));

    m_busy = true;
    emit busyChanged();
    m_status = QStringLiteral("Slicing %1").arg(modelName(modelPath));
    emit statusChanged();

    const QVector<next_layer::PlateSliceObject> objects =
        m_document != nullptr ? m_document->sliceObjects()
                              : QVector<next_layer::PlateSliceObject>();

    m_worker = std::thread([this, modelPath, outputPath, objects] {
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
            [this, ok, message, outputPath, elapsedMs, layers, moves, counts] {
                publishResult(ok, message, outputPath, elapsedMs, layers, moves,
                              counts.grams, counts.time);
            },
            Qt::QueuedConnection
        );
    });
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
