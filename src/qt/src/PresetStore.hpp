// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <thread>

class SettingsModel;

// Preset catalogue and resolver over the engine's own bundle. The engine
// 3.0 engine already evaluates the vendor YAML (inherits, variant conditions,
// nozzle/feeder logic), so this store asks the app's one engine runtime for the
// bundle's own answers instead of reimplementing the bundle format: the printer
// list, the profiles of a printer, and the resolved config of a
// printer/print/filament triple are the engine's own JSON, not a paraphrase.
class PresetStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QVariantList printProfiles READ printProfiles NOTIFY printProfilesChanged)
    Q_PROPERTY(QVariantList filamentProfiles READ filamentProfiles NOTIFY filamentProfilesChanged)
    Q_PROPERTY(QString currentPrinter READ currentPrinter NOTIFY currentPrinterChanged)
    Q_PROPERTY(QString currentPrint READ currentPrint NOTIFY currentPrintChanged)
    Q_PROPERTY(QString currentFilament READ currentFilament NOTIFY currentFilamentChanged)
    Q_PROPERTY(QStringList filaments READ filaments NOTIFY filamentsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit PresetStore(QObject* parent = nullptr);
    ~PresetStore() override;

    QVariantList printers() const { return m_printers; }
    QVariantList printProfiles() const { return m_printProfiles; }
    QVariantList filamentProfiles() const { return m_filamentProfiles; }
    QString currentPrinter() const { return m_currentPrinter; }
    QString currentPrint() const { return m_currentPrint; }
    QString currentFilament() const { return m_currentFilament; }
    /// One material per tool. A tool changer prints its tools in these, and a
    /// painted facet is printed by the tool whose index it carries, so the list
    /// is the thing a multicolour slice is built from.
    QStringList filaments() const { return m_filaments; }
    QString filamentForTool(int tool) const;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }

    // QML-facing, asynchronous. Each call joins any previous worker so the
    // engine is never driven from two threads at once.
    Q_INVOKABLE void refreshPrinters();
    Q_INVOKABLE void selectPrinter(const QString& name);
    Q_INVOKABLE void resolve(const QString& printer, const QString& print, const QString& filament);
    /// Grows the list to `tool` entries, sets that tool's material and re-resolves
    /// the same printer/process. The tool count itself comes from the printer.
    Q_INVOKABLE void setFilamentForTool(int tool, const QString& name);
    Q_INVOKABLE void setFilaments(const QStringList& names);
    Q_INVOKABLE void reload(const QString& printer, const QString& print, const QString& filament);
    bool reloadSync(const QString& printer, const QString& print, const QString& filament);

    // Preset management over the engine's own tree
    // (<datadir>/presets/user/<repo>/<vendor>/). Saving writes the values the
    // settings model reports as changed for `tab` and inherits the currently
    // selected preset of that kind by its root id, so the file only carries the
    // edit. Every call returns an empty string on success or the reason it
    // failed.
    Q_INVOKABLE QString saveUserPreset(const QString& kind, const QString& name, const QString& tab);
    Q_INVOKABLE QString renameUserPreset(const QString& kind, const QString& oldName,
                                         const QString& newName);
    Q_INVOKABLE QString deleteUserPreset(const QString& kind, const QString& name);
    Q_INVOKABLE bool presetIsUser(const QString& kind, const QString& name) const;

    /// Sharing: a user preset's own file out to any path, and back in. The file
    /// is the preset verbatim -- kind, name, parent id and the values it
    /// overrides -- so it stays readable and can be handed around or kept in a
    /// repository. An import never overwrites: a taken name lands as
    /// "<name> (imported)" and `importedPresetName()` says which file was written.
    Q_INVOKABLE QString exportPreset(const QString& kind, const QString& name,
                                     const QString& path) const;
    Q_INVOKABLE QString importPreset(const QString& kind, const QString& path);
    QString importedPresetName() const { return m_importedPresetName; }

    /// Writes the configuration the app would slice with -- the resolved preset
    /// plus every edit in the settings panel -- as a JSON file, and reads one
    /// back into the settings panel. The round trip is the same shape the
    /// engine's own \c --dump-config writes, so a file exported here can be
    /// sliced by the CLI and a CLI dump can be opened here.
    Q_INVOKABLE QString exportConfiguration(const QString& path) const;
    Q_INVOKABLE QString importConfiguration(const QString& path);

    // Writes a complete custom printer bundle: a new vendor under
    // <datadir>/presets/local/next-layer-custom/Custom holding the hardware
    // definitions plus printer/process/tool/filament presets taken from the
    // edited settings, so the machine slices without any inherited profile.
    Q_INVOKABLE QString createPrinter(const QString& name, double bedWidth,
                                      double bedHeight, double bedDepth);

    void setSettings(SettingsModel* settings) { m_settings = settings; }

    // Synchronous core, also used by the headless knobs. Return false on failure.
    bool refreshPrintersSync();
    bool selectPrinterSync(const QString& name);
    bool resolveSync(const QString& printer, const QString& print, const QString& filament);

    /// Where the app keeps its own data: presets, projects, the print-host
    /// connection. One place, so nothing else has to guess at a path.
    static QString dataDir();

    mutable QString m_importedPresetName;

    QByteArray resolvedConfigJson() const { return m_resolvedConfigJson; }
    QVariantMap resolvedConfiguration() const { return m_resolvedConfiguration; }
    QString resolvedPrinterModel() const { return m_resolvedPrinterModel; }
    QVariantList resolvedBedShape() const { return m_resolvedBedShape; }
    double resolvedMaxPrintHeight() const { return m_resolvedMaxPrintHeight; }

signals:
    void printersChanged();
    void printProfilesChanged();
    void filamentProfilesChanged();
    void currentPrinterChanged();
    void currentPrintChanged();
    void currentFilamentChanged();
    void filamentsChanged();
    void busyChanged();
    void errorChanged();
    void resolved();

private:
    struct BundleLocation
    {
        QString repo;
        QString vendor;
        bool valid() const { return !repo.isEmpty() && !vendor.isEmpty(); }
    };

    struct ReloadResult
    {
        QVariantList printers;
        QVariantList printProfiles;
        QVariantList filamentProfiles;
        QByteArray resolvedConfig;
        QString printer;
        QString print;
        QString filament;
        QString failure;
    };

    static BundleLocation locateBundle(const QString& vendorId);
    static QString presetPrefix(const QString& kind);
    static QString presetPathFor(const BundleLocation& bundle, const QString& kind,
                                 const QString& name);
    static bool validPresetName(const QString& name);

    void parsePrinters(const QByteArray& json);
    void parseProfiles(const QByteArray& json);
    void parseResolved(const QByteArray& json);
    void noteCurrentVendor(const QString& printerName);
    QStringList filamentsForResolve(const QString& printer, const QString& filament);
    void adoptResolvedFilaments();
    static ReloadResult computeReload(const QString& printer, const QString& print,
                                      const QString& filament, const QStringList& filaments);
    bool applyReload(const ReloadResult& result);

    QVariantList m_printers;
    QVariantList m_printProfiles;
    QVariantList m_filamentProfiles;
    QString m_currentPrinter;
    QString m_currentPrint;
    QString m_currentFilament;
    QStringList m_filaments;
    QByteArray m_resolvedConfigJson;
    QVariantMap m_resolvedConfiguration;
    QVariantMap m_resolvedPreset;
    QString m_resolvedPrinterModel;
    QVariantList m_resolvedBedShape;
    double m_resolvedMaxPrintHeight = 0.0;
    bool m_busy = false;
    QString m_error;
    QString m_currentVendorId;
    SettingsModel* m_settings = nullptr;
    std::thread m_worker;
};
