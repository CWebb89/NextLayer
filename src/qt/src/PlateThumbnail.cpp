// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PlateThumbnail.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QList>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace next_layer {

namespace {

constexpr int kSizes[] = {512, 32};
constexpr int kLineChars = 78;

QByteArray encodePng(const QImage& image, const int edge)
{
    const QImage scaled =
        image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB888);

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    if (!scaled.save(&buffer, "PNG"))
        return {};
    return png;
}

QStringList thumbnailBlock(const QByteArray& png, const int edge)
{
    QStringList block;
    block.append(QStringLiteral("; thumbnail begin %1x%1 %2").arg(edge).arg(png.size()));

    const QString base64 = QString::fromLatin1(png.toBase64());
    for (int at = 0; at < base64.size(); at += kLineChars)
        block.append(QStringLiteral("; ") + base64.mid(at, kLineChars));

    block.append(QStringLiteral("; thumbnail end"));
    return block;
}

bool isConfigBlockBegin(const QString& line)
{
    static const QRegularExpression pattern(
        QStringLiteral("^;\\s*\\w+_config = begin\\s*$"));
    return pattern.match(line).hasMatch();
}

}

QString embedPlateThumbnails(const QString& gcodePath, const QImage& plate)
{
    if (plate.isNull())
        return QStringLiteral("the plate render is empty");

    QStringList blocks;
    for (const int edge : kSizes) {
        const QByteArray png = encodePng(plate, edge);
        if (png.isEmpty())
            return QStringLiteral("the plate render could not be encoded as PNG");
        blocks.append(thumbnailBlock(png, edge));
    }

    QFile file(gcodePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("cannot read %1").arg(gcodePath);

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    QStringList rewritten;
    rewritten.reserve(lines.size() + blocks.size() * 2);

    bool insideThumbnail = false;
    bool inserted = false;

    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("; thumbnail begin"))) {
            insideThumbnail = true;
            continue;
        }
        if (insideThumbnail) {
            if (line.startsWith(QStringLiteral("; thumbnail end")))
                insideThumbnail = false;
            continue;
        }
        if (!inserted && isConfigBlockBegin(line)) {
            rewritten.append(blocks);
            inserted = true;
        }
        rewritten.append(line);
    }

    if (!inserted)
        rewritten.append(blocks);

    QSaveFile out(gcodePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("cannot write %1").arg(gcodePath);

    const QByteArray body = rewritten.join(QLatin1Char('\n')).toUtf8();
    if (out.write(body) != body.size() || !out.commit())
        return QStringLiteral("cannot write %1").arg(gcodePath);

    return {};
}

}
