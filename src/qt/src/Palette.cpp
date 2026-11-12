// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Palette.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

QVariantMap readRoles(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return {};

    QVariantMap roles;
    const QJsonObject object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it.value().isString())
            roles.insert(it.key(), it.value().toString());
    return roles;
}

}

next_layer::Palette::Palette(QObject* parent)
    : QObject(parent)
{
    const QString cache = QDir::homePath() + "/.cache/matugen/";
    const QStringList candidates{
        qEnvironmentVariable("NEXT_LAYER_COLORS"),
        cache + "next-layer-colors.json",
        cache + "layer-colors.json",
    };

    for (const QString& candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        QVariantMap roles = readRoles(candidate);
        if (roles.isEmpty())
            continue;
        m_roles = std::move(roles);
        m_source = candidate;
        break;
    }
}
