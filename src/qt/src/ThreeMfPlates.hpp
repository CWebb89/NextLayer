// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace next_layer {

/// The plate layout of a project file, read from the records its authoring
/// slicer writes beside the geometry. The 3MF scene holds every object in one
/// coordinate space in which the plates are laid out hundreds of millimetres to
/// metres apart, and the reader re-centres each object by its own bounding box,
/// so an object's loaded position cannot be matched back to a plate. The scene's
/// build list can: it lists the placed objects in the order they are read, one
/// per object this application ends up with, and the project's plate records name
/// the plate each one belongs to.
struct ThreeMfPlates
{
    bool valid = false;
    int plateCount = 0;
    QVector<int> buildOrder;
    QHash<int, int> plateOfObject;
};

ThreeMfPlates readThreeMfPlates(const QString& path);

} // namespace next_layer
