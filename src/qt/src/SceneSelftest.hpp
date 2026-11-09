// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>
#include <QStringList>

namespace next_layer {

// Stands up the engine's project/scene API headlessly and drives it the way the
// plate document will: one project, several objects on one bed, per-object
// placement, then a single slice exported to G-code. Prints what it measured and
// returns 0 only when every object survived into the toolpath.
int runSceneSelftest(const QStringList& models,
                     const QString& configPath,
                     const QString& gcodePath,
                     const QString& dataDir);

// Exercises the plate tools that touch geometry rather than the engine: arrange
// (two objects deliberately stacked, then packed apart inside the bed),
// auto-orient (a tilted object lowered onto a face) and place-on-face. Prints
// what it measured and returns 0 only when each tool changed the object the way
// it claims to.
int runPlateToolsSelftest(const QStringList& models,
                          const QString& configPath,
                          const QString& dataDir);

// Proves the three things a plate can carry beyond geometry reach the slicer:
// a per-object setting (layer height), a variable layer height profile, and
// painted facets (support enforcers on a face that needs no support). Each is
// measured in the exported toolpath, not assumed from the call succeeding.
int runSliceFeaturesSelftest(const QString& model,
                             const QString& configPath,
                             const QString& dataDir);

// Saves a plate with per-object settings, a layer height profile and paint,
// reloads the file into a cleared document, and checks that nothing was lost --
// then slices the reloaded plate, because a project that loads but does not
// slice is not a project.
int runProjectSelftest(const QString& model, const QString& configPath,
                       const QString& dataDir);

/// Writes a plate with per-object settings, a layer height profile and paint as
/// a project 3MF another slicer can open, then loads that file back through the
/// engine and slices it. The two toolpaths must hash the same: a file that drops
/// an override or a painted facet loads fine and slices something else.
int runThreeMfExportSelftest(const QString& model, const QString& configPath,
                             const QString& outPath, const QString& dataDir);

// Proves the two CAD-suite moves reach the slicer: a primitive added straight
// onto the plate is a real object that prints, and a negative volume attached to
// one subtracts from it. Both are measured in the exported toolpath -- a
// negative volume that the engine ignored would leave the move count unchanged.
int runMultiToolSelftest(const QString& configPath, const QString& dataDir);
int runVolumeSettingsSelftest(const QString& configPath, const QString& dataDir);
int runCalibrationSelftest(const QString& configPath, const QString& dataDir,
                           const QString& keepGcodePath = {});

/// Measures a cube from its mesh, hollows it into a shell, merges two boxes into
/// one object and splits that back into two -- each result is read back out of
/// the document rather than assumed from the call succeeding.
int runMeshToolsSelftest(const QString& configPath, const QString& dataDir);
int runShapeSelftest(const QString& configPath, const QString& dataDir);

}
