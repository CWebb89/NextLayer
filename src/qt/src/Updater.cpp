// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Updater.hpp"

#include "GcodeBrand.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

namespace next_layer {

namespace {

constexpr const char* kChannelDefault = "https://github.com/CWebb89/NextLayer";
constexpr const char* kManifestPath = "/releases/latest/download/update.json";
constexpr const char* kBinaryName = "next-layer-qt";

QString sha256OfFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool copyTree(const QString& from, const QString& to, QString& error)
{
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString relative = QDir(from).relativeFilePath(source);
        const QString target = to + QLatin1Char('/') + relative;
        const QFileInfo info(source);
        if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                error = QStringLiteral("could not create %1").arg(target);
                return false;
            }
            continue;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            error = QStringLiteral("could not write %1").arg(target);
            return false;
        }
        QFile::setPermissions(target, info.permissions());
    }
    return true;
}

}

bool versionIsNewer(const QString& candidate, const QString& current)
{
    const QStringList left = candidate.split(QLatin1Char('.'));
    const QStringList right = current.split(QLatin1Char('.'));
    if (left.isEmpty() || right.isEmpty())
        return false;

    for (int i = 0; i < std::max(left.size(), right.size()); ++i) {
        bool leftOk = false;
        bool rightOk = false;
        const int a = i < left.size() ? left.at(i).toInt(&leftOk) : 0;
        const int b = i < right.size() ? right.at(i).toInt(&rightOk) : 0;
        if (i < left.size() && !leftOk)
            return false;
        if (i < right.size() && !rightOk)
            return false;
        if (a != b)
            return a > b;
    }
    return false;
}

Updater::Updater(QObject* parent) : QObject(parent) {}

QString Updater::channel()
{
    const QString override = qEnvironmentVariable("NEXT_LAYER_UPDATE_URL");
    if (!override.isEmpty())
        return override;
    return QString::fromLatin1(kChannelDefault);
}

QString Updater::manifestUrl(const QString& channel)
{
    return channel + QString::fromLatin1(kManifestPath);
}

QString Updater::installRoot()
{
    const QString override = qEnvironmentVariable("NEXT_LAYER_UPDATE_ROOT");
    if (!override.isEmpty())
        return override;
    return QCoreApplication::applicationDirPath();
}

QString Updater::currentVersion()
{
    return applicationVersion();
}

bool Updater::canInstall() const
{
    return m_downloaded && !m_stagedPath.isEmpty();
}

void Updater::setStatus(const QString& status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Updater::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

bool Updater::checkSync(const QString& channel, ReleaseInfo& out)
{
    out = ReleaseInfo();

    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(manifestUrl(channel))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const QString failure = reply->error() == QNetworkReply::NoError
                                ? QString()
                                : reply->errorString();
    reply->deleteLater();

    if (!failure.isEmpty()) {
        out.error = failure;
        return false;
    }

    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        out.error = QStringLiteral("the update manifest is not JSON");
        return false;
    }

    const QJsonObject object = document.object();
    out.version = object.value(QStringLiteral("version")).toString();
    out.url = object.value(QStringLiteral("url")).toString();
    out.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
    out.notes = object.value(QStringLiteral("notes")).toString();

    if (out.version.isEmpty()) {
        out.error = QStringLiteral("the update manifest carries no version");
        return false;
    }
    if (out.url.isEmpty() || out.sha256.isEmpty()) {
        out.error = QStringLiteral("%1 is published without an artifact").arg(out.version);
        return false;
    }
    if (!out.url.startsWith(QLatin1String("http")))
        out.url = channel + QLatin1Char('/') + out.url;

    return versionIsNewer(out.version, currentVersion());
}

bool Updater::downloadSync(const ReleaseInfo& release, const QString& root, QString& error)
{
    if (!release.valid()) {
        error = QStringLiteral("there is no release to download");
        return false;
    }

    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(release.url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const QString failure = reply->error() == QNetworkReply::NoError
                                ? QString() : reply->errorString();
    reply->deleteLater();
    if (!failure.isEmpty()) {
        error = failure;
        return false;
    }

    const QString staging = root + QStringLiteral("/.update-") + release.version;
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging)) {
        error = QStringLiteral("could not create %1").arg(staging);
        return false;
    }

    const QString archive = staging + QStringLiteral("/release.tar.gz");
    QFile file(archive);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("could not write %1").arg(archive);
        return false;
    }
    file.write(body);
    file.close();

    const QString digest = sha256OfFile(archive);
    if (digest.isEmpty() || digest != release.sha256) {
        error = QStringLiteral("the artifact's sha256 is %1, the manifest says %2")
                    .arg(digest.isEmpty() ? QStringLiteral("unreadable") : digest,
                         release.sha256);
        return false;
    }

    QProcess tar;
    tar.setWorkingDirectory(staging);
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-xzf"), archive, QStringLiteral("--strip-components=0")});
    if (!tar.waitForFinished(120000) || tar.exitCode() != 0) {
        error = QStringLiteral("could not unpack the artifact: %1")
                    .arg(QString::fromUtf8(tar.readAllStandardError()).trimmed());
        return false;
    }

    const QString unpacked = staging + QLatin1Char('/') + QString::fromLatin1(kBinaryName);
    if (!QFile::exists(unpacked)) {
        error = QStringLiteral("the artifact carries no %1").arg(QString::fromLatin1(kBinaryName));
        return false;
    }

    m_stagedPath = staging;
    m_release = release;
    m_downloaded = true;
    emit statusChanged();
    return true;
}

bool Updater::installSync(const QString& root, QString& error)
{
    if (!canInstall()) {
        error = QStringLiteral("nothing has been downloaded");
        return false;
    }

    const QString stagedBinary = m_stagedPath + QLatin1Char('/') + QString::fromLatin1(kBinaryName);
    const QString targetBinary = root + QLatin1Char('/') + QString::fromLatin1(kBinaryName);

    // The running executable is replaced by rename, not by writing into it: the
    // old inode stays mapped until this process exits.
    const QString previous = targetBinary + QStringLiteral(".previous");
    QFile::remove(previous);
    if (QFile::exists(targetBinary) && !QFile::rename(targetBinary, previous)) {
        error = QStringLiteral("could not move %1 aside").arg(targetBinary);
        return false;
    }
    QFile::remove(targetBinary);
    if (!QFile::rename(stagedBinary, targetBinary)) {
        QFile::rename(previous, targetBinary);
        error = QStringLiteral("could not install %1").arg(targetBinary);
        return false;
    }

    const QString stagedQml = m_stagedPath + QStringLiteral("/qml");
    if (QDir(stagedQml).exists()) {
        const QString targetQml = root + QStringLiteral("/qml");
        if (!copyTree(stagedQml, targetQml, error)) {
            QFile::rename(previous, targetBinary);
            return false;
        }
    }

    QDir(m_stagedPath).removeRecursively();
    m_stagedPath.clear();
    m_downloaded = false;
    emit statusChanged();
    return true;
}

void Updater::check()
{
    setBusy(true);
    setStatus(QStringLiteral("checking %1").arg(channel()));

    ReleaseInfo release;
    if (!checkSync(channel(), release)) {
        setBusy(false);
        setStatus(release.error.isEmpty()
                      ? QStringLiteral("version %1 is the newest").arg(currentVersion())
                      : QStringLiteral("update check failed: %1").arg(release.error));
        return;
    }

    m_release = release;
    setBusy(false);
    setStatus(QStringLiteral("%1 is available (this build is %2)")
                  .arg(release.version, currentVersion()));
    emit statusChanged();
}

void Updater::download()
{
    if (m_release.version.isEmpty()) {
        setStatus(QStringLiteral("check for updates first"));
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("downloading %1").arg(m_release.version));

    QString error;
    const bool ok = downloadSync(m_release, installRoot(), error);
    setBusy(false);
    setStatus(ok ? QStringLiteral("%1 downloaded and verified").arg(m_release.version)
                 : QStringLiteral("download failed: %1").arg(error));
}

void Updater::install()
{
    setBusy(true);
    QString error;
    const bool ok = installSync(installRoot(), error);
    setBusy(false);
    setStatus(ok ? QStringLiteral("%1 installed -- restart to run it").arg(m_release.version)
                 : QStringLiteral("install failed: %1").arg(error));
}

void Updater::relaunch()
{
    const QString binary = installRoot() + QLatin1Char('/') + QString::fromLatin1(kBinaryName);
    if (!QProcess::startDetached(binary, QCoreApplication::arguments().mid(1))) {
        setStatus(QStringLiteral("could not start %1").arg(binary));
        return;
    }
    QCoreApplication::quit();
}

}
