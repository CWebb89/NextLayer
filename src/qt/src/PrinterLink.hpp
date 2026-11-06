#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace next_layer {

/// Sending a sliced plate to the machine, through the engine's own print-host
/// family (Moonraker for Klipper, plus OctoPrint, PrusaLink, Duet and the rest).
/// The connection is stored next to the presets, the API key with file
/// permissions, and every call reports the engine's message rather than a
/// success flag.
class PrinterLink : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY configChanged)
    Q_PROPERTY(QString port READ port WRITE setPort NOTIFY configChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY configChanged)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY configChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    /// The page the Device tab shows: the printer's own web UI, or whatever the
    /// user pointed it at (Mainsail and Fluida serve the same origin; a custom
    /// URL is how anything else -- a dashboard, a camera page -- gets in.
    Q_PROPERTY(QString deviceUrl READ deviceUrl WRITE setDeviceUrl NOTIFY configChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit PrinterLink(QObject* parent = nullptr);

    QString host() const { return m_host; }
    QString port() const { return m_port; }
    QString apiKey() const { return m_apiKey; }
    QString kind() const { return m_kind; }
    bool configured() const { return !m_host.trimmed().isEmpty(); }
    QString deviceUrl() const { return m_deviceUrl; }
    bool busy() const { return m_busy; }

    void setHost(const QString& value);
    void setPort(const QString& value);
    void setApiKey(const QString& value);
    void setKind(const QString& value);
    void setDeviceUrl(const QString& value);

    /// The kinds the engine can upload to, as `{id, label}` maps.
    Q_INVOKABLE QVariantList kinds() const;

    /// Reads and writes the stored connection. Called once at startup and after
    /// every edit, so the fields in the dialog are always what will be used.
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();

    /// Both calls block on the network and report the engine's own message.
    /// They run synchronously because the engine's print host does: a wrong key
    /// or a printer that is off has to come back as an error, not a hang.
    Q_INVOKABLE QString test();

    /// The printer's web interface, tried at the plain host first: Moonraker's
    /// own UI is usually served by a reverse proxy on port 80 while the API
    /// listens on 7125, so guessing from the API port gets it wrong.
    Q_INVOKABLE QString resolveDeviceUrl();

    /// One line of printer state for the Device tab's status strip, straight out
    /// of Moonraker: "printing 42 % - cube.gcode, 12 min left".
    Q_INVOKABLE QString printerState();
    Q_INVOKABLE QString upload(const QString& gcodePath, bool startPrint);

    /// Headless entry point for the harness: test, then upload, then report.
    /// With `startPrint` the machine begins the job as the upload lands.
    QString runHeadless(const QString& gcodePath, bool startPrint = false);

signals:
    void configChanged();
    void busyChanged();
    void finished(bool ok, const QString& message);

private:
    QString configPath() const;
    QString baseUrl() const;
    QString perform(const QString& gcodePath, bool startPrint);

    QString m_host;
    QString m_port;
    QString m_apiKey;
    QString m_kind = QStringLiteral("moonraker");
    QString m_deviceUrl;
    bool m_busy = false;
};

} // namespace next_layer
