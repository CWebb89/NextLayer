// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class PlateDocument;
class SettingsModel;

/// The calibration suite: the test plates a slicer ships, built from
/// the engine's own primitives and the plate's layer G-code rather than from a
/// checked-in model. Every test states what to measure and what to change, the
/// parameters that shape it are the ones the dialog asks for, and the
/// plate it builds slices, previews and sends like any other.
namespace next_layer {

class Calibration : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList tests READ tests CONSTANT)

public:
    explicit Calibration(PlateDocument* document, SettingsModel* settings,
                         QObject* parent = nullptr);

    /// Every test with its title, its instruction, the setting its measurement
    /// becomes and the parameters it takes.
    QVariantList tests() const;

    /// Replaces the active plate with the named test and returns a one-line
    /// description of what it built, or the reason it built nothing.
    Q_INVOKABLE QString build(const QString& kind, const QVariantMap& params);

    /// The same test with its documented defaults, for the headless selftest.
    QString build(const QString& kind, double baseTemperature = 0.0);

    /// The default parameter map for a test, which is what the dialog opens with.
    Q_INVOKABLE QVariantMap defaultParams(const QString& kind) const;

private:
    PlateDocument* m_document = nullptr;
    SettingsModel* m_settings = nullptr;
};

} // namespace next_layer
