// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "ThreeMfProject.hpp"

#include <QByteArray>
#include <QXmlStreamReader>

#include <miniz.h>

namespace next_layer {

namespace {

const char* const kModelSettings = "Metadata/model_settings.config";
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

} // namespace

ThreeMfProject readThreeMfProject(const QString& path)
{
    ThreeMfProject project;

    const QByteArray settings = readZipEntry(path, kModelSettings);
    const QByteArray scene = readZipEntry(path, kScene);
    if (scene.isEmpty())
        return project;

    readPlateRecords(settings, project);
    readBuildList(scene, project);
    if (project.buildTransforms.isEmpty())
        return ThreeMfProject{};

    project.valid = true;
    return project;
}

} // namespace next_layer
