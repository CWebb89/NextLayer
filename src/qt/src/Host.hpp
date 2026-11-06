// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

namespace next_layer {

class Palette;

// Everything QML needs that only the application can determine: the resolved
// palette, the document under inspection, the printer's bed polygon
// ([[x, y], ...] in printer coordinates), its Z limit, and the engine config
// template. The document and its camera framing change whenever a model is
// opened, so they are writable and notify.
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

public:
    Host(const Palette& palette, QVariantList bedShape, double maxPrintHeight,
         QVariantList focusWorld, double modelRadius,
         QString printerModel, QVector3D bedMin, QVector3D bedMax,
         bool bedPlateLoaded, QObject* parent = nullptr);

    Q_INVOKABLE bool copyFile(const QString& source, const QString& target) const;

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

    void setModel(QString path, QVariantList focusWorld, double modelRadius);

    void setPrinter(QVariantList bedShape, double maxPrintHeight, QString printerModel,
                    QVector3D bedMin, QVector3D bedMax, double bedRadius,
                    QVariantList bedCentreWorld);

    void setBedPlateLoaded(bool loaded);

signals:
    void modelPathChanged();
    void framingChanged();
    void bedChanged();
    void bedPlateLoadedChanged();

private:
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
