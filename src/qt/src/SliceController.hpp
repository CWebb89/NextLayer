#pragma once

#include "SliceRuntime.hpp"

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <string>
#include <thread>

class SettingsModel;
class PlateDocument;

class SliceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString gcodePath READ gcodePath NOTIFY gcodePathChanged)
    Q_PROPERTY(double elapsedMs READ elapsedMs NOTIFY elapsedMsChanged)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY layerCountChanged)
    Q_PROPERTY(int moveCount READ moveCount NOTIFY moveCountChanged)
    Q_PROPERTY(double filamentGrams READ filamentGrams NOTIFY moveCountChanged)
    Q_PROPERTY(QString estimatedTime READ estimatedTime NOTIFY moveCountChanged)

public:
    explicit SliceController(std::string modelPath, SettingsModel* settings, QObject* parent = nullptr);
    ~SliceController() override;

    bool busy() const;
    QString status() const;
    QString gcodePath() const;
    double elapsedMs() const;
    int layerCount() const;
    int moveCount() const;
    /// What the engine wrote into the G-code's own statistics: the weight of
    /// filament and the printing time it estimates for the plate.
    double filamentGrams() const { return m_filamentGrams; }
    QString estimatedTime() const { return m_estimatedTime; }

    Q_INVOKABLE void slice();
    void setModelPath(std::string modelPath);
    void setDocument(PlateDocument* document) { m_document = document; }

signals:
    void busyChanged();
    void statusChanged();
    void gcodePathChanged();
    void elapsedMsChanged();
    void layerCountChanged();
    void moveCountChanged();
    void finished();
    void failed(const QString& message);

private:
    void publishResult(bool ok,
                       const QString& message,
                       const QString& gcodePath,
                       double elapsedMs,
                       int layerCount,
                       int moveCount,
                       double filamentGrams,
                       const QString& estimatedTime);

    std::string m_modelPath;
    SettingsModel* m_settings;
    PlateDocument* m_document = nullptr;
    next_layer::SliceRuntime m_runtime;
    QTemporaryDir m_tempDir;
    QString m_configPath;
    std::thread m_worker;
    int m_sliceIndex = 0;
    bool m_busy = false;
    QString m_status;
    QString m_gcodePath;
    double m_elapsedMs = 0.0;
    int m_layerCount = 0;
    int m_moveCount = 0;
    double m_filamentGrams = 0.0;
    QString m_estimatedTime;
};
