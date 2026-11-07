// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Host.hpp"

#include "Palette.hpp"
#include "SliceRuntime.hpp"

#include <boost/filesystem/path.hpp>

#include <QFile>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <Slic3r/Biz/FileLoadingLogic.hpp>

next_layer::Host::Host(const Palette& palette, QVariantList bedShape, double maxPrintHeight,
                       QVariantList focusWorld, double modelRadius,
                       QString printerModel, QVector3D bedMin, QVector3D bedMax,
                       bool bedPlateLoaded, QObject* parent)
    : QObject(parent)
    , m_palette(palette.roles())
    , m_paletteSource(palette.source())
    , m_bedShape(std::move(bedShape))
    , m_maxPrintHeight(maxPrintHeight)
    , m_focusWorld(std::move(focusWorld))
    , m_modelRadius(modelRadius)
    , m_printerModel(std::move(printerModel))
    , m_bedMin(bedMin)
    , m_bedMax(bedMax)
    , m_bedPlateLoaded(bedPlateLoaded)
{
    // The plate's framing is fixed: the constructor's focus and radius are the
    // bed's, and setModel() later overwrites them with the model's. Kept apart
    // so a moved model never drags the camera with it.
    m_bedCentreWorld = m_focusWorld;
    m_bedRadius = modelRadius;
}

QString next_layer::Host::downloadFolder() const
{
    // `Downloads` and `downloads` are different directories on a case-sensitive
    // filesystem, and only one of them holds the files. The one with more models
    // in it wins; the system answer is the tie-breaker.
    QStringList candidates;
    candidates << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString home = QDir::homePath();
    candidates << home + QStringLiteral("/Downloads") << home + QStringLiteral("/downloads");

    QStringList patterns;
    for (const std::string& extension : Slic3r::Biz::FileLoadingLogic::get_import_extensions()) {
        const QString bare = QString::fromStdString(extension);
        patterns << QStringLiteral("*") + bare << QStringLiteral("*") + bare.toUpper();
    }

    QString best;
    int bestCount = -1;
    QStringList seen;
    for (const QString& candidate : candidates) {
        if (candidate.isEmpty() || seen.contains(candidate) || !QFileInfo(candidate).isDir())
            continue;
        seen << candidate;
        const int count = QDir(candidate).entryList(patterns, QDir::Files).size();
        if (count > bestCount) {
            bestCount = count;
            best = candidate;
        }
    }
    return best.isEmpty() ? QDir::homePath() : best;
}

bool next_layer::Host::copyFile(const QString& source, const QString& target) const
{
    const QString from = source.startsWith(QStringLiteral("file:"))
        ? QUrl(source).toLocalFile() : source;
    const QString to = target.startsWith(QStringLiteral("file:"))
        ? QUrl(target).toLocalFile() : target;
    return QFile::copy(from, to);
}

void next_layer::Host::setModel(QString path, QVariantList focusWorld, double modelRadius)
{
    if (m_modelPath == path) {
        m_focusWorld = std::move(focusWorld);
        m_modelRadius = modelRadius;
        emit framingChanged();
        return;
    }

    m_modelPath = std::move(path);
    m_focusWorld = std::move(focusWorld);
    m_modelRadius = modelRadius;
    emit modelPathChanged();
    emit framingChanged();
}

void next_layer::Host::setPrinter(QVariantList bedShape, double maxPrintHeight,
                                  QString printerModel, QVector3D bedMin, QVector3D bedMax,
                                  double bedRadius, QVariantList bedCentreWorld)
{
    m_bedShape = std::move(bedShape);
    m_maxPrintHeight = maxPrintHeight;
    m_printerModel = std::move(printerModel);
    m_bedMin = bedMin;
    m_bedMax = bedMax;
    m_bedRadius = bedRadius;
    m_bedCentreWorld = std::move(bedCentreWorld);
    emit bedChanged();
}

void next_layer::Host::setBedPlateLoaded(bool loaded)
{
    if (m_bedPlateLoaded == loaded)
        return;
    m_bedPlateLoaded = loaded;
    emit bedPlateLoadedChanged();
}
