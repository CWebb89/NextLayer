// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "ThreeMfProject.hpp"

#include <QByteArray>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <miniz.h>

namespace next_layer {

namespace {

const char* const kModelSettings = "Metadata/model_settings.config";
const char* const kModelConfig = "Metadata/Slic3r_PE_model.config";
const char* const kLayerHeights = "Metadata/Slic3r_PE_layer_heights_profile.txt";
const char* const kLayerGcodes = "Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml";
const char* const kScene = "3D/3dmodel.model";

QByteArray readZipEntry(const QString& path, const char* entry)
{
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_file(&archive, path.toUtf8().constData(), 0))
        return {};

    QByteArray contents;
    const int index = mz_zip_reader_locate_file(&archive, entry, nullptr, 0);
    if (index >= 0) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&archive, mz_uint(index), &stat) && stat.m_uncomp_size > 0) {
            contents.resize(int(stat.m_uncomp_size));
            if (!mz_zip_reader_extract_to_mem(&archive, mz_uint(index), contents.data(),
                                              size_t(contents.size()), 0))
                contents.clear();
        }
    }

    mz_zip_reader_end(&archive);
    return contents;
}

/// The plate records name a plate per object twice over: an object carries a
/// `plater_id` of its own, and each `<plate>` lists the instances it holds by
/// `object_id`. Both forms appear in the wild, and the plate list is the one that
/// counts the plates, so both are read.
void readPlateRecords(const QByteArray& settings, ThreeMfProject& project)
{
    QXmlStreamReader xml(settings);
    int objectId = 0;
    int plateId = 0;
    int instanceObject = 0;
    bool inPlate = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("object")) {
                objectId = xml.attributes().value(QLatin1String("id")).toInt();
            } else if (tag == QLatin1String("plate")) {
                inPlate = true;
                plateId = 0;
            } else if (tag == QLatin1String("model_instance")) {
                instanceObject = 0;
            } else if (tag == QLatin1String("metadata")) {
                const QXmlStreamAttributes attributes = xml.attributes();
                const QStringView key = attributes.value(QLatin1String("key"));
                const QStringView value = attributes.value(QLatin1String("value"));
                if (key != QLatin1String("plater_id") && key != QLatin1String("object_id"))
                    continue;
                if (inPlate) {
                    if (key == QLatin1String("plater_id"))
                        plateId = value.toInt();
                    else
                        instanceObject = value.toInt();
                } else if (key == QLatin1String("plater_id") && objectId > 0 && value.toInt() > 0) {
                    project.plateOfObject.insert(objectId, value.toInt());
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("model_instance")) {
                if (inPlate && plateId > 0 && instanceObject > 0)
                    project.plateOfObject.insert(instanceObject, plateId);
            } else if (tag == QLatin1String("plate")) {
                inPlate = false;
                project.plateCount = qMax(project.plateCount, plateId);
            } else if (tag == QLatin1String("object")) {
                objectId = 0;
            }
        }
    }

    if (xml.hasError()) {
        project.plateOfObject.clear();
        project.plateCount = 0;
    }
}

/// The build list: one `<item>` per placed object, in the order the model file
/// lists them. That order is how an object is matched to the records below.
void readBuildList(const QByteArray& scene, ThreeMfProject& project)
{
    QXmlStreamReader xml(scene);
    bool inBuild = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("build")) {
                inBuild = true;
            } else if (tag == QLatin1String("item") && inBuild) {
                const QXmlStreamAttributes attributes = xml.attributes();
                const int objectId = attributes.value(QLatin1String("objectid")).toInt();
                if (objectId <= 0)
                    continue;

                ThreeMfProject::Placement placement{{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
                const QStringList numbers = attributes.value(QLatin1String("transform"))
                                                .toString()
                                                .split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (numbers.size() == int(placement.size())) {
                    for (int i = 0; i < int(placement.size()); ++i)
                        placement[size_t(i)] = numbers.at(i).toDouble();
                } else if (!numbers.isEmpty()) {
                    continue;
                }

                project.buildOrder.append(objectId);
                project.buildTransforms.append(placement);
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("build")) {
            inBuild = false;
        }
    }

    if (xml.hasError()) {
        project.buildOrder.clear();
        project.buildTransforms.clear();
    }
}

/// The object and volume records: what type each volume is, the overrides the file
/// set on it, and the range of triangles its mesh occupies. The paint itself is in
/// the model file, keyed by that range.
void readModelConfig(const QByteArray& config, ThreeMfProject& project)
{
    QXmlStreamReader xml(config);
    int objectId = 0;
    ThreeMfObjectRecord record;
    ThreeMfVolumeRecord volume;
    int firstTriangle = 0;
    bool inVolume = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            const QXmlStreamAttributes attributes = xml.attributes();
            if (tag == QLatin1String("object")) {
                objectId = attributes.value(QLatin1String("id")).toInt();
                record = ThreeMfObjectRecord{};
            } else if (tag == QLatin1String("volume")) {
                inVolume = true;
                volume = ThreeMfVolumeRecord{};
                firstTriangle = attributes.value(QLatin1String("firstid")).toInt();
                volume.settings.insert(QStringLiteral("__first_triangle"),
                                       QString::number(firstTriangle));
            } else if (tag == QLatin1String("metadata")) {
                const QString key = attributes.value(QLatin1String("key")).toString();
                const QString value = attributes.value(QLatin1String("value")).toString();
                if (inVolume) {
                    if (key == QLatin1String("volume_type"))
                        volume.type = value;
                    else if (key != QLatin1String("matrix"))
                        volume.settings.insert(key, value);
                } else if (objectId > 0) {
                    // `name` is the object's own label, not a setting: it is kept
                    // apart so the settings reader cannot report a name it has no
                    // definition for.
                    if (key == QLatin1String("name"))
                        record.name = value;
                    else
                        record.settings.insert(key, value);
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("volume") && inVolume) {
                record.volumes.append(volume);
                inVolume = false;
            } else if (tag == QLatin1String("object") && objectId > 0) {
                project.objectRecords.insert(objectId, record);
                objectId = 0;
            }
        }
    }
    if (xml.hasError())
        project.objectRecords.clear();
}

/// The variable layer height bands, one `object_id=N|z;height;z;height;...` line per
/// object.
void readLayerHeights(const QByteArray& body, ThreeMfProject& project)
{
    const QStringList lines = QString::fromUtf8(body).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const int marker = line.indexOf(QLatin1String("object_id="));
        const int bar = line.indexOf(QLatin1Char('|'));
        if (marker < 0 || bar < 0)
            continue;
        const int id = line.mid(marker + 10, bar - marker - 10).toInt();
        if (id <= 0)
            continue;
        QVector<double>& bands = project.objectRecords[id].layerHeights;
        for (const QString& number :
             line.mid(bar + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts))
            bands.append(number.toDouble());
    }
}

/// The painted facets, written as attributes on the model's own triangles. A
/// triangle's index inside its volume is what the engine's selector is keyed by, so
/// the global index is collected here and mapped onto the volume ranges afterwards.
void readPaint(const QByteArray& scene, ThreeMfProject& project)
{
    QXmlStreamReader xml(scene);
    QHash<int, QHash<int, QString>> supports;
    QHash<int, QHash<int, QString>> seams;
    QHash<int, QHash<int, QString>> tools;
    int objectId = 0;
    int triangle = 0;
    bool inMesh = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("object")) {
                objectId = xml.attributes().value(QLatin1String("id")).toInt();
            } else if (tag == QLatin1String("mesh")) {
                inMesh = true;
                triangle = 0;
            } else if (tag == QLatin1String("triangle") && inMesh && objectId > 0) {
                // The reference slicer writes these attributes in its own XML
                // namespace, so they arrive as `slic3rpe:custom_supports` and match
                // by local name only.
                QString painted;
                QString seam;
                QString tool;
                for (const QXmlStreamAttribute& attribute : xml.attributes()) {
                    if (attribute.name() == QLatin1String("custom_supports"))
                        painted = attribute.value().toString();
                    else if (attribute.name() == QLatin1String("custom_seam"))
                        seam = attribute.value().toString();
                    else if (attribute.name() == QLatin1String("paint_color"))
                        tool = attribute.value().toString();
                }
                if (!painted.isEmpty())
                    supports[objectId].insert(triangle, painted);
                if (!seam.isEmpty())
                    seams[objectId].insert(triangle, seam);
                if (!tool.isEmpty())
                    tools[objectId].insert(triangle, tool);
                ++triangle;
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("mesh")) {
            inMesh = false;
        }
    }
    if (xml.hasError())
        return;

    for (auto it = project.objectRecords.begin(); it != project.objectRecords.end(); ++it) {
        const QHash<int, QString>& objectSupports = supports.value(it.key());
        const QHash<int, QString>& objectSeams = seams.value(it.key());
        const QHash<int, QString>& objectTools = tools.value(it.key());
        if (objectSupports.isEmpty() && objectSeams.isEmpty() && objectTools.isEmpty())
            continue;

        QVector<ThreeMfVolumeRecord>& volumes = it->volumes;
        if (volumes.isEmpty())
            continue;
        for (int index = 0; index < volumes.size(); ++index) {
            const int first = volumes.at(index)
                                  .settings.value(QStringLiteral("__first_triangle"))
                                  .toInt();
            const int last = index + 1 < volumes.size()
                                 ? volumes.at(index + 1)
                                       .settings.value(QStringLiteral("__first_triangle"))
                                       .toInt()
                                       - 1
                                 : std::numeric_limits<int>::max();
            auto take = [&](const QHash<int, QString>& source, QHash<int, QString>& target) {
                for (auto painted = source.constBegin(); painted != source.constEnd(); ++painted) {
                    if (painted.key() < first || painted.key() > last)
                        continue;
                    target.insert(painted.key() - first, painted.value());
                }
            };
            take(objectSupports, volumes[index].supports);
            take(objectSeams, volumes[index].seams);
            take(objectTools, volumes[index].tools);
        }
    }
}

/// A marker's type, as the integer this format standardised on, or as the name
/// another writer may have used instead.
int markerType(const QStringView value)
{
    bool numeric = false;
    const int number = value.toInt(&numeric);
    if (numeric)
        return number;
    if (value.compare(QLatin1String("ColorChange"), Qt::CaseInsensitive) == 0)
        return 0;
    if (value.compare(QLatin1String("PausePrint"), Qt::CaseInsensitive) == 0)
        return 1;
    if (value.compare(QLatin1String("ToolChange"), Qt::CaseInsensitive) == 0)
        return 2;
    if (value.compare(QLatin1String("Template"), Qt::CaseInsensitive) == 0)
        return 3;
    return 4;
}

/// The timeline markers, per bed: pauses, colour and tool changes and custom
/// G-code, each at its own Z.
void readLayerGcodes(const QByteArray& body, ThreeMfProject& project)
{
    QXmlStreamReader xml(body);
    int bed = 0;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token != QXmlStreamReader::StartElement)
            continue;
        const QXmlStreamAttributes attributes = xml.attributes();
        if (xml.name() == QLatin1String("custom_gcodes_per_print_z")) {
            bed = attributes.value(QLatin1String("bed_idx")).toInt();
        } else if (xml.name() == QLatin1String("code")) {
            ThreeMfLayerGcode marker;
            marker.z = attributes.value(QLatin1String("print_z")).toDouble();
            marker.type = markerType(attributes.value(QLatin1String("type")));
            const QString extra = attributes.value(QLatin1String("extra")).toString();
            const QString color = attributes.value(QLatin1String("color")).toString();
            // The colour field holds a colour, not a note. A file written before
            // this was enforced -- this app's own exports among them -- carries free
            // text there, so the text is read from wherever it actually is rather
            // than from the field's name.
            const bool realColor = QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6}$"))
                                       .match(color)
                                       .hasMatch();
            marker.text = extra.isEmpty() ? color : extra;
            if (marker.type == 0 && !realColor)
                marker.type = 4;
            if (marker.z > 0.0)
                project.layerGcodes[bed].append(marker);
        }
    }
}

/// The object settings another slicer keeps in `model_settings.config`: Bambu and
/// Orca write them as plain `<metadata key value>` under the object, where
/// PrusaSlicer writes a model config file of its own. Their names are that
/// slicer's, so what comes back is filtered against this engine's definitions
/// before it is applied; here only the layout keys are dropped.
void readForeignSettings(const QByteArray& settings, ThreeMfProject& project)
{
    static const QSet<QString> layout{
        QStringLiteral("name"), QStringLiteral("matrix"),
        QStringLiteral("source_file"), QStringLiteral("source_object_id"),
        QStringLiteral("source_volume_id"), QStringLiteral("source_offset_x"),
        QStringLiteral("source_offset_y"), QStringLiteral("source_offset_z"),
        QStringLiteral("identify_id"), QStringLiteral("instance_id"),
        QStringLiteral("object_id"), QStringLiteral("plater_id"),
        QStringLiteral("plater_name"), QStringLiteral("locked"),
        QStringLiteral("thumbnail_file"), QStringLiteral("top_file"),
        QStringLiteral("pick_file"),
    };

    QXmlStreamReader xml(settings);
    int objectId = 0;
    bool inPart = false;
    QHash<QString, QString> record;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("object")) {
                objectId = xml.attributes().value(QLatin1String("id")).toInt();
                record.clear();
            } else if (tag == QLatin1String("part")) {
                inPart = true;
            } else if (tag == QLatin1String("metadata") && objectId > 0 && !inPart) {
                const QString key = xml.attributes().value(QLatin1String("key")).toString();
                const QString value = xml.attributes().value(QLatin1String("value")).toString();
                if (key.isEmpty())
                    continue;
                if (key == QLatin1String("name")) {
                    project.foreignNames.insert(objectId, value);
                    continue;
                }
                if (layout.contains(key))
                    continue;
                record.insert(key, value);
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("part")) {
                inPart = false;
            } else if (tag == QLatin1String("object") && objectId > 0) {
                if (!record.isEmpty())
                    project.foreignSettings.insert(objectId, record);
                objectId = 0;
                record.clear();
            }
        }
    }
    if (xml.hasError())
        project.foreignSettings.clear();
}

} // namespace

ThreeMfProject readThreeMfProject(const QString& path)
{
    ThreeMfProject project;

    const QByteArray settings = readZipEntry(path, kModelSettings);
    const QByteArray scene = readZipEntry(path, kScene);
    if (scene.isEmpty())
        return project;

    readPlateRecords(settings, project);
    readForeignSettings(settings, project);
    readBuildList(scene, project);
    readModelConfig(readZipEntry(path, kModelConfig), project);
    readLayerHeights(readZipEntry(path, kLayerHeights), project);
    readLayerGcodes(readZipEntry(path, kLayerGcodes), project);
    readPaint(scene, project);
    if (project.buildTransforms.isEmpty())
        return ThreeMfProject{};

    project.valid = true;
    return project;
}

}
