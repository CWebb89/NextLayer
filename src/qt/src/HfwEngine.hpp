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

/// Writes the plate as HFW's own project document: every object with its
/// placement, its overrides, its volumes and their kinds, its own layer heights,
/// and the plate's timeline and in-layer strokes.
///
/// This is the only shape that carries all of that. An STL carries geometry and
/// nothing else, which is why a plate handed over as meshes loses volumes, layer
/// heights, markers and strokes without saying so.
bool writePlateFile(const QVector<PlateSliceObject>& objects,
                    const QVector<PlateLayerGcode>& layerGcodes,
                    const QVector<PlateInLayerPolyline>& inLayer,
                    const QString& path,
                    QString& error);

/// Slices a plate with HFW, writing the G-code this application prints.
///
/// The engine is HFW and nothing else: there is no second path, no environment
/// switch and no fallback, so a missing binary is the caller's error to show. The
/// plate goes over as its own document -- see writePlateFile -- and is sliced with
/// `hfw-cli --plate`, so what the engine reads is the same file a person could
/// keep.
///
/// \p placeOnBed centres the document on the bed before printing it. A plate the
/// window laid out is already placed, and moving it would move every object
/// relative to the bed the user put it on; a single model named on the command
/// line is not, so that caller asks for the centring.
bool sliceWithHfw(const QVector<PlateSliceObject>& objects,
                  const QString& configPath,
                  const QString& gcodePath,
                  QString& error,
                  const QVector<PlateLayerGcode>& layerGcodes = {},
                  const QVector<PlateInLayerPolyline>& inLayer = {},
                  bool placeOnBed = false);

} // namespace next_layer
