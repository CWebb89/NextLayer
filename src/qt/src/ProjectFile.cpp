#include "ProjectFile.hpp"

#include "Paint.hpp"

#include <Slic3r/Biz/Format/STL.hpp>
#include <Slic3r/Domain/Model.hpp>
#include <Slic3r/Domain/ModelObject.hpp>
#include <Slic3r/Domain/ModelVolume.hpp>

#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QVariantMap>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <array>

namespace next_layer {

namespace {

using nlohmann::ordered_json;
using Slic3r::Domain::Transformation;
using Slic3r::Domain::TriangleSelector::TriangleBitStreamMapping;
using Slic3r::Domain::TriangleSelector::TriangleSplittingData;

constexpr int kFormatVersion = 1;
constexpr const char* kMagic = "next-layer project";

QString bitsToString(const std::vector<bool>& bits)
{
    QString text;
    text.reserve(int(bits.size()));
    for (const bool bit : bits)
        text.append(bit ? QLatin1Char('1') : QLatin1Char('0'));
    return text;
}

std::vector<bool> stringToBits(const QString& text)
{
    std::vector<bool> bits;
    bits.reserve(size_t(text.size()));
    for (const QChar character : text)
        bits.push_back(character == QLatin1Char('1'));
    return bits;
}

ordered_json paintToJson(const TriangleSplittingData& data)
{
    ordered_json json = ordered_json::object();
    if (data.triangles_to_split.empty())
        return json;

    ordered_json splits = ordered_json::array();
    for (const TriangleBitStreamMapping& mapping : data.triangles_to_split)
        splits.push_back(ordered_json::array({mapping.triangle_idx, mapping.bitstream_start_idx}));

    ordered_json usedStates = ordered_json::array();
    for (size_t state = 0; state < data.used_states.size(); ++state)
        if (data.used_states[state])
            usedStates.push_back(state);

    json["triangles"] = std::move(splits);
    json["bitstream"] = bitsToString(data.bitstream).toStdString();
    json["states"] = std::move(usedStates);
    return json;
}

TriangleSplittingData paintFromJson(const ordered_json& json)
{
    TriangleSplittingData data;
    if (!json.is_object() || !json.contains("triangles"))
        return data;

    for (const ordered_json& entry : json["triangles"])
        data.triangles_to_split.emplace_back(entry[0].get<int>(), entry[1].get<int>());

    data.bitstream = stringToBits(QString::fromStdString(json.value("bitstream", std::string())));
    data.used_states.assign(Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT, false);
    for (const ordered_json& state : json.value("states", ordered_json::array()))
        data.used_states[state.get<size_t>()] = true;
    return data;
}

ordered_json valueToJson(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool: return ordered_json(value.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong: return ordered_json(value.toInt());
    case QMetaType::Double:
    case QMetaType::Float: return ordered_json(value.toDouble());
    case QMetaType::QVariantMap: {
        const QVariantMap object = value.toMap();
        return ordered_json{{"value", object.value(QStringLiteral("value")).toDouble()},
                            {"is_percent", object.value(QStringLiteral("is_percent")).toBool()}};
    }
    default: return ordered_json(value.toString().toStdString());
    }
}

QVariant jsonToValue(const ordered_json& json)
{
    if (json.is_boolean())
        return json.get<bool>();
    if (json.is_number_integer())
        return json.get<int>();
    if (json.is_number())
        return json.get<double>();
    if (json.is_object()) {
        QVariantMap object;
        object.insert(QStringLiteral("value"), json.value("value", 0.0));
        object.insert(QStringLiteral("is_percent"), json.value("is_percent", false));
        return object;
    }
    return QString::fromStdString(json.is_string() ? json.get<std::string>() : json.dump());
}

QString meshToBase64(const Slic3r::Domain::TriangleMesh& mesh, QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("mesh.stl"));
    QFile::remove(path);
    if (!Slic3r::Biz::store_stl(path.toStdString(), mesh, true, "next-layer"))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(file.readAll().toBase64());
}

bool meshFromBase64(const QString& base64, QTemporaryDir& dir, Slic3r::Domain::TriangleMesh& mesh)
{
    const QByteArray bytes = QByteArray::fromBase64(base64.toLatin1());
    if (bytes.isEmpty())
        return false;
    const QString path = dir.filePath(QStringLiteral("in.stl"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(bytes) != bytes.size())
        return false;
    file.close();

    const auto loaded = Slic3r::Biz::load_stl(path.toStdString());
    if (!loaded)
        return false;
    mesh = *loaded;
    return true;
}

ordered_json matrixToJson(const Transformation& trafo)
{
    ordered_json json = ordered_json::array();
    const Slic3r::Domain::Transform3d& matrix = trafo.get_matrix();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            json.push_back(matrix(row, column));
    return json;
}

Transformation matrixFromJson(const ordered_json& json)
{
    Transformation trafo;
    if (!json.is_array() || json.size() != 16)
        return trafo;
    Slic3r::Domain::Transform3d matrix = Slic3r::Domain::Transform3d::Identity();
    int index = 0;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix(row, column) = json[index++].get<double>();
    trafo.set_matrix(matrix);
    return trafo;
}

}

QByteArray serialiseProject(const std::vector<ProjectObject>& objects,
                            const ProjectMetadata& metadata)
{
    ordered_json root = ordered_json::object();
    root["format"] = kMagic;
    root["version"] = kFormatVersion;
    root["application"] = metadata.applicationVersion.toStdString();

    ordered_json setup = ordered_json::object();
    setup["printer"] = metadata.printer.toStdString();
    setup["process"] = metadata.process.toStdString();
    setup["filament"] = metadata.filament.toStdString();
    root["print_setup"] = std::move(setup);

    root["active_plate"] = metadata.activePlate;
    root["plates"] = metadata.plateCount;

    ordered_json plateFlags = ordered_json::array();
    for (int i = 0; i < metadata.plateCount; ++i) {
        ordered_json entry = ordered_json::object();
        entry["name"] = i < metadata.plateNames.size()
                            ? metadata.plateNames[i].toStdString() : std::string();
        entry["locked"] = i < metadata.plateLocked.size() ? metadata.plateLocked[i] : false;
        plateFlags.push_back(std::move(entry));
    }
    root["plate_flags"] = std::move(plateFlags);

    ordered_json tools = ordered_json::array();
    for (const QString& colour : metadata.toolColours)
        tools.push_back(colour.toStdString());
    root["tool_colours"] = std::move(tools);

    ordered_json markers = ordered_json::array();
    for (const ProjectLayerGcode& marker : metadata.layerGcodes) {
        ordered_json entry = ordered_json::object();
        entry["z"] = marker.z;
        entry["type"] = marker.type.toStdString();
        entry["text"] = marker.text.toStdString();
        markers.push_back(std::move(entry));
    }
    root["layer_gcodes"] = std::move(markers);

    QTemporaryDir scratch;
    ordered_json list = ordered_json::array();
    for (const ProjectObject& object : objects) {
        ordered_json entry = ordered_json::object();
        entry["name"] = object.name.toStdString();
        entry["source"] = object.sourcePath.toStdString();
        entry["plate"] = object.plate;
        entry["visible"] = object.visible;
        entry["matrix"] = matrixToJson(object.transform);

        const QString mesh = meshToBase64(object.mesh, scratch);
        if (mesh.isEmpty())
            return {};
        entry["mesh"] = mesh.toStdString();

        ordered_json volumes = ordered_json::array();
        for (const ProjectVolume& volume : object.volumes) {
            const QString volumeMesh = meshToBase64(volume.mesh, scratch);
            if (volumeMesh.isEmpty())
                return {};
            ordered_json stored = ordered_json::object();
            stored["name"] = volume.name.toStdString();
            stored["type"] = volume.type.toStdString();
            stored["visible"] = volume.visible;
            stored["matrix"] = matrixToJson(volume.transform);
            stored["mesh"] = volumeMesh.toStdString();
            volumes.push_back(std::move(stored));
        }
        entry["volumes"] = std::move(volumes);

        ordered_json settings = ordered_json::object();
        for (auto it = object.settings.constBegin(); it != object.settings.constEnd(); ++it)
            settings[it.key().toStdString()] = valueToJson(it.value());
        entry["settings"] = std::move(settings);

        ordered_json profile = ordered_json::array();
        for (const Slic3r::Domain::ZHeightPair& pair : object.layerHeights)
            profile.push_back(ordered_json::array({pair.z, pair.layer_height}));
        entry["layer_heights"] = std::move(profile);

        entry["support_paint"] = paintToJson(object.supportPaint);
        entry["seam_paint"] = paintToJson(object.seamPaint);
        entry["tool_paint"] = paintToJson(object.toolPaint);
        list.push_back(std::move(entry));
    }
    root["objects"] = std::move(list);

    return QByteArray::fromStdString(root.dump(1));
}

bool parseProject(const QByteArray& body, std::vector<ProjectObject>& objects,
                  ProjectMetadata& metadata, QString& error)
{
    ordered_json root = ordered_json::parse(body.constData(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = QStringLiteral("this is not a next-layer project file");
        return false;
    }
    if (root.value("format", std::string()) != kMagic) {
        error = QStringLiteral("this is not a next-layer project file");
        return false;
    }
    if (root.value("version", 0) > kFormatVersion) {
        error = QStringLiteral("this project was written by a newer next-layer");
        return false;
    }

    const ordered_json setup = root.value("print_setup", ordered_json::object());
    metadata.printer = QString::fromStdString(setup.value("printer", std::string()));
    metadata.process = QString::fromStdString(setup.value("process", std::string()));
    metadata.filament = QString::fromStdString(setup.value("filament", std::string()));
    metadata.activePlate = root.value("active_plate", 0);
    metadata.plateCount = root.value("plates", 1);

    metadata.plateNames.clear();
    metadata.plateLocked.clear();
    for (const ordered_json& entry : root.value("plate_flags", ordered_json::array())) {
        metadata.plateNames.append(QString::fromStdString(entry.value("name", std::string())));
        metadata.plateLocked.append(entry.value("locked", false));
    }

    metadata.toolColours.clear();
    for (const ordered_json& colour : root.value("tool_colours", ordered_json::array()))
        metadata.toolColours.append(QString::fromStdString(colour.get<std::string>()));

    metadata.layerGcodes.clear();
    for (const ordered_json& marker : root.value("layer_gcodes", ordered_json::array())) {
        ProjectLayerGcode entry;
        entry.z = marker.value("z", 0.0);
        entry.type = QString::fromStdString(marker.value("type", std::string("gcode")));
        entry.text = QString::fromStdString(marker.value("text", std::string()));
        if (entry.z > 0.0)
            metadata.layerGcodes.push_back(std::move(entry));
    }

    QTemporaryDir scratch;
    objects.clear();
    for (const ordered_json& entry : root.value("objects", ordered_json::array())) {
        ProjectObject object;
        object.name = QString::fromStdString(entry.value("name", std::string()));
        object.sourcePath = QString::fromStdString(entry.value("source", std::string()));
        object.plate = entry.value("plate", 0);
        object.visible = entry.value("visible", true);
        object.transform = matrixFromJson(entry.value("matrix", ordered_json::array()));

        if (!meshFromBase64(QString::fromStdString(entry.value("mesh", std::string())), scratch,
                            object.mesh)) {
            error = QStringLiteral("object \"%1\" has no readable mesh").arg(object.name);
            return false;
        }

        for (const ordered_json& stored : entry.value("volumes", ordered_json::array())) {
            ProjectVolume volume;
            volume.name = QString::fromStdString(stored.value("name", std::string()));
            volume.type = QString::fromStdString(stored.value("type", std::string("part")));
            volume.visible = stored.value("visible", true);
            volume.transform = matrixFromJson(stored.value("matrix", ordered_json::array()));
            if (!meshFromBase64(QString::fromStdString(stored.value("mesh", std::string())),
                                scratch, volume.mesh)) {
                error = QStringLiteral("volume \"%1\" of \"%2\" has no readable mesh")
                            .arg(volume.name, object.name);
                return false;
            }
            object.volumes.push_back(std::move(volume));
        }

        const ordered_json settings = entry.value("settings", ordered_json::object());
        for (auto it = settings.begin(); it != settings.end(); ++it)
            object.settings.insert(QString::fromStdString(it.key()), jsonToValue(it.value()));

        for (const ordered_json& pair : entry.value("layer_heights", ordered_json::array())) {
            Slic3r::Domain::ZHeightPair heights;
            heights.z = pair[0].get<double>();
            heights.layer_height = pair[1].get<double>();
            object.layerHeights.push_back(heights);
        }

        object.supportPaint = paintFromJson(entry.value("support_paint", ordered_json::object()));
        object.seamPaint = paintFromJson(entry.value("seam_paint", ordered_json::object()));
        object.toolPaint = paintFromJson(entry.value("tool_paint", ordered_json::object()));
        objects.push_back(std::move(object));
    }

    if (objects.empty()) {
        error = QStringLiteral("this project has nothing on any plate");
        return false;
    }
    return true;
}

}
