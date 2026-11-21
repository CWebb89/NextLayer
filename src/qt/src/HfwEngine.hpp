// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "Paint.hpp"
#include "SliceRuntime.hpp"

#include "next-layer-app/src/bridge.rs.h"

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <string>
#include <utility>

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
    /// What the exchange dropped and why, in the caller's words: today that is an
    /// object's tool paint, which this engine has no channel for. A caller shows
    /// them rather than letting a capability disappear without a trace.
    QStringList dropped;
};

namespace hfwEngineDetail {

/// The implementation is `src/app/src/HfwEngine.rs`. This header is the adapter
/// that hands it the document's own mesh, transform and paint, and takes them
/// back, because those types still live in `SliceRuntime.hpp` and `Paint.hpp`.

inline QString fromRust(const rust::String& value)
{
    return QString::fromStdString(static_cast<std::string>(value));
}

inline QByteArray bytesOf(const rust::Vec<uint8_t>& bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), int(bytes.size()));
}

/// The object's triangles as the flat arrays the engine rebuilds from, in the order
/// the document holds them: triangle `i` here is triangle `i` of the paint channels.
inline rust::Vec<double> vertexArray(const Slic3r::Domain::TriangleMesh& mesh)
{
    rust::Vec<double> record;
    record.reserve(mesh.its.vertices.size() * 3);
    for (const Slic3r::Domain::Vec3f& vertex : mesh.its.vertices) {
        record.push_back(vertex.x());
        record.push_back(vertex.y());
        record.push_back(vertex.z());
    }
    return record;
}

inline rust::Vec<int64_t> indexArray(const Slic3r::Domain::TriangleMesh& mesh)
{
    rust::Vec<int64_t> record;
    record.reserve(mesh.its.indices.size() * 3);
    for (const auto& face : mesh.its.indices) {
        record.push_back(int64_t(face[0]));
        record.push_back(int64_t(face[1]));
        record.push_back(int64_t(face[2]));
    }
    return record;
}

/// Sixteen `f64`, row-major, four rows of four. The document composes this the way
/// the engine does — `T * Rz * Ry * Rx * S` — so it crosses unchanged.
inline rust::Vec<double> matrixArray(const Slic3r::Domain::Transformation& transform)
{
    rust::Vec<double> record;
    record.reserve(16);
    const Slic3r::Domain::Transform3d& matrix = transform.get_matrix();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            record.push_back(matrix(row, column));
    return record;
}

inline Slic3r::Domain::Transformation transformationOf(const rust::Vec<double>& record)
{
    Slic3r::Domain::Transform3d matrix = Slic3r::Domain::Transform3d::Identity();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix(row, column) = record[size_t(row * 4 + column)];
    return Slic3r::Domain::Transformation(matrix);
}

inline Slic3r::Domain::TriangleMesh meshOf(const rust::Vec<double>& vertices,
                                           const rust::Vec<int64_t>& indices)
{
    Slic3r::Domain::TriangleMesh mesh;
    const size_t points = vertices.size() / 3;
    mesh.its.vertices.reserve(points);
    for (size_t point = 0; point < points; ++point) {
        mesh.its.vertices.emplace_back(float(vertices[point * 3]),
                                       float(vertices[point * 3 + 1]),
                                       float(vertices[point * 3 + 2]));
    }
    const size_t triangles = indices.size() / 3;
    mesh.its.indices.reserve(triangles);
    for (size_t triangle = 0; triangle < triangles; ++triangle) {
        mesh.its.indices.push_back(Slic3r::Domain::Index3{int(indices[triangle * 3]),
                                                          int(indices[triangle * 3 + 1]),
                                                          int(indices[triangle * 3 + 2])});
    }
    return mesh;
}

/// One paint channel as the ABI carries it: a state per facet, `0` none, `1`
/// enforcer, `2` blocker. The document's own paint records which pieces of a facet
/// were painted, so the reduction is `paintFacetStates`, and it happens here rather
/// than in Rust because a `TriangleSplittingData` is the engine's.
inline rust::Vec<uint8_t> paintStates(
    const Slic3r::Domain::TriangleMesh& mesh,
    const Slic3r::Domain::TriangleSelector::TriangleSplittingData& painted)
{
    rust::Vec<uint8_t> record;
    const QVector<quint8> states = paintFacetStates(mesh, painted);
    record.reserve(size_t(states.size()));
    for (const quint8 state : states)
        record.push_back(uint8_t(state));
    return record;
}

/// The document's name for a volume kind: `part`, `negative`, `modifier`, `blocker`
/// or `enforcer`. The five are the engine's five, so the name is a perfect inverse of
/// the code and a round trip cannot fold one kind into another.
inline QString volumeKind(Slic3r::Domain::ModelVolumeType type)
{
    switch (type) {
    case Slic3r::Domain::ModelVolumeType::NEGATIVE_VOLUME: return QStringLiteral("negative");
    case Slic3r::Domain::ModelVolumeType::PARAMETER_MODIFIER: return QStringLiteral("modifier");
    case Slic3r::Domain::ModelVolumeType::SUPPORT_BLOCKER: return QStringLiteral("blocker");
    case Slic3r::Domain::ModelVolumeType::SUPPORT_ENFORCER: return QStringLiteral("enforcer");
    default: return QStringLiteral("part");
    }
}

inline Slic3r::Domain::ModelVolumeType volumeTypeOf(const QString& kind)
{
    if (kind == QLatin1String("negative"))
        return Slic3r::Domain::ModelVolumeType::NEGATIVE_VOLUME;
    if (kind == QLatin1String("modifier"))
        return Slic3r::Domain::ModelVolumeType::PARAMETER_MODIFIER;
    if (kind == QLatin1String("blocker"))
        return Slic3r::Domain::ModelVolumeType::SUPPORT_BLOCKER;
    if (kind == QLatin1String("enforcer"))
        return Slic3r::Domain::ModelVolumeType::SUPPORT_ENFORCER;
    return Slic3r::Domain::ModelVolumeType::MODEL_PART;
}

inline rust::Vec<EngineVolume> volumesOf(const PlateSliceObject& object)
{
    rust::Vec<EngineVolume> volumes;
    for (const PlateSliceVolume& volume : object.volumes) {
        EngineVolume entry;
        entry.kind = volumeKind(volume.type).toStdString();
        entry.settings_json = QString::fromUtf8(volume.settingsJson).toStdString();
        entry.vertices = vertexArray(volume.mesh);
        entry.indices = indexArray(volume.mesh);
        volumes.push_back(std::move(entry));
    }
    return volumes;
}

inline rust::Vec<EngineSetting> settingsOf(const QHash<QString, QString>& settings)
{
    rust::Vec<EngineSetting> rows;
    for (auto entry = settings.constBegin(); entry != settings.constEnd(); ++entry) {
        EngineSetting row;
        row.name = entry.key().toStdString();
        row.value = entry.value().toStdString();
        rows.push_back(std::move(row));
    }
    return rows;
}

/// How many facets the document holds on its tool channel. Nothing crosses with
/// them: the engine has no third channel, so the count is what the reason names.
inline int64_t toolPaintFacets(const PlateSliceObject& object)
{
    int64_t facets = 0;
    for (const quint8 state : paintFacetStates(object.mesh, object.toolPaint))
        if (state != 0)
            ++facets;
    return facets;
}

inline EngineObject objectOf(const PlateSliceObject& object)
{
    EngineObject entry;
    entry.name = object.name.toStdString();
    entry.vertices = vertexArray(object.mesh);
    entry.indices = indexArray(object.mesh);
    entry.matrix = matrixArray(object.transform);
    entry.settings_json = QString::fromUtf8(object.settingsJson).toStdString();
    entry.foreign_settings = settingsOf(object.foreignSettings);
    for (const Slic3r::Domain::ZHeightPair& pair : object.layerHeights) {
        entry.layer_heights.push_back(pair.z);
        entry.layer_heights.push_back(pair.layer_height);
    }
    entry.support_paint = paintStates(object.mesh, object.supportPaint);
    entry.seam_paint = paintStates(object.mesh, object.seamPaint);
    entry.tool_paint_facets = toolPaintFacets(object);
    entry.volumes = volumesOf(object);
    return entry;
}

inline EnginePlate plateOf(const QVector<PlateSliceObject>& objects,
                           const QVector<PlateLayerGcode>& layerGcodes,
                           const QVector<PlateInLayerPolyline>& inLayer, bool placeOnBed)
{
    EnginePlate plate;
    for (const PlateSliceObject& object : objects)
        plate.objects.push_back(objectOf(object));
    for (const PlateLayerGcode& marker : layerGcodes) {
        EngineMarker entry;
        entry.z = marker.z;
        entry.kind = marker.type.toStdString();
        entry.text = marker.text.toStdString();
        plate.markers.push_back(std::move(entry));
    }
    for (const PlateInLayerPolyline& stroke : inLayer) {
        EngineStroke entry;
        entry.z = stroke.z;
        entry.points.reserve(size_t(stroke.points.size()) * 2);
        for (const QPointF& point : stroke.points) {
            entry.points.push_back(point.x());
            entry.points.push_back(point.y());
        }
        entry.line_width = stroke.line_width;
        entry.layer_height = stroke.layer_height;
        entry.speed = stroke.speed;
        entry.prefix = stroke.prefix.toStdString();
        plate.strokes.push_back(std::move(entry));
    }
    plate.place_on_bed = placeOnBed;
    return plate;
}

inline PlateLayerGcode markerOf(const EngineMarker& marker)
{
    PlateLayerGcode entry;
    entry.z = marker.z;
    entry.type = fromRust(marker.kind);
    entry.text = fromRust(marker.text);
    return entry;
}

inline PlateInLayerPolyline strokeOf(const EngineStroke& stroke)
{
    PlateInLayerPolyline entry;
    entry.z = stroke.z;
    for (size_t at = 0; at + 1 < stroke.points.size(); at += 2)
        entry.points.append(QPointF(stroke.points[at], stroke.points[at + 1]));
    entry.line_width = stroke.line_width;
    entry.layer_height = stroke.layer_height;
    entry.speed = stroke.speed;
    entry.prefix = fromRust(stroke.prefix);
    return entry;
}

inline PlateSnapshot snapshotOf(const PlateReadback& read)
{
    PlateSnapshot snapshot;
    snapshot.metadata = bytesOf(read.metadata);
    for (const rust::String& reason : read.dropped)
        snapshot.dropped.append(fromRust(reason));
    for (const EngineMarker& marker : read.markers)
        snapshot.markers.append(markerOf(marker));
    for (const EngineStroke& stroke : read.strokes)
        snapshot.strokes.append(strokeOf(stroke));

    for (const ReadbackObject& object : read.objects) {
        PlateSnapshot::Object entry;
        entry.name = fromRust(object.name);
        entry.mesh = meshOf(object.vertices, object.indices);
        entry.placement = transformationOf(object.matrix);
        entry.enabled = object.enabled;
        for (const EngineSetting& setting : object.settings)
            entry.settings.insert(fromRust(setting.name), fromRust(setting.value));
        for (size_t at = 0; at + 1 < object.layer_heights.size(); at += 2)
            entry.layerHeights.append(
                {object.layer_heights[at], object.layer_heights[at + 1]});
        entry.supportPaint = bytesOf(object.support_paint);
        entry.seamPaint = bytesOf(object.seam_paint);
        for (const ReadbackVolume& volume : object.volumes) {
            PlateSnapshot::Volume attached;
            attached.type = volumeTypeOf(fromRust(volume.kind));
            attached.mesh = meshOf(volume.vertices, volume.indices);
            for (const EngineSetting& setting : volume.settings)
                attached.settings.insert(fromRust(setting.name), fromRust(setting.value));
            entry.volumes.append(attached);
        }
        snapshot.objects.append(entry);
    }
    return snapshot;
}

/// The engine's reason, or the caller's, as the caller spells it.
inline bool reportOf(const EngineOutcome& outcome, QString& error)
{
    if (outcome.ok)
        return true;
    error = fromRust(outcome.error);
    return false;
}

} // namespace hfwEngineDetail

/// Reads a plate back through the ABI into the shape a document is built from.
///
/// This is the inverse of what a `PlateDocument` hands the slicer, and it is what
/// makes the document a view over the engine's plate rather than its owner: the
/// objects, their geometry, placement, paint, layer heights, volumes and settings
/// all come from the engine, which is also where they are sliced from.
inline bool readPlateSnapshot(const QVector<PlateSliceObject>& objects,
                              const QVector<PlateLayerGcode>& layerGcodes,
                              const QVector<PlateInLayerPolyline>& inLayer,
                              PlateSnapshot& snapshot, QString& error)
{
    const PlateOutcome outcome = read_plate_snapshot(
        hfwEngineDetail::plateOf(objects, layerGcodes, inLayer, false));
    if (!outcome.ok) {
        error = hfwEngineDetail::fromRust(outcome.error);
        return false;
    }
    snapshot = hfwEngineDetail::snapshotOf(outcome.snapshot);
    return true;
}

/// Reads a saved document -- HFW's own project file -- back into a snapshot.
///
/// This is how a project opens: the file *is* the engine's document, so a saved
/// plate reopens as the plate that was saved rather than as a second format that
/// has to be kept in step with it. The caller's own bytes come back in
/// `snapshot.metadata`, and what the file held and this engine cannot is named in
/// `snapshot.dropped`.
inline bool readPlateDocument(const QByteArray& bytes, PlateSnapshot& snapshot, QString& error)
{
    const PlateOutcome outcome =
        read_plate_document(rust::Slice<const uint8_t>(
            reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size())));
    if (!outcome.ok) {
        error = hfwEngineDetail::fromRust(outcome.error);
        return false;
    }
    snapshot = hfwEngineDetail::snapshotOf(outcome.snapshot);
    return true;
}

/// Saves the plate as HFW's project file, with \p metadata inside it.
///
/// One file: the document is the engine's, and the application's own state travels
/// in the engine's opaque bytes rather than beside it in a second file that can be
/// lost.
inline bool savePlateDocument(const QVector<PlateSliceObject>& objects,
                              const QVector<PlateLayerGcode>& layerGcodes,
                              const QVector<PlateInLayerPolyline>& inLayer,
                              const QByteArray& metadata, const QString& path, QString& error)
{
    const EngineOutcome outcome = save_plate_document(
        hfwEngineDetail::plateOf(objects, layerGcodes, inLayer, false),
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t*>(metadata.constData()),
                                   size_t(metadata.size())),
        path.toStdString());
    return hfwEngineDetail::reportOf(outcome, error);
}

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
inline bool roundTripPlate(const QVector<PlateSliceObject>& objects,
                           const QVector<PlateLayerGcode>& layerGcodes,
                           const QVector<PlateInLayerPolyline>& inLayer, QString& report,
                           QString& error)
{
    const EngineOutcome outcome = round_trip_plate(
        hfwEngineDetail::plateOf(objects, layerGcodes, inLayer, false));
    if (!outcome.ok) {
        error = hfwEngineDetail::fromRust(outcome.error);
        return false;
    }
    report = hfwEngineDetail::fromRust(outcome.report);
    return true;
}

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
inline bool sliceWithHfw(const QVector<PlateSliceObject>& objects, const QString& configPath,
                         const QString& gcodePath, QString& error,
                         const QVector<PlateLayerGcode>& layerGcodes = {},
                         const QVector<PlateInLayerPolyline>& inLayer = {},
                         bool placeOnBed = false)
{
    const EngineOutcome outcome =
        slice_with_hfw(hfwEngineDetail::plateOf(objects, layerGcodes, inLayer, placeOnBed),
                       configPath.toStdString(), gcodePath.toStdString());
    return hfwEngineDetail::reportOf(outcome, error);
}

} // namespace next_layer
