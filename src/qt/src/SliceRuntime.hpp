// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <Slic3r/Domain/LayerHeightProfile.hpp>
#include <Slic3r/Domain/ModelVolume.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/TriangleSelector.hpp>

#include <memory>
#include <mutex>
#include <optional>

namespace next_layer {

/// One object on the plate, as the slicer needs it: geometry already in printer
/// coordinates, the per-object overrides the object may carry, its own layer
/// height profile (empty for a uniform object), and its painted facets. Paint is
/// the engine's own triangle-state data, so what the viewport shows and what the
/// slicer reads are the same numbers.
/// A volume attached to an object, already in printer coordinates: a part the
/// engine unions into the object, a negative volume it subtracts, a modifier it
/// reads settings from, or a support blocker/enforcer.
struct PlateSliceVolume
{
    Slic3r::Domain::TriangleMesh mesh;
    Slic3r::Domain::ModelVolumeType type = Slic3r::Domain::ModelVolumeType::MODEL_PART;
    /// The modifier's own overrides, as the engine's volume-settings JSON.
    QByteArray settingsJson;
};

/// One marker on the plate's timeline: a custom G-code snippet, a pause or a
/// colour/tool change at a Z height. The engine calls these custom G-codes and
/// stores them per bed instance.
struct PlateLayerGcode
{
    double z = 0.0;
    QString type = QStringLiteral("gcode");
    QString text;
};

struct PlateSliceObject
{
    QString name;
    Slic3r::Domain::TriangleMesh mesh;
    QVector<PlateSliceVolume> volumes;
    QByteArray settingsJson;
    Slic3r::Domain::ZHeightPairs layerHeights;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData supportPaint;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData seamPaint;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData toolPaint;
};

/// The app's own engine runtime: one `CLIRuntime`, created on first use and kept
/// for the life of the process, so a slice no longer starts a second engine.
class SliceRuntime
{
public:
    struct Runtime;

    SliceRuntime() = default;
    ~SliceRuntime() = default;

    SliceRuntime(const SliceRuntime&) = delete;
    SliceRuntime& operator=(const SliceRuntime&) = delete;

    /// The process's runtime handle. It holds no state itself; the engine lives
    /// behind it in one instance shared by every caller.
    static SliceRuntime& instance();

    /// Slices one model file with a resolved config and exports G-code.
    bool slice(const QString& modelPath, const QString& configPath,
               const QString& gcodePath, QString& error);

    /// Slices a whole plate. The objects go in as one project -- the engine's own
    /// project file, written by the engine's own writer, carrying their object
    /// settings, layer height profiles and painted facets -- so per-object
    /// overrides, variable layer height and painting all reach the slicer with no
    /// reimplementation of the 3MF format on our side.
    bool slicePlate(const QVector<PlateSliceObject>& objects, const QString& configPath,
                    const QString& gcodePath, QString& error,
                    const QVector<PlateLayerGcode>& layerGcodes = {});

    /// The engine's printer-model list as JSON. Empty when the bundle has none.
    /// Re-reads every preset bundle from disk. A vendor that gained presets on
    /// disk is invisible to the loaded bundle until this runs.
    void reloadPresets();

    QByteArray printerModels();

    /// The print/tool/filament presets available for one printer, as JSON.
    QByteArray profilesFor(const QString& printer);

    /// The resolved configuration of a printer/print/filament triple, as JSON in
    /// the same shape `--configuration-save` writes. Empty when it does not
    /// resolve.
    /// One entry per tool. A tool changer resolves one material per extruder, so
    /// the list, not a single name, is what decides how many extruders the
    /// engine sees and which colour each of them carries.
    QByteArray resolvedConfig(const QString& printer, const QString& print,
                              const QStringList& filaments);

    /// Serialises engine access for every caller in the process. The engine is
    /// not safe to drive from two runtimes at once, and the preset code still
    /// forks its own for the actions the runtime does not cover yet.
    static std::mutex& engineMutex();

    /// Tears the runtime down while the process is still healthy. Its destructor
    /// closes the main-thread dispatcher, and the engine asserts if that happens
    /// after static teardown has begun with events still queued.
    static void shutdown();

    /// The data directory the engine resolves presets against. The plate path
    /// needs it for its own load, and it must match what the runtime was built
    /// with.
    static std::string dataDirForParams();

private:
    std::optional<QTemporaryDir> m_plateDir;
};

} // namespace next_layer
