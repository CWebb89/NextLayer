// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PlateThumbnail.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace next_layer {

namespace {

constexpr int kSizes[] = {512, 32};
constexpr int kLineChars = 78;

QByteArray encodePng(const QImage& image, const int edge)
{
    // A host trusts the size in the `; thumbnail begin WxH` line, so the image is
    // padded onto a square canvas rather than left at whatever aspect the crop
    // happened to have: a 512x340 PNG declared as 512x512 renders stretched.
    const QImage scaled = image.scaled(edge, edge, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB888);
    QImage canvas(edge, edge, QImage::Format_RGB888);
    canvas.fill(Qt::black);
    {
        QPainter painter(&canvas);
        painter.drawImage((edge - scaled.width()) / 2, (edge - scaled.height()) / 2, scaled);
    }

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    if (!canvas.save(&buffer, "PNG"))
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

}

QString embedPlateThumbnails(const QString& gcodePath, const QImage& plate)
{
    if (plate.isNull())
        return QStringLiteral("the plate render is empty");

    QByteArray blocks;
    for (const int edge : kSizes) {
        const QByteArray png = encodePng(plate, edge);
        if (png.isEmpty())
            return QStringLiteral("the plate render could not be encoded as PNG");
        for (const QString& line : thumbnailBlock(png, edge)) {
            blocks += line.toUtf8();
            blocks += '\n';
        }
    }

    // Streamed rather than read whole: a day-long print is a few hundred
    // megabytes, and holding it in memory twice to insert twenty kilobytes at the
    // top is how a thumbnail addition runs a machine out of RAM.
    //
    // The block goes at the very top, which is where the reference slicers put it
    // and what a host that stops reading after the first kilobyte needs.
    QFile in(gcodePath);
    if (!in.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot read %1").arg(gcodePath);

    QSaveFile out(gcodePath);
    if (!out.open(QIODevice::WriteOnly))
        return QStringLiteral("cannot write %1").arg(gcodePath);

    if (out.write(blocks) != blocks.size()) {
        out.cancelWriting();
        return QStringLiteral("cannot write %1").arg(gcodePath);
    }

    bool insideThumbnail = false;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine();
        if (line.startsWith("; thumbnail begin")) {
            insideThumbnail = true;
            continue;
        }
        if (insideThumbnail) {
            if (line.startsWith("; thumbnail end"))
                insideThumbnail = false;
            continue;
        }
        if (out.write(line) != line.size()) {
            out.cancelWriting();
            return QStringLiteral("cannot write %1").arg(gcodePath);
        }
    }
    in.close();

    if (!out.commit())
        return QStringLiteral("cannot write %1").arg(gcodePath);

    return {};
}

}
