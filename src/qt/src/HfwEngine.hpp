// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "SliceRuntime.hpp"

#include <QString>
#include <QVector>

namespace next_layer {

/// One object of a plate as the engine holds it, in the terms the document uses.
///
/// This is the shape a document is rebuilt from: read a plate back through the ABI
/// and a `PlateDocument` can be made out of it with nothing left over. Settings
/// travel as the text the engine accepted, because that is what it holds and what
/// it will take back; the panel's own typing of each setting belongs to the
/// settings model, which is the next phase.
struct PlateSnapshot
{
    struct Volume
    {
        Slic3r::Domain::ModelVolumeType type = Slic3r::Domain::ModelVolumeType::MODEL_PART;
        Slic3r::Domain::TriangleMesh mesh;
        QHash<QString, QString> settings;
    };

    struct Object
    {
        QString name;
        Slic3r::Domain::TriangleMesh mesh;
        Slic3r::Domain::Transformation placement;
        bool enabled = true;
        QHash<QString, QString> settings;
        QVector<QPair<double, double>> layerHeights;
        QByteArray supportPaint;
        QByteArray seamPaint;
        QVector<Volume> volumes;
    };

    QVector<Object> objects;
    QVector<PlateLayerGcode> markers;
    QVector<PlateInLayerPolyline> strokes;
    /// The bytes the application keeps in the document for itself: which plates
    /// exist, their names, the tool colours, each object's source path.
    QByteArray metadata;
};

/// Reads a plate back through the ABI into the shape a document is built from.
///
/// This is the inverse of what a `PlateDocument` hands the slicer, and it is what
/// makes the document a view over the engine's plate rather than its owner: the
/// objects, their geometry, placement, paint, layer heights, volumes and settings
/// all come from the engine, which is also where they are sliced from.
bool readPlateSnapshot(const QVector<PlateSliceObject>& objects,
                       const QVector<PlateLayerGcode>& layerGcodes,
                       const QVector<PlateInLayerPolyline>& inLayer, PlateSnapshot& snapshot,
                       QString& error);

/// Reads a saved document -- HFW's own project file -- back into a snapshot.
///
/// This is how a project opens: the file *is* the engine's document, so a saved
/// plate reopens as the plate that was saved rather than as a second format that
/// has to be kept in step with it. The caller's own bytes come back in
/// `snapshot.metadata`.
bool readPlateDocument(const QByteArray& bytes, PlateSnapshot& snapshot, QString& error);

/// Saves the plate as HFW's project file, with \p metadata inside it.
///
/// One file: the document is the engine's, and the application's own state travels
/// in the engine's opaque bytes rather than beside it in a second file that can be
/// lost.
bool savePlateDocument(const QVector<PlateSliceObject>& objects,
                       const QVector<PlateLayerGcode>& layerGcodes,
                       const QVector<PlateInLayerPolyline>& inLayer, const QByteArray& metadata,
                       const QString& path, QString& error);

/// Reads a plate back and rebuilds it, to prove a document survives being handed to
/// the engine.
///
/// The plate is built from \p objects, every part of it is read through the ABI --
/// each object's name, placement, settings, geometry, paint, layer heights and
/// volumes with their kinds and settings -- and a second plate is built from
/// nothing but what was read. The two documents must be identical byte for byte, or
/// something the engine holds is not something it can give back.
///
/// \p report says what came back, so a gate reads a count rather than an exit code.
bool roundTripPlate(const QVector<PlateSliceObject>& objects,
                    const QVector<PlateLayerGcode>& layerGcodes,
                    const QVector<PlateInLayerPolyline>& inLayer, QString& report,
                    QString& error);

/// Slices a plate with HFW, writing the G-code this application prints.
///
/// The engine is HFW and nothing else: there is no second path, no environment
/// switch and no fallback, so a failure is the caller's error to show. The plate is
/// built in this process through the ABI -- objects, placement, overrides, volumes,
/// layer heights, paint, markers and strokes -- and sliced there, so nothing about
/// the document is reduced to what a file format can carry on the way in.
///
/// \p placeOnBed lays the plate on the bed the profile describes before printing it.
/// A plate the window laid out is already placed, and moving it would move every
/// object relative to the bed the user put it on; a single model named on the
/// command line is not, so that caller asks for the centring.
bool sliceWithHfw(const QVector<PlateSliceObject>& objects,
                  const QString& configPath,
                  const QString& gcodePath,
                  QString& error,
                  const QVector<PlateLayerGcode>& layerGcodes = {},
                  const QVector<PlateInLayerPolyline>& inLayer = {},
                  bool placeOnBed = false);

} // namespace next_layer
