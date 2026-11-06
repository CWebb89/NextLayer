// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QObject>
#include <QString>

namespace next_layer {

/// One release of this application, as the channel describes it. The manifest is
/// a single JSON file next to the artifact, so the check is one small GET and
/// nothing else is trusted: the artifact is verified by its sha256 before a byte
/// of it is installed.
struct ReleaseInfo
{
    QString version;
    QString url;
    QString sha256;
    QString notes;
    QString error;

    bool valid() const { return error.isEmpty() && !version.isEmpty() && !url.isEmpty(); }
};

/// Updater over a static HTTPS channel: <channel>/releases/latest/download/ holds
/// update.json and the artifact the manifest names. GitHub Releases satisfies
/// that layout, which is why it is the default channel; any host that can serve
/// those two paths works the same way.
///
/// The install is a staging directory plus renames, never an in-place write: on
/// Linux the running binary stays mapped under its old inode, so replacing it is
/// safe while the app is running and the new one takes over on restart.
class Updater : public QObject
{
    Q_OBJECT

public:
    explicit Updater(QObject* parent = nullptr);

    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY statusChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool downloaded READ downloaded NOTIFY statusChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY statusChanged)
    /// The URL this build checks, so Preferences can show where it looks instead
    /// of a constant nobody can verify.
    Q_PROPERTY(QString channel READ channelString CONSTANT)

    QString status() const { return m_status; }
    QString availableVersion() const { return m_release.version; }
    QString notes() const { return m_release.notes; }
    bool busy() const { return m_busy; }
    bool downloaded() const { return m_downloaded; }
    bool canInstall() const;

    /// Where this build looks for releases: NEXT_LAYER_UPDATE_URL when it is set
    /// (the gate points it at a local server), else the published channel.
    static QString channel();
    static QString channelString() { return channel(); }
    static QString manifestUrl(const QString& channel);
    /// The install prefix: the directory holding the running executable and its
    /// qml directory. NEXT_LAYER_UPDATE_ROOT overrides it, so the update path can
    /// be exercised without touching the copy a person is using.
    static QString installRoot();
    static QString currentVersion();

    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void install();
    Q_INVOKABLE void relaunch();

    /// Blocking forms for the headless knobs. Return true when the release is
    /// newer than this build and its manifest is complete.
    bool checkSync(const QString& channel, ReleaseInfo& out);
    bool downloadSync(const ReleaseInfo& release, const QString& root, QString& error);
    bool installSync(const QString& root, QString& error);

signals:
    void statusChanged();
    void busyChanged();

private:
    void setStatus(const QString& status);
    void setBusy(bool busy);

    ReleaseInfo m_release;
    QString m_status;
    QString m_stagedPath;
    bool m_busy = false;
    bool m_downloaded = false;
};

/// Strictly newer, comparing dotted numbers. A malformed version is never newer.
bool versionIsNewer(const QString& candidate, const QString& current);

}
