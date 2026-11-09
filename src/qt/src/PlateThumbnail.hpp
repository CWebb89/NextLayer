// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

class QImage;

namespace next_layer {

/// Embeds a render of the plate into an exported G-code file as `; thumbnail`
/// blocks, which is what Mainsail, Fluidd, PrusaLink and OctoPrint read to show
/// a preview of a print without slicing it. The blocks go immediately before the
/// config block at the end of the file, in the two sizes the hosts ask for.
///
/// Returns an empty string on success, or why the file could not be rewritten.
/// Idempotent: any thumbnail block already in the file is replaced, so embedding
/// twice does not double the file size.
QString embedPlateThumbnails(const QString& gcodePath, const QImage& plate);

}
