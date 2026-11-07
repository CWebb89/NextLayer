// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "ThreeMfPlates.hpp"

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

void readPlateRecords(const QByteArray& settings, ThreeMfPlates& plates)
{
    QXmlStreamReader xml(settings);
    int plateId = 0;
    int instanceObject = 0;
    bool inPlate = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("plate")) {
                inPlate = true;
                plateId = 0;
            } else if (tag == QLatin1String("model_instance")) {
                instanceObject = 0;
            } else if (tag == QLatin1String("metadata") && inPlate) {
                const QXmlStreamAttributes attributes = xml.attributes();
                const QStringView key = attributes.value(QLatin1String("key"));
                const QStringView value = attributes.value(QLatin1String("value"));
                if (key == QLatin1String("plater_id"))
                    plateId = value.toInt();
                else if (key == QLatin1String("object_id"))
                    instanceObject = value.toInt();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView tag = xml.name();
            if (tag == QLatin1String("model_instance")) {
                if (inPlate && plateId > 0 && instanceObject > 0)
                    plates.plateOfObject.insert(instanceObject, plateId);
            } else if (tag == QLatin1String("plate")) {
                inPlate = false;
                plates.plateCount = qMax(plates.plateCount, plateId);
            }
        }
    }

    if (xml.hasError())
        plates.plateOfObject.clear();
}

void readBuildOrder(const QByteArray& scene, ThreeMfPlates& plates)
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
                const int objectId = xml.attributes().value(QLatin1String("objectid")).toInt();
                if (objectId > 0)
                    plates.buildOrder.append(objectId);
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("build")) {
            inBuild = false;
        }
    }

    if (xml.hasError())
        plates.buildOrder.clear();
}

} // namespace

ThreeMfPlates readThreeMfPlates(const QString& path)
{
    ThreeMfPlates plates;

    const QByteArray settings = readZipEntry(path, kModelSettings);
    const QByteArray scene = readZipEntry(path, kScene);
    if (settings.isEmpty() || scene.isEmpty())
        return plates;

    readPlateRecords(settings, plates);
    readBuildOrder(scene, plates);
    if (plates.plateCount == 0 || plates.buildOrder.isEmpty() || plates.plateOfObject.isEmpty())
        return ThreeMfPlates{};

    plates.valid = true;
    return plates;
}

} // namespace next_layer
