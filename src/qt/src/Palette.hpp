#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace next_layer {

// Resolves the 36-key matugen MD3 palette, first match wins:
// $NEXT_LAYER_COLORS, ~/.cache/matugen/next-layer-colors.json,
// ~/.cache/matugen/klipshell-colors.json. Unreadable, malformed or
// non-string-valued entries fall through to the next candidate; when none
// resolves, roles() is empty and Colors.qml keeps its compiled-in defaults.
class Palette : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap roles READ roles CONSTANT)
    Q_PROPERTY(QString source READ source CONSTANT)

public:
    explicit Palette(QObject* parent = nullptr);

    QVariantMap roles() const { return m_roles; }
    QString source() const { return m_source; }

private:
    QVariantMap m_roles;
    QString m_source;
};

}
