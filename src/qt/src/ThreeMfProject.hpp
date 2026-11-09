// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <array>

namespace next_layer {

/// One volume as a project file records it: what it is (a model part, a modifier,
/// a negative volume, a support blocker), where it sits in the object's frame, the
/// overrides it carries, and the facets that were painted on it.
///
/// The paint is kept as the file's own per-triangle strings, keyed by the
/// triangle's index inside the volume. Converting them is the caller's job,
/// because the engine's selector is what understands the encoding.
struct ThreeMfVolumeRecord
{
    QString type;
    QHash<QString, QString> settings;
    QHash<int, QString> supports;
    QHash<int, QString> seams;
    QHash<int, QString> tools;
};

/// A marker on the plate's timeline: a pause, a colour or tool change, or a
/// snippet of custom G-code at a Z height. It belongs to the plate, not to an
/// object, and the file keeps them per bed.
struct ThreeMfLayerGcode
{
    double z = 0.0;
    int type = 4;
    QString text;
};

/// What a project file says about one object beyond its geometry and placement.
struct ThreeMfObjectRecord
{
    /// The label the author gave the object, as opposed to the one this app makes
    /// up from the file name.
    QString name;
    QHash<QString, QString> settings;
    QVector<ThreeMfVolumeRecord> volumes;
    /// The variable layer height bands, as z/height pairs flattened in order.
    QVector<double> layerHeights;
};

/// What a project file says about where its objects go, read straight from the
/// records its authoring slicer writes.
///
/// The engine's reader returns each object with the placement the build list gave
/// it, so the build items are read here for their order (which is how an object is
/// matched to the records that describe it) and the plate records for the plate
/// each object belongs to. Everything else -- the object's overrides, its volumes
/// and their types, its layer height bands, and the facets painted on it -- is in
/// the file in formats this engine's loader discards, so it is read here too.
struct ThreeMfProject
{
    /// Twelve numbers per build item, the 3MF `<item transform>` in row-major
    /// 4x3 form: the first three rows are the rotation and scale, the last row
    /// is the translation.
    using Placement = std::array<double, 12>;

    bool valid = false;
    /// The `Application` the file names, as written: `BambuStudio-02.00.00.00`,
    /// `OrcaSlicer-2.1.1`, `PrusaSlicer-2.9.6` and so on. Empty when the file does
    /// not say, which is the case for a file this app wrote before it started
    /// naming itself.
    QString application;
    int plateCount = 0;
    QVector<int> buildOrder;
    QVector<Placement> buildTransforms;
    QHash<int, int> plateOfObject;
    QHash<int, ThreeMfObjectRecord> objectRecords;
    /// Object settings a foreign slicer wrote the Bambu/Orca way: as plain
    /// metadata in `model_settings.config`, under its own setting names. They are
    /// kept apart from the records above because a name or a value this engine
    /// does not know must not stop the file from opening -- it is filtered against
    /// the engine's own definitions and reported, never guessed at.
    QHash<int, QHash<QString, QString>> foreignSettings;
    /// The label a foreign project gives each object, which it keeps in the same
    /// place as the settings.
    QHash<int, QString> foreignNames;
    /// Timeline markers per bed, in the file's own type codes: colour change 0,
    /// pause 1, tool change 2, template 3, custom G-code 4.
    QHash<int, QVector<ThreeMfLayerGcode>> layerGcodes;
};

ThreeMfProject readThreeMfProject(const QString& path);

/// Whether the file is a print that has already been sliced rather than a model:
/// a `.gcode`, or a `.3mf` archive that carries a G-code and no mesh (what
/// MakerWorld and Printables hand out). Re-slicing one of those is not a thing
/// that can happen, so the caller offers the toolpath preview instead of a mesh.
bool isPreSlicedFile(const QString& path);

/// Writes the G-code inside a pre-sliced file to \p outPath, which is how a
/// `.gcode.3mf` becomes something the toolpath view can read. A plain `.gcode`
/// is copied. False when the file holds no G-code.
bool extractPreSlicedGcode(const QString& path, const QString& outPath);

}
