// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <Slic3r/Domain/LayerHeightProfile.hpp>
#include <Slic3r/Domain/ModelVolume.hpp>
#include <Slic3r/Domain/SelectionId.hpp>
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
    /// The plate this object sits on, 0-based. A project file records it: the
    /// slicer only ever slices one plate at a time, but the file is the document.
    int plate = 0;
    /// Geometry in the object's own frame. The slicer path hands it over already
    /// placed -- every vertex in printer coordinates, `transform` left identity,
    /// which is why a slice is unaffected by how an object was moved; the project
    /// export hands it over unplaced with the transform that places it, because a
    /// 3MF has to record where the object sits rather than bake it into the mesh.
    Slic3r::Domain::TriangleMesh mesh;
    Slic3r::Domain::Transformation transform;
    QVector<PlateSliceVolume> volumes;
    QByteArray settingsJson;
    /// Settings the object's file carried that this engine cannot act on, kept in
    /// the dialect they were written in so a project from another slicer survives a
    /// round trip through this one.
    QHash<QString, QString> foreignSettings;
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

    /// Writes the plate as a project file in the format PrusaSlicer and
    /// OrcaSlicer read: the objects and their volumes in one model, their own
    /// settings, layer height profiles and painted facets, and the resolved
    /// printer/print/filament configuration, all in one 3MF. What the format
    /// cannot carry is reported by the caller rather than dropped here.
    bool exportPlate3mf(const QVector<PlateSliceObject>& objects, const QString& configPath,
                        const QString& outPath, QString& error,
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

    /// Creates the engine runtime if it does not exist yet, which installs the
    /// platform services the engine's own file loader asserts on. STEP and the
    /// project formats go through that loader, so a model named on the command
    /// line needs this before it is opened, not when a slice first runs.
    static void ensurePlatformServices();

    /// The data directory the engine resolves presets against. The plate path
    /// needs it for its own load, and it must match what the runtime was built
    /// with.
    static std::string dataDirForParams();

private:
    /// Builds the engine project for a plate: the objects with their volumes,
    /// settings, layer height profiles and paint, the bed they are attached to
    /// and the per-layer markers. Returns the project's id with it loaded and
    /// selected, or nothing after removing it on failure. Both the slice and the
    /// project export start here, so a file written for another slicer is the
    /// same project the slicer itself would have run.
    /// \p carried, when given, receives the overrides the engine refused for an
    /// object, keyed by the object's index in \p objects. A project export hands
    /// one over so a setting this engine cannot apply is written to the file
    /// instead of failing the export; a slice passes nothing, because there the
    /// refusal is the answer.
    std::optional<Slic3r::Domain::SelectionId> preparePlate(
        Runtime& runtime, const QVector<PlateSliceObject>& objects, const QString& configPath,
        const QString& gcodePath, const QVector<PlateLayerGcode>& layerGcodes, QString& error,
        QHash<int, QHash<QString, QString>>* carried = nullptr);

    std::optional<QTemporaryDir> m_plateDir;
};

} // namespace next_layer
