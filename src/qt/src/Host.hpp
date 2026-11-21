// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "Palette.hpp"

#include "next-layer-app/src/bridge.rs.h"

#include <QDir>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

#include <string>
#include <utility>

namespace next_layer {

// Everything QML needs that only the application can determine: the resolved
// palette, the document under inspection, the printer's bed polygon
// ([[x, y], ...] in printer coordinates) and its Z limit. The document and its
// camera framing change whenever a model is opened, so they are writable and
// notify.
//
// The two pieces of logic -- which directory a person's models are in, and putting
// an exported file on disk -- are Rust's (src/app/src/Host.rs). Everything else is
// this object's property storage: QML binds it, so the notify has to come from
// here, and moving the values across the bridge would buy a conversion per read.
class Host : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap palette READ palette CONSTANT)
    Q_PROPERTY(QString paletteSource READ paletteSource CONSTANT)
    Q_PROPERTY(QString modelPath READ modelPath NOTIFY modelPathChanged)
    Q_PROPERTY(QVariantList bedShape READ bedShape NOTIFY bedChanged)
    Q_PROPERTY(double maxPrintHeight READ maxPrintHeight NOTIFY bedChanged)
    Q_PROPERTY(QVariantList focusWorld READ focusWorld NOTIFY framingChanged)
    Q_PROPERTY(QVariantList bedCentreWorld READ bedCentreWorld NOTIFY bedChanged)
    Q_PROPERTY(double bedRadius READ bedRadius NOTIFY bedChanged)
    Q_PROPERTY(double modelRadius READ modelRadius NOTIFY framingChanged)
    Q_PROPERTY(QString printerModel READ printerModel NOTIFY bedChanged)
    Q_PROPERTY(QVector3D bedMin READ bedMin NOTIFY bedChanged)
    Q_PROPERTY(QVector3D bedMax READ bedMax NOTIFY bedChanged)
    Q_PROPERTY(bool bedPlateLoaded READ bedPlateLoaded NOTIFY bedPlateLoadedChanged)
    /// Where the system says downloads live. The model dialog starts here when no
    /// folder has been used yet, and QML keeps the last one used after that.
    Q_PROPERTY(QString downloadFolder READ downloadFolder CONSTANT)

public:
    Host(const Palette& palette, QVariantList bedShape, double maxPrintHeight,
         QVariantList focusWorld, double modelRadius,
         QString printerModel, QVector3D bedMin, QVector3D bedMax,
         bool bedPlateLoaded, QObject* parent = nullptr)
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

    Q_INVOKABLE bool copyFile(const QString& source, const QString& target) const
    {
        return copy_file(localPathOf(source).toStdString(),
                         localPathOf(target).toStdString());
    }

    QVariantMap palette() const { return m_palette; }
    QString paletteSource() const { return m_paletteSource; }
    QString modelPath() const { return m_modelPath; }
    QVariantList bedShape() const { return m_bedShape; }
    double maxPrintHeight() const { return m_maxPrintHeight; }
    QVariantList focusWorld() const { return m_focusWorld; }
    QVariantList bedCentreWorld() const { return m_bedCentreWorld; }
    double bedRadius() const { return m_bedRadius; }
    double modelRadius() const { return m_modelRadius; }
    QString printerModel() const { return m_printerModel; }
    QVector3D bedMin() const { return m_bedMin; }
    QVector3D bedMax() const { return m_bedMax; }
    bool bedPlateLoaded() const { return m_bedPlateLoaded; }

    QString downloadFolder() const
    {
        return QString::fromStdString(static_cast<std::string>(
            download_folder(
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                    .toStdString(),
                QDir::homePath().toStdString())));
    }

    void setModel(QString path, QVariantList focusWorld, double modelRadius)
    {
        const bool pathChanged = m_modelPath != path;
        m_modelPath = std::move(path);
        m_focusWorld = std::move(focusWorld);
        m_modelRadius = modelRadius;
        if (pathChanged)
            emit modelPathChanged();
        emit framingChanged();
    }

    void setPrinter(QVariantList bedShape, double maxPrintHeight, QString printerModel,
                    QVector3D bedMin, QVector3D bedMax, double bedRadius,
                    QVariantList bedCentreWorld)
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

    void setBedPlateLoaded(bool loaded)
    {
        if (m_bedPlateLoaded == loaded)
            return;
        m_bedPlateLoaded = loaded;
        emit bedPlateLoadedChanged();
    }

signals:
    void modelPathChanged();
    void framingChanged();
    void bedChanged();
    void bedPlateLoadedChanged();

private:
    /// The local path a URL names, or the string as it stands.
    ///
    /// The export dialog hands over a `file:` URL, and `QUrl::toLocalFile` is what
    /// percent-decodes it: a reimplementation that strips the prefix leaves
    /// `///tmp/a%20b.gcode`, which happens to open on Linux until a path holds a
    /// space.
    static QString localPathOf(const QString& value)
    {
        return value.startsWith(QStringLiteral("file:")) ? QUrl(value).toLocalFile()
                                                         : value;
    }

    QVariantMap m_palette;
    QString m_paletteSource;
    QString m_modelPath;
    QVariantList m_bedShape;
    double m_maxPrintHeight = 0.0;
    QVariantList m_focusWorld;
    QVariantList m_bedCentreWorld;
    double m_bedRadius = 0.0;
    double m_modelRadius = 0.0;
    QString m_printerModel;
    QVector3D m_bedMin;
    QVector3D m_bedMax;
    bool m_bedPlateLoaded = false;
};

}
