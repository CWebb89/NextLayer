#pragma once

#include <QColor>
#include <QLatin1String>
#include <QString>
#include <QVariantMap>

namespace next_layer {

// PrusaSlicer 3.0 emits the exact feature names below in `;TYPE:` comments.
// Preview colours are a fixed palette, not theme roles: the sliced view has to
// separate line types at a glance, which a single-hue MD3 scheme cannot do.
inline QColor featureColor(const QString& feature, const QVariantMap& roles)
{
    Q_UNUSED(roles)

    if (feature == QLatin1String("External perimeter"))
        return QColor("#ff7a18");
    if (feature == QLatin1String("Overhang perimeter"))
        return QColor("#ff5ca8");
    if (feature == QLatin1String("Perimeter"))
        return QColor("#ffc247");
    if (feature == QLatin1String("Internal infill"))
        return QColor("#e7543f");
    if (feature.startsWith(QLatin1String("Solid infill")) || feature.startsWith(QLatin1String("Top solid infill"))
        || feature.startsWith(QLatin1String("Bottom solid infill")))
        return QColor("#b8442f");
    if (feature.startsWith(QLatin1String("Bridge infill")))
        return QColor("#4f9ee8");
    if (feature == QLatin1String("Support material"))
        return QColor("#58b368");
    if (feature == QLatin1String("Support material interface"))
        return QColor("#8fd39a");
    if (feature == QLatin1String("Skirt") || feature == QLatin1String("Brim")
        || feature == QLatin1String("Skirt/Brim"))
        return QColor("#8d7be0");
    if (feature == QLatin1String("Custom"))
        return QColor("#d9d9d9");
    return QColor("#9aa4b2");
}

}
