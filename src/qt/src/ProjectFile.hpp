// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Biz/Format/STL.hpp>
#include <Slic3r/Domain/LayerHeightProfile.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/TriangleSelector.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <string>
#include <vector>

#include "Scratch.hpp"
#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

/// One volume attached to a project object: geometry in the object's own frame, its
/// own transform, and the role it plays in the slice.
struct ProjectVolume
{
    QString name;
    QString type;
    bool visible = true;
    Slic3r::Domain::TriangleMesh mesh;
    Slic3r::Domain::Transformation transform;
};

/// One object as a project file stores it: geometry, its own transform, and the
/// three things this app can attach to an object beyond geometry -- overrides, a
/// layer height profile and paint.
struct ProjectObject
{
    QString name;
    QString sourcePath;
    int plate = 0;
    bool visible = true;
    Slic3r::Domain::TriangleMesh mesh;
    Slic3r::Domain::Transformation transform;
    std::vector<ProjectVolume> volumes;
    QVariantMap settings;
    Slic3r::Domain::ZHeightPairs layerHeights;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData supportPaint;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData seamPaint;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData toolPaint;
};

/// A marker on the plate's timeline, stored with the plate because the engine
/// applies it to the bed instance rather than to any one object.
struct ProjectLayerGcode
{
    double z = 0.0;
    QString type;
    QString text;
};

struct ProjectMetadata
{
    QString applicationVersion;
    QString printer;
    QString process;
    QString filament;
    int activePlate = 0;
    int plateCount = 1;
    QStringList plateNames;
    QVector<bool> plateLocked;
    QStringList toolColours;
    std::vector<ProjectLayerGcode> layerGcodes;
};

namespace projectFileDetail {

inline QString fromRust(const rust::String& value)
{
    return QString::fromStdString(static_cast<std::string>(value));
}

inline rust::Slice<const uint8_t> bytesOf(const QByteArray& body)
{
    return rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t*>(body.constData()),
                                      size_t(body.size()));
}

inline QByteArray toByteArray(const rust::Vec<uint8_t>& bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), int(bytes.size()));
}

/// The mesh as the bytes of a binary STL, which is what the project file stores and
/// what the engine's own writer produces.
inline rust::Vec<uint8_t> meshBytes(const Slic3r::Domain::TriangleMesh& mesh,
                                    QTemporaryDir& scratch)
{
    rust::Vec<uint8_t> record;
    const QString path = scratch.filePath(QStringLiteral("mesh.stl"));
    QFile::remove(path);
    if (!Slic3r::Biz::store_stl(path.toStdString(), mesh, true, "next-layer"))
        return record;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return record;
    const QByteArray bytes = file.readAll();
    record.reserve(size_t(bytes.size()));
    for (const char byte : bytes)
        record.push_back(uint8_t(byte));
    return record;
}

inline bool meshFromBytes(const rust::Vec<uint8_t>& bytes, QTemporaryDir& scratch,
                          Slic3r::Domain::TriangleMesh& mesh)
{
    if (bytes.empty())
        return false;
    const QString path = scratch.filePath(QStringLiteral("in.stl"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) !=
        qint64(bytes.size()))
        return false;
    file.close();

    const auto loaded = Slic3r::Biz::load_stl(path.toStdString());
    if (!loaded)
        return false;
    mesh = *loaded;
    return true;
}

/// Sixteen `f64`, row-major, four rows of four: the shape the file writes.
inline rust::Vec<double> matrixRecord(const Slic3r::Domain::Transformation& transform)
{
    rust::Vec<double> record;
    record.reserve(16);
    const Slic3r::Domain::Transform3d& matrix = transform.get_matrix();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            record.push_back(matrix(row, column));
    return record;
}

inline Slic3r::Domain::Transformation matrixFromRecord(const rust::Vec<double>& record)
{
    Slic3r::Domain::Transformation transform;
    if (record.size() != 16)
        return transform;
    Slic3r::Domain::Transform3d matrix = Slic3r::Domain::Transform3d::Identity();
    size_t at = 0;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix(row, column) = record[at++];
    transform.set_matrix(matrix);
    return transform;
}

inline ProjectPaint paintRecord(
    const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data)
{
    ProjectPaint record;
    for (const auto& mapping : data.triangles_to_split) {
        record.triangles.push_back(int64_t(mapping.triangle_idx));
        record.triangles.push_back(int64_t(mapping.bitstream_start_idx));
    }
    std::string bits;
    bits.reserve(data.bitstream.size());
    for (const bool bit : data.bitstream)
        bits.push_back(bit ? '1' : '0');
    record.bitstream = std::move(bits);
    for (size_t state = 0; state < data.used_states.size(); ++state)
        if (data.used_states[state])
            record.states.push_back(uint32_t(state));
    return record;
}

inline Slic3r::Domain::TriangleSelector::TriangleSplittingData paintFromRecord(
    const ProjectPaint& record)
{
    Slic3r::Domain::TriangleSelector::TriangleSplittingData data;
    for (size_t at = 0; at + 1 < record.triangles.size(); at += 2)
        data.triangles_to_split.emplace_back(int(record.triangles[at]),
                                             size_t(record.triangles[at + 1]));

    const std::string bits = static_cast<std::string>(record.bitstream);
    data.bitstream.reserve(bits.size());
    for (const char bit : bits)
        data.bitstream.push_back(bit == '1');

    data.used_states.assign(Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT, false);
    for (const uint32_t state : record.states)
        if (state < data.used_states.size())
            data.used_states[state] = true;
    return data;
}

inline ProjectSetting settingRecord(const QString& name, const QVariant& value)
{
    ProjectSetting record;
    record.name = name.toStdString();
    record.kind = 0;
    record.text = value.toString().toStdString();
    switch (value.typeId()) {
    case QMetaType::Bool:
        record.kind = 1;
        record.truth = value.toBool();
        break;
    case QMetaType::Int:
    case QMetaType::LongLong:
        record.kind = 2;
        record.number = double(value.toInt());
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        record.kind = 3;
        record.number = value.toDouble();
        break;
    case QMetaType::QVariantMap: {
        const QVariantMap object = value.toMap();
        record.kind = 4;
        record.number = object.value(QStringLiteral("value")).toDouble();
        record.truth = object.value(QStringLiteral("is_percent")).toBool();
        break;
    }
    default: break;
    }
    return record;
}

inline QVariant settingValue(const ProjectSetting& record)
{
    switch (record.kind) {
    case 1: return QVariant(record.truth);
    case 2: return QVariant(int(record.number));
    case 3: return QVariant(record.number);
    case 4: {
        QVariantMap object;
        object.insert(QStringLiteral("value"), record.number);
        object.insert(QStringLiteral("is_percent"), record.truth);
        return object;
    }
    default: return QVariant(fromRust(record.text));
    }
}

}

/// Writes the document as one JSON file. Meshes are embedded as base64 binary STL,
/// so a project is one file that moves between machines without the models it was
/// built from.
///
/// The document is Rust's (`src/app/src/ProjectFile.rs`). This header is the adapter
/// that gives it the engine's mesh and transform, which is where those still live.
inline QByteArray serialiseProject(const std::vector<ProjectObject>& objects,
                                  const ProjectMetadata& metadata)
{
    const auto scratchDir = next_layer::makeScratchDir();
    QTemporaryDir& scratch = *scratchDir;

    rust::Vec<ProjectObjectRecord> records;
    for (const ProjectObject& object : objects) {
        ProjectObjectRecord record;
        record.name = object.name.toStdString();
        record.source = object.sourcePath.toStdString();
        record.plate = object.plate;
        record.visible = object.visible;
        record.matrix = projectFileDetail::matrixRecord(object.transform);
        record.mesh = projectFileDetail::meshBytes(object.mesh, scratch);
        if (record.mesh.empty())
            return {};

        for (const ProjectVolume& volume : object.volumes) {
            ProjectVolumeRecord stored;
            stored.name = volume.name.toStdString();
            stored.kind = volume.type.toStdString();
            stored.visible = volume.visible;
            stored.matrix = projectFileDetail::matrixRecord(volume.transform);
            stored.mesh = projectFileDetail::meshBytes(volume.mesh, scratch);
            if (stored.mesh.empty())
                return {};
            record.volumes.push_back(std::move(stored));
        }

        for (auto it = object.settings.constBegin(); it != object.settings.constEnd(); ++it)
            record.settings.push_back(projectFileDetail::settingRecord(it.key(), it.value()));

        for (const Slic3r::Domain::ZHeightPair& pair : object.layerHeights) {
            record.layer_heights.push_back(pair.z);
            record.layer_heights.push_back(pair.layer_height);
        }

        record.support_paint = projectFileDetail::paintRecord(object.supportPaint);
        record.seam_paint = projectFileDetail::paintRecord(object.seamPaint);
        record.tool_paint = projectFileDetail::paintRecord(object.toolPaint);
        records.push_back(std::move(record));
    }

    ProjectMetadataRecord setup;
    setup.application = metadata.applicationVersion.toStdString();
    setup.printer = metadata.printer.toStdString();
    setup.process = metadata.process.toStdString();
    setup.filament = metadata.filament.toStdString();
    setup.active_plate = metadata.activePlate;
    setup.plate_count = metadata.plateCount;
    for (const QString& name : metadata.plateNames)
        setup.plate_names.push_back(name.toStdString());
    for (const bool locked : metadata.plateLocked)
        setup.plate_locked.push_back(locked);
    for (const QString& colour : metadata.toolColours)
        setup.tool_colours.push_back(colour.toStdString());
    for (const ProjectLayerGcode& marker : metadata.layerGcodes) {
        ProjectMarkerRecord entry;
        entry.z = marker.z;
        entry.kind = marker.type.toStdString();
        entry.text = marker.text.toStdString();
        setup.markers.push_back(std::move(entry));
    }

    return projectFileDetail::toByteArray(serialise_project(
        rust::Slice<const ProjectObjectRecord>(records.data(), records.size()), setup));
}

/// Reads a project file back. Returns false and fills \a error when the file is
/// not a project, is newer than this build, or names a mesh that cannot be read.
inline bool parseProject(const QByteArray& body, std::vector<ProjectObject>& objects,
                         ProjectMetadata& metadata, QString& error)
{
    ProjectOutcome outcome = parse_project(projectFileDetail::bytesOf(body));
    if (!outcome.ok) {
        error = projectFileDetail::fromRust(outcome.error);
        return false;
    }

    const auto scratchDir = next_layer::makeScratchDir();
    QTemporaryDir& scratch = *scratchDir;

    objects.clear();
    for (const ProjectObjectRecord& record : outcome.objects) {
        ProjectObject object;
        object.name = projectFileDetail::fromRust(record.name);
        object.sourcePath = projectFileDetail::fromRust(record.source);
        object.plate = record.plate;
        object.visible = record.visible;
        object.transform = projectFileDetail::matrixFromRecord(record.matrix);
        if (!projectFileDetail::meshFromBytes(record.mesh, scratch, object.mesh)) {
            error = QStringLiteral("object \"%1\" has no readable mesh").arg(object.name);
            return false;
        }

        for (const ProjectVolumeRecord& stored : record.volumes) {
            ProjectVolume volume;
            volume.name = projectFileDetail::fromRust(stored.name);
            volume.type = projectFileDetail::fromRust(stored.kind);
            volume.visible = stored.visible;
            volume.transform = projectFileDetail::matrixFromRecord(stored.matrix);
            if (!projectFileDetail::meshFromBytes(stored.mesh, scratch, volume.mesh)) {
                error = QStringLiteral("volume \"%1\" of \"%2\" has no readable mesh")
                            .arg(volume.name, object.name);
                return false;
            }
            object.volumes.push_back(std::move(volume));
        }

        for (const ProjectSetting& setting : record.settings)
            object.settings.insert(projectFileDetail::fromRust(setting.name),
                                   projectFileDetail::settingValue(setting));

        for (size_t at = 0; at + 1 < record.layer_heights.size(); at += 2) {
            Slic3r::Domain::ZHeightPair heights;
            heights.z = record.layer_heights[at];
            heights.layer_height = record.layer_heights[at + 1];
            object.layerHeights.push_back(heights);
        }

        object.supportPaint = projectFileDetail::paintFromRecord(record.support_paint);
        object.seamPaint = projectFileDetail::paintFromRecord(record.seam_paint);
        object.toolPaint = projectFileDetail::paintFromRecord(record.tool_paint);
        objects.push_back(std::move(object));
    }

    const ProjectMetadataRecord& setup = outcome.metadata;
    metadata.applicationVersion = projectFileDetail::fromRust(setup.application);
    metadata.printer = projectFileDetail::fromRust(setup.printer);
    metadata.process = projectFileDetail::fromRust(setup.process);
    metadata.filament = projectFileDetail::fromRust(setup.filament);
    metadata.activePlate = setup.active_plate;
    metadata.plateCount = setup.plate_count;
    metadata.plateNames.clear();
    for (const rust::String& name : setup.plate_names)
        metadata.plateNames.append(projectFileDetail::fromRust(name));
    metadata.plateLocked.clear();
    for (const bool locked : setup.plate_locked)
        metadata.plateLocked.append(locked);
    metadata.toolColours.clear();
    for (const rust::String& colour : setup.tool_colours)
        metadata.toolColours.append(projectFileDetail::fromRust(colour));
    metadata.layerGcodes.clear();
    for (const ProjectMarkerRecord& marker : setup.markers) {
        ProjectLayerGcode entry;
        entry.z = marker.z;
        entry.type = projectFileDetail::fromRust(marker.kind);
        entry.text = projectFileDetail::fromRust(marker.text);
        metadata.layerGcodes.push_back(std::move(entry));
    }
    return true;
}

/// The project file version this build writes, and the newest it will read.
inline int formatVersion()
{
    return project_format_version();
}

/// One paint channel as text, and back.
///
/// A channel the engine has no consumer for -- tool paint today -- still belongs to
/// the document, so it travels in the application's own bytes rather than being
/// dropped. The encoding is the project file's own, so a value written years ago
/// still reads.
inline QByteArray paintToText(
    const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data)
{
    return projectFileDetail::toByteArray(
        paint_to_text(projectFileDetail::paintRecord(data)));
}

inline bool paintFromText(const QByteArray& text,
                          Slic3r::Domain::TriangleSelector::TriangleSplittingData& data)
{
    const ProjectPaintOutcome outcome = paint_from_text(projectFileDetail::bytesOf(text));
    if (!outcome.ok)
        return false;
    data = projectFileDetail::paintFromRecord(outcome.paint);
    return true;
}

}
