// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Resources.hpp"

#include <QCoreApplication>
#include <QFileInfo>

namespace next_layer {

QString resourcesDir()
{
    const QString environment = qEnvironmentVariable("NEXT_LAYER_RESOURCES");
    if (!environment.isEmpty() && QFileInfo(environment).isDir())
        return environment;

    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        base + QStringLiteral("/resources"),
        base + QStringLiteral("/../share/next-layer/resources"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir())
            return QFileInfo(candidate).canonicalFilePath();
    }

    return QString::fromLatin1(NEXT_LAYER_RESOURCES_DIR);
}

}
