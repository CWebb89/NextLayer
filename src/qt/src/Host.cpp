// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Host.hpp"

#include "Palette.hpp"
#include "SliceRuntime.hpp"

#include <boost/filesystem/path.hpp>

#include <QFile>

#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

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
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
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
