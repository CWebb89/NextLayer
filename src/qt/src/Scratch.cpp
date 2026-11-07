// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Scratch.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QStringList>
#include <QTemporaryDir>

#include <cerrno>
#include <cstring>
#include <signal.h>

namespace next_layer {

namespace {

const char* const kPrefix = "next-layer-";

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    errno = 0;
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
}

} // namespace

QString scratchTemplate()
{
    const QString tag = QStringLiteral("%1%2-")
                            .arg(QLatin1String(kPrefix),
                                 QString::number(QCoreApplication::applicationPid()));
    return QDir::tempPath() + QLatin1Char('/') + tag + QStringLiteral("XXXXXX");
}

std::unique_ptr<QTemporaryDir> makeScratchDir()
{
    return std::make_unique<QTemporaryDir>(scratchTemplate());
}

void sweepScratchDirs()
{
    const QDir scratch(QDir::tempPath());
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    const QFileInfoList leftovers =
        scratch.entryInfoList({QString::fromLatin1(kPrefix) + QStringLiteral("*")},
                              QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& entry : leftovers) {
        const QString name = entry.fileName().mid(int(std::strlen(kPrefix)));
        const qint64 pid = name.section(QLatin1Char('-'), 0, 0).toLongLong();
        const bool owned = pid > 0;
        if (owned ? processAlive(pid) : entry.lastModified() >= cutoff)
            continue;
        QDir(entry.absoluteFilePath()).removeRecursively();
    }
}

} // namespace next_layer
