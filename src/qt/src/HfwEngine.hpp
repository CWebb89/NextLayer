// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "SliceRuntime.hpp"

#include <QString>
#include <QVector>

namespace next_layer {

/// Whether a slice should be routed to HFW instead of the bundled engine.
///
/// Set `NEXT_LAYER_ENGINE=hfw` in the environment. It is read per slice rather
/// than cached, so one build can produce both engines' output without a restart.
bool hfwRequested();

/// The HFW binary this process will run: `NEXT_LAYER_HFW` when set, then
/// `hfw-cli` on PATH, then the development build tree. Empty when none exists.
QString hfwBinary();

/// Slices a plate with HFW, writing the same G-code the bundled engine would.
///
/// The plate arrives in printer coordinates, so the objects are written as STL
/// where they are and HFW is told not to arrange them. The configuration is the
/// app's own resolved engine configuration, which HFW reads through its preset
/// intake; a setting HFW cannot apply is dropped by that intake and reported
/// rather than silently changing the job.
///
/// Returns false and fills \p error with the intake report and the verifier's
/// verdict when HFW refuses or the file fails verification.
bool sliceWithHfw(const QVector<PlateSliceObject>& objects,
                  const QString& configPath,
                  const QString& gcodePath,
                  QString& error);

} // namespace next_layer
