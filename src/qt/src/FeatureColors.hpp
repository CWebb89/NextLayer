// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QColor>
#include <QLatin1String>
#include <QString>
#include <QVariantMap>

namespace next_layer {

// One colour per line type, chosen so that the types a printed part actually
// shows are far apart on the wheel: walls warm (orange, amber), fill cool
// (violet, orchid), the visible top surface bright mint, bridges cyan, supports
// green. Every name the engine and both slicer dialects emit is listed, so a
// role never falls through to the grey default by accident.
inline QColor featureColor(const QString& feature, const QVariantMap& roles)
{
    Q_UNUSED(roles)

    if (feature == QLatin1String("External perimeter") || feature == QLatin1String("Outer wall"))
        return QColor("#ff6a00");
    if (feature == QLatin1String("Perimeter") || feature == QLatin1String("Inner wall"))
        return QColor("#ffd400");
    if (feature == QLatin1String("Overhang perimeter") || feature == QLatin1String("Overhang wall"))
        return QColor("#ff3ea5");
    if (feature == QLatin1String("Internal infill") || feature == QLatin1String("Sparse infill"))
        return QColor("#7b4bff");
    if (feature == QLatin1String("Solid infill") || feature == QLatin1String("Internal solid infill")
        || feature == QLatin1String("Bottom solid infill"))
        return QColor("#c04bff");
    if (feature == QLatin1String("Top solid infill") || feature == QLatin1String("Top surface"))
        return QColor("#00e6a8");
    if (feature == QLatin1String("Bottom surface"))
        return QColor("#2f6bff");
    if (feature == QLatin1String("Ironing"))
        return QColor("#b6e600");
    if (feature == QLatin1String("Bridge infill") || feature == QLatin1String("Bridge"))
        return QColor("#00c2d1");
    if (feature == QLatin1String("Internal bridge infill") || feature == QLatin1String("Internal bridge"))
        return QColor("#7fd8e0");
    if (feature == QLatin1String("Gap fill") || feature == QLatin1String("Gap infill"))
        return QColor("#ffb3c1");
    if (feature == QLatin1String("Support material") || feature == QLatin1String("Support"))
        return QColor("#3bb143");
    if (feature == QLatin1String("Support material interface")
        || feature == QLatin1String("Support interface"))
        return QColor("#a4de4a");
    if (feature == QLatin1String("Skirt") || feature == QLatin1String("Brim")
        || feature == QLatin1String("Skirt/Brim"))
        return QColor("#6d8bff");
    if (feature == QLatin1String("Wipe tower") || feature == QLatin1String("Prime tower"))
        return QColor("#b58cff");
    if (feature == QLatin1String("Custom"))
        return QColor("#dcdcdc");
    return QColor("#9aa4b2");
}

}
