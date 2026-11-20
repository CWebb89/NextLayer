// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "SliceRuntime.hpp"

#include <QString>
#include <QVector>

namespace next_layer {

/// The HFW binary this process will run: `NEXT_LAYER_HFW` when set, then
/// `hfw-cli` beside the executable (its installed home), then `hfw-cli` on PATH,
/// then the development build tree. Empty when none exists.
QString hfwBinary();

/// Why no binary was found, in the words the interface can show. Empty when one
/// was. Cheap enough to ask at start-up, so a window with no engine says so
/// before anyone presses Slice.
QString hfwUnavailableReason();

/// Slices a plate with HFW, writing the G-code this application prints.
///
/// The engine is HFW and nothing else: there is no second path, no environment
/// switch and no fallback, so a missing binary is the caller's error to show.
bool sliceWithHfw(const QVector<PlateSliceObject>& objects,
                  const QString& configPath,
                  const QString& gcodePath,
                  QString& error);

} // namespace next_layer
