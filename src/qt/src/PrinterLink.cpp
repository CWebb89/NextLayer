// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PrinterLink.hpp"

#include "PresetStore.hpp"

#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <memory>

#include <boost/filesystem.hpp>

#include <Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp>
#include <Slic3r/Biz/PrintHost/PrintHostFactory.hpp>
#include <Slic3r/Biz/PrintHost/PrintHostJobData.hpp>

namespace next_layer {
namespace {

struct KindDef
{
    const char* id;
    const char* label;
    Slic3r::Domain::PrintHostType type;
};

const KindDef kKinds[] = {
    {"moonraker", "Klipper / Moonraker", Slic3r::Domain::PrintHostType::Moonraker},
    {"octoprint", "OctoPrint", Slic3r::Domain::PrintHostType::OctoPrint},
    {"prusalink", "PrusaLink", Slic3r::Domain::PrintHostType::PrusaLink},
    {"duet", "Duet / RepRapFirmware", Slic3r::Domain::PrintHostType::Duet},
    {"repetier", "Repetier Server", Slic3r::Domain::PrintHostType::Repetier},
    {"astrobox", "AstroBox", Slic3r::Domain::PrintHostType::AstroBox},
    {"mks", "MKS", Slic3r::Domain::PrintHostType::MKS},
};

Slic3r::Domain::PrintHostType typeOf(const QString& kind)
{
    for (const KindDef& def : kKinds) {
        if (kind == QLatin1String(def.id))
            return def.type;
    }
    return Slic3r::Domain::PrintHostType::Moonraker;
}

/// The engine's print hosts take a host, an optional port and an API key; the
/// rest of the struct exists for the machines that need credentials, which are
/// not what a Klipper box asks for.
Slic3r::Biz::PhysicalPrinter::PhysicalPrinterConfig makeConfig(const QString& host,
                                                               const QString& port,
                                                               const QString& apiKey,
                                                               const QString& kind)
{
    Slic3r::Biz::PhysicalPrinter::PrinterUpload upload;
    upload.type = typeOf(kind);
    upload.api_key = apiKey.toStdString();
    upload.port = port.toStdString();
    upload.auth_type = apiKey.isEmpty() ? Slic3r::Domain::PrintHostAuthType::None
                                        : Slic3r::Domain::PrintHostAuthType::ApiKey;

    Slic3r::Biz::PhysicalPrinter::PhysicalPrinterConfig config;
    config.payload = upload;
    config.host = host.toStdString();
    config.name = "next-layer";
    config.uuid = "next-layer-print-host";
    return config;
}

Slic3r::Biz::PrintHost::PrintHostJobData makeJob(const QString& gcodePath, bool startPrint)
{
    // The job carries a movable payload, not a copyable one: the engine reads the
    // bytes through data_ptr, so an empty variant with the paths set is what an
    // upload of a file on disk needs.
    Slic3r::Biz::PrintHost::PrintHostJobData data(
        Slic3r::Biz::PrintHost::DataPtrVariant{},
        boost::filesystem::path(QFileInfo(gcodePath).fileName().toStdString()),
        Slic3r::Biz::PrintHost::PrintHostExportFormat::GCode);
    data.source_path = boost::filesystem::path(gcodePath.toStdString());
    data.post_action = startPrint ? Slic3r::Biz::PrintHost::PrintHostAfterUploadAction::StartPrint
                                  : Slic3r::Biz::PrintHost::PrintHostAfterUploadAction::None;
    return data;
}

} // namespace

PrinterLink::PrinterLink(QObject* parent) : QObject(parent) {}

namespace {

/// The stored address and the one a user types are both "where the printer is",
/// but people write a URL: scheme, trailing slash, sometimes a port. The engine's
/// print host wants a bare host and a bare port, so the scheme is dropped here
/// rather than failing a network call with "http://10.0.0.22/ is not reachable".
QString plainHost(const QString& value)
{
    QString host = value.trimmed();
    const qsizetype scheme = host.indexOf(QStringLiteral("://"));
    if (scheme >= 0)
        host = host.mid(scheme + 3);
    const qsizetype slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);
    if (host.endsWith(QLatin1Char('/')))
        host.chop(1);
    return host;
}

} // namespace

void PrinterLink::setHost(const QString& value)
{
    const QString normalized = plainHost(value);
    if (m_host == normalized)
        return;
    m_host = normalized;
    emit configChanged();
}

void PrinterLink::setPort(const QString& value)
{
    if (m_port == value)
        return;
    m_port = value;
    emit configChanged();
}

void PrinterLink::setApiKey(const QString& value)
{
    if (m_apiKey == value)
        return;
    m_apiKey = value;
    emit configChanged();
}

void PrinterLink::setKind(const QString& value)
{
    if (m_kind == value)
        return;
    m_kind = value;
    emit configChanged();
}

QVariantList PrinterLink::kinds() const
{
    QVariantList list;
    for (const KindDef& def : kKinds) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), QString::fromLatin1(def.id));
        entry.insert(QStringLiteral("label"), QString::fromLatin1(def.label));
        list.append(entry);
    }
    return list;
}

QString PrinterLink::configPath() const
{
    return QDir(PresetStore::dataDir())
        .filePath(QStringLiteral("print-host.json"));
}

void PrinterLink::load()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_host = plainHost(root.value(QStringLiteral("host")).toString());
    m_port = root.value(QStringLiteral("port")).toString();
    m_apiKey = root.value(QStringLiteral("apiKey")).toString();
    m_kind = root.value(QStringLiteral("kind")).toString(QStringLiteral("moonraker"));
    m_deviceUrl = root.value(QStringLiteral("deviceUrl")).toString();
    emit configChanged();
}

void PrinterLink::save()
{
    QJsonObject root;
    root.insert(QStringLiteral("host"), m_host.trimmed());
    root.insert(QStringLiteral("port"), m_port.trimmed());
    root.insert(QStringLiteral("apiKey"), m_apiKey);
    root.insert(QStringLiteral("kind"), m_kind);
    root.insert(QStringLiteral("deviceUrl"), m_deviceUrl);

    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    QFile file(configPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    // The key is a credential: nobody else on the machine needs to read it.
    QFile::setPermissions(configPath(), QFile::ReadOwner | QFile::WriteOwner);
}

QString PrinterLink::test()
{
    if (!configured())
        return QStringLiteral("no printer address is set");
    if (m_busy)
        return QStringLiteral("a transfer is already running");

    m_busy = true;
    emit busyChanged();

    std::unique_ptr<Slic3r::Biz::PrintHost::IPrintHost> host =
        Slic3r::Biz::PrintHost::create_print_host(
            makeConfig(m_host.trimmed(), m_port.trimmed(), m_apiKey, m_kind),
            makeJob(QString(), false));

    QString message;
    if (host == nullptr) {
        message = QStringLiteral("%1 is not a print host this engine can reach").arg(m_kind);
    } else {
        std::string error;
        const auto progress = [](Slic3r::Biz::Network::IHttp::Progress status, bool& cancel) {
            Q_UNUSED(status)
            cancel = false;
        };
        const auto retry = [](Slic3r::Biz::Network::IHttp::Retry status, bool& cancel) {
            Q_UNUSED(status)
            cancel = false;
        };
        if (!host->test(error, progress, retry) || !error.empty())
            message = QString::fromStdString(error.empty() ? "the printer did not answer" : error);
    }

    m_busy = false;
    emit busyChanged();
    if (message.isEmpty())
        message = QStringLiteral("connected to %1").arg(m_host.trimmed());
    emit finished(message.startsWith(QLatin1String("connected")), message);
    return message;
}

void PrinterLink::setDeviceUrl(const QString& value)
{
    if (m_deviceUrl == value)
        return;
    m_deviceUrl = value;
    emit configChanged();
}

namespace {

/// A blocking GET, because the Device tab asks once per poll and a reply that
/// arrives after the window closed is worth nothing. Returns an empty array on
/// any failure, which the callers report as "not reachable".
QByteArray httpGet(const QString& url, int timeoutMs = 2500)
{
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    QByteArray body;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError)
        body = reply->readAll();
    reply->deleteLater();
    return body;
}

} // namespace

QString PrinterLink::resolveDeviceUrl()
{
    if (!configured())
        return QString();

    const QString host = m_host.trimmed();
    const QString port = m_port.trimmed();

    // An address the user typed with a scheme is taken as given, https included.
    if (host.contains(QStringLiteral("://")))
        return host;

    // Otherwise LAN candidates only, never https. Probing https makes QtNetwork
    // load its TLS backend, and on this machine (qt6-base 6.12 against system
    // OpenSSL 3.6) that call dies inside QSslCertificate::fromFile -- taking the
    // whole app with it when the Device tab opened. A Moonraker host on a LAN
    // serves http; anything else is something the user can type.
    const QStringList candidates = {
        port.isEmpty() ? QString() : QStringLiteral("http://%1:%2/").arg(host, port),
        QStringLiteral("http://%1/").arg(host),
    };

    for (const QString& candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        if (!httpGet(candidate).isEmpty())
            return candidate;
    }
    return QStringLiteral("http://%1/")
        .arg(port.isEmpty() ? host : host + QLatin1Char(':') + port);
}

QString PrinterLink::printerState()
{
    if (!configured())
        return QStringLiteral("no printer is configured");

    const QString port = m_port.trimmed().isEmpty() ? QStringLiteral("7125") : m_port.trimmed();
    const QByteArray body = httpGet(QStringLiteral("http://%1:%2/printer/objects/query"
                                                   "?print_stats&virtual_sdcard&heater_bed&extruder")
                                        .arg(m_host.trimmed(), port));
    if (body.isEmpty())
        return QStringLiteral("%1 did not answer").arg(m_host.trimmed());

    const QJsonObject status = QJsonDocument::fromJson(body)
                                   .object()
                                   .value(QStringLiteral("result"))
                                   .toObject()
                                   .value(QStringLiteral("status"))
                                   .toObject();
    if (status.isEmpty())
        return QStringLiteral("%1 is unreachable").arg(m_host.trimmed());

    const QJsonObject stats = status.value(QStringLiteral("print_stats")).toObject();
    const QString state = stats.value(QStringLiteral("state")).toString();
    const QString file = stats.value(QStringLiteral("filename")).toString();
    const double progress =
        status.value(QStringLiteral("virtual_sdcard")).toObject().value(QStringLiteral("progress")).toDouble()
        * 100.0;
    const int bed = int(status.value(QStringLiteral("heater_bed"))
                            .toObject()
                            .value(QStringLiteral("temperature"))
                            .toDouble());
    const int nozzle = int(status.value(QStringLiteral("extruder"))
                               .toObject()
                               .value(QStringLiteral("temperature"))
                               .toDouble());

    QString line = state;
    if (state == QLatin1String("printing") || state == QLatin1String("paused"))
        line += QStringLiteral(" %1 % - %2").arg(progress, 0, 'f', 0).arg(file);
    line += QStringLiteral("  |  bed %1 C, nozzle %2 C").arg(bed).arg(nozzle);
    return line;
}

QString PrinterLink::perform(const QString& gcodePath, bool startPrint)
{
    if (!configured())
        return QStringLiteral("no printer address is set");
    if (gcodePath.isEmpty() || !QFileInfo::exists(gcodePath))
        return QStringLiteral("there is no G-code file to send yet");

    std::unique_ptr<Slic3r::Biz::PrintHost::IPrintHost> host =
        Slic3r::Biz::PrintHost::create_print_host(
            makeConfig(m_host.trimmed(), m_port.trimmed(), m_apiKey, m_kind),
            makeJob(gcodePath, startPrint));
    if (host == nullptr)
        return QStringLiteral("%1 is not a print host this engine can reach").arg(m_kind);

    std::string failure;
    QString progressText;
    int percent = -1;
    const auto progress = [&](Slic3r::Biz::Network::IHttp::Progress status, bool& cancel) {
        const int next = status.ultotal > 0 ? int(status.ulnow * 100 / status.ultotal) : -1;
        cancel = false;
        if (next == percent)
            return;
        percent = next;
        if (next >= 0)
            progressText = QStringLiteral("%1 %").arg(next);
    };
    const auto retry = [](Slic3r::Biz::Network::IHttp::Retry status, bool& cancel) {
        Q_UNUSED(status)
        cancel = false;
    };
    const auto error = [&](std::string text) { failure = std::move(text); };
    const auto info = [](Slic3r::Biz::PrintHost::PrintHostJobInfoTag tag, std::string status) {
        Q_UNUSED(tag)
        Q_UNUSED(status)
    };

    const bool ok = host->perform(progress, retry, error, info);
    if (!ok)
        return QString::fromStdString(failure.empty() ? "the upload failed" : failure);
    return QStringLiteral("sent %1 to %2").arg(QFileInfo(gcodePath).fileName(), m_host.trimmed());
}

QString PrinterLink::upload(const QString& gcodePath, bool startPrint)
{
    if (m_busy)
        return QStringLiteral("a transfer is already running");
    m_busy = true;
    emit busyChanged();
    const QString message = perform(gcodePath, startPrint);
    m_busy = false;
    emit busyChanged();
    emit finished(message.startsWith(QLatin1String("sent")), message);
    return message;
}

QString PrinterLink::runHeadless(const QString& gcodePath, bool startPrint)
{
    load();
    const QString tested = test();
    if (!tested.startsWith(QLatin1String("connected")))
        return QStringLiteral("test: %1").arg(tested);
    const QString sent = upload(gcodePath, startPrint);
    return QStringLiteral("%1; %2").arg(tested, sent);
}

} // namespace next_layer
