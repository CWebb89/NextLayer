// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "SliceRuntime.hpp"
#include "Scratch.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>
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
    /// Whether the run that just finished sliced every plate rather than the active
    /// one. The window needs the difference: a single slice owns the active plate's
    /// toolpath, a slice-all leaves one per plate and the active plate's is the one
    /// to show.
    Q_PROPERTY(bool slicedAll READ slicedAll NOTIFY busyChanged)
    /// Bumped whenever a plate's toolpath changes, so the plate list re-reads
    /// `plateGcode` instead of caching what it saw first.
    Q_PROPERTY(int gcodeVersion READ gcodeVersion NOTIFY plateGcodesChanged)

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
    bool slicedAll() const { return m_sliceAll; }
    int gcodeVersion() const { return m_gcodeVersion; }
    /// The G-code one plate was last sliced to, or an empty string. A project's
    /// plates each keep their own toolpath: switching to a plate shows what that
    /// plate was sliced to, not the last run's output.
    Q_INVOKABLE QString plateGcode(int plate) const { return m_plateGcodes.value(plate); }
    /// Keeps the per-plate toolpaths lined up with the plate list when a plate is
    /// added (delta +1) or removed (delta -1) at `at`. Without this, deleting the
    /// second plate would leave the third plate showing the fourth one's toolpath.
    Q_INVOKABLE void shiftPlateGcodes(int at, int delta);
    Q_INVOKABLE void duplicatePlateGcode(int from, int at);

    Q_INVOKABLE void slice();
    /// Slices every plate in the document, one after another, and reports each
    /// result. One worker doing them in order is what a printer with one nozzle
    /// wants: the numbers in the footer are the whole job, and each plate's
    /// toolpath is kept so switching to it shows what was sliced.
    Q_INVOKABLE void sliceAll();
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
    /// One plate of a slice-all run: which plate, where its G-code went, and what
    /// it holds. Emitted once per plate, in plate order.
    void plateSliced(int plate, const QString& gcodePath, int layerCount, int moveCount,
                     double filamentGrams, const QString& estimatedTime);
    void plateGcodesChanged();

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
    std::unique_ptr<QTemporaryDir> m_tempDir = next_layer::makeScratchDir();
    QString m_configPath;
    std::thread m_worker;
    int m_sliceIndex = 0;
    bool m_busy = false;
    bool m_sliceAll = false;
    QHash<int, QString> m_plateGcodes;
    int m_gcodeVersion = 0;
    QString m_status;
    QString m_gcodePath;
    double m_elapsedMs = 0.0;
    int m_layerCount = 0;
    int m_moveCount = 0;
    double m_filamentGrams = 0.0;
    QString m_estimatedTime;
};
