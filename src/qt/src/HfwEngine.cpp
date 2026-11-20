// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "HfwEngine.hpp"

#include "Paint.hpp"
#include "hfw.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

namespace next_layer {
namespace {

QString developmentBinary()
{
    return QStringLiteral("/home/cwebb/dev/HFW/target/release/hfw-cli");
}

QString tail(const QString& text, int lines)
{
    const QStringList all = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const int first = std::max(0, int(all.size()) - lines);
    return all.mid(first).join(QLatin1Char('\n')).trimmed();
}

/// The engine's own words for why the call that just failed did.
QString engineError()
{
    const char* message = hfw_last_error();
    return message != nullptr && *message != '\0' ? QString::fromUtf8(message)
                                                  : QStringLiteral("the engine gave no reason");
}

/// Owns a plate handle, so an early return cannot leak one.
class PlateHandle
{
public:
    PlateHandle() : m_plate(hfw_plate_new()) {}
    PlateHandle(const PlateHandle&) = delete;
    PlateHandle& operator=(const PlateHandle&) = delete;
    ~PlateHandle()
    {
        if (m_plate != nullptr)
            hfw_plate_free(m_plate);
    }

    HfwPlate* get() const { return m_plate; }

private:
    HfwPlate* m_plate;
};

/// One setting as the text a preset writes it: `50%` for a percentage, `1`/`0`
/// for a flag, a comma-joined list for a vector.
QString settingText(const QJsonValue& value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 10);
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (!object.contains(QLatin1String("value")))
            return {};
        const QString number = settingText(object.value(QLatin1String("value")));
        return object.value(QLatin1String("is_percent")).toBool() ? number + QLatin1Char('%')
                                                                 : number;
    }
    if (value.isArray()) {
        QStringList parts;
        const QJsonArray items = value.toArray();
        for (const QJsonValue& item : items)
            parts.append(settingText(item));
        return parts.join(QLatin1Char(','));
    }
    return {};
}

/// The object's uniform layer height, when it carries one as an override.
///
/// A layer height is not a setting on HFW's plate: it decides which planes the object
/// is sliced on, which is a channel of its own, and the engine refuses it as an object
/// setting for exactly that reason. It is taken out of the overrides here and folded
/// into the object's layer height pairs, so a uniform override and a variable profile
/// reach the engine the same way. Zero means the object carries none.
double layerHeightOverride(const PlateSliceObject& object)
{
    const QJsonObject settings = QJsonDocument::fromJson(object.settingsJson).object();
    if (settings.contains(QLatin1String("layer_height"))) {
        const double height = settingText(settings.value(QLatin1String("layer_height"))).toDouble();
        if (height > 0.0)
            return height;
    }
    const auto foreign = object.foreignSettings.constFind(QStringLiteral("layer_height"));
    if (foreign != object.foreignSettings.constEnd()) {
        const double height = foreign.value().toDouble();
        if (height > 0.0)
            return height;
    }
    return 0.0;
}

/// The object's own settings as the engine reads them: its JSON overrides first,
/// then the ones its file carried in another slicer's dialect, which are that
/// file's words and go across unchanged. A name the engine does not know is
/// refused with the engine's own reason rather than dropped here.
bool applySettings(HfwPlate* plate, quint64 id, const PlateSliceObject& object, QString& error)
{
    QHash<QString, QString> overrides;
    const QJsonObject settings = QJsonDocument::fromJson(object.settingsJson).object();
    for (auto entry = settings.constBegin(); entry != settings.constEnd(); ++entry) {
        const QString value = settingText(entry.value());
        if (!value.isEmpty())
            overrides.insert(entry.key(), value);
    }
    for (auto entry = object.foreignSettings.constBegin();
         entry != object.foreignSettings.constEnd(); ++entry)
        overrides.insert(entry.key(), entry.value());
    overrides.remove(QStringLiteral("layer_height"));

    QStringList names;
    for (auto entry = overrides.constBegin(); entry != overrides.constEnd(); ++entry)
        names.append(entry.key());
    names.sort();
    for (const QString& name : names) {
        const QByteArray utf8Name = name.toUtf8();
        const QByteArray utf8Value = overrides.value(name).toUtf8();
        if (hfw_plate_object_set(plate, id, utf8Name.constData(), utf8Value.constData()) != 0) {
            error = QStringLiteral("%1: setting %2 = %3: %4")
                        .arg(object.name, name, overrides.value(name), engineError());
            return false;
        }
    }
    return true;
}

/// The engine's handle for one of the plate's meshes.
///
/// The app's mesh is a triangle list in millimetres; the engine takes an array of
/// vertices plus triangle numbers and keeps the order it is given, so anything the
/// app holds per triangle stays aligned with it.
HfwMesh* meshHandle(const Slic3r::Domain::TriangleMesh& mesh, const QString& name, QString& error)
{
    const indexed_triangle_set& its = mesh.its;
    QVector<double> vertices;
    vertices.reserve(qsizetype(its.vertices.size()) * 3);
    for (const Slic3r::Domain::Vec3f& vertex : its.vertices) {
        vertices.append(vertex.x());
        vertices.append(vertex.y());
        vertices.append(vertex.z());
    }
    QVector<uint32_t> indices;
    indices.reserve(qsizetype(its.indices.size()) * 3);
    for (const auto& face : its.indices) {
        indices.append(face[0]);
        indices.append(face[1]);
        indices.append(face[2]);
    }
    if (vertices.isEmpty() || indices.isEmpty()) {
        error = QStringLiteral("%1 has no triangles").arg(name);
        return nullptr;
    }
    HfwMesh* handle = hfw_mesh_new(vertices.constData(), size_t(vertices.size() / 3),
                                   indices.constData(), size_t(indices.size() / 3));
    if (handle == nullptr)
        error = QStringLiteral("%1: %2").arg(name, engineError());
    return handle;
}

int markerKind(const QString& type)
{
    if (type == QLatin1String("pause"))
        return HFW_MARKER_PAUSE;
    if (type == QLatin1String("color") || type == QLatin1String("colour"))
        return HFW_MARKER_COLOUR;
    return HFW_MARKER_CUSTOM;
}

int volumeKind(Slic3r::Domain::ModelVolumeType type)
{
    switch (type) {
    case Slic3r::Domain::ModelVolumeType::NEGATIVE_VOLUME: return HFW_VOLUME_NEGATIVE;
    case Slic3r::Domain::ModelVolumeType::PARAMETER_MODIFIER: return HFW_VOLUME_MODIFIER;
    case Slic3r::Domain::ModelVolumeType::SUPPORT_BLOCKER: return HFW_VOLUME_SUPPORT_BLOCKER;
    case Slic3r::Domain::ModelVolumeType::SUPPORT_ENFORCER: return HFW_VOLUME_SUPPORT_ENFORCER;
    default: return HFW_VOLUME_PART;
    }
}

/// Attaches one modeller volume to an object, then the settings it prints with.
///
/// Only a modifier has a region of its own to print differently, so a key set on
/// any other kind would be stored and ignored; the engine refuses it, and this
/// only asks where it makes sense.
bool addVolume(HfwPlate* plate, quint64 id, const PlateSliceObject& object,
               const PlateSliceVolume& volume, QString& error)
{
    HfwMesh* mesh = meshHandle(volume.mesh, object.name, error);
    if (mesh == nullptr)
        return false;
    const int kind = volumeKind(volume.type);
    const int added = hfw_plate_object_add_volume(plate, id, kind, mesh);
    hfw_mesh_free(mesh);
    if (added != 0) {
        error = QStringLiteral("%1: volume: %2").arg(object.name, engineError());
        return false;
    }
    if (kind != HFW_VOLUME_MODIFIER)
        return true;

    const QJsonObject settings = QJsonDocument::fromJson(volume.settingsJson).object();
    QStringList names;
    for (auto entry = settings.constBegin(); entry != settings.constEnd(); ++entry)
        names.append(entry.key());
    names.sort();
    const size_t index = size_t(hfw_plate_object_volume_count(plate, id) - 1);
    for (const QString& name : names) {
        const QString value = settingText(settings.value(name));
        if (value.isEmpty())
            continue;
        const QByteArray utf8Name = name.toUtf8();
        const QByteArray utf8Value = value.toUtf8();
        if (hfw_plate_volume_set(plate, id, index, utf8Name.constData(), utf8Value.constData())
            != 0) {
            error = QStringLiteral("%1: volume setting %2 = %3: %4")
                        .arg(object.name, name, value, engineError());
            return false;
        }
    }
    return true;
}

/// Paints one channel onto the object, reducing the engine's split triangles to the
/// one state per facet HFW holds.
///
/// The app's paint records which pieces of a facet were painted; HFW's paint is a
/// state per facet. `paintFacetStates` gives a facet the state that covers most of
/// it, so an edge painted across a facet lands on that facet rather than being lost.
/// The mesh here is already placed for the plate, and placement keeps the triangle
/// order, so the states still line up with HFW's copy of the mesh.
bool setPaint(HfwPlate* plate, quint64 id, const PlateSliceObject& object,
              const Slic3r::Domain::TriangleSelector::TriangleSplittingData& painted, int channel,
              QString& error)
{
    const QVector<quint8> states = paintFacetStates(object.mesh, painted);
    if (states.isEmpty())
        return true;
    // `quint8` and the engine's byte are both unsigned of one byte; the engine keeps
    // the pointer's lifetime out of the call, so the vector's own buffer is enough.
    if (hfw_plate_object_paint(plate, id, channel,
                              reinterpret_cast<const uint8_t*>(states.constData()),
                              size_t(states.size()))
        != 0) {
        error = QStringLiteral("%1: paint: %2").arg(object.name, engineError());
        return false;
    }
    return true;
}

bool setLayerHeights(HfwPlate* plate, quint64 id, const PlateSliceObject& object, QString& error)
{
    QVector<double> pairs;
    if (object.layerHeights.empty()) {
        const double uniform = layerHeightOverride(object);
        if (uniform <= 0.0)
            return true;
        pairs.append(0.0);
        pairs.append(uniform);
    } else {
        pairs.reserve(qsizetype(object.layerHeights.size()) * 2);
        for (const Slic3r::Domain::ZHeightPair& pair : object.layerHeights) {
            pairs.append(pair.z);
            pairs.append(pair.layer_height);
        }
    }
    if (hfw_plate_object_layer_heights(plate, id, pairs.constData(), size_t(pairs.size() / 2))
        != 0) {
        error = QStringLiteral("%1: layer heights: %2").arg(object.name, engineError());
        return false;
    }
    return true;
}

bool setMarkers(HfwPlate* plate, const QVector<PlateLayerGcode>& layerGcodes, QString& error)
{
    if (layerGcodes.isEmpty())
        return true;
    QVector<int> kinds;
    QVector<double> zs;
    QVector<QByteArray> texts;
    kinds.reserve(layerGcodes.size());
    zs.reserve(layerGcodes.size());
    texts.reserve(layerGcodes.size());
    for (const PlateLayerGcode& marker : layerGcodes) {
        kinds.append(markerKind(marker.type));
        zs.append(marker.z);
        texts.append(marker.text.toUtf8());
    }
    QVector<const char*> pointers;
    pointers.reserve(texts.size());
    for (const QByteArray& text : texts)
        pointers.append(text.constData());
    if (hfw_plate_set_markers(plate, kinds.constData(), zs.constData(), pointers.constData(),
                             size_t(layerGcodes.size()))
        != 0) {
        error = QStringLiteral("the plate's timeline: %1").arg(engineError());
        return false;
    }
    return true;
}

bool setInLayer(HfwPlate* plate, const QVector<PlateInLayerPolyline>& inLayer, QString& error)
{
    if (inLayer.isEmpty())
        return true;
    QVector<double> zs;
    QVector<size_t> counts;
    QVector<double> widths;
    QVector<double> heights;
    QVector<double> speeds;
    QVector<size_t> starts;
    QVector<double> points;
    QVector<QByteArray> prefixes;
    for (const PlateInLayerPolyline& stroke : inLayer) {
        zs.append(stroke.z);
        counts.append(size_t(stroke.points.size()));
        widths.append(stroke.line_width);
        heights.append(stroke.layer_height);
        speeds.append(stroke.speed);
        starts.append(size_t(points.size() / 2));
        for (const QPointF& point : stroke.points) {
            points.append(point.x());
            points.append(point.y());
        }
        prefixes.append(stroke.prefix.toUtf8());
    }
    QVector<const char*> pointers;
    pointers.reserve(prefixes.size());
    for (const QByteArray& prefix : prefixes)
        pointers.append(prefix.constData());
    if (hfw_plate_set_in_layer(plate, zs.constData(), counts.constData(), widths.constData(),
                              heights.constData(), speeds.constData(), starts.constData(),
                              points.constData(), pointers.constData(), size_t(inLayer.size()))
        != 0) {
        error = QStringLiteral("the plate's in-layer strokes: %1").arg(engineError());
        return false;
    }
    return true;
}

/// Builds the document the engine slices: one object per plate entry, with its
/// overrides, its volumes and their kinds, its layer heights, and the plate's
/// timeline and strokes.
bool fillPlate(HfwPlate* plate, const QVector<PlateSliceObject>& objects,
               const QVector<PlateLayerGcode>& layerGcodes,
               const QVector<PlateInLayerPolyline>& inLayer, QString& error)
{
    for (const PlateSliceObject& object : objects) {
        HfwMesh* mesh = meshHandle(object.mesh, object.name, error);
        if (mesh == nullptr)
            return false;
        const quint64 id = hfw_plate_add_positioned(plate, mesh);
        hfw_mesh_free(mesh);
        if (id == 0) {
            error = QStringLiteral("%1: %2").arg(object.name, engineError());
            return false;
        }
        const QByteArray name = object.name.toUtf8();
        hfw_plate_object_name(plate, id, name.constData());

        if (!applySettings(plate, id, object, error))
            return false;
        for (const PlateSliceVolume& volume : object.volumes) {
            if (!addVolume(plate, id, object, volume, error))
                return false;
        }
        if (!setLayerHeights(plate, id, object, error))
            return false;
        if (!setPaint(plate, id, object, object.supportPaint, HFW_PAINT_SUPPORT, error))
            return false;
        if (!setPaint(plate, id, object, object.seamPaint, HFW_PAINT_SEAM, error))
            return false;
    }
    return setMarkers(plate, layerGcodes, error) && setInLayer(plate, inLayer, error);
}

} // namespace

QString hfwBinary()
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString configured = environment.value(QStringLiteral("NEXT_LAYER_HFW"));
    if (!configured.isEmpty())
        return QFileInfo::exists(configured) ? configured : QString();

    // An installed copy keeps the engine in its own directory beside the
    // executable, so the two travel together through a package.
    const QString beside =
        QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../libexec/next-layer"))
            .filePath(QStringLiteral("hfw-cli"));
    if (QFileInfo(beside).isExecutable())
        return beside;

    const QString found = QStandardPaths::findExecutable(QStringLiteral("hfw-cli"));
    if (!found.isEmpty())
        return found;

    return QFileInfo(developmentBinary()).isExecutable() ? developmentBinary() : QString();
}

QString hfwUnavailableReason()
{
    if (!hfwBinary().isEmpty())
        return {};
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!environment.value(QStringLiteral("NEXT_LAYER_HFW")).isEmpty())
        return QStringLiteral("NEXT_LAYER_HFW points at a file that is not there");
    return QStringLiteral("hfw-cli was not found beside the application, on PATH or in the "
                          "development build tree; set NEXT_LAYER_HFW to its path");
}

bool writePlateFile(const QVector<PlateSliceObject>& objects,
                    const QVector<PlateLayerGcode>& layerGcodes,
                    const QVector<PlateInLayerPolyline>& inLayer, const QString& path,
                    QString& error)
{
    if (objects.isEmpty()) {
        error = QStringLiteral("no object on the plate");
        return false;
    }

    PlateHandle plate;
    if (plate.get() == nullptr) {
        error = QStringLiteral("the engine could not create a plate: %1").arg(engineError());
        return false;
    }
    if (!fillPlate(plate.get(), objects, layerGcodes, inLayer, error))
        return false;

    size_t length = 0;
    char* bytes = hfw_plate_project_bytes(plate.get(), &length);
    if (bytes == nullptr) {
        error = QStringLiteral("the engine could not write the plate: %1").arg(engineError());
        return false;
    }
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    const qint64 written = opened ? file.write(bytes, qint64(length)) : -1;
    file.close();
    hfw_string_free(bytes);
    if (!opened || written != qint64(length)) {
        error = QStringLiteral("could not write %1").arg(path);
        return false;
    }
    return true;
}

bool sliceWithHfw(const QVector<PlateSliceObject>& objects, const QString& configPath,
                  const QString& gcodePath, QString& error,
                  const QVector<PlateLayerGcode>& layerGcodes,
                  const QVector<PlateInLayerPolyline>& inLayer, bool placeOnBed)
{
    if (objects.isEmpty()) {
        error = QStringLiteral("no object on the plate");
        return false;
    }
    for (const PlateLayerGcode& marker : layerGcodes) {
        if (marker.type == QLatin1String("tool")) {
            error = QStringLiteral("the plate timeline changes tool at Z%1, and this engine "
                                  "prints with one extruder; remove the tool change or slice "
                                  "another plate")
                        .arg(marker.z, 0, 'f', 2);
            return false;
        }
    }
    const QString binary = hfwBinary();
    if (binary.isEmpty()) {
        error = hfwUnavailableReason();
        return false;
    }

    QTemporaryDir work;
    if (!work.isValid()) {
        error = QStringLiteral("could not create a scratch directory");
        return false;
    }

    const QString platePath = work.filePath(QStringLiteral("plate.hfw"));
    if (!writePlateFile(objects, layerGcodes, inLayer, platePath, error))
        return false;

    QStringList arguments;
    arguments << QStringLiteral("--plate") << platePath << QStringLiteral("--preset") << configPath
              << QStringLiteral("--gcode") << gcodePath;
    if (placeOnBed)
        arguments << QStringLiteral("--place");

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(binary, arguments);
    if (!process.waitForStarted(15000)) {
        error = QStringLiteral("could not start %1").arg(binary);
        return false;
    }
    if (!process.waitForFinished(-1)) {
        process.kill();
        error = QStringLiteral("%1 did not finish").arg(binary);
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    // What the engine did with the document, which is otherwise the one thing about a
    // slice this application cannot show: what the file carried, and whether a stroke
    // landed on a printed layer.
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QLatin1String("warning:")) || line.contains(QLatin1String("Carries:")))
            std::fprintf(stderr, "%s\n", line.trimmed().toUtf8().constData());
    }
    const bool wrote = QFileInfo(gcodePath).size() > 0;
    const bool verified = output.contains(QStringLiteral("verify RESULT PASS"));
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && wrote && verified)
        return true;

    if (!wrote) {
        error = QStringLiteral("hfw wrote no G-code: %1").arg(tail(output, 6));
        return false;
    }
    if (!verified) {
        error = QStringLiteral("the file failed HFW's own verifier: %1").arg(tail(output, 6));
        return false;
    }
    error = tail(output, 6);
    return false;
}

} // namespace next_layer
