// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QString>

#include <string>
#include <vector>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

namespace thumbnailDetail {

inline QByteArray encodePng(const QImage& image, const uint32_t edge)
{
    // A host trusts the size in the `; thumbnail begin WxH` line, so the image is
    // padded onto a square canvas rather than left at whatever aspect the crop
    // happened to have: a 512x340 PNG declared as 512x512 renders stretched.
    const int side = int(edge);
    const QImage scaled = image.scaled(side, side, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB888);
    QImage canvas(side, side, QImage::Format_RGB888);
    canvas.fill(Qt::black);
    {
        QPainter painter(&canvas);
        painter.drawImage((side - scaled.width()) / 2, (side - scaled.height()) / 2, scaled);
    }

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    if (!canvas.save(&buffer, "PNG"))
        return {};
    return png;
}

}

/// Embeds a render of the plate into an exported G-code file as `; thumbnail`
/// blocks, which is what Mainsail, Fluidd, PrusaLink and OctoPrint read to show
/// a preview of a print without slicing it. The blocks go at the very top of the
/// file, in the two sizes the hosts ask for, which is where the reference slicers
/// put them and what a host that stops reading after the first kilobyte needs.
///
/// Returns an empty string on success, or why the file could not be rewritten.
/// Idempotent: any thumbnail block already in the file is replaced, so embedding
/// twice does not double the file size.
///
/// The file rewrite and the block encoding are Rust's
/// (`src/app/src/PlateThumbnail.rs`); this header is the Qt-typed adapter, because
/// a `QImage` is Qt's and the scaling above has to happen before the PNG exists.
inline QString embedPlateThumbnails(const QString& gcodePath, const QImage& plate)
{
    if (plate.isNull())
        return QStringLiteral("the plate render is empty");

    const rust::Vec<uint32_t> edges = thumbnail_edges();
    std::vector<std::string> pngs;
    pngs.reserve(edges.size());
    for (const uint32_t edge : edges) {
        const QByteArray png = thumbnailDetail::encodePng(plate, edge);
        if (png.isEmpty())
            return QStringLiteral("the plate render could not be encoded as PNG");
        pngs.emplace_back(png.constData(), size_t(png.size()));
    }
    if (pngs.size() != 2)
        return QStringLiteral("the plate render could not be encoded as PNG");

    return QString::fromStdString(static_cast<std::string>(
        embed_plate_thumbnails(gcodePath.toStdString(), pngs[0], pngs[1])));
}

}
