// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <string>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

// The resolution — three candidates, first match wins, non-string entries
// dropped — is Rust's (src/app/src/Palette.rs). What stays here is the QVariantMap
// Qt wants it as.
class Palette : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap roles READ roles CONSTANT)
    Q_PROPERTY(QString source READ source CONSTANT)

public:
    explicit Palette(QObject* parent = nullptr) : QObject(parent)
    {
        const QJsonObject resolved =
            QJsonDocument::fromJson(
                QByteArray::fromStdString(static_cast<std::string>(palette_json())))
                .object();
        m_source = resolved.value(QStringLiteral("source")).toString();
        const QJsonObject roles = resolved.value(QStringLiteral("roles")).toObject();
        for (auto it = roles.begin(); it != roles.end(); ++it)
            m_roles.insert(it.key(), it.value().toString());
    }

    QVariantMap roles() const { return m_roles; }
    QString source() const { return m_source; }

private:
    QVariantMap m_roles;
    QString m_source;
};

}
