#pragma once

#include <QString>

namespace next_layer::yaml {

// One YAML scalar the engine's preset reader accepts. Single-quoted style is
// used everywhere it can be, which keeps names, enum spellings and `x xy`
// points verbatim; a multi-line string (start/end G-code) has to switch to the
// double-quoted style so its newlines survive as escapes.
inline QString quoted(const QString& text)
{
    if (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))) {
        QString escaped = text;
        escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
        escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
        escaped.replace(QLatin1Char('\r'), QLatin1String("\\r"));
        escaped.replace(QLatin1Char('\t'), QLatin1String("\\t"));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

inline QString number(double value)
{
    return QString::number(value, 'g', 15);
}

/// The inverse of quoted() for one scalar: the catalog writes `id: 'Anycubic'`
/// where the engine's own bundles write `id: Anycubic`, and a comparison that
/// does not strip the quotes silently finds no bundle at all.
inline QString unquoted(const QString& text)
{
    QString value = text.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))) {
        value = value.mid(1, value.size() - 2);
        value.replace(QLatin1String("''"), QLatin1String("'"));
        return value;
    }
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        return value.mid(1, value.size() - 2);
    return value;
}

inline QString point(double x, double y)
{
    return quoted(number(x) + QLatin1Char('x') + number(y));
}

} // namespace next_layer::yaml
